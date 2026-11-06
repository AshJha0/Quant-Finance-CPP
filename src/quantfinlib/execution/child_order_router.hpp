// The one path from a schedule to venues. BenchmarkExecutor and
// PortfolioExecutor decide how much is due; this class turns that into child
// orders and keeps the executor's in-flight count right, and it cannot be
// called without a RouteGuard. A parent's limit, the clock and the maximum
// quote age therefore reach the router on every child.
//
// Two routing back ends, chosen at construction:
//  - AdaptiveSor, when a VenueScorecard has real fill history to learn from;
//  - smart_order_router::route, the stateless all-in-price router, for venues
//    with no history yet.
//
// Fail closed: when every quote is stale or through the limit nothing is sent
// and nothing is marked working, so the schedule asks again next interval.
// AdaptiveSor's dark probes are additive to the lit legs. They are trimmed
// here so lit plus probes never exceed what the parent has left, which is the
// overfill check the router leaves to its caller.
//
// The caller owns sending the legs and reports fills and cancels back to the
// executor as usual. Single writer, like the executors.
#pragma once

#include <cstdint>
#include <vector>

#include "quantfinlib/execution/adaptive_sor.hpp"
#include "quantfinlib/execution/benchmark_executor.hpp"
#include "quantfinlib/execution/portfolio_executor.hpp"
#include "quantfinlib/execution/smart_order_router.hpp"
#include "quantfinlib/execution/venue_quote.hpp"

namespace quantfinlib {

class ChildOrderRouter {
public:
    struct Dispatch {
        std::vector<smart_order_router::RouteLeg> legs;
        std::vector<smart_order_router::RouteLeg> probes;  // contingent dark legs
        std::int64_t due = 0;       // what the schedule asked for
        std::int64_t sent = 0;      // legs plus probes, now counted as working
        std::int64_t unrouted = 0;  // due that no eligible venue could take
    };

    /** Routes through the stateless router. */
    explicit ChildOrderRouter(bool preferDark) : preferDark_(preferDark) {}

    /** Routes through the adaptive router, which must outlive this object. */
    explicit ChildOrderRouter(AdaptiveSor& adaptive) : adaptive_(&adaptive) {}

    /** Plans one child; touches no executor. @throws std::invalid_argument on a bad guard */
    Dispatch route(Side side, std::int64_t quantity, const std::vector<VenueQuote>& venues,
                   const smart_order_router::RouteGuard& guard);

    /** One interval of one parent: dueQuantity, route, then onSent for what went out. */
    Dispatch dispatch(BenchmarkExecutor& parent, double scheduleFraction,
                      const BenchmarkExecutor::MarketState& market, const std::vector<VenueQuote>& venues,
                      const smart_order_router::RouteGuard& guard);

    /**
     * One interval of a basket: decide, then route each handle against its
     * own venues and guard. All guards are checked before anything is sent.
     * The three vectors are indexed by handle.
     */
    std::vector<Dispatch> dispatch(PortfolioExecutor& basket, double scheduleFraction,
                                   const std::vector<BenchmarkExecutor::MarketState>& markets,
                                   const std::vector<std::vector<VenueQuote>>& venues,
                                   const std::vector<smart_order_router::RouteGuard>& guards);

private:
    /** Trims probes to `room`, the parent quantity left after the lit legs. */
    static void trimProbes(Dispatch& d, std::int64_t room);

    AdaptiveSor* adaptive_ = nullptr;
    bool preferDark_ = false;
};

}  // namespace quantfinlib
