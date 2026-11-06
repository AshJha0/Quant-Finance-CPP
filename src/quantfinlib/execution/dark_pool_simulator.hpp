// Port of com.quantfinlib.execution.DarkPoolSimulator: midpoint-cross dark
// pool model -- hidden resting orders match at the current lit-market
// midpoint, honoring minimum-execution-quantity constraints (a standard
// anti-gaming feature). No pre-trade transparency: resting interest is
// only observable through fills. IllegalArgumentException maps to
// std::invalid_argument.
#pragma once

#include <cstdint>
#include <limits>
#include <list>
#include <vector>

#include "quantfinlib/orderbook/side.hpp"

namespace quantfinlib {

class DarkPoolSimulator {
public:
    struct Fill {
        std::int64_t buyOrderId;
        std::int64_t sellOrderId;
        double price;
        std::int64_t quantity;
    };

    DarkPoolSimulator() = default;

    /**
     * Update the lit reference mid. A LOCKED or CROSSED reference (bid >=
     * ask) or a non-positive/non-finite side invalidates the mid: a real
     * midpoint pool is prohibited from executing during a locked/crossed
     * NBBO, so crossing pauses until a valid two-sided market returns --
     * resting interest stays resting.
     */
    void onQuote(double bid, double ask);

    /**
     * Submits an order: crosses immediately against resting contra
     * interest at the current mid (time priority), then rests the
     * remainder. Returns the fills generated (empty if it fully rested).
     *
     * Minimum-execution-quantity is honored AGGREGATE-first, the common
     * pool semantics: the incoming order's MEQ is checked against the
     * total crossable contra quantity (an order wanting 100 fills against
     * two resting 60s), while each resting order's own MEQ still gates its
     * individual slice. MEQ guards the first execution: once an order has
     * traded at least that much, its remainder rests with no minimum, or a
     * tail smaller than the MEQ could never fill.
     */
    std::vector<Fill> submit(Side side, std::int64_t quantity, std::int64_t minExecutionQty);

    /** Total hidden resting quantity on a side (for simulation introspection only). */
    std::int64_t restingQty(Side side) const;

private:
    struct Resting {
        std::int64_t id;
        std::int64_t minQty;
        std::int64_t qty;
    };

    std::list<Resting> buys_;
    std::list<Resting> sells_;
    double mid_ = std::numeric_limits<double>::quiet_NaN();
    std::int64_t nextId_ = 1;
};

}  // namespace quantfinlib
