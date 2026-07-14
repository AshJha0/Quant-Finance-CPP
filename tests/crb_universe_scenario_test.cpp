// Port of crb.CrbPersistenceAndUniverseTest (hedge-universe + PnL-ledger
// subset only) and crb.CrbRealWorldScenarioTest (Java): the hedge-universe
// builder flattening what it spans, the index-future regression hedge, the
// PnL ledger's field-by-field economics, and three of the four desk-level
// scenario days (quiet two-way flow, one-way institutional flow with
// hedge escalation, the vol-spike stress day).
//
// DEVIATIONS FROM JAVA:
//  - Every Checkpoint round-trip assertion (both in
//    CrbPersistenceAndUniverseTest and CrbRealWorldScenarioTest's Day 4) is
//    dropped: persist.Checkpoint has no C++ port in this codebase (see
//    central_risk_book.hpp). Day 4's non-persistence logic (settleFixing,
//    pendingFixing, the offsetting NDF re-book) is ported directly instead
//    of round-tripping through a checkpoint file.
//  - quietTwoWayDayInternalizationIsThePnl replaces java.util.Random(7)
//    with a deterministic alternating-direction/size sequence. The Java
//    test pins no exact numeric values from that stream -- only
//    qualitative properties (warehouse never breached, internalization
//    rate > 0.5, spread captured/improvement paid > 0) -- and the
//    alternating sequence preserves the same "two-way flow" property
//    those assertions check.
#include <cmath>
#include <vector>

#include "qtest.hpp"
#include "quantfinlib/crb/central_risk_book.hpp"
#include "quantfinlib/crb/crb_auto_hedger.hpp"
#include "quantfinlib/crb/crb_hedge_universe.hpp"
#include "quantfinlib/crb/crb_pnl_ledger.hpp"
#include "quantfinlib/crb/crb_router.hpp"
#include "quantfinlib/crb/hedge_optimizer.hpp"
#include "quantfinlib/crb/internalization_engine.hpp"
#include "quantfinlib/crb/skewed_quoter.hpp"
#include "quantfinlib/risk/stress_tester.hpp"
#include "quantfinlib/risk/var_engine.hpp"

using quantfinlib::CentralRiskBook;
using quantfinlib::CrbAutoHedger;
using quantfinlib::CrbHedgeUniverse;
using quantfinlib::CrbPnlLedger;
using quantfinlib::InternalizationEngine;
namespace crb_router = quantfinlib::crb_router;
namespace hedge_optimizer = quantfinlib::hedge_optimizer;
namespace skewed_quoter = quantfinlib::skewed_quoter;
namespace stress_tester = quantfinlib::stress_tester;
namespace var_engine = quantfinlib::var_engine;

// ---------------------------------------------------------- Hedge universe

TEST(universeBuildsTheMatrixAndTheOptimizerFlattensWhatItSpans) {
    CentralRiskBook book;
    book.bookFxSpot("fx-desk", "EURUSD", 10'000'000, 1.10);
    book.bookNdf("em-desk", "USDINR", 5'000'000, 84.0);

    CrbHedgeUniverse universe(book.factors());
    universe.addFxForward("EURUSD-1W", "EURUSD", 1.10, 1.0);
    universe.addSingleFactor("INR-OUTRIGHT", "CCY:INR", 2.0);
    EXPECT_TRUE(universe.size() == 2);
    EXPECT_TRUE(universe.name(0) == "EURUSD-1W");

    int n = book.factors().size();
    std::vector<std::vector<double>> cov(n, std::vector<double>(n, 0.0));
    for (int f = 0; f < n; f++) {
        cov[f][f] = 1e-4;
    }
    std::vector<double> e = book.netExposures();
    std::vector<std::vector<double>> loadings = universe.loadings();
    std::vector<double> h = hedge_optimizer::hedge(e, cov, loadings, universe.costs(), 0);

    std::vector<double> residual = hedge_optimizer::residual(e, loadings, h);
    int inr = book.factors().idIfPresent("CCY:INR");
    EXPECT_NEAR(420'000'000, h[1], 1.0);
    EXPECT_NEAR(0, residual[inr], 1e-3);
    double before = hedge_optimizer::risk(e, cov);
    double after = hedge_optimizer::risk(residual, cov);
    EXPECT_TRUE(after < 0.05 * before);
}

