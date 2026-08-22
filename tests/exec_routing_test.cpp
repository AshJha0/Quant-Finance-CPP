// Phase 5B execution tests: routing (HftSor, SmartOrderRouter, AdaptiveSor
// + VenueScorecard, Ucb1Selector), the dark-pool simulator, and the peg /
// benchmark helpers. Pins are ported 1:1 from the Java reference tests
// (HftSorTest, ExecutionTest, AdaptiveSorTest) so the routing arithmetic
// can never silently drift.
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

#include "qtest.hpp"
#include "quantfinlib/execution/adaptive_sor.hpp"
#include "quantfinlib/execution/dark_pool_simulator.hpp"
#include "quantfinlib/execution/hft_sor.hpp"
#include "quantfinlib/execution/mid_peg_tracker.hpp"
#include "quantfinlib/execution/smart_order_router.hpp"
#include "quantfinlib/execution/ucb1_selector.hpp"
#include "quantfinlib/execution/venue_benchmark.hpp"
#include "quantfinlib/execution/venue_quote.hpp"
#include "quantfinlib/execution/venue_scorecard.hpp"
#include "quantfinlib/orderbook/side.hpp"

using namespace quantfinlib;

namespace {
constexpr std::int64_t US = 1000LL;  // one microsecond in nanos
}  // namespace

// ------------------------------------------------------------------ HftSor

TEST(hftSorRoutesByAllInPriceAcrossVenues) {
    HftSor sor(3);
    sor.venueQuote(0, 9999, 500, 10001, 300);
    sor.venueQuote(1, 9998, 400, 10000, 200);  // best raw ask
    sor.venueQuote(2, 10000, 100, 10002, 900);
    std::vector<std::int64_t> out(3);

    std::int64_t routed = sor.route(Side::BUY, 600, std::numeric_limits<std::int32_t>::max(), out.data());
    EXPECT_TRUE(routed == 600);
    EXPECT_TRUE(out[1] == 200);  // 10_000 first
    EXPECT_TRUE(out[0] == 300);  // then 10_001
    EXPECT_TRUE(out[2] == 100);  // then 10_002

    // Sell sweeps bids from the highest.
    routed = sor.route(Side::SELL, 550, std::numeric_limits<std::int32_t>::min(), out.data());
    EXPECT_TRUE(routed == 550);
    EXPECT_TRUE(out[2] == 100);  // 10_000
    EXPECT_TRUE(out[0] == 450);  // 9_999 (only 450 needed)
    EXPECT_TRUE(out[1] == 0);
}

TEST(hftSorFeesReorderVenuesAndRebatesAttract) {
    HftSor sor(2);
    sor.venueQuote(0, 0, 0, 10000, 500);
    sor.venueQuote(1, 0, 0, 10001, 500);
    sor.fee(0, 3.0);   // 10_000 + 3 = 10_003 all-in
    sor.fee(1, -0.5);  // 10_001 - 0.5 = 10_000.5
    std::vector<std::int64_t> out(2);
    sor.route(Side::BUY, 400, std::numeric_limits<std::int32_t>::max(), out.data());
    EXPECT_TRUE(out[1] == 400);
    EXPECT_TRUE(out[0] == 0);
}

TEST(hftSorLimitPriceExcludesVenuesBeyondIt) {
    HftSor sor(2);
    sor.venueQuote(0, 0, 0, 10000, 100);
    sor.venueQuote(1, 0, 0, 10005, 900);
    std::vector<std::int64_t> out(2);
    std::int64_t routed = sor.route(Side::BUY, 500, 10002, out.data());
    EXPECT_TRUE(routed == 100);
    EXPECT_TRUE(out[0] == 100);
    EXPECT_TRUE(out[1] == 0);
}

TEST(hftSorNanFeeVenueIsExcludedRatherThanPoisoningSelection) {
    HftSor sor(2);
    sor.venueQuote(0, 0, 0, 10000, 500);   // best raw price, but NaN fee
    sor.venueQuote(1, 0, 0, 10005, 500);   // worse raw price, clean fee
    sor.fee(0, std::numeric_limits<double>::quiet_NaN());
    sor.fee(1, 0.0);
    std::vector<std::int64_t> out(2);
    sor.route(Side::BUY, 400, std::numeric_limits<std::int32_t>::max(), out.data());
    EXPECT_TRUE(out[1] == 400);  // venue 0's NaN fee must not poison selection
    EXPECT_TRUE(out[0] == 0);
}

