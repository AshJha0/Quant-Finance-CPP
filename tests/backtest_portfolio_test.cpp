// Port of the PositionSizing pins in PortfolioBacktestTest.java, the
// PortfolioOptimizer suite (PortfolioOptimizerTest.java + the tangency
// pin of FormulaPinsTest.java), and PortfolioConstructionTest.java
// (risk parity, Black-Litterman, constrained optimization). Same pins,
// same tolerances; the optimizers draw the Java-exact SplittableRandom
// stream, so the seeded searches walk the identical path.
#include <cmath>
#include <vector>

#include "qtest.hpp"
#include "quantfinlib/backtest/portfolio/black_litterman.hpp"
#include "quantfinlib/backtest/portfolio/constrained_portfolio_optimizer.hpp"
#include "quantfinlib/backtest/portfolio/portfolio_optimizer.hpp"
#include "quantfinlib/backtest/portfolio/position_sizing.hpp"
#include "quantfinlib/backtest/portfolio/risk_parity_optimizer.hpp"
#include "quantfinlib/util/math_utils.hpp"

using quantfinlib::Allocation;
using quantfinlib::ConstrainedPortfolioOptimizer;
using quantfinlib::PortfolioOptimizer;
namespace ps = quantfinlib::position_sizing;
namespace rp = quantfinlib::risk_parity_optimizer;
namespace bl = quantfinlib::black_litterman;
using Matrix = quantfinlib::math_utils::Matrix;

namespace {
const std::vector<double> MU = {0.06, 0.08, 0.05, 0.11};
const Matrix COV = {
        {0.04, 0.01, 0.00, 0.01},
        {0.01, 0.09, 0.02, 0.02},
        {0.00, 0.02, 0.02, 0.00},
        {0.01, 0.02, 0.00, 0.16}};
}  // namespace

// ---------------------------------------------------------- PositionSizing

TEST(positionSizingFormulasPinned) {
    // Kelly f* = mu / sigma^2 = 0.02 / 0.01 = 2; half-Kelly 1.
    EXPECT_NEAR(ps::kellyFraction(0.02, 0.01), 2.0, 1e-12);
    EXPECT_NEAR(ps::halfKelly(0.02, 0.01), 1.0, 1e-12);
    // Zero variance -> 0 (never divide by zero into a bet).
    EXPECT_NEAR(ps::kellyFraction(0.02, 0.0), 0.0, 0.0);
    // Risk 1% of 100k with a $2 stop distance: 500 shares.
    EXPECT_NEAR(ps::fixedFractionalQuantity(100000, 0.01, 50, 48), 500, 1e-9);
    EXPECT_THROWS(ps::fixedFractionalQuantity(100000, 0.01, 50, 50));
    // Inverse vol: vols 0.1 and 0.3 -> weights 0.75 / 0.25.
    std::vector<double> w = ps::inverseVolatilityWeights({0.1, 0.3});
    EXPECT_NEAR(w[0], 0.75, 1e-12);
    EXPECT_NEAR(w[1], 0.25, 1e-12);
    // A zero vol degenerates to equal weight.
    std::vector<double> eq = ps::inverseVolatilityWeights({0.0, 0.3});
    EXPECT_NEAR(eq[0], 0.5, 1e-12);
    EXPECT_NEAR(eq[1], 0.5, 1e-12);
    // Vol targeting: current 20%, target 10% -> half leverage; 0 vol -> 0.
    EXPECT_NEAR(ps::volatilityTargetLeverage(0.20, 0.10), 0.5, 1e-12);
    EXPECT_NEAR(ps::volatilityTargetLeverage(0.0, 0.10), 0.0, 0.0);
}

// ------------------------------------------------------- PortfolioOptimizer

TEST(minVolatilityMatchesTwoAssetAnalyticSolution) {
    // Uncorrelated assets, vol 10% and 20%: w1* = s2^2 / (s1^2 + s2^2) = 0.8.
    Allocation a = PortfolioOptimizer({0.05, 0.10}, {{0.01, 0.0}, {0.0, 0.04}})
                           .minVolatility();
    EXPECT_NEAR(a.weights[0], 0.8, 0.02);
    EXPECT_NEAR(a.weights[1], 0.2, 0.02);
    EXPECT_NEAR(a.weights[0] + a.weights[1], 1.0, 1e-9);
    // Analytic min vol = sqrt(0.8^2*0.01 + 0.2^2*0.04) ~ 0.0894.
    EXPECT_NEAR(a.volatility, 0.0894, 0.005);
}

