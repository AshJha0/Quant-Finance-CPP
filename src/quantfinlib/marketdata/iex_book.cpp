#include "quantfinlib/marketdata/iex_book.hpp"

namespace quantfinlib {

namespace {

template <class Map>
bool setLevel(Map& levels, std::int64_t price, std::int64_t size, std::uint64_t& refused) {
    if (size == 0) {
        return levels.erase(price) != 0;
    }
    if (levels.size() >= IexLevelBook::MAX_LEVELS && levels.find(price) == levels.end()) {
        ++refused;
        return false;
    }
    levels[price] = size;
    return true;
}

template <class Map>
std::int64_t levelSize(const Map& levels, std::int64_t price) {
    auto it = levels.find(price);
    return it == levels.end() ? 0 : it->second;
}

}  // namespace

bool IexLevelBook::set(bool buy, std::int64_t price, std::int64_t size) {
    if (size < 0) {
        ++refused_;
        return false;
    }
    return buy ? setLevel(bids_, price, size, refused_) : setLevel(asks_, price, size, refused_);
}

bool IexLevelBook::add(bool buy, std::int64_t price, std::int64_t delta) {
    std::int64_t now = size(buy, price) + delta;
    if (now < 0) {
        ++refused_;
        return false;
    }
    return set(buy, price, now) || now == 0;
}

std::int64_t IexLevelBook::size(bool buy, std::int64_t price) const {
    return buy ? levelSize(bids_, price) : levelSize(asks_, price);
}

IexBbo IexLevelBook::bbo() const {
    IexBbo out;
    if (!bids_.empty()) {
        out.bid = bids_.begin()->first;
        out.bidSize = bids_.begin()->second;
    }
    if (!asks_.empty()) {
        out.ask = asks_.begin()->first;
        out.askSize = asks_.begin()->second;
    }
    return out;
}

void IexLevelBook::clear() {
    bids_.clear();
    asks_.clear();
}

void IexOrderBook::take(const Order& order, std::int64_t size) {
    if (!levels_.add(order.buy, order.price, -size)) {
        ++stats_.negativeLevels;
    }
}

bool IexOrderBook::add(std::int64_t id, bool buy, std::int64_t price, std::uint32_t size) {
    if (orders_.size() >= MAX_ORDERS) {
        ++stats_.refused;
        return false;
    }
    if (!orders_.emplace(id, Order{buy, size, price}).second) {
        ++stats_.duplicateOrders;
        return false;
    }
    if (!levels_.add(buy, price, size)) {
        orders_.erase(id);
        ++stats_.refused;
        return false;
    }
    return true;
}

bool IexOrderBook::modify(std::int64_t id, std::int64_t price, std::uint32_t size) {
    auto it = orders_.find(id);
    if (it == orders_.end()) {
        ++stats_.unknownOrders;
        return false;
    }
    take(it->second, it->second.size);
    if (!levels_.add(it->second.buy, price, size)) {
        orders_.erase(it);
        ++stats_.refused;
        return false;
    }
    it->second.price = price;
    it->second.size = size;
    return true;
}

bool IexOrderBook::remove(std::int64_t id) {
    auto it = orders_.find(id);
    if (it == orders_.end()) {
        ++stats_.unknownOrders;
        return false;
    }
    take(it->second, it->second.size);
    orders_.erase(it);
    return true;
}

bool IexOrderBook::execute(std::int64_t id, std::uint32_t size) {
    auto it = orders_.find(id);
    if (it == orders_.end()) {
        ++stats_.unknownOrders;
        return false;
    }
    if (size > it->second.size) {
        ++stats_.oversizeExecutions;
        return false;
    }
    take(it->second, size);
    it->second.size -= size;
    if (it->second.size == 0) {
        orders_.erase(it);
    }
    return true;
}

void IexOrderBook::clear() {
    orders_.clear();
    levels_.clear();
}

const IexOrderBook::Order* IexOrderBook::find(std::int64_t id) const {
    auto it = orders_.find(id);
    return it == orders_.end() ? nullptr : &it->second;
}

}  // namespace quantfinlib
