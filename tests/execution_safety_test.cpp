// Execution and central risk book behaviour that must fail closed: in-flight
// accounting, horizon completion, lots, router eligibility, dark MEQ, quoting
// on bad inputs, CRB re-marks and currency conversion.
#include <cmath>
#include <cstdint>
#include <limits>
#include <numeric>
#include <vector>

#include "qtest.hpp"
#include "quantfinlib/crb/central_risk_book.hpp"
#include "quantfinlib/crb/crb_pnl_ledger.hpp"
#include "quantfinlib/execution/adaptive_sor.hpp"
#include "quantfinlib/execution/benchmark_executor.hpp"
#include "quantfinlib/execution/dark_pool_simulator.hpp"
#include "quantfinlib/execution/hft_sor.hpp"
#include "quantfinlib/execution/liquidity_seeking_algo.hpp"
#include "quantfinlib/execution/portfolio_executor.hpp"
#include "quantfinlib/execution/pov_tracker.hpp"
#include "quantfinlib/execution/smart_order_router.hpp"
#include "quantfinlib/execution/twap_scheduler.hpp"
#include "quantfinlib/execution/venue_scorecard.hpp"
#include "quantfinlib/execution/vwap_scheduler.hpp"
#include "quantfinlib/microstructure/avellaneda_stoikov.hpp"

using namespace quantfinlib;
using Bench = BenchmarkExecutor::Benchmark;

namespace {
const double kNaN = std::numeric_limits<double>::quiet_NaN();
BenchmarkExecutor::MarketState neutral(double f) {
    return BenchmarkExecutor::MarketState::neutral(100, f);
}
}  // namespace

// ---------------------------------------------------------- BenchmarkExecutor

TEST(workingQuantityIsNotReleasedTwice) {
    BenchmarkExecutor e = BenchmarkExecutor::of(Side::BUY, 1000, Bench::TWAP);
    std::int64_t due = e.dueQuantity(0.5, neutral(0.5));
    EXPECT_TRUE(due == 500);
    e.onSent(due);
    EXPECT_TRUE(e.working() == 500);
    EXPECT_TRUE(e.dueQuantity(0.5, neutral(0.5)) == 0);  // was 500 again
    e.onFill(200);
    EXPECT_TRUE(e.working() == 300 && e.executed() == 200);
    EXPECT_TRUE(e.dueQuantity(0.5, neutral(0.5)) == 0);
    e.onCancelled(300);
    EXPECT_TRUE(e.dueQuantity(0.5, neutral(0.5)) == 300);
}

TEST(sendingMoreThanTheParentThrows) {
    BenchmarkExecutor e = BenchmarkExecutor::of(Side::BUY, 1000, Bench::TWAP);
    e.onSent(900);
    EXPECT_THROWS(e.onSent(101));
    e.onSent(100);
    EXPECT_TRUE(e.dueQuantity(1.0, neutral(1.0)) == 0);
}

TEST(povCountsWorkingQuantity) {
    BenchmarkExecutor e = BenchmarkExecutor::pov(Side::BUY, 1000, 0.1);
    e.onMarketVolume(1000);
    std::int64_t due = e.dueQuantity(0, neutral(0));
    EXPECT_TRUE(due == 100);
    e.onSent(due);
    EXPECT_TRUE(e.dueQuantity(0, neutral(0)) == 0);
}

TEST(horizonEndAsksForEverythingLeftWhateverTheSignals) {
    BenchmarkExecutor e = BenchmarkExecutor::of(Side::BUY, 1000, Bench::TWAP);
    auto adverse = neutral(1.0);
    adverse.alpha = -5;  // used to clamp urgency to 0.25: 764 of 1,000 after five polls
    EXPECT_TRUE(e.dueQuantity(1.0, adverse) == 1000);
    BenchmarkExecutor v = BenchmarkExecutor::of(Side::BUY, 1000, Bench::VWAP);
    auto curve = neutral(0.6);  // the volume curve ran short of the clock
    EXPECT_TRUE(v.dueQuantity(1.0, curve) == 1000);
}