TEST(indexFutureHedgesSingleNamesThroughTheCovarianceNotABetaTable) {
    CentralRiskBook book;
    book.bookCashEquity("cash-desk", "AAPL", 50'000, 200);
    CrbHedgeUniverse universe(book.factors());
    universe.addSingleFactor("ES-FUTURE", "EQ:SPX", 0.5);

    EXPECT_NEAR(0, book.exposure("EQ:SPX"), 1e-12);
    int n = book.factors().size();
    EXPECT_TRUE(n == 2);
    std::vector<double> e = book.netExposures();
    EXPECT_TRUE(static_cast<int>(e.size()) == n);

    double sa2 = 4e-4, sb2 = 2.25e-4, rho = 0.8;
    double cab = rho * std::sqrt(sa2 * sb2);
    std::vector<std::vector<double>> cov = {{sa2, cab}, {cab, sb2}};
    std::vector<double> h = hedge_optimizer::hedge(e, cov, universe.loadings(), universe.costs(), 0);
    EXPECT_NEAR(-10'000'000 * cab / sb2, h[0], 1e-3);

    crb_router::Allocation alloc = crb_router::route(std::fabs(h[0]), 2'000'000, {}, 4, 1);
    EXPECT_NEAR(2'000'000, alloc.internalNotional, 1e-9);
    EXPECT_NEAR(std::fabs(h[0]) - 2'000'000, alloc.lit, 1e-6);

    EXPECT_THROWS(universe.add("BAD", -1, {"EQ:SPX"}, {1}));
    EXPECT_THROWS(universe.add("BAD", 1, {"EQ:SPX"}, {1, 2}));
    EXPECT_THROWS(universe.addFxForward("BAD", "EUR", 1.1, 1));
}

// --------------------------------------------------------------- PnL ledger

TEST(ledgerRoundTripsEveryFieldAndIgnoresFullyRoutedFlow) {
    CrbPnlLedger ledger;
    ledger.onInternalized(1'000'000, 4, 1);
    ledger.onHedge(-500'000, 2);
    ledger.onRoute(200'000, crb_router::Allocation{0, {}, 200'000, 3});
    EXPECT_NEAR(300, ledger.spreadCaptured(), 1e-9);
    EXPECT_NEAR(100, ledger.improvementPaid(), 1e-9);
    EXPECT_NEAR(100, ledger.hedgeCost(), 1e-9);
    EXPECT_NEAR(60, ledger.routerCost(), 1e-9);
    EXPECT_TRUE(ledger.internalizations() == 1);
    EXPECT_TRUE(ledger.hedges() == 1);
    EXPECT_NEAR(300 - 100 - 60, ledger.netEconomics(), 1e-9);

    ledger.onDecision(InternalizationEngine::Decision{0, 5'000'000, 0}, 4);
    EXPECT_TRUE(ledger.internalizations() == 1);
    EXPECT_NEAR(300, ledger.spreadCaptured(), 1e-9);

    EXPECT_THROWS(ledger.onInternalized(1'000'000, 4, 5));
}

// ----------------------------------------------------------- Scenario days

TEST(quietTwoWayDayInternalizationIsThePnl) {
    CentralRiskBook book;
    double halfSpreadBps = 2.0;
    InternalizationEngine engine(5'000'000, 0.4);
    CrbPnlLedger ledger;

    double price = 500;
    // Deterministic two-way flow (see file header for why this replaces
    // java.util.Random(7) -- no exact values are pinned downstream).
    for (int t = 0; t < 24; t++) {
        double magnitude = (0.5 + 2.5 * ((t * 37 % 100) / 100.0)) * 1'000'000;
        double notional = (t % 2 == 0) ? magnitude : -magnitude;
        InternalizationEngine::Decision d = engine.decide(book.exposure("EQ:SPY"), notional,
                                                           halfSpreadBps);
        if (d.internalized != 0) {
            book.bookCashEquity("client-flow", "SPY", d.internalized / price, price);
        }
        ledger.onDecision(d, halfSpreadBps);
    }

    EXPECT_TRUE(std::fabs(book.exposure("EQ:SPY")) <= 5'000'000 + 1e-6);
    EXPECT_TRUE(engine.internalizationRate() > 0.5);
    EXPECT_TRUE(ledger.spreadCaptured() > 0);
    EXPECT_TRUE(ledger.improvementPaid() > 0);
    EXPECT_NEAR(ledger.spreadCaptured(), ledger.netEconomics(), 1e-9);
}

TEST(oneWayInstitutionalDayHedgesAndStaysProfitable) {
    CentralRiskBook book;
    double halfSpreadBps = 2.5;
    InternalizationEngine engine(20'000'000, 0.3);
    CrbPnlLedger ledger;

    double price = 500;
    for (int t = 0; t < 6; t++) {
        InternalizationEngine::Decision d = engine.decide(book.exposure("EQ:SPY"), 3'000'000,
                                                           halfSpreadBps);
        if (d.internalized != 0) {
            book.bookCashEquity("pension-flow", "SPY", d.internalized / price, price);
        }
        ledger.onDecision(d, halfSpreadBps);
    }
    EXPECT_NEAR(18'000'000, book.exposure("EQ:SPY"), 1e-6);

    CrbHedgeUniverse universe(book.factors());
    universe.addSingleFactor("ES-PROXY", "EQ:SPX", 0.4);
    universe.addSingleFactor("SPY-PROGRAM", "EQ:SPY", 2.0);
    double spyVar = 1.44e-4, spxVar = 1.21e-4;
    double cross = 0.97 * std::sqrt(spyVar * spxVar);
    int spy = book.factors().idIfPresent("EQ:SPY");
    int spx = book.factors().idIfPresent("EQ:SPX");
    std::vector<std::vector<double>> cov(2, std::vector<double>(2, 0.0));
    cov[spy][spy] = spyVar;
    cov[spx][spx] = spxVar;
    cov[spy][spx] = cross;
    cov[spx][spy] = cross;

    CrbAutoHedger hedger({10'000'000, 25'000'000}, 0.6, 1);
    auto orders = hedger.check(book.netExposures(), cov, universe.loadings(), universe.costs(),
                               6000, 0);
    EXPECT_TRUE(orders.size() == 1);
    EXPECT_TRUE(universe.name(orders[0].instrument) == "SPY-PROGRAM");
    EXPECT_NEAR(-12'000'000, orders[0].notional, 1.0);
    ledger.onHedge(orders[0].notional, 2.0);

    crb_router::Allocation alloc = crb_router::route(
        std::fabs(orders[0].notional), 0,
        {crb_router::DarkVenue("PRINT-POOL", 10'000'000, 1.0, 15),
         crb_router::DarkVenue("MIDPOINT", 6'000'000, 0.7, 1.2)},
        0.5, 0.8);
    EXPECT_NEAR(4'200'000, alloc.dark[1], 1e-6);
    EXPECT_NEAR(0, alloc.dark[0], 1e-9);
    EXPECT_NEAR(7'800'000, alloc.lit, 1.0);
    ledger.onRoute(std::fabs(orders[0].notional), alloc);

    EXPECT_TRUE(ledger.netEconomics() > 0);
    EXPECT_NEAR(ledger.spreadCaptured() - ledger.hedgeCost() - ledger.routerCost(),
               ledger.netEconomics(), 1e-9);
}

TEST(volSpikeDayStressesTheResidualBook) {
    double vegaPerPoint = -40'000;
    std::vector<double> exposures = {8'000'000, 0, -5'000'000, 0, vegaPerPoint * 100};
    double pnl = stress_tester::scenarioPnl(exposures, stress_tester::covidMarch2020());
    EXPECT_NEAR(-2'210'000, pnl, 1e-6);
    EXPECT_TRUE(pnl < 0);

    std::vector<double> liquid = {8'000'000, -5'000'000};
    std::vector<std::vector<double>> cov = {{1.44e-4, 0.3 * std::sqrt(1.44e-4 * 3.6e-5)},
                                            {0.3 * std::sqrt(1.44e-4 * 3.6e-5), 3.6e-5}};
    stress_tester::ReverseStress reverse = stress_tester::reverseStress(liquid, cov, 2'000'000);
    EXPECT_NEAR(-2'000'000, stress_tester::scenarioPnl(liquid, reverse.shocks), 1e-6);
    EXPECT_NEAR(2'000'000 / var_engine::portfolioStdev(liquid, cov), reverse.mahalanobisSigmas, 1e-9);
    EXPECT_TRUE(reverse.mahalanobisSigmas > 10);

    skewed_quoter::Quote crisis = skewed_quoter::quote(500, 8, 4'000'000, 5'000'000, 0.6);
    EXPECT_TRUE(crisis.bid > 0 && crisis.bid < crisis.ask);
    EXPECT_TRUE(crisis.skewBps < -3.5);
}

TEST(ndfFixingDayRollsThroughToTheNextSession) {
    // Direct port of the non-persistence half of
    // ndfFixingDayRollsThroughTheOvernightCheckpoint: the fixing mechanics
    // (settleFixing, pendingFixing, the offsetting re-book) on one
    // CentralRiskBook standing in for "today" and "tomorrow" (no
    // Checkpoint round-trip -- see file header).
    CentralRiskBook book;
    book.bookNdf("em-desk", "USDINR", 8'000'000, 84.0);
    book.bookNdf("em-desk", "USDBRL", 4'000'000, 5.60);

    EXPECT_NEAR(8'000'000, book.pendingFixing("USDINR"), 1e-9);
    book.settleFixing("USDINR", 8'000'000);
    book.bookNdf("em-desk", "USDINR", -8'000'000, 84.0);
    book.settleFixing("USDINR", 8'000'000);
    EXPECT_NEAR(0, book.pendingFixing("USDINR"), 1e-9);
    EXPECT_NEAR(0, book.exposure("CCY:INR"), 1e-6);
    EXPECT_NEAR(4'000'000, book.pendingFixing("USDBRL"), 1e-9);
    EXPECT_NEAR(4'000'000, book.exposure("CCY:USD"), 1e-6);
}

QTEST_MAIN()