TEST(hftSorEmptyBooksRouteNothing) {
    HftSor sor(2);
    std::vector<std::int64_t> out(2);
    EXPECT_TRUE(sor.route(Side::BUY, 100, std::numeric_limits<std::int32_t>::max(), out.data()) == 0);
    sor.venueQuote(0, 9999, 100, 0, 0);  // bid only
    EXPECT_TRUE(sor.route(Side::BUY, 100, std::numeric_limits<std::int32_t>::max(), out.data()) == 0);
    EXPECT_TRUE(sor.route(Side::SELL, 100, std::numeric_limits<std::int32_t>::min(), out.data()) == 100);
}

// ---------------------------------------------------------- SmartOrderRouter

namespace {
std::vector<VenueQuote> smartOrderRouterVenues() {
    return {VenueQuote{"LIT_A", 99.98, 400, 100.02, 400, 0.0, 500000, false},
            VenueQuote{"LIT_B", 99.97, 600, 100.00, 300, 1.0, 800000, false},
            VenueQuote{"DARK_X", 99.98, 500, 100.02, 500, 0.2, 1000000, true}};
}
}  // namespace

TEST(sorPicksBestAllInPriceAcrossVenues) {
    smart_order_router::RoutingPlan plan =
            smart_order_router::route(Side::BUY, 900, smartOrderRouterVenues(), false);
    EXPECT_TRUE(plan.routedQty == 900);
    EXPECT_TRUE(plan.unroutedQty == 0);
    // Dark mid = 100.00 with 0.2bps fee (100.002 all-in) vs LIT_B ask 100.00
    // + 1bps (100.01) vs LIT_A 100.02. Dark wins, then LIT_B, then LIT_A.
    EXPECT_TRUE(plan.legs[0].venue == "DARK_X");
    EXPECT_TRUE(plan.legs[0].quantity == 500);
    EXPECT_TRUE(plan.legs[1].venue == "LIT_B");
    EXPECT_TRUE(plan.legs[1].quantity == 300);
    EXPECT_TRUE(plan.legs[2].venue == "LIT_A");
    EXPECT_TRUE(plan.legs[2].quantity == 100);
    EXPECT_TRUE(plan.expectedAvgAllInPrice < 100.02);
}

TEST(sorReportsUnroutedWhenLiquidityInsufficient) {
    smart_order_router::RoutingPlan plan =
            smart_order_router::route(Side::BUY, 5000, smartOrderRouterVenues(), false);
    EXPECT_TRUE(plan.routedQty == 1200);
    EXPECT_TRUE(plan.unroutedQty == 3800);
}

TEST(sorSellSideUsesBids) {
    smart_order_router::RoutingPlan plan =
            smart_order_router::route(Side::SELL, 400, smartOrderRouterVenues(), false);
    EXPECT_TRUE(plan.legs.front().venue == "DARK_X");
    EXPECT_TRUE(plan.legs.front().quantity == 400);
}

TEST(sorPreferDarkRoutesDarkFirstEvenWhenLitCompetitive) {
    std::vector<VenueQuote> venues = {
            VenueQuote{"LIT", 99.99, 1000, 100.00, 1000, 0.0, 1, false},
            VenueQuote{"DARK", 99.98, 300, 100.04, 300, 0.0, 1, true}};  // dark mid 100.01 worse than lit ask
    smart_order_router::RoutingPlan plan = smart_order_router::route(Side::BUY, 500, venues, true);
    EXPECT_TRUE(plan.legs.front().venue == "DARK");
    EXPECT_TRUE(plan.legs.front().quantity == 300);
    EXPECT_TRUE(plan.legs[1].venue == "LIT");
}