TEST(horizonCompletionStillRespectsDepth) {
    BenchmarkExecutor e = BenchmarkExecutor::of(Side::BUY, 1000, Bench::TWAP);
    auto m = neutral(1.0);
    m.displayedDepth = 400;
    EXPECT_TRUE(e.dueQuantity(1.0, m) == 100);
}

TEST(participationIgnoresTheClockAtHorizonEnd) {
    BenchmarkExecutor e = BenchmarkExecutor::pov(Side::BUY, 1000, 0.1);
    EXPECT_TRUE(e.dueQuantity(1.0, neutral(1.0)) == 0);  // no volume, no shares
}

TEST(nanDepthAndNanFractionSendNothing) {
    BenchmarkExecutor e = BenchmarkExecutor::of(Side::BUY, 1000, Bench::TWAP);
    auto m = neutral(1.0);
    m.displayedDepth = kNaN;
    EXPECT_TRUE(e.dueQuantity(1.0, m) == 0);
    m.displayedDepth = -1;
    EXPECT_TRUE(e.dueQuantity(1.0, m) == 0);
    e.onFill(10);
    EXPECT_TRUE(e.dueQuantity(kNaN, neutral(0.5)) == 0);
}

TEST(nanFractionDoesNotStopAVolumeDrivenParent) {
    BenchmarkExecutor e = BenchmarkExecutor::pov(Side::BUY, 1000, 0.1);
    e.onMarketVolume(1000);
    EXPECT_TRUE(e.dueQuantity(kNaN, neutral(0)) == 100);
}

TEST(dueQuantityRoundsToLotsAndReleasesTheOddTail) {
    BenchmarkExecutor e = BenchmarkExecutor::of(Side::BUY, 1050, Bench::TWAP);
    e.withLotSize(100);
    EXPECT_TRUE(e.dueQuantity(0.5, neutral(0.5)) == 500);  // 525 rounded down
    e.onFill(500);
    EXPECT_TRUE(e.dueQuantity(1.0, neutral(1.0)) == 550);  // the whole remainder
    EXPECT_THROWS(e.withLotSize(0));
}

TEST(lotsBelowDepthCapGiveZeroRatherThanAnOddLot) {
    BenchmarkExecutor e = BenchmarkExecutor::of(Side::BUY, 1000, Bench::TWAP);
    e.withLotSize(100);
    auto m = neutral(1.0);
    m.displayedDepth = 200;  // cap 50: below one lot
    EXPECT_TRUE(e.dueQuantity(1.0, m) == 0);
}

// ---------------------------------------------------------------- PovTracker

TEST(povTrackerCountsWorkingAndCompletesASmallTail) {
    PovTracker p(1000, 0.5, 100, 1000);
    p.onMarketVolume(400);
    std::int64_t due = p.dueQuantity();
    EXPECT_TRUE(due == 200);
    p.onSent(due);
    EXPECT_TRUE(p.dueQuantity() == 0);
    p.onExecuted(200);
    EXPECT_TRUE(p.working() == 0);
    EXPECT_THROWS(p.onSent(801));
    // 950 done: a 50-share tail is under minSlice but must still go out.
    p.onExecuted(750);
    p.onMarketVolume(1600);
    EXPECT_TRUE(p.dueQuantity() == 50);
}

// ----------------------------------------------------------------- portfolio

