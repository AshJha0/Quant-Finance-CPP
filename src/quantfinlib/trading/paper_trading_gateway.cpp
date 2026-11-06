#include "quantfinlib/trading/paper_trading_gateway.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace quantfinlib {

namespace {

bool usablePrice(double p) { return std::isfinite(p) && p > 0; }

double sgn(double v) { return v > 0 ? 1.0 : (v < 0 ? -1.0 : 0.0); }

/** FNV-1a: a fixed per-symbol stream, independent of the standard library's hash. */
std::uint64_t mixSeed(std::uint64_t seed, const std::string& symbol) {
    std::uint64_t h = 1469598103934665603ULL ^ seed;
    for (unsigned char c : symbol) {
        h = (h ^ c) * 1099511628211ULL;
    }
    return h;
}

}  // namespace

PaperTradingGateway::PaperTradingGateway(double initialCash, double commissionRate, HftRiskGate& gate,
                                         std::unordered_map<std::string, int> symbolIds,
                                         const FillConfig& fills)
        : commissionRate_(commissionRate),
          fillConfig_(fills),
          riskGate_(&gate),
          gateSymbolIds_(std::move(symbolIds)),
          cash_(initialCash) {}

PaperTradingGateway::Venue& PaperTradingGateway::venue(const std::string& symbol) {
    auto it = venues_.find(symbol);
    if (it == venues_.end()) {
        FillConfig config = fillConfig_;
        config.seed = mixSeed(fillConfig_.seed, symbol);
        it = venues_.emplace(symbol, Venue(config)).first;
        it->second.simulator.advanceTo(clock_);
    }
    return it->second;
}

void PaperTradingGateway::settle() {
    for (auto& [symbol, v] : venues_) {
        v.simulator.advanceTo(clock_);
        for (const auto& report : v.simulator.drainReports(clock_)) {
            applyReport(v, report);
        }
    }
}

void PaperTradingGateway::applyReport(Venue& venue, const QueueFillSimulator::Report& report) {
    std::int64_t id = venue.orderIds.at(report.orderId);
    WorkingOrder& order = orders_.at(id);
    if (report.type == QueueFillSimulator::ReportType::CANCELLED) {
        order.status = OrderStatus::CANCELED;
        if (riskGate_ != nullptr) {
            riskGate_->releaseOpen(gateSymbolIds_.at(order.symbol), order.side, report.quantity);
        }
        venue.orderIds.erase(report.orderId);
        return;
    }
    applyFill(order.symbol, order.side, report.price, report.quantity);
    if (riskGate_ != nullptr) {
        riskGate_->onOrderFill(gateSymbolIds_.at(order.symbol), order.side, report.quantity, report.quantity);
    }
    order.filled += report.quantity;
    bool done = order.filled == order.quantity;
    order.status = done ? OrderStatus::FILLED : OrderStatus::PARTIALLY_FILLED;
    if (done) {
        venue.orderIds.erase(report.orderId);
    }
    for (const auto& l : listeners_) {
        l(order.id, order.symbol, order.side, report.price, report.quantity, report.exchangeNanos);
    }
}

void PaperTradingGateway::onQuote(const std::string& symbol, std::int64_t nanos, double bid,
                                  std::int64_t bidSize, double ask, std::int64_t askSize) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!usablePrice(bid) || !usablePrice(ask) || bid > ask || bidSize < 0 || askSize < 0 || nanos < clock_) {
        logRejection("bad quote ignored for " + symbol);
        return;
    }
    clock_ = nanos;
    venue(symbol).simulator.onBook(nanos, bid, bidSize, ask, askSize);
    if (riskGate_ != nullptr) {
        auto sit = gateSymbolIds_.find(symbol);
        if (sit != gateSymbolIds_.end()) {
            riskGate_->setReferencePrice(sit->second, (bid + ask) / 2);
        }
    }
    settle();
}

void PaperTradingGateway::onTrade(const std::string& symbol, std::int64_t nanos, double price,
                                  std::int64_t quantity) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!usablePrice(price) || quantity <= 0 || nanos < clock_) {
        logRejection("bad trade ignored for " + symbol);
        return;
    }
    clock_ = nanos;
    venue(symbol).simulator.onTrade(nanos, price, quantity);
    settle();
}

PaperTradingGateway::WorkingOrder& PaperTradingGateway::newOrder(const std::string& symbol, Side side,
                                                                 std::int64_t quantity) {
    std::int64_t id = nextId_++;
    return orders_.emplace(id, WorkingOrder{id, symbol, side, quantity, 0, OrderStatus::NEW, 0, false})
            .first->second;
}

std::int64_t PaperTradingGateway::submitLimit(const std::string& symbol, Side side,
                                              std::int64_t quantity, double price) {
    std::lock_guard<std::mutex> lock(mutex_);
    WorkingOrder& stored = newOrder(symbol, side, quantity);
    std::int64_t id = stored.id;
    if (!passesRiskGate(stored, price)) {
        return id;
    }
    Venue& v = venue(symbol);
    stored.simId = v.simulator.submitLimit(clock_, side, quantity, price);
    v.orderIds[stored.simId] = id;
    settle();
    return id;
}

std::int64_t PaperTradingGateway::submitMarket(const std::string& symbol, Side side,
                                               std::int64_t quantity) {
    std::lock_guard<std::mutex> lock(mutex_);
    WorkingOrder& stored = newOrder(symbol, side, quantity);
    std::int64_t id = stored.id;
    Venue& v = venue(symbol);
    // The strategy only knows the book its feed has delivered.
    auto seen = v.simulator.visibleBook(clock_);
    if (quantity > 0 && !seen) {
        stored.status = OrderStatus::REJECTED;
        logRejection("order " + std::to_string(id) + ": NO_QUOTE for " + symbol);
        return id;
    }
    double touch = !seen ? 0 : (side == Side::BUY ? seen->ask : seen->bid);
    if (!passesRiskGate(stored, quantity > 0 ? touch : 1.0)) {
        return id;
    }
    stored.simId = v.simulator.submitMarket(clock_, side, quantity);
    v.orderIds[stored.simId] = id;
    settle();
    return id;
}

bool PaperTradingGateway::cancel(std::int64_t orderId) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = orders_.find(orderId);
    if (it == orders_.end() || it->second.cancelSent
        || (it->second.status != OrderStatus::NEW && it->second.status != OrderStatus::PARTIALLY_FILLED)) {
        return false;
    }
    WorkingOrder& order = it->second;
    if (!venues_.at(order.symbol).simulator.cancel(clock_, order.simId)) {
        return false;  // finished at the venue, the report is on its way
    }
    order.cancelSent = true;
    settle();
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

std::int64_t PaperTradingGateway::filledQuantity(std::int64_t orderId) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = orders_.find(orderId);
    if (it == orders_.end()) {
        throw std::invalid_argument("unknown order " + std::to_string(orderId));
    }
    return it->second.filled;
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
        auto vit = venues_.find(symbol);
        auto seen = vit == venues_.end() ? std::nullopt : vit->second.simulator.visibleBook(clock_);
        if (seen) {
            value += p.quantity * seen->mid();
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

QueueFillSimulator::AdverseSelection PaperTradingGateway::adverseSelection(const std::string& symbol) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = venues_.find(symbol);
    return it == venues_.end() ? QueueFillSimulator::AdverseSelection{} : it->second.simulator.adverseSelection();
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
