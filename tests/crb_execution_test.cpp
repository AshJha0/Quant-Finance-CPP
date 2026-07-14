// Port of crb.CrbExecutionTest (Java): CRB flow economics and execution --
// skewed quotes, internalization, the minimum-variance hedge optimizer,
// the auto-hedger's band/cooldown loop, and the internal-then-dark-then-lit
// router. Pins and tolerances unchanged.
#include <cmath>
#include <vector>

#include "qtest.hpp"
#include "quantfinlib/crb/crb_auto_hedger.hpp"
#include "quantfinlib/crb/crb_router.hpp"
#include "quantfinlib/crb/hedge_optimizer.hpp"
#include "quantfinlib/crb/internalization_engine.hpp"
#include "quantfinlib/crb/skewed_quoter.hpp"

using quantfinlib::CrbAutoHedger;
using quantfinlib::InternalizationEngine;
namespace crb_router = quantfinlib::crb_router;
namespace hedge_optimizer = quantfinlib::hedge_optimizer;
namespace skewed_quoter = quantfinlib::skewed_quoter;

// ------------------------------------------------------------ SkewedQuoter

TEST(skewShadesBothQuotesTowardReducingInventory) {
    skewed_quoter::Quote q = skewed_quoter::quote(100, 10, 50, 100, 0.5);
    EXPECT_NEAR(-2.5, q.skewBps, 1e-12);
    EXPECT_NEAR(100 * (1 - 12.5e-4), q.bid, 1e-12);
    EXPECT_NEAR(100 * (1 + 7.5e-4), q.ask, 1e-12);

    skewed_quoter::Quote flat = skewed_quoter::quote(100, 10, 0, 100, 0.5);
    EXPECT_NEAR(0, flat.skewBps, 1e-12);
    EXPECT_NEAR(flat.ask - 100, 100 - flat.bid, 1e-12);

    skewed_quoter::Quote capped = skewed_quoter::quote(100, 10, 500, 100, 0.9);
    EXPECT_NEAR(-9, capped.skewBps, 1e-12);
    EXPECT_TRUE(capped.bid < capped.ask);

    skewed_quoter::Quote shortBook = skewed_quoter::quote(100, 10, -50, 100, 0.5);
    EXPECT_NEAR(2.5, shortBook.skewBps, 1e-12);

    EXPECT_THROWS(skewed_quoter::quote(100, 10, 0, 100, 1.0));
    EXPECT_THROWS(skewed_quoter::quote(100, 10, std::nan(""), 100, 0.5));
    EXPECT_THROWS(skewed_quoter::quote(100, 12'000, 0, 100, 0));
}

// ------------------------------------------------------- InternalizationEngine

TEST(riskReducingFlowIsInternalizedWithImprovement) {
    InternalizationEngine engine(10'000'000, 0.5);
    InternalizationEngine::Decision d = engine.decide(5'000'000, -3'000'000, 10);
    EXPECT_NEAR(-3'000'000, d.internalized, 1e-9);
    EXPECT_NEAR(0, d.routed, 1e-9);
    EXPECT_NEAR(5, d.improvementBps, 1e-12);
    EXPECT_NEAR(1.0, engine.internalizationRate(), 1e-12);
}

TEST(flowThroughZeroBlendsImprovementAndWarehousesTheFlip) {
    InternalizationEngine engine(10'000'000, 0.5);
    InternalizationEngine::Decision d = engine.decide(5'000'000, -8'000'000, 10);
    EXPECT_NEAR(-8'000'000, d.internalized, 1e-9);
    EXPECT_NEAR(0, d.routed, 1e-9);
    EXPECT_NEAR(5.0 * 5 / 8, d.improvementBps, 1e-12);
}

TEST(riskAddingFlowIsWarehousedOnlyInsideTheLimit) {
    InternalizationEngine engine(10'000'000, 0.5);
    InternalizationEngine::Decision d = engine.decide(9'000'000, 5'000'000, 10);
    EXPECT_NEAR(1'000'000, d.internalized, 1e-9);
    EXPECT_NEAR(4'000'000, d.routed, 1e-9);
    EXPECT_NEAR(0, d.improvementBps, 1e-12);
    EXPECT_NEAR(0.2, engine.internalizationRate(), 1e-12);

    InternalizationEngine::Decision full = engine.decide(10'000'000, 2'000'000, 10);
    EXPECT_NEAR(0, full.internalized, 1e-9);
    EXPECT_NEAR(2'000'000, full.routed, 1e-9);

    EXPECT_THROWS(engine.decide(0, 0, 10));
    EXPECT_THROWS(InternalizationEngine(1e6, 1.5));
}

// ------------------------------------------------------------ HedgeOptimizer

TEST(zeroCostRecoversTheClosedFormMinimumVarianceHedge) {
    double sa2 = 4e-4, sb2 = 2.25e-4;
    double cab = 0.6 * std::sqrt(sa2 * sb2);
    std::vector<std::vector<double>> cov = {{sa2, cab}, {cab, sb2}};
    std::vector<double> e = {10'000'000, 0};
    std::vector<std::vector<double>> loadings = {{0}, {1}};
    std::vector<double> h = hedge_optimizer::hedge(e, cov, loadings, {1}, 0);
    EXPECT_NEAR(-10'000'000 * cab / sb2, h[0], 1e-3);

    double before = hedge_optimizer::risk(e, cov);
    double after = hedge_optimizer::risk(hedge_optimizer::residual(e, loadings, h), cov);
    EXPECT_TRUE(after < before);
    EXPECT_NEAR(before * std::sqrt(1 - 0.36), after, before * 1e-6);
}

TEST(costTermZeroesHedgesNotWorthTheirPrice) {
    std::vector<std::vector<double>> cov = {{4e-4}};
    std::vector<double> e = {1'000'000};
    std::vector<std::vector<double>> loadings = {{1}};
    std::vector<double> none = hedge_optimizer::hedge(e, cov, loadings, {1}, 1000);
    EXPECT_NEAR(0, none[0], 1e-9);
    std::vector<double> partial = hedge_optimizer::hedge(e, cov, loadings, {1}, 400);
    EXPECT_TRUE(partial[0] < 0 && partial[0] > -1'000'000);
    std::vector<double> full = hedge_optimizer::hedge(e, cov, loadings, {1}, 0);
    EXPECT_NEAR(-1'000'000, full[0], 1e-3);
}

TEST(optimizerPicksTheCheaperOfTwoIdenticalInstruments) {
    std::vector<std::vector<double>> cov = {{4e-4}};
    std::vector<double> e = {5'000'000};
    std::vector<std::vector<double>> loadings = {{1, 1}};
    std::vector<double> h = hedge_optimizer::hedge(e, cov, loadings, {1, 5}, 100);
    EXPECT_TRUE(h[0] < 0);
    EXPECT_NEAR(0, h[1], 0.0);
    EXPECT_THROWS(hedge_optimizer::hedge(e, cov, loadings, {1, 5}, -1));
    EXPECT_THROWS(hedge_optimizer::hedge({std::nan("")}, cov, {{1}}, {1}, 0));
    EXPECT_THROWS(hedge_optimizer::hedge({1e7}, {{std::nan("")}}, {{1}}, {1}, 0));
    EXPECT_THROWS(hedge_optimizer::hedge({1e7}, {{-1e-4}}, {{1}}, {1}, 0));
}

// ------------------------------------------------------------- CrbAutoHedger

TEST(autoHedgerHedgesTheExcessBackToTheBandAndCoolsDown) {
    CrbAutoHedger hedger({10'000'000}, 0.5, 2);
    std::vector<std::vector<double>> cov = {{4e-4}};
    std::vector<std::vector<double>> loadings = {{1}};
    std::vector<double> costs = {1};

    EXPECT_TRUE(hedger.check({8'000'000}, cov, loadings, costs, 0, 0).empty());

    auto orders = hedger.check({12'000'000}, cov, loadings, costs, 0, 10);
    EXPECT_TRUE(orders.size() == 1);
    EXPECT_NEAR(-7'000'000, orders[0].notional, 1e-3);
    EXPECT_TRUE(orders[0].instrument == 0);
    EXPECT_TRUE(hedger.hedgesEmitted() == 1);

    EXPECT_TRUE(hedger.check({12'000'000}, cov, loadings, costs, 0, 11).empty());
    EXPECT_TRUE(hedger.check({12'000'000}, cov, loadings, costs, 0, 12).size() == 1);

    EXPECT_THROWS(CrbAutoHedger({0}, 0.5, 1));
    EXPECT_THROWS(hedger.check({1, 2}, cov, loadings, costs, 0, 0));
    EXPECT_THROWS(hedger.check({std::nan("")}, cov, loadings, costs, 0, 20));
}

TEST(hardLimitOutranksCostThrift) {
    CrbAutoHedger hedger({10'000'000}, 0.5, 0);
    std::vector<std::vector<double>> cov = {{4e-4}};
    std::vector<std::vector<double>> loadings = {{1}};
    auto orders = hedger.check({12'000'000}, cov, loadings, {1}, 1e9, 0);
    EXPECT_TRUE(orders.size() == 1);
    EXPECT_NEAR(-7'000'000, orders[0].notional, 1e-3);
}

// ------------------------------------------------------------------ CrbRouter

TEST(routerCrossesInternallyFirstThenPricesDarkAgainstLit) {
    crb_router::DarkVenue toxic("TOXIC", 10'000'000, 1.0, 20);
    crb_router::DarkVenue clean("CLEAN", 4'000'000, 0.5, 2);
    crb_router::Allocation a = crb_router::route(10'000'000, 3'000'000, {toxic, clean}, 5, 3);

    EXPECT_NEAR(3'000'000, a.internalNotional, 1e-9);
    EXPECT_NEAR(0, a.dark[0], 1e-9);
    EXPECT_NEAR(2'000'000, a.dark[1], 1e-9);
    EXPECT_NEAR(5'000'000, a.lit, 1e-9);
    EXPECT_NEAR(4.4, a.expectedCostBps, 1e-12);
}

TEST(fullyInternalizedOrderCostsNothing) {
    crb_router::Allocation a = crb_router::route(2'000'000, 5'000'000, {}, 5, 3);
    EXPECT_NEAR(2'000'000, a.internalNotional, 1e-9);
    EXPECT_NEAR(0, a.lit, 1e-9);
    EXPECT_NEAR(0, a.expectedCostBps, 1e-12);
    EXPECT_THROWS(crb_router::route(0, 0, {}, 5, 3));
    EXPECT_THROWS(crb_router::DarkVenue("X", 1e6, 1.2, 5));
}

QTEST_MAIN()