TEST(portfolioLegBandCountsWorkingOrders) {
    PortfolioExecutor pe(2, PortfolioExecutor::Config(50000, std::numeric_limits<double>::infinity()));
    int buy = pe.add(BenchmarkExecutor::of(Side::BUY, 1000, Bench::TWAP));
    std::vector<BenchmarkExecutor::MarketState> states = {neutral(0.4)};
    std::vector<std::int64_t> due(1, 0);
    pe.decide(0.4, states, due);
    EXPECT_TRUE(due[0] == 400);  // 40,000 inside the band
    pe.onSent(buy, 400);
    pe.decide(0.8, states, due);
    EXPECT_TRUE(due[0] == 100);  // 40,000 in flight leaves 10,000 of band
    pe.onCancelled(buy, 400);
    EXPECT_TRUE(pe.child(buy).working() == 0);
}

// ------------------------------------------------------------ liquidity seek

TEST(liquiditySeekFailsClosedOnNanFractionAndNanDepth) {
    LiquiditySeekingAlgo seek(1000);
    BenchmarkExecutor::MarketState m{100, 0.01, 0.1, kNaN, 0.5, 0, 1};
    EXPECT_TRUE(seek.dueQuantity(0.3, m, 0.02) == 0);
    m.displayedDepth = 1000;
    EXPECT_TRUE(seek.dueQuantity(0.3, m, 0.02) == 250);
    EXPECT_TRUE(seek.dueQuantity(kNaN, m, 0.02) == 0);
}

// ---------------------------------------------------------------- schedulers

TEST(zeroVwapProfileThrowsInsteadOfDumpingInSliceZero) {
    EXPECT_THROWS(vwap_scheduler::schedule(100000, {0, 0, 0, 0}, 3600000));
}

TEST(roundToLotsCarriesLeftoversAndKeepsTheTotal) {
    auto slices = twap_scheduler::schedule(1050, 1000, 4);  // 263 263 262 262
    auto lots = vwap_scheduler::roundToLots(slices, 100);
    std::int64_t total = 0;
    for (std::size_t i = 0; i < lots.size(); i++) {
        total += lots[i].quantity;
        if (i + 1 < lots.size()) {
            EXPECT_TRUE(lots[i].quantity % 100 == 0);
        }
    }
    EXPECT_TRUE(total == 1050);
    EXPECT_TRUE(lots[0].quantity == 200 && lots[1].quantity == 300);  // 63 carried, then 26
    EXPECT_TRUE(lots.back().quantity == 350);
    EXPECT_THROWS(vwap_scheduler::roundToLots(slices, 0));
}

// ----------------------------------------------------------------------- SOR

namespace {
VenueQuote lit(const char* name, double bid, double ask, double fee, std::int64_t latency = 0) {
    return VenueQuote{name, bid, 100, ask, 100, fee, latency, false};
}
}  // namespace

TEST(unsetStampIsStaleOnceAMaxAgeIsSet) {
    std::vector<VenueQuote> v{lit("A", 99, 100, 0)};
    auto p = smart_order_router::route(Side::BUY, 100, v, false, 1e9, 5000000000LL, 1000);
    EXPECT_TRUE(p.legs.empty());
    p = smart_order_router::route(Side::BUY, 100, v, false);  // no max age: not checked
    EXPECT_TRUE(p.legs.size() == 1);
}

TEST(staleBoundaryAndFutureStamps) {
    std::vector<VenueQuote> v{lit("A", 99, 100, 0)};
    v[0].lastUpdateNanos = 4000;
    EXPECT_TRUE(smart_order_router::route(Side::BUY, 100, v, false, 1e9, 5000, 1000).legs.size() == 1);
    EXPECT_TRUE(smart_order_router::route(Side::BUY, 100, v, false, 1e9, 5001, 1000).legs.empty());
    EXPECT_TRUE(smart_order_router::route(Side::BUY, 100, v, false, 1e9, 3999, 1000).legs.empty());
}

TEST(nanLimitAndNegativeAgeAreRefused) {
    std::vector<VenueQuote> v{lit("A", 99, 100, 0)};
    EXPECT_THROWS(smart_order_router::route(Side::BUY, 100, v, false, kNaN));
    EXPECT_THROWS(smart_order_router::route(Side::BUY, 100, v, false, 1e9, 0, -1));
}

