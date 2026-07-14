#include "quantfinlib/orderbook/order_book.hpp"

#include <algorithm>
#include <cmath>

namespace quantfinlib {

namespace {
const double kNaN = std::numeric_limits<double>::quiet_NaN();
}

void OrderBook::addTradeListener(TradeListener listener) {
    listeners_.push_back(std::move(listener));
}

// ------------------------------------------------------------------
// Order entry
// ------------------------------------------------------------------

std::int64_t OrderBook::submitLimit(Side side, double price, std::int64_t quantity,
                                    std::int64_t timestampNanos) {
    orderCount_++;
    std::int64_t id = nextId_++;
    std::int64_t remaining = match(side, quantity, price, false, id, timestampNanos);
    if (remaining > 0) {
        orders_.emplace(id, LimitOrder{id, side, price, remaining, timestampNanos});
        if (side == Side::BUY) {
            PriceLevel& level = bids_[price];
            level.price = price;
            level.queue.push_back(id);
            level.totalQty += remaining;
        } else {
            PriceLevel& level = asks_[price];
            level.price = price;
            level.queue.push_back(id);
            level.totalQty += remaining;
        }
    }
    return id;
}

std::int64_t OrderBook::submitMarket(Side side, std::int64_t quantity,
                                     std::int64_t timestampNanos) {
    orderCount_++;
    std::int64_t id = nextId_++;
    std::int64_t remaining = match(side, quantity, 0, true, id, timestampNanos);
    return quantity - remaining;
}

bool OrderBook::cancel(std::int64_t orderId) {
    auto it = orders_.find(orderId);
    if (it == orders_.end()) {
        return false;
    }
    LimitOrder order = it->second;
    orders_.erase(it);
    cancelCount_++;
    if (order.side == Side::BUY) {
        auto lv = bids_.find(order.price);
        if (lv != bids_.end()) {
            auto& q = lv->second.queue;
            q.erase(std::find(q.begin(), q.end(), orderId));
            lv->second.totalQty -= order.quantity;
            if (q.empty()) {
                bids_.erase(lv);
            }
        }
    } else {
        auto lv = asks_.find(order.price);
        if (lv != asks_.end()) {
            auto& q = lv->second.queue;
            q.erase(std::find(q.begin(), q.end(), orderId));
            lv->second.totalQty -= order.quantity;
            if (q.empty()) {
                asks_.erase(lv);
            }
        }
    }
    return true;
}

std::int64_t OrderBook::match(Side takerSide, std::int64_t qty, double limitPrice,
                              bool market, std::int64_t takerId,
                              std::int64_t timestampNanos) {
    return takerSide == Side::BUY
                   ? matchAgainst(asks_, takerSide, qty, limitPrice, market, takerId,
                                  timestampNanos)
                   : matchAgainst(bids_, takerSide, qty, limitPrice, market, takerId,
                                  timestampNanos);
}

template <typename Book>
std::int64_t OrderBook::matchAgainst(Book& opposite, Side takerSide, std::int64_t qty,
                                     double limitPrice, bool market, std::int64_t takerId,
                                     std::int64_t timestampNanos) {
    while (qty > 0 && !opposite.empty()) {
        PriceLevel& best = opposite.begin()->second;
        if (!market) {
            if (takerSide == Side::BUY && best.price > limitPrice) {
                break;
            }
            if (takerSide == Side::SELL && best.price < limitPrice) {
                break;
            }
        }
        std::int64_t makerId = best.queue.front();
        LimitOrder& maker = orders_.at(makerId);
        std::int64_t fill = std::min(qty, maker.quantity);
        maker.quantity -= fill;
        best.totalQty -= fill;
        qty -= fill;
        tradeCount_++;
        for (const auto& l : listeners_) {
            l(makerId, takerId, best.price, fill, timestampNanos);
        }
        if (maker.quantity == 0) {
            best.queue.pop_front();
            orders_.erase(makerId);
        }
        if (best.queue.empty()) {
            opposite.erase(opposite.begin());
        }
    }
    return qty;
}

// ------------------------------------------------------------------
// Top of book / depth
// ------------------------------------------------------------------

double OrderBook::bestBid() const {
    return bids_.empty() ? kNaN : bids_.begin()->first;
}

double OrderBook::bestAsk() const {
    return asks_.empty() ? kNaN : asks_.begin()->first;
}

std::int64_t OrderBook::bestBidSize() const {
    return bids_.empty() ? 0 : bids_.begin()->second.totalQty;
}

std::int64_t OrderBook::bestAskSize() const {
    return asks_.empty() ? 0 : asks_.begin()->second.totalQty;
}

double OrderBook::mid() const {
    double b = bestBid();
    double a = bestAsk();
    return std::isnan(b) || std::isnan(a) ? kNaN : (b + a) / 2;
}

std::vector<std::pair<double, std::int64_t>> OrderBook::levels(Side side,
                                                               int maxLevels) const {
    std::vector<std::pair<double, std::int64_t>> out;
    out.reserve(std::min(maxLevels, 16));
    if (side == Side::BUY) {
        for (const auto& [price, level] : bids_) {
            if (static_cast<int>(out.size()) >= maxLevels) {
                break;
            }
            out.emplace_back(price, level.totalQty);
        }
    } else {
        for (const auto& [price, level] : asks_) {
            if (static_cast<int>(out.size()) >= maxLevels) {
                break;
            }
            out.emplace_back(price, level.totalQty);
        }
    }
    return out;
}

std::int64_t OrderBook::depth(Side side, int maxLevels) const {
    std::int64_t total = 0;
    int n = 0;
    if (side == Side::BUY) {
        for (const auto& [price, level] : bids_) {
            if (n++ >= maxLevels) {
                break;
            }
            total += level.totalQty;
        }
    } else {
        for (const auto& [price, level] : asks_) {
            if (n++ >= maxLevels) {
                break;
            }
            total += level.totalQty;
        }
    }
    return total;
}

// ------------------------------------------------------------------
// Queue position & surveillance counters
// ------------------------------------------------------------------

std::int64_t OrderBook::qtyAhead(std::int64_t orderId) const {
    auto it = orders_.find(orderId);
    if (it == orders_.end()) {
        return -1;
    }
    const LimitOrder& order = it->second;
    const std::deque<std::int64_t>* queue = nullptr;
    if (order.side == Side::BUY) {
        auto lv = bids_.find(order.price);
        queue = &lv->second.queue;
    } else {
        auto lv = asks_.find(order.price);
        queue = &lv->second.queue;
    }
    std::int64_t ahead = 0;
    for (std::int64_t id : *queue) {
        if (id == orderId) {
            return ahead;
        }
        ahead += orders_.at(id).quantity;
    }
    return ahead;
}

const LimitOrder* OrderBook::order(std::int64_t orderId) const {
    auto it = orders_.find(orderId);
    return it == orders_.end() ? nullptr : &it->second;
}

double OrderBook::orderToTradeRatio() const {
    return tradeCount_ == 0
                   ? std::numeric_limits<double>::infinity()
                   : static_cast<double>(orderCount_ + cancelCount_) /
                             static_cast<double>(tradeCount_);
}

}  // namespace quantfinlib
