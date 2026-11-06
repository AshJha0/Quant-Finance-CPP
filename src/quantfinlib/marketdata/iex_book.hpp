// Books rebuilt from IEX feeds, used by the replay checks.
//
// IexLevelBook is the DEEP view: aggregate size per price. IexOrderBook is the
// DEEP+ view: displayed orders, aggregated into an IexLevelBook. Both are
// bounded, so a hostile stream cannot grow them without limit: an update that
// would pass the bound is refused and counted.
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <unordered_map>

namespace quantfinlib {

struct IexBbo {
    std::int64_t bid = 0;  // 0 when the side is empty
    std::int64_t bidSize = 0;
    std::int64_t ask = 0;
    std::int64_t askSize = 0;

    bool operator==(const IexBbo&) const = default;
    bool crossed() const { return bid != 0 && ask != 0 && bid > ask; }
    bool locked() const { return bid != 0 && bid == ask; }
};

class IexLevelBook {
public:
    static constexpr std::size_t MAX_LEVELS = 1U << 16;

    /** Sets a level's size; 0 removes it. False when refused, or removing an absent level. */
    bool set(bool buy, std::int64_t price, std::int64_t size);

    /** Adds delta to a level. False, with no change, if the result would go below zero. */
    bool add(bool buy, std::int64_t price, std::int64_t delta);

    std::int64_t size(bool buy, std::int64_t price) const;
    IexBbo bbo() const;
    bool empty() const { return bids_.empty() && asks_.empty(); }
    void clear();

    std::uint64_t refused() const { return refused_; }

private:
    std::map<std::int64_t, std::int64_t, std::greater<>> bids_;
    std::map<std::int64_t, std::int64_t> asks_;
    std::uint64_t refused_ = 0;
};

class IexOrderBook {
public:
    static constexpr std::size_t MAX_ORDERS = 1U << 22;

    struct Order {
        bool buy;
        std::uint32_t size;
        std::int64_t price;
    };

    struct Stats {
        std::uint64_t unknownOrders = 0;
        std::uint64_t duplicateOrders = 0;
        std::uint64_t oversizeExecutions = 0;
        std::uint64_t negativeLevels = 0;
        std::uint64_t refused = 0;
    };

    bool add(std::int64_t id, bool buy, std::int64_t price, std::uint32_t size);
    bool modify(std::int64_t id, std::int64_t price, std::uint32_t size);
    bool remove(std::int64_t id);
    /** Reduces the order; false when unknown or the size exceeds what remains. */
    bool execute(std::int64_t id, std::uint32_t size);
    void clear();

    const Order* find(std::int64_t id) const;
    const std::unordered_map<std::int64_t, Order>& orders() const { return orders_; }
    const IexLevelBook& levels() const { return levels_; }
    const Stats& stats() const { return stats_; }

private:
    void take(const Order& order, std::int64_t size);

    std::unordered_map<std::int64_t, Order> orders_;
    IexLevelBook levels_;
    Stats stats_;
};

}  // namespace quantfinlib