TEST(nonPositivePricesAreNeverRouted) {
    std::vector<VenueQuote> zero{lit("A", 0.0, 100, 0)};
    EXPECT_TRUE(smart_order_router::route(Side::SELL, 100, zero, false).legs.empty());
    std::vector<VenueQuote> negative{lit("A", 99, -1, 0)};
    EXPECT_TRUE(smart_order_router::route(Side::BUY, 100, negative, false).legs.empty());
    VenueQuote dark{"D", 0.0, 100, 100, 100, 0, 0, true};  // one-sided reference
    EXPECT_TRUE(smart_order_router::route(Side::SELL, 100, {dark}, false).legs.empty());
}

TEST(latencyBreaksATieAndCanBeSwitchedOff) {
    std::vector<VenueQuote> v{lit("SLOW", 99.99, 100, 0, 50000000), lit("FAST", 99.99, 100, 0, 1000)};
    auto p = smart_order_router::route(Side::BUY, 100, v, false);
    EXPECT_TRUE(p.legs[0].venue == "FAST");
    smart_order_router::RouteOptions o;
    o.limitPrice = smart_order_router::marketLimit(Side::BUY);
    o.latencyBpsPerMs = 0;
    EXPECT_TRUE(smart_order_router::route(Side::BUY, 100, v, false, o).legs[0].venue == "SLOW");
}

TEST(latencyDoesNotChangeTheReportedAllInPrice) {
    std::vector<VenueQuote> v{lit("SLOW", 99.99, 100, 0, 50000000)};
    EXPECT_NEAR(smart_order_router::route(Side::BUY, 100, v, false).expectedAvgAllInPrice, 100, 1e-12);
}

TEST(lowFeeCannotTradeThroughABetterDisplayedQuote) {
    std::vector<VenueQuote> v{lit("A", 99.99, 100.00, 30), lit("B", 99.98, 100.01, 0)};
    auto p = smart_order_router::route(Side::BUY, 100, v, false);
    EXPECT_TRUE(p.legs[0].venue == "A");
    smart_order_router::RouteOptions o;
    o.limitPrice = smart_order_router::marketLimit(Side::BUY);
    o.allowTradeThrough = true;
    EXPECT_TRUE(smart_order_router::route(Side::BUY, 100, v, false, o).legs[0].venue == "B");
    // Sell side mirrors it.
    std::vector<VenueQuote> s{lit("A", 100.00, 100.5, 30), lit("B", 99.99, 100.5, 0)};
    EXPECT_TRUE(smart_order_router::route(Side::SELL, 100, s, false).legs[0].venue == "A");
}

TEST(feesStillRankVenuesShowingTheSamePrice) {
    std::vector<VenueQuote> v{lit("A", 99.99, 100, 30), lit("B", 99.99, 100, 0)};
    EXPECT_TRUE(smart_order_router::route(Side::BUY, 100, v, false).legs[0].venue == "B");
}

TEST(hftSorGuardsTradeThroughAndRefusesZeroPrices) {
    HftSor sor(2);
    std::vector<std::int64_t> out(2, 0);
    sor.venueQuote(0, 0, 0, 10000, 100);
    sor.venueQuote(1, 0, 0, 10001, 100);
    sor.fee(0, 3.0);
    sor.route(Side::BUY, 100, std::numeric_limits<std::int32_t>::max(), out.data());
    EXPECT_TRUE(out[0] == 100 && out[1] == 0);
    sor.allowTradeThrough(true);
    sor.route(Side::BUY, 100, std::numeric_limits<std::int32_t>::max(), out.data());
    EXPECT_TRUE(out[1] == 100);

    HftSor zero(1);
    std::vector<std::int64_t> one(1, 0);
    zero.venueQuote(0, 0, 100, 10, 100);
    EXPECT_TRUE(zero.route(Side::SELL, 100, std::numeric_limits<std::int32_t>::min(), one.data()) == 0);
}

