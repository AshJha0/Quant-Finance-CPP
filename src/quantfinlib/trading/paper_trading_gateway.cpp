#include "quantfinlib/trading/paper_trading_gateway.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <stdexcept>

namespace quantfinlib {

namespace {

bool usablePrice(double p) { return std::isfinite(p) && p > 0; }

double sgn(double v) { return v > 0 ? 1.0 : (v < 0 ? -1.0 : 0.0); }

std::int64_t nowNanos() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
                   std::chrono::steady_clock::now().time_since_epoch())
            .count();
}

}  // namespace

void PaperTradingGateway::attachRiskGate(HftRiskGate& gate,
                                         std::unordered_map<std::string, int> symbolIds) {
    std::lock_guard<std::mutex> lock(mutex_);
    riskGate_ = &gate;
    gateSymbolIds_ = std::move(symbolIds);
}

void PaperTradingGateway::onQuote(const std::string& symbol, double bid, double ask) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!usablePrice(bid) || !usablePrice(ask) || bid > ask) {
        logRejection("bad quote ignored for " + symbol);
        return;
    }
    quotes_[symbol] = Quote{bid, ask};
    if (riskGate_ != nullptr) {
        auto sit = gateSymbolIds_.find(symbol);
        if (sit != gateSymbolIds_.end()) {
            riskGate_->setReferencePrice(sit->second, (bid + ask) / 2);
        }
    }
    std::vector<std::int64_t> remaining;
    remaining.reserve(restingIds_.size());
    for (std::int64_t id : restingIds_) {
        WorkingOrder& order = orders_.at(id);
        if (order.symbol != symbol) {
            remaining.push_back(id);
            continue;
        }
        if (order.side == Side::BUY && ask <= order.limitPrice) {
            fill(order, std::min(order.limitPrice, ask));
            continue;
        }
        if (order.side == Side::SELL && bid >= order.limitPrice) {
            fill(order, std::max(order.limitPrice, bid));
            continue;
        }
        remaining.push_back(id);
    }
    restingIds_ = std::move(remaining);
}

std::int64_t PaperTradingGateway::submitLimit(const std::string& symbol, Side side,
                                              std::int64_t quantity, double price) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::int64_t id = nextId_++;
    orders_.emplace(id, WorkingOrder{id, symbol, side, price, quantity, OrderStatus::NEW});
    WorkingOrder& stored = orders_.at(id);
    if (!passesRiskGate(stored, price)) {
        return id;
    }
    auto qit = quotes_.find(symbol);
    if (qit != quotes_.end()
            && (side == Side::BUY ? qit->second.ask <= price : qit->second.bid >= price)) {
        fill(stored, side == Side::BUY ? qit->second.ask : qit->second.bid);
    } else {
        restingIds_.push_back(id);
    }
    return id;
}

std::int64_t PaperTradingGateway::submitMarket(const std::string& symbol, Side side,
                                               std::int64_t quantity) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::int64_t id = nextId_++;
    orders_.emplace(id, WorkingOrder{id, symbol, side, std::nan(""), quantity, OrderStatus::NEW});
    WorkingOrder& stored = orders_.at(id);
    auto qit = quotes_.find(symbol);
    if (qit == quotes_.end()) {
        stored.status = OrderStatus::REJECTED;
        logRejection("order " + std::to_string(id) + ": NO_QUOTE for " + symbol);
        return id;
    }
    double touch = side == Side::BUY ? qit->second.ask : qit->second.bid;
    if (!passesRiskGate(stored, touch)) {
        return id;
    }
    fill(stored, touch);
    return id;
}

bool PaperTradingGateway::cancel(std::int64_t orderId) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = orders_.find(orderId);
    if (it == orders_.end() || it->second.status != OrderStatus::NEW) {
        return false;
    }
    it->second.status = OrderStatus::CANCELED;
    if (riskGate_ != nullptr) {
        riskGate_->releaseOpen(gateSymbolIds_.at(it->second.symbol), it->second.side,
                               it->second.quantity);
    }
    restingIds_.erase(std::remove(restingIds_.begin(), restingIds_.end(), orderId),
                       restingIds_.end());
    return true;
}

OrderStatus PaperTradingGateway::status(std::int64_t orderId) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = orders_.find(orderId);
    if (it == orders_.end()) {
        throw std::invalid_argument("unknown order " + std::to_string(orderId));
    }
    return it->second.status;
}

void PaperTradingGateway::addExecutionListener(ExecutionListener listener) {
    std::lock_guard<std::mutex> lock(mutex_);
    listeners_.push_back(std::move(listener));
}

