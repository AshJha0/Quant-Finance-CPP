#include "quantfinlib/execution/child_order_router.hpp"

#include <algorithm>
#include <stdexcept>

namespace quantfinlib {

ChildOrderRouter::Dispatch ChildOrderRouter::route(Side side, std::int64_t quantity,
                                                   const std::vector<VenueQuote>& venues,
                                                   const smart_order_router::RouteGuard& guard) {
    Dispatch d;
    d.due = quantity;
    if (adaptive_ != nullptr) {
        AdaptiveSor::RoutingDecision decision = adaptive_->route(side, quantity, venues, guard);
        d.legs = std::move(decision.lit);
        d.probes = std::move(decision.probes);
        d.sent = decision.routedQty;
        d.unrouted = decision.unrouted;
    } else {
        smart_order_router::RoutingPlan plan = smart_order_router::route(side, quantity, venues, preferDark_, guard);
        d.legs = std::move(plan.legs);
        d.sent = plan.routedQty;
        d.unrouted = plan.unroutedQty;
    }
    for (const auto& probe : d.probes) {
        d.sent += probe.quantity;
    }
    return d;
}

void ChildOrderRouter::trimProbes(Dispatch& d, std::int64_t room) {
    std::vector<smart_order_router::RouteLeg> kept;
    std::int64_t sent = d.sent;
    for (const auto& probe : d.probes) {
        sent -= probe.quantity;
    }
    for (auto probe : d.probes) {
        probe.quantity = std::min(probe.quantity, room);
        if (probe.quantity > 0) {
            room -= probe.quantity;
            sent += probe.quantity;
            kept.push_back(probe);
        }
    }
    d.probes = std::move(kept);
    d.sent = sent;
}

ChildOrderRouter::Dispatch ChildOrderRouter::dispatch(BenchmarkExecutor& parent, double scheduleFraction,
                                                      const BenchmarkExecutor::MarketState& market,
                                                      const std::vector<VenueQuote>& venues,
                                                      const smart_order_router::RouteGuard& guard) {
    smart_order_router::requireGuard(guard);
    std::int64_t due = parent.dueQuantity(scheduleFraction, market);
    if (due <= 0) {
        return Dispatch{};
    }
    Dispatch d = route(parent.side(), due, venues, guard);
    std::int64_t lit = due - d.unrouted;
    trimProbes(d, parent.parentQty() - parent.executed() - parent.working() - lit);
    parent.onSent(d.sent);
    return d;
}

std::vector<ChildOrderRouter::Dispatch> ChildOrderRouter::dispatch(
        PortfolioExecutor& basket, double scheduleFraction,
        const std::vector<BenchmarkExecutor::MarketState>& markets,
        const std::vector<std::vector<VenueQuote>>& venues,
        const std::vector<smart_order_router::RouteGuard>& guards) {
    auto count = static_cast<std::size_t>(basket.size());
    if (venues.size() < count || guards.size() < count) {
        throw std::invalid_argument("venues and guards must cover every handle");
    }
    for (std::size_t h = 0; h < count; h++) {
        smart_order_router::requireGuard(guards[h]);
    }
    std::vector<std::int64_t> due(count, 0);
    basket.decide(scheduleFraction, markets, due);
    std::vector<Dispatch> out(count);
    for (std::size_t h = 0; h < count; h++) {
        if (due[h] <= 0) {
            continue;
        }
        const BenchmarkExecutor& child = basket.child(static_cast<int>(h));
        out[h] = route(child.side(), due[h], venues[h], guards[h]);
        std::int64_t lit = due[h] - out[h].unrouted;
        trimProbes(out[h], child.parentQty() - child.executed() - child.working() - lit);
        basket.onSent(static_cast<int>(h), out[h].sent);
    }
    return out;
}

}  // namespace quantfinlib