TEST(adaptiveSorHonoursLimitStalenessAndDisplayedPrice) {
    VenueScorecard card(2);
    AdaptiveSor sor(card);
    std::vector<VenueQuote> v{lit("A", 99, 100, 0), lit("B", 99, 101, 0)};
    v[0].lastUpdateNanos = 1000;
    v[1].lastUpdateNanos = 1000;
    auto d = sor.route(Side::BUY, 150, v, 100.5, 1000, 100);
    EXPECT_TRUE(d.lit.size() == 1 && d.unrouted == 50);  // B is through the limit
    d = sor.route(Side::BUY, 100, v, 200, 5000, 100);
    EXPECT_TRUE(d.lit.empty());  // both stale
    VenueQuote dark{"D", 99, 100, 101, 100, 0, 0, true};
    v.push_back(dark);
    d = sor.route(Side::BUY, 100, v, 99.5, 1000, std::numeric_limits<std::int64_t>::max());
    EXPECT_TRUE(d.probes.empty());  // mid 100 is through the limit

    // A cheap fee does not jump the better displayed price.
    std::vector<VenueQuote> w{lit("A", 99, 100, 30), lit("B", 99, 100.01, 0)};
    EXPECT_TRUE(sor.route(Side::BUY, 50, w).lit[0].venue == "A");
    EXPECT_THROWS(sor.route(Side::BUY, 50, w, kNaN, 0, 100));
}

// ---------------------------------------------------------------------- dark

TEST(meqAppliesToFirstExecutionNotToTheRemainder) {
    DarkPoolSimulator d;
    d.onQuote(99, 101);
    d.submit(Side::SELL, 70, 0);
    auto f = d.submit(Side::BUY, 100, 50);
    EXPECT_TRUE(f.size() == 1 && f[0].quantity == 70);
    EXPECT_TRUE(d.restingQty(Side::BUY) == 30);
    auto later = d.submit(Side::SELL, 1000, 0);  // used to leave the 30 orphaned forever
    EXPECT_TRUE(later.size() == 1 && later[0].quantity == 30);
    EXPECT_TRUE(d.restingQty(Side::BUY) == 0);
}

TEST(unfilledOrderKeepsItsMeqAndPartlyFilledRestingOrderDropsIt) {
    DarkPoolSimulator d;
    d.onQuote(99, 101);
    d.submit(Side::BUY, 100, 60);
    EXPECT_TRUE(d.submit(Side::SELL, 50, 0).empty());  // under the resting MEQ
    d.submit(Side::BUY, 10, 0);                        // crosses the resting 50 sell
    DarkPoolSimulator e;
    e.onQuote(99, 101);
    e.submit(Side::BUY, 100, 60);
    EXPECT_TRUE(e.submit(Side::SELL, 70, 0).size() == 1);  // leaves 30 resting
    EXPECT_TRUE(e.submit(Side::SELL, 30, 0).size() == 1);  // under 60, but MEQ was met
}

// ------------------------------------------------------------------------ AS

TEST(avellanedaStoikovFailsClosedOnBadInputs) {
    AvellanedaStoikov as(0.1, 1.5);
    double inf = std::numeric_limits<double>::infinity();
    EXPECT_THROWS(as.bidQuote(100, 1, kNaN, 10));
    EXPECT_THROWS(as.askQuote(100, 1, 0.01, kNaN));
    EXPECT_THROWS(as.bidQuote(kNaN, 1, 0.01, 10));
    EXPECT_THROWS(as.askQuote(100, kNaN, 0.01, 10));
    EXPECT_THROWS(as.optimalHalfSpread(inf, 10));
    EXPECT_THROWS(as.optimalHalfSpread(-1, 10));
    // Zero is a real answer, not garbage: only the liquidity floor remains.
    EXPECT_NEAR(as.reservationPrice(100, 5, 0, 10), 100, 1e-12);
}