TEST(sorExcludesNanFeeVenueInsteadOfPoisoningAllInPrice) {
    std::vector<VenueQuote> venues = {
            VenueQuote{"CHEAP_NAN", 0, 0, 100.00, 500, std::numeric_limits<double>::quiet_NaN(), 1, false},
            VenueQuote{"CLEAN", 0, 0, 100.05, 500, 0.0, 1, false}};
    smart_order_router::RoutingPlan plan =
            smart_order_router::route(Side::BUY, 400, venues, false);
    EXPECT_TRUE(plan.legs.size() == 1);
    EXPECT_TRUE(plan.legs[0].venue == "CLEAN");
    EXPECT_TRUE(plan.legs[0].quantity == 400);
}

TEST(sorRejectsNegativeQuantity) {
    EXPECT_THROWS(smart_order_router::route(Side::BUY, -1, smartOrderRouterVenues(), false));
}

TEST(sorRespectsLimitPriceExcludingBestVenue) {
    std::vector<VenueQuote> venues = {
            VenueQuote{"CHEAP_BUT_ABOVE_LIMIT", 0, 0, 100.00, 500, 0.0, 1, false},
            VenueQuote{"COMPLIANT", 0, 0, 100.10, 500, 0.0, 1, false}};
    smart_order_router::RoutingPlan plan =
            smart_order_router::route(Side::BUY, 300, venues, false, 100.05);
    EXPECT_TRUE(plan.legs.size() == 1);
    EXPECT_TRUE(plan.legs[0].venue == "COMPLIANT");
}

TEST(sorReportsShortfallWhenLimitPriceExcludesAllVenues) {
    std::vector<VenueQuote> venues = smartOrderRouterVenues();
    smart_order_router::RoutingPlan plan =
            smart_order_router::route(Side::BUY, 500, venues, false, 100.00);
    EXPECT_TRUE(plan.legs.empty());
    EXPECT_TRUE(plan.unroutedQty == 500);
}

TEST(sorExcludesStaleQuoteEvenWithBestPrice) {
    std::vector<VenueQuote> venues = {
            VenueQuote{"STALE_BEST", 0, 0, 100.00, 500, 0.0, 1, false, 1000},
            VenueQuote{"FRESH", 0, 0, 100.05, 500, 0.0, 1, false, 9000}};
    smart_order_router::RoutingPlan plan = smart_order_router::route(
            Side::BUY, 300, venues, false, std::numeric_limits<double>::infinity(), 10000, 2000);
    EXPECT_TRUE(plan.legs.size() == 1);
    EXPECT_TRUE(plan.legs[0].venue == "FRESH");
}

// -------------------------------------------------------------- MidPegTracker

TEST(midPegRepricesOnlyBeyondThreshold) {
    MidPegTracker peg(Side::BUY, -0.01, std::numeric_limits<double>::quiet_NaN(), 0.02);
    EXPECT_NEAR(peg.onQuote(99.98, 100.02), 99.99, 1e-9);  // first quote always prices
    EXPECT_TRUE(std::isnan(peg.onQuote(99.99, 100.02)));   // mid +0.005: below threshold
    EXPECT_NEAR(peg.onQuote(100.03, 100.07), 100.04, 1e-9);  // big move: reprice
    EXPECT_NEAR(peg.currentPrice(), 100.04, 1e-9);
}

TEST(midPegRespectsLimitCap) {
    MidPegTracker peg(Side::BUY, 0, 100.00, 0.001);
    EXPECT_NEAR(peg.onQuote(100.10, 100.20), 100.00, 1e-9);  // mid 100.15 capped at limit
}

// ------------------------------------------------------------ DarkPoolSimulator

TEST(darkPoolCrossesAtLitMid) {
    DarkPoolSimulator pool;
    pool.onQuote(99.99, 100.01);
    EXPECT_TRUE(pool.submit(Side::SELL, 500, 0).empty());  // rests hidden
    EXPECT_TRUE(pool.restingQty(Side::SELL) == 500);

    std::vector<DarkPoolSimulator::Fill> fills = pool.submit(Side::BUY, 300, 0);
    EXPECT_TRUE(fills.size() == 1);
    EXPECT_NEAR(fills[0].price, 100.00, 1e-9);
    EXPECT_TRUE(fills[0].quantity == 300);
    EXPECT_TRUE(pool.restingQty(Side::SELL) == 200);
}

