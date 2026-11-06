// Live routing: every child carries a limit, a clock and a quote age, and a
// quote of unknown age gets nothing (H18 wiring).
#include <cmath>
#include <limits>
#include <vector>

#include "qtest.hpp"
#include "quantfinlib/execution/child_order_router.hpp"
#include "quantfinlib/execution/hft_sor.hpp"

using namespace quantfinlib;
using smart_order_router::RouteGuard;
using Bench = BenchmarkExecutor::Benchmark;

namespace {

constexpr std::int64_t NOW = 1'000'000;
constexpr std::int64_t NO_BOUND = std::numeric_limits<std::int64_t>::max();

VenueQuote lit(const char* name, double ask, std::int64_t size, std::int64_t stamp) {
    return VenueQuote{name, ask - 0.02, size, ask, size, 0, 0, false, stamp};
}

VenueQuote unstamped(const char* name, double ask, std::int64_t size) {
    VenueQuote q{name, ask - 0.02, size, ask, size, 0, 0, false};
    return q;
}

RouteGuard guard(double limit = 101) { return RouteGuard{limit, NOW, 1000}; }

BenchmarkExecutor::MarketState market() { return BenchmarkExecutor::MarketState::neutral(100, 1.0); }

}  // namespace

TEST(guardRefusesToBeSwitchedOff) {
    std::vector<VenueQuote> v = {lit("A", 100, 100, NOW)};
    ChildOrderRouter router(false);
    EXPECT_THROWS(router.route(Side::BUY, 10, v, RouteGuard{std::nan(""), NOW, 1000}));
    EXPECT_THROWS(router.route(Side::BUY, 10, v, RouteGuard{smart_order_router::marketLimit(Side::BUY), NOW, 1000}));
    EXPECT_THROWS(router.route(Side::BUY, 10, v, RouteGuard{0, NOW, 1000}));
    EXPECT_THROWS(router.route(Side::BUY, 10, v, RouteGuard{101, NOW, NO_BOUND}));
    EXPECT_THROWS(router.route(Side::BUY, 10, v, RouteGuard{101, NOW, -1}));
}

TEST(unstampedAndStaleQuotesGetNothingOnEitherBackEnd) {
    VenueScorecard card(2);
    AdaptiveSor sor(card);
    ChildOrderRouter adaptive(sor);
    ChildOrderRouter plain(false);
    std::vector<VenueQuote> bad = {unstamped("A", 100, 100), lit("B", 100, 100, NOW - 1001),
                                   lit("C", 100, 100, NOW + 1)};
    for (ChildOrderRouter* r : {&adaptive, &plain}) {
        auto d = r->route(Side::BUY, 50, bad, guard());
        EXPECT_TRUE(d.legs.empty() && d.sent == 0 && d.unrouted == 50);
    }
}

TEST(limitBindsOnBothBackEnds) {
    VenueScorecard card(2);
    AdaptiveSor sor(card);
    ChildOrderRouter adaptive(sor);
    ChildOrderRouter plain(false);
    std::vector<VenueQuote> v = {lit("A", 100, 30, NOW), lit("B", 102, 100, NOW)};
    for (ChildOrderRouter* r : {&adaptive, &plain}) {
        auto d = r->route(Side::BUY, 50, v, guard(101));
        EXPECT_TRUE(d.legs.size() == 1 && d.legs[0].venue == "A");
        EXPECT_TRUE(d.sent == 30 && d.unrouted == 20);
    }
}

TEST(executorDispatchSendsWhatRoutedAndCountsItWorking) {
    BenchmarkExecutor parent = BenchmarkExecutor::of(Side::BUY, 100, Bench::TWAP);
    ChildOrderRouter router(false);
    auto d = router.dispatch(parent, 1.0, market(), {lit("A", 100, 60, NOW)}, guard());
    EXPECT_TRUE(d.due == 100 && d.sent == 60 && d.unrouted == 40);
    EXPECT_TRUE(parent.working() == 60);
}

TEST(staleFeedSendsNothingAndTheScheduleAsksAgain) {
    BenchmarkExecutor parent = BenchmarkExecutor::of(Side::BUY, 100, Bench::TWAP);
    ChildOrderRouter router(false);
    auto d = router.dispatch(parent, 1.0, market(), {unstamped("A", 100, 500)}, guard());
    EXPECT_TRUE(d.sent == 0 && parent.working() == 0);
    d = router.dispatch(parent, 1.0, market(), {lit("A", 100, 500, NOW)}, guard());
    EXPECT_TRUE(d.sent == 100 && parent.working() == 100);
}

TEST(badGuardLeavesTheExecutorUntouched) {
    BenchmarkExecutor parent = BenchmarkExecutor::of(Side::BUY, 100, Bench::TWAP);
    ChildOrderRouter router(false);
    EXPECT_THROWS(router.dispatch(parent, 1.0, market(), {lit("A", 100, 500, NOW)}, RouteGuard{101, NOW, NO_BOUND}));
    EXPECT_TRUE(parent.working() == 0);
}