// ----------------------------------------------------------------------- CRB

TEST(remarkMovesCashExposureAndPnl) {
    CentralRiskBook b;
    b.bookCashEquity("d1", "AAA", 100, 10);
    b.remarkEquity("AAA", 12);
    EXPECT_NEAR(b.exposure("EQ:AAA"), 1200, 1e-9);
    EXPECT_NEAR(b.grossExposure("EQ:AAA"), 1200, 1e-9);
    EXPECT_NEAR(b.deskExposure("d1", "EQ:AAA"), 1200, 1e-9);
    EXPECT_NEAR(b.inventoryPnl(), 200, 1e-9);
    b.remarkEquity("AAA", 9);
    EXPECT_NEAR(b.inventoryPnl(), -100, 1e-9);
}

TEST(remarkedOptionGreeksEqualAFreshBookingAtTheNewMarket) {
    CentralRiskBook b;
    b.bookEquityOption("d", "AAA", OptionType::CALL, 10, 100, 100, 100, 0.02, 0, 0.2, 1.0);
    b.remarkEquity("AAA", 110, 0.25, 0.25);
    CentralRiskBook fresh;
    fresh.bookEquityOption("d", "AAA", OptionType::CALL, 10, 100, 110, 100, 0.02, 0, 0.25, 0.75);
    EXPECT_NEAR(b.exposure("EQ:AAA"), fresh.exposure("EQ:AAA"), 1e-6);
    EXPECT_NEAR(b.exposure("EQGAMMA:AAA"), fresh.exposure("EQGAMMA:AAA"), 1e-6);
    EXPECT_NEAR(b.exposure("EQVEGA:AAA"), fresh.exposure("EQVEGA:AAA"), 1e-6);
    double was = black_scholes::price(OptionType::CALL, 100, 100, 0.02, 0, 0.2, 1.0);
    double now = black_scholes::price(OptionType::CALL, 110, 100, 0.02, 0, 0.25, 0.75);
    EXPECT_NEAR(b.inventoryPnl(), 1000 * (now - was), 1e-6);
}

TEST(expiredOptionLeavesTheRiskAtIntrinsic) {
    CentralRiskBook b;
    b.bookEquityOption("d", "AAA", OptionType::CALL, 1, 100, 100, 100, 0, 0, 0.2, 0.5);
    double paid = black_scholes::price(OptionType::CALL, 100, 100, 0, 0, 0.2, 0.5);
    b.remarkEquity("AAA", 120, kNaN, 1.0);
    EXPECT_NEAR(b.exposure("EQ:AAA"), 0, 1e-9);
    EXPECT_NEAR(b.exposure("EQGAMMA:AAA"), 0, 1e-9);
    EXPECT_NEAR(b.inventoryPnl(), 100 * (20 - paid), 1e-9);
}

TEST(fxRemarkKeepsCurrencyLegsAndConvertsPnl) {
    CentralRiskBook b;
    b.bookFxSpot("d", "USDJPY", 1e6, 150);
    b.remarkFx("USDJPY", 160);
    EXPECT_NEAR(b.exposure("CCY:USD"), 1e6, 1e-6);
    EXPECT_NEAR(b.exposure("CCY:JPY"), -1.5e8, 1e-3);
    // Long a million dollars gained 10 yen each, worth 1/160 of a dollar.
    EXPECT_NEAR(b.inventoryPnl(), 1e7 / 160, 1e-6);
}

TEST(fxOptionRemarkEqualsFreshBooking) {
    CentralRiskBook b;
    b.bookFxOption("d", "EURUSD", OptionType::CALL, 1e6, 1.10, 1.10, 0.03, 0.02, 0.1, 1.0);
    b.remarkFx("EURUSD", 1.20);
    CentralRiskBook fresh;
    fresh.bookFxOption("d", "EURUSD", OptionType::CALL, 1e6, 1.20, 1.10, 0.03, 0.02, 0.1, 1.0);
    EXPECT_NEAR(b.exposure("CCY:EUR"), fresh.exposure("CCY:EUR"), 1e-6);
    EXPECT_NEAR(b.exposure("FXVEGA:EURUSD"), fresh.exposure("FXVEGA:EURUSD"), 1e-6);
}

