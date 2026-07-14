// Port of the alpha.PortfolioConstructionTest pins (Java): gross
// targeting, dollar neutrality, caps, exact sector/beta neutrality,
// risk-budgeting proportions, and the mean-variance tilt using the
// correlation structure z-scoring ignores.
#include "quantfinlib/alpha/alpha_context.hpp"
#include "quantfinlib/alpha/portfolio_construction.hpp"
#include "quantfinlib/risk/java_random.hpp"

#include <cmath>
#include <unordered_map>
#include <vector>

#include "qtest.hpp"

using quantfinlib::AlphaContext;
using quantfinlib::BarSeries;
namespace pc = quantfinlib::portfolio_construction;

namespace {

double gross(const std::vector<double>& w) {
    double g = 0;
    for (double x : w) g += std::abs(x);
    return g;
}

double sum(const std::vector<double>& w) {
    double s = 0;
    for (double x : w) s += x;
    return s;
}

BarSeries flatSeries(const std::string& symbol) {
    auto b = BarSeries::builder(symbol);
    for (int i = 0; i < 10; i++) {
        b.add(i, 100, 101, 99, 100, 1000);
    }
    return b.build();
}

BarSeries follow(const std::string& symbol, const std::vector<double>& moves, double beta) {
    auto b = BarSeries::builder(symbol);
    double close = 100;
    for (std::size_t i = 0; i < moves.size(); i++) {
        double open = close;
        close = open * (1 + beta * moves[i]);
        b.add(static_cast<int>(i), open, std::max(open, close), std::min(open, close), close, 1000);
    }
    return b.build();
}

}  // namespace

TEST(zScoreWeightsAreDollarNeutralAtTargetGross) {
    std::vector<double> scores = {2.0, 1.0, 0.0, -1.0, -2.0, std::nan("")};
    std::vector<double> w = pc::zScoreWeights(scores, 1.0, 1.0);
    EXPECT_NEAR(gross(w), 1.0, 1e-12);
    EXPECT_NEAR(sum(w), 0.0, 1e-12);
    EXPECT_NEAR(w[5], 0.0, 0.0);
    EXPECT_TRUE(w[0] > w[1] && w[1] > w[2] && w[2] > w[3] && w[3] > w[4]);
    EXPECT_NEAR(gross(pc::zScoreWeights({3, 3, 3}, 1.0, 1.0)), 0.0, 1e-12);
    EXPECT_THROWS(pc::zScoreWeights(scores, 0, 0.1));
}

TEST(outliersAreWinsorizedAndCapsRespected) {
    std::vector<double> scores = {1000.0, 1.0, 0.5, -0.5, -1.0};
    std::vector<double> w = pc::zScoreWeights(scores, 1.0, 0.30);
    for (double x : w) {
        EXPECT_TRUE(std::abs(x) <= 0.30 + 1e-12);
    }
    EXPECT_NEAR(std::abs(w[0]), 0.30, 1e-9);
}

TEST(inverseVolBudgetEqualizesRiskContributions) {
    std::vector<double> w = {0.25, -0.25, 0.25, -0.25};
    std::vector<double> vols = {0.01, 0.04, 0.02, 0.02};
    std::vector<double> out = pc::inverseVolBudget(w, vols, 1.0);
    EXPECT_NEAR(gross(out), 1.0, 1e-12);
    double risk0 = std::abs(out[0]) * vols[0];
    for (std::size_t i = 1; i < out.size(); i++) {
        EXPECT_NEAR(risk0, std::abs(out[i]) * vols[i], 1e-12);
    }
    EXPECT_TRUE(out[0] > 0 && out[1] < 0);
    EXPECT_THROWS(pc::inverseVolBudget(w, {0.01, 0, 0.02, 0.02}, 1));
}