TEST(darkProbesCannotOverfillTheParent) {
    VenueScorecard card(2);
    AdaptiveSor sor(card);
    ChildOrderRouter router(sor);
    BenchmarkExecutor parent = BenchmarkExecutor::of(Side::BUY, 100, Bench::TWAP);
    VenueQuote dark{"D", 99.9, 1000, 100.1, 1000, 0, 0, true, NOW};
    // Lit covers the parent, so the probe has no room: without the trim onSent throws.
    auto d = router.dispatch(parent, 1.0, market(), {lit("A", 100, 500, NOW), dark}, guard());
    EXPECT_TRUE(d.sent == 100 && d.probes.empty());

    BenchmarkExecutor thin = BenchmarkExecutor::of(Side::BUY, 100, Bench::TWAP);
    d = router.dispatch(thin, 1.0, market(), {lit("A", 100, 70, NOW), dark}, guard());
    EXPECT_TRUE(d.probes.size() == 1 && d.probes[0].quantity == 30);
    EXPECT_TRUE(thin.working() == 100);
}

TEST(basketDispatchGuardsEachHandleAndKeepsTheLedger) {
    PortfolioExecutor basket(2, PortfolioExecutor::Config::unconstrained());
    int live = basket.add(BenchmarkExecutor::of(Side::BUY, 100, Bench::TWAP));
    int frozen = basket.add(BenchmarkExecutor::of(Side::SELL, 100, Bench::TWAP));
    ChildOrderRouter router(false);
    std::vector<std::vector<VenueQuote>> venues = {{lit("A", 100, 500, NOW)}, {unstamped("B", 100, 500)}};
    auto out = router.dispatch(basket, 1.0, {market(), market()}, venues,
                               {guard(), RouteGuard{90, NOW, 1000}});
    EXPECT_TRUE(out[0].sent == 100 && out[1].sent == 0);
    EXPECT_TRUE(basket.child(live).working() == 100);
    EXPECT_TRUE(basket.child(frozen).working() == 0);
}

TEST(basketDispatchChecksAllGuardsBeforeSendingAny) {
    PortfolioExecutor basket(2, PortfolioExecutor::Config::unconstrained());
    basket.add(BenchmarkExecutor::of(Side::BUY, 100, Bench::TWAP));
    basket.add(BenchmarkExecutor::of(Side::BUY, 100, Bench::TWAP));
    ChildOrderRouter router(false);
    std::vector<std::vector<VenueQuote>> venues = {{lit("A", 100, 500, NOW)}, {lit("A", 100, 500, NOW)}};
    EXPECT_THROWS(router.dispatch(basket, 1.0, {market(), market()}, venues,
                                  {guard(), RouteGuard{101, NOW, NO_BOUND}}));
    EXPECT_TRUE(basket.child(0).working() == 0);
}

TEST(adaptiveRouterSkipsAVenueWithAnUnusableFee) {
    VenueScorecard card(2);
    AdaptiveSor sor(card);
    VenueQuote bad = lit("BAD", 100, 100, NOW);
    bad.feeBps = std::nan("");
    auto d = sor.route(Side::BUY, 50, {bad, lit("OK", 100, 100, NOW)}, guard());
    EXPECT_TRUE(d.lit.size() == 1 && d.lit[0].venue == "OK");
}

TEST(hotLaneRouterFailsClosedOnAgeAndOnAMissingLimit) {
    HftSor sor(2);
    std::vector<std::int64_t> out(2);
    sor.venueQuote(0, 99, 100, 100, 100);             // never stamped
    sor.venueQuote(1, 99, 100, 100, 100, NOW - 5000); // too old
    EXPECT_TRUE(sor.route(Side::BUY, 50, 101, out.data(), NOW, 1000) == 0);
    sor.venueQuote(1, 99, 100, 100, 100, NOW);
    EXPECT_TRUE(sor.route(Side::BUY, 50, 101, out.data(), NOW, 1000) == 50);
    EXPECT_TRUE(out[0] == 0 && out[1] == 50);
    // A later unstamped update must not inherit the old stamp.
    sor.venueQuote(1, 99, 100, 100, 100);
    EXPECT_TRUE(sor.route(Side::BUY, 50, 101, out.data(), NOW, 1000) == 0);

    sor.venueQuote(1, 99, 100, 100, 100, NOW);
    EXPECT_TRUE(sor.route(Side::BUY, 50, std::numeric_limits<std::int32_t>::max(), out.data(), NOW, 1000) == 0);
    EXPECT_TRUE(sor.route(Side::BUY, 50, 101, out.data(), NOW, NO_BOUND) == 0);
    EXPECT_TRUE(sor.route(Side::BUY, 50, 101, out.data(), NOW, -1) == 0);
    EXPECT_TRUE(out[1] == 0);
}

QTEST_MAIN()