double PaperTradingGateway::position(const std::string& symbol) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = positions_.find(symbol);
    return it == positions_.end() ? 0.0 : it->second.quantity;
}

double PaperTradingGateway::cash() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return cash_;
}

double PaperTradingGateway::realizedPnl() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return realizedPnl_;
}

double PaperTradingGateway::equity() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return equityLocked();
}

double PaperTradingGateway::equityLocked() const {
    double value = cash_;
    for (const auto& [symbol, p] : positions_) {
        auto qit = quotes_.find(symbol);
        if (qit != quotes_.end()) {
            value += p.quantity * qit->second.mid();
        }
    }
    return value;
}

std::vector<std::string> PaperTradingGateway::rejectionLog() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return rejectionLog_;
}

std::unordered_map<std::string, double> PaperTradingGateway::positionsSnapshot() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return positionsSnapshotLocked();
}

std::unordered_map<std::string, double> PaperTradingGateway::positionsSnapshotLocked() const {
    std::unordered_map<std::string, double> out;
    for (const auto& [symbol, p] : positions_) {
        if (p.quantity != 0) {
            out[symbol] = p.quantity;
        }
    }
    return out;
}

PaperTradingGateway::AccountSnapshot PaperTradingGateway::snapshot() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return AccountSnapshot{cash_, equityLocked(), realizedPnl_, rejectionCount_,
                           positionsSnapshotLocked()};
}

bool PaperTradingGateway::passesRiskGate(WorkingOrder& order, double referencePrice) {
    std::string reason;
    if (order.quantity <= 0) {
        reason = "INVALID_QUANTITY";
    } else if (!usablePrice(referencePrice)) {
        reason = "INVALID_PRICE";
    } else if (riskGate_ != nullptr) {
        auto sit = gateSymbolIds_.find(order.symbol);
        if (sit == gateSymbolIds_.end()) {
            reason = "UNKNOWN_SYMBOL";
        } else {
            int code = riskGate_->reserve(sit->second, order.side, order.quantity, referencePrice);
            if (code != HftRiskGate::OK) {
                reason = HftRiskGate::reasonName(code);
            }
        }
    }
    if (reason.empty()) {
        return true;
    }
    order.status = OrderStatus::REJECTED;
    logRejection("order " + std::to_string(order.id) + ": " + reason + " for " + order.symbol);
    return false;
}

void PaperTradingGateway::logRejection(const std::string& entry) {
    rejectionCount_++;
    if (static_cast<int>(rejectionLog_.size()) < REJECTION_LOG_CAP) {
        rejectionLog_.push_back(entry);
    }
}

void PaperTradingGateway::fill(WorkingOrder& order, double price) {
    std::int64_t qty = order.quantity;
    applyFill(order.symbol, order.side, price, qty);
    if (riskGate_ != nullptr) {
        riskGate_->onOrderFill(gateSymbolIds_.at(order.symbol), order.side, qty, qty);
    }
    order.quantity = 0;
    order.status = OrderStatus::FILLED;
    std::int64_t ts = nowNanos();
    for (const auto& l : listeners_) {
        l(order.id, order.symbol, order.side, price, qty, ts);
    }
}

void PaperTradingGateway::applyFill(const std::string& symbol, Side side, double price,
                                    std::int64_t qty) {
    double signedQty = side == Side::BUY ? static_cast<double>(qty) : -static_cast<double>(qty);
    double notional = price * static_cast<double>(qty);
    cash_ += side == Side::BUY ? -notional : notional;
    double fee = notional * commissionRate_;
    cash_ -= fee;

    Position& p = positions_[symbol];
    if (p.quantity == 0 || sgn(p.quantity) == sgn(signedQty)) {
        // Opening or adding: blend the average cost.
        double newQty = p.quantity + signedQty;
        p.avgCost = (p.avgCost * std::fabs(p.quantity) + price * static_cast<double>(qty))
                    / std::fabs(newQty);
        p.quantity = newQty;
    } else {
        double closing = std::min(std::fabs(p.quantity), static_cast<double>(qty));
        realizedPnl_ += (price - p.avgCost) * closing * sgn(p.quantity);
        double newQty = p.quantity + signedQty;
        if (sgn(newQty) != sgn(p.quantity) && newQty != 0) {
            p.avgCost = price;  // position flipped: remainder opened at this fill
        }
        p.quantity = newQty;
        if (newQty == 0) {
            p.avgCost = 0;
        }
    }
}

}  // namespace quantfinlib