TEST(remarkRefusesBadInputsAndTouchesOnlyItsKey) {
    CentralRiskBook b;
    b.bookCashEquity("d", "AAA", 100, 10);
    b.bookCashEquity("d", "BBB", 100, 10);
    EXPECT_THROWS(b.remarkEquity("AAA", kNaN));
    EXPECT_THROWS(b.remarkEquity("AAA", 10, -1));
    EXPECT_THROWS(b.remarkEquity("AAA", 10, kNaN, -1));
    EXPECT_THROWS(b.remarkFx("EURUSD", 0));
    b.remarkEquity("AAA", 20);
    EXPECT_NEAR(b.exposure("EQ:BBB"), 1000, 1e-9);
}

TEST(yenIsConvertedBeforeSummingWithDollars) {
    CentralRiskBook b;
    b.bookFxSpot("d", "USDJPY", 1e6, 150);
    // +$1m and -Y150m are each worth $1m: nothing netted. Unconverted, the
    // yen swamped the sum.
    EXPECT_NEAR(b.nettingEfficiency(), 0, 1e-12);
    b.bookFxSpot("d", "USDJPY", -1e6, 150);
    EXPECT_NEAR(b.nettingEfficiency(), 1, 1e-12);
    auto ex = b.bookExposures();
    EXPECT_NEAR(std::fabs(ex[0]) + std::fabs(ex[1]), 0, 1e-6);
}

TEST(reportIsInBookCurrency) {
    CentralRiskBook b;
    b.bookFxSpot("d", "USDJPY", 1e6, 150);
    std::vector<std::vector<double>> cov = {{0, 0}, {0, 0.0001}};  // only JPY moves, 1% vol
    auto r = b.report(cov, 0.99);
    EXPECT_NEAR(r.grossExposure, 2e6, 1e-6);
    EXPECT_NEAR(r.var, 2.3263478740 * 1e6 * 0.01, 1.0);  // $1m of yen, not 150m
    EXPECT_NEAR(r.standaloneDeskVar, r.var, 1e-6);
}

TEST(missingRateThrowsAndSetRateWins) {
    CentralRiskBook b;
    b.bookFxSpot("d", "EURGBP", 1e6, 0.85);
    EXPECT_THROWS(b.nettingEfficiency());
    EXPECT_THROWS(b.bookExposures());
    b.setFxRate("EUR", 1.10);
    b.setFxRate("GBP", 1.30);
    EXPECT_NEAR(b.bookExposures()[0], 1.10e6, 1e-6);
    EXPECT_THROWS(b.setFxRate("EUR", kNaN));
    EXPECT_THROWS(CentralRiskBook("DOLLARS"));
}

TEST(bookCurrencyIsAParameter) {
    CentralRiskBook b("JPY");
    b.bookFxSpot("d", "USDJPY", 1e6, 150);
    EXPECT_NEAR(b.bookExposures()[0], 1.5e8, 1e-3);
}

TEST(ledgerCarriesInventoryApartFromFlowEconomics) {
    CrbPnlLedger l;
    l.onInternalized(1e6, 2, 0);
    l.markInventory(-50);
    EXPECT_NEAR(l.netEconomics(), 200, 1e-9);
    EXPECT_NEAR(l.totalPnl(), 150, 1e-9);
    l.markInventory(25);  // a level, not a delta
    EXPECT_NEAR(l.totalPnl(), 225, 1e-9);
    EXPECT_THROWS(l.markInventory(kNaN));
}

QTEST_MAIN()