TEST(darkPoolHonorsMinimumExecutionQuantity) {
    DarkPoolSimulator pool;
    pool.onQuote(99.99, 100.01);
    pool.submit(Side::SELL, 200, 500);  // resting min 500 > its qty: never fillable
    std::vector<DarkPoolSimulator::Fill> fills = pool.submit(Side::BUY, 300, 0);
    EXPECT_TRUE(fills.empty());  // constraint blocks the cross
    EXPECT_TRUE(pool.restingQty(Side::BUY) == 300);  // buyer rests instead
}

TEST(darkPoolAggregateMeqCountsOnlyWhatWouldActuallyFill) {
    // Incoming BUY 100 with aggregate MEQ 100 against SELL 60 (no MEQ) and
    // SELL 60 (MEQ 50). A static pre-scan against the ORIGINAL remaining
    // counts 60 + 60 = 120 and crosses -- but after the first fill only 40
    // remain, below the second seller's MEQ 50, so the true executable
    // total is 60 < 100: the cross must not happen.
    DarkPoolSimulator pool;
    pool.onQuote(99.99, 100.01);
    pool.submit(Side::SELL, 60, 0);
    pool.submit(Side::SELL, 60, 50);
    std::vector<DarkPoolSimulator::Fill> fills = pool.submit(Side::BUY, 100, 100);
    EXPECT_TRUE(fills.empty());  // aggregate MEQ 100 cannot be met: only 60 can execute
    EXPECT_TRUE(pool.restingQty(Side::SELL) == 120);
    EXPECT_TRUE(pool.restingQty(Side::BUY) == 100);

    // Same book, MEQ 60: exactly satisfiable, fills the first seller only.
    DarkPoolSimulator ok;
    ok.onQuote(99.99, 100.01);
    ok.submit(Side::SELL, 60, 0);
    ok.submit(Side::SELL, 60, 50);
    std::vector<DarkPoolSimulator::Fill> okFills = ok.submit(Side::BUY, 100, 60);
    EXPECT_TRUE(okFills.size() == 1);
    EXPECT_TRUE(okFills[0].quantity == 60);
    EXPECT_TRUE(ok.restingQty(Side::SELL) == 60);  // the MEQ-50 seller untouched
    EXPECT_TRUE(ok.restingQty(Side::BUY) == 40);   // remainder rests
}

TEST(darkPoolLockedOrCrossedMarketPausesCrossing) {
    DarkPoolSimulator pool;
    pool.onQuote(100.01, 99.99);  // crossed (bid > ask): must invalidate the mid
    pool.submit(Side::SELL, 500, 0);
    std::vector<DarkPoolSimulator::Fill> fills = pool.submit(Side::BUY, 300, 0);
    EXPECT_TRUE(fills.empty());
    EXPECT_TRUE(pool.restingQty(Side::SELL) == 500);
    EXPECT_TRUE(pool.restingQty(Side::BUY) == 300);
}

// -------------------------------------------------------------- VenueBenchmark

TEST(venueBenchmarkRanksByExecutionQuality) {
    VenueBenchmark bench;
    bench.add(VenueBenchmark::Sample{"GOOD", Side::BUY, 100.01, 100, 100.00, 100.01, 1000000, true})
            .add(VenueBenchmark::Sample{"GOOD", Side::BUY, 100.01, 100, 100.00, 100.02, 1200000, true})
            .add(VenueBenchmark::Sample{"BAD", Side::BUY, 100.05, 100, 100.00, 99.98, 9000000, true})
            .add(VenueBenchmark::Sample{"BAD", Side::BUY, 0, 0, 0, 0, 0, false});

    std::vector<VenueBenchmark::VenueStats> stats = bench.rank();
    EXPECT_TRUE(stats.front().venue == "GOOD");
    EXPECT_NEAR(stats.front().fillRate, 1.0, 1e-12);
    EXPECT_NEAR(stats[1].fillRate, 0.5, 1e-12);
    EXPECT_TRUE(stats[1].avgEffectiveSpreadBps > stats.front().avgEffectiveSpreadBps);
    EXPECT_TRUE(stats[1].avgMarkoutBps < 0);  // adverse selection at BAD
}