TEST(maxSharpeFindsTheAnalyticTangencyPortfolio) {
    // Diagonal cov: tangency weights proportional to (mu - rf)/sigma^2 =
    // {4, 2} -> {2/3, 1/3}. Ignoring rf lands at {0.706, 0.294} and
    // fails the tolerance (the FormulaPinsTest pin).
    Allocation tangency = PortfolioOptimizer({0.06, 0.10},
                                             {{0.01, 0}, {0, 0.04}}, 42)
                                  .maxSharpe(0.02);
    EXPECT_NEAR(tangency.weights[0], 2.0 / 3, 0.03);
    EXPECT_NEAR(tangency.weights[1], 1.0 / 3, 0.03);
}

TEST(maxSharpeFavorsDominantAssetAndWeightsStayOnSimplex) {
    // Asset B dominates: same vol, higher return.
    Allocation a = PortfolioOptimizer({0.04, 0.12},
                                      {{0.0225, 0.0}, {0.0, 0.0225}})
                           .maxSharpe(0.02);
    EXPECT_TRUE(a.weights[1] > a.weights[0]);
    EXPECT_TRUE(a.sharpe > 0);
    // Simplex invariants on the 4-asset problem, both objectives.
    for (const Allocation& alloc : {PortfolioOptimizer(MU, COV).maxSharpe(0.02),
                                    PortfolioOptimizer(MU, COV).minVolatility()}) {
        double sum = 0;
        for (double w : alloc.weights) {
            EXPECT_TRUE(w >= -1e-9);
            sum += w;
        }
        EXPECT_NEAR(sum, 1.0, 1e-9);
    }
}

TEST(efficientFrontierVolatilityRisesWithReturnAndRebalanceSumsToZero) {
    std::vector<Allocation> frontier =
            PortfolioOptimizer({0.04, 0.12}, {{0.01, 0.002}, {0.002, 0.05}})
                    .efficientFrontier(5);
    EXPECT_TRUE(frontier.size() == 5);
    // Ends of the frontier: low-return end has lower vol than high-return end.
    EXPECT_TRUE(frontier.front().volatility <= frontier.back().volatility + 1e-6);

    std::vector<double> deltas = PortfolioOptimizer::rebalance({0.6, 0.4}, {0.5, 0.5});
    EXPECT_NEAR(deltas[0], -0.1, 1e-12);
    EXPECT_NEAR(deltas[1], 0.1, 1e-12);

    EXPECT_THROWS(PortfolioOptimizer({0.05}, COV));  // dimension mismatch
}

// ------------------------------------------------------------- Risk parity

TEST(riskParityEqualizesContributions) {
    Allocation erc = rp::equalRiskContribution(MU, COV);
    std::vector<double> rc = rp::riskContributions(erc.weights, COV);
    for (double contribution : rc) {
        EXPECT_NEAR(contribution, 0.25, 1e-6);
    }
    double sum = 0;
    for (double w : erc.weights) {
        EXPECT_TRUE(w > 0);
        sum += w;
    }
    EXPECT_NEAR(sum, 1.0, 1e-9);
}

TEST(riskParityMatchesInverseVolForUncorrelatedAssetsAndRefusesDegenerates) {
    // Uncorrelated 10%/20% vol: ERC weights are proportional to 1/sigma.
    Allocation erc = rp::equalRiskContribution({0.05, 0.05}, {{0.01, 0}, {0, 0.04}});
    EXPECT_NEAR(erc.weights[0], 2.0 / 3, 1e-6);
    EXPECT_NEAR(erc.weights[1], 1.0 / 3, 1e-6);
    // Gates: dimension mismatch, ragged row, zero-variance asset (the
    // fixed point does not exist -> refuse rather than spin or stall).
    EXPECT_THROWS(rp::equalRiskContribution({0.1}, COV));
    Matrix ragged = {{0.01, 0.0}, {0.0}};
    EXPECT_THROWS(rp::equalRiskContribution({0.1, 0.1}, ragged));
    Matrix degenerate = {{0.01, 0.0}, {0.0, 0.0}};
    EXPECT_THROWS(rp::equalRiskContribution({0.1, 0.1}, degenerate));
}

// --------------------------------------------------------- Black-Litterman

TEST(noViewsReturnsEquilibrium) {
    std::vector<double> pi = bl::impliedEquilibriumReturns(2.5, COV, {0.3, 0.3, 0.2, 0.2});
    std::vector<double> posterior = bl::posteriorReturns(0.05, COV, pi, {}, {}, {});
    for (std::size_t i = 0; i < pi.size(); i++) {
        EXPECT_NEAR(posterior[i], pi[i], 1e-12);
    }
    // Riskier assets carry higher implied returns under reverse optimization.
    EXPECT_TRUE(pi[3] > pi[2]);
}