TEST(contextSectorOverloadCannotMisalign) {
    std::unordered_map<std::string, BarSeries> data;
    data.emplace("ZZZ", flatSeries("ZZZ"));
    data.emplace("AAA", flatSeries("AAA"));
    data.emplace("MMM", flatSeries("MMM"));
    AlphaContext ctx = AlphaContext::of(data);  // panel order: AAA, MMM, ZZZ
    std::vector<double> w = {0.4, -0.1, 0.2};
    std::vector<double> out = pc::sectorNeutralize(ctx, w, {{"AAA", "TECH"}, {"ZZZ", "TECH"}, {"MMM", "FIN"}});
    EXPECT_NEAR(out[0] + out[2], 0.0, 1e-12);
    EXPECT_NEAR(out[0], 0.1, 1e-12);
    EXPECT_NEAR(out[1], 0.0, 1e-12);
    std::vector<double> partial = pc::sectorNeutralize(ctx, w, {{"AAA", "TECH"}, {"ZZZ", "TECH"}});
    EXPECT_NEAR(partial[1], 0.0, 1e-12);
    std::vector<double> scores = {2.0, -2.0, 0.0};
    EXPECT_NEAR(gross(pc::zScoreWeights(scores, 1.0)), 1.0, 1e-12);
}

TEST(sectorNeutralizationZeroesEverySectorNet) {
    std::vector<double> w = {0.30, 0.10, -0.05, 0.20, -0.40, 0.0};
    std::vector<std::string> sectors = {"TECH", "TECH", "TECH", "FIN", "FIN", "FIN"};
    std::vector<double> out = pc::sectorNeutralize(w, sectors);
    EXPECT_NEAR(out[0] + out[1] + out[2], 0.0, 1e-12);
    EXPECT_NEAR(out[3] + out[4], 0.0, 1e-12);
    EXPECT_NEAR(out[5], 0.0, 0.0);
}

TEST(betaNeutralizationZeroesPortfolioBeta) {
    std::vector<double> w = {0.40, 0.30, -0.30, -0.40};
    std::vector<double> betas = {1.5, 1.0, 0.8, 0.5};
    std::vector<double> out = pc::betaNeutralize(w, betas);
    double portfolioBeta = 0;
    for (std::size_t i = 0; i < out.size(); i++) {
        portfolioBeta += out[i] * betas[i];
    }
    EXPECT_NEAR(portfolioBeta, 0.0, 1e-12);
    EXPECT_THROWS(pc::betaNeutralize(w, {1, std::nan(""), 1, 1}));
}

TEST(trailingBetasRecoverTheLeveragedName) {
    std::unordered_map<std::string, BarSeries> data;
    std::vector<double> marketMoves(60);
    quantfinlib::JavaRandom rng(11);
    for (double& m : marketMoves) m = (rng.nextDouble() - 0.5) * 0.02;
    data.emplace("HI", follow("HI", marketMoves, 2.0));
    data.emplace("LO", follow("LO", marketMoves, 0.5));
    AlphaContext ctx = AlphaContext::of(data);
    std::vector<double> betas = pc::trailingBetas(ctx, 59, 50);
    int hi = -1, lo = -1;
    for (int i = 0; i < ctx.symbolCount(); i++) {
        if (ctx.symbols()[static_cast<std::size_t>(i)] == "HI") hi = i;
        if (ctx.symbols()[static_cast<std::size_t>(i)] == "LO") lo = i;
    }
    EXPECT_NEAR(betas[static_cast<std::size_t>(hi)], 1.6, 0.01);
    EXPECT_NEAR(betas[static_cast<std::size_t>(lo)], 0.4, 0.01);
}

TEST(meanVarianceTiltUsesTheCovarianceStructure) {
    std::vector<double> alphas = {1.0, 1.0, 1.0, std::nan("")};
    std::vector<std::vector<double>> cov = {
            {0.04, 0, 0, 0}, {0, 0.01, 0, 0}, {0, 0, 0.01, 0}, {0, 0, 0, 0.01}};
    std::vector<double> w = pc::meanVarianceTilt(alphas, cov, 1.0);
    EXPECT_NEAR(gross(w), 1.0, 1e-12);
    EXPECT_NEAR(w[1] / 4, w[0], 1e-12);
    EXPECT_NEAR(w[1], w[2], 1e-12);
    EXPECT_NEAR(w[3], 0.0, 0.0);
    std::vector<std::vector<double>> bad(2, std::vector<double>(2));
    EXPECT_THROWS(pc::meanVarianceTilt(alphas, bad, 1.0));
}

QTEST_MAIN()