// ------------------------------------------------------- AdaptiveSor + Scorecard

TEST(adaptiveSorClassicExampleRoutesBFirstThenAAndProbesDark) {
    // Exchange A: 10,000 @ 120us, Exchange B: 8,000 @ 80us (SAME price),
    // Dark Pool: unknown. Expected: 8,000 to B, 2,000 to A, dark probe.
    VenueScorecard card(3);
    AdaptiveSor sor(card);
    sor.registerVenue("A", 0);
    sor.registerVenue("B", 1);
    sor.registerVenue("DARK", 2);

    std::vector<VenueQuote> venues = {
            VenueQuote{"A", 99.98, 0, 100.00, 10000, 0, 120 * US, false},
            VenueQuote{"B", 99.98, 0, 100.00, 8000, 0, 80 * US, false},
            VenueQuote{"DARK", 99.99, 5000, 100.01, 5000, 0, 200 * US, true}};

    AdaptiveSor::RoutingDecision d = sor.route(Side::BUY, 10000, venues);
    EXPECT_TRUE(d.routedQty == 10000);
    EXPECT_TRUE(d.unrouted == 0);
    EXPECT_TRUE(d.lit[0].venue == "B" && d.lit[0].quantity == 8000);
    EXPECT_TRUE(d.lit[1].venue == "A" && d.lit[1].quantity == 2000);
    EXPECT_TRUE(d.probes.size() == 1);
    EXPECT_TRUE(d.probes[0].venue == "DARK");
    EXPECT_TRUE(d.probes[0].dark);
    EXPECT_NEAR(d.probes[0].price, 100.00, 1e-9);
    EXPECT_TRUE(d.probes[0].quantity == 5000);  // default probe, dark-capped at 50% of 10k
}

TEST(adaptiveSorLatencyBreaksTiesAndReliabilityVetoesUnderThreshold) {
    VenueScorecard card(2, 0.5, 0.95);
    AdaptiveSor sor(card);
    sor.registerVenue("FAST", 0);
    sor.registerVenue("SLOW", 1);
    std::vector<VenueQuote> venues = {VenueQuote{"FAST", 0, 0, 100.00, 5000, 0, 50 * US, false},
                                      VenueQuote{"SLOW", 0, 0, 100.00, 5000, 0, 500 * US, false}};
    // Equal price+fee: the faster venue is cheaper-expected.
    EXPECT_TRUE(sor.route(Side::BUY, 1000, venues).lit[0].venue == "FAST");

    // Now hammer FAST's fill rate below the 0.5 floor: it gets vetoed.
    for (int i = 0; i < 20; i++) {
        card.onMiss(0, 50 * US);
    }
    AdaptiveSor::RoutingDecision d = sor.route(Side::BUY, 1000, venues);
    EXPECT_TRUE(d.lit[0].venue == "SLOW");  // an unreliable venue is vetoed
}

TEST(adaptiveSorPostFillMarkoutMeasuresAdverseSelectionPerVenue) {
    VenueScorecard card(2, 0.5, 0.95, 100 * US);
    // Venue 0: buy at mid 100.00, mid then REVERTS to 99.90 -- adverse.
    card.onFill(0, 50 * US, true, 100.00, 0);
    card.onMid(99.90, 200 * US);
    EXPECT_NEAR(card.postFillMarkout(0), -0.10, 1e-12);
    // Venue 1: sell at 100.00, mid falls -- the move went OUR way.
    card.onFill(1, 50 * US, false, 100.00, 300 * US);
    card.onMid(99.90, 500 * US);
    EXPECT_NEAR(card.postFillMarkout(1), 0.10, 1e-12);
    EXPECT_TRUE(card.maturedFillMarkouts() == 2);

    // Non-finite discipline: NaN/Inf fill mids never arm, NaN/Inf mid
    // updates never mature.
    card.onFill(0, 50 * US, true, std::numeric_limits<double>::quiet_NaN(), 600 * US);
    card.onFill(0, 50 * US, true, std::numeric_limits<double>::infinity(), 700 * US);
    card.onMid(std::numeric_limits<double>::quiet_NaN(), 900 * US);
    card.onMid(std::numeric_limits<double>::infinity(), 950 * US);
    EXPECT_TRUE(card.maturedFillMarkouts() == 2);
    EXPECT_NEAR(card.postFillMarkout(0), -0.10, 1e-12);
}