TEST(confidentAbsoluteViewPullsPosteriorToTheView) {
    std::vector<double> pi =
            bl::impliedEquilibriumReturns(2.5, COV, {0.25, 0.25, 0.25, 0.25});
    // View: asset 0 returns exactly 15%, with near-certainty.
    Matrix p = {{1, 0, 0, 0}};
    std::vector<double> posterior = bl::posteriorReturns(0.05, COV, pi, p, {0.15}, {1e-8});
    EXPECT_NEAR(posterior[0], 0.15, 1e-3);
    // Weak view barely moves the prior.
    std::vector<double> weak = bl::posteriorReturns(0.05, COV, pi, p, {0.15}, {10.0});
    EXPECT_NEAR(weak[0], pi[0], 0.005);
    // Non-positive view variance is refused.
    EXPECT_THROWS(bl::posteriorReturns(0.05, COV, pi, p, {0.15}, {0.0}));
    EXPECT_THROWS(bl::posteriorReturns(0.05, COV, pi, p, {0.15}, {}));
}

TEST(relativeViewMovesTheSpread) {
    std::vector<double> pi =
            bl::impliedEquilibriumReturns(2.5, COV, {0.25, 0.25, 0.25, 0.25});
    double priorSpread = pi[1] - pi[2];
    // View: asset 1 outperforms asset 2 by 10% (more than the prior spread).
    Matrix p = {{0, 1, -1, 0}};
    std::vector<double> posterior = bl::posteriorReturns(0.05, COV, pi, p, {0.10}, {1e-6});
    EXPECT_NEAR(posterior[1] - posterior[2], 0.10, 1e-3);
    EXPECT_TRUE(posterior[1] - posterior[2] > priorSpread);
}

// ------------------------------------------------- Constrained optimization

TEST(positionCapsAreRespected) {
    std::vector<double> minW = {0, 0, 0, 0};
    std::vector<double> maxW = {0.30, 0.30, 0.30, 0.30};
    Allocation capped = ConstrainedPortfolioOptimizer(MU, COV)
                                .withBounds(minW, maxW)
                                .maxSharpe(0.02);
    double sum = 0;
    for (double w : capped.weights) {
        EXPECT_TRUE(w <= 0.30 + 1e-9 && w >= -1e-9);
        sum += w;
    }
    EXPECT_NEAR(sum, 1.0, 1e-6);
    // Cap must bind somewhere: 4 assets at <= 30% forces near-full usage.
    EXPECT_TRUE(capped.weights[2] > 0.05);
}

TEST(turnoverPenaltyAnchorsToCurrentHoldings) {
    std::vector<double> current = {0.25, 0.25, 0.25, 0.25};
    Allocation free = ConstrainedPortfolioOptimizer(MU, COV).maxSharpe(0.02);
    Allocation sticky = ConstrainedPortfolioOptimizer(MU, COV)
                                .withTurnoverPenalty(current, 0.50)  // punitive
                                .maxSharpe(0.02);
    double freeTurnover = 0, stickyTurnover = 0;
    for (int i = 0; i < 4; i++) {
        freeTurnover += std::fabs(free.weights[static_cast<std::size_t>(i)]
                                  - current[static_cast<std::size_t>(i)]);
        stickyTurnover += std::fabs(sticky.weights[static_cast<std::size_t>(i)]
                                    - current[static_cast<std::size_t>(i)]);
    }
    EXPECT_TRUE(stickyTurnover < 0.25 * freeTurnover);
}

TEST(infeasibleBoundsAreRejected) {
    // Max weights summing to 0.8 < 1 admit no fully-invested portfolio.
    EXPECT_THROWS(ConstrainedPortfolioOptimizer(MU, COV)
                          .withBounds({0, 0, 0, 0}, {0.2, 0.2, 0.2, 0.2}));
    // Negative floor / cap above 1 / crossed bounds are each refused.
    EXPECT_THROWS(ConstrainedPortfolioOptimizer(MU, COV)
                          .withBounds({-0.1, 0, 0, 0}, {1, 1, 1, 1}));
    EXPECT_THROWS(ConstrainedPortfolioOptimizer(MU, COV)
                          .withBounds({0, 0, 0, 0}, {1.5, 1, 1, 1}));
    EXPECT_THROWS(ConstrainedPortfolioOptimizer(MU, COV)
                          .withBounds({0.5, 0, 0, 0}, {0.4, 1, 1, 1}));
}

QTEST_MAIN()