TEST(adaptiveSorDarkProbeSizeIsLearnedFromRealizedFills) {
    VenueScorecard card(1, 0.5, 0.95);
    AdaptiveSor sor(card);
    sor.registerVenue("DARK", 0);
    std::vector<VenueQuote> venues = {VenueQuote{"DARK", 99.99, 1, 100.01, 1, 0, 200 * US, true}};
    // Unknown pool: default probe (5,000), capped at 50% of 6,000 = 3,000.
    EXPECT_TRUE(sor.route(Side::BUY, 6000, venues).probes[0].quantity == 3000);
    // Learn that this pool typically fills ~1,200 shares.
    for (int i = 0; i < 30; i++) {
        card.onDarkProbe(0, 1200);
    }
    EXPECT_TRUE(sor.route(Side::BUY, 100000, venues).probes[0].quantity == 1200);
}

TEST(adaptiveSorFirstSuccessfulFillDoesNotCraterFillRate) {
    // Regression: EWMAs must seed from the prior/first observation, not
    // ramp from 0 -- otherwise a venue's first fill records fillRate 0.05
    // and the router vetoes the venue that just proved it works.
    VenueScorecard card(2, 0.05, 0.95);
    card.onFill(0, 100 * US);
    EXPECT_TRUE(card.fillRate(0) > 0.9);
    EXPECT_NEAR(card.measuredLatencyNanos(0), static_cast<double>(100 * US), 1e-6);

    AdaptiveSor sor(card);
    sor.registerVenue("PROVEN", 0);
    sor.registerVenue("UNTRIED", 1);
    std::vector<VenueQuote> venues = {VenueQuote{"PROVEN", 0, 0, 100.00, 5000, 0, 100 * US, false},
                                      VenueQuote{"UNTRIED", 0, 0, 100.01, 5000, 0, 100 * US, false}};
    EXPECT_TRUE(sor.route(Side::BUY, 1000, venues).lit[0].venue == "PROVEN");
}

TEST(adaptiveSorConfigAndRegistrationValidate) {
    VenueScorecard card(2);
    EXPECT_THROWS(AdaptiveSor::Config(-1, 1, 0.5, 5000, 0.5));
    EXPECT_THROWS(AdaptiveSor::Config(1, 1, 1.5, 5000, 0.5));
    AdaptiveSor sor(card);
    EXPECT_THROWS(sor.registerVenue("X", 5));
    EXPECT_THROWS(VenueScorecard(0));
}

// -------------------------------------------------------------- Ucb1Selector

TEST(ucb1SelectorTriesUntriedArmsFirstThenExploitsWithOptimism) {
    Ucb1Selector sel(3);
    // Each arm earns one look first, in index order, regardless of reward.
    EXPECT_TRUE(sel.select() == 0);
    sel.record(0, 0.1);
    EXPECT_TRUE(sel.select() == 1);
    sel.record(1, 0.9);
    EXPECT_TRUE(sel.select() == 2);
    sel.record(2, 0.5);

    // All arms tried once: highest UCB (mean + optimism bonus) wins.
    // Arm 1 has the best mean reward and ties on pulls, so it should lead.
    int next = sel.select();
    EXPECT_TRUE(next == 1);
    EXPECT_TRUE(std::isnan(Ucb1Selector(2).meanReward(0)));
    EXPECT_TRUE(sel.totalPulls() == 3);

    EXPECT_THROWS(Ucb1Selector(1));
    EXPECT_THROWS(sel.record(5, 0.5));
    EXPECT_THROWS(sel.record(0, 1.5));
}

QTEST_MAIN()
