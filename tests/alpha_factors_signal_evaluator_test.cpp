// Port of the alpha.SignalEvaluatorTest pins (Java): a factor that ranks
// by true drift earns IC ~= +1, its negation ~= -1, hit rate follows
// signs, turnover distinguishes stable from flipping signals, and
// factor exposure detects disguised duplicates. Same panel construction,
// same tolerances.
#include "quantfinlib/alpha/alpha_context.hpp"
#include "quantfinlib/alpha/alpha_factor.hpp"
#include "quantfinlib/alpha/factors.hpp"
#include "quantfinlib/alpha/signal_evaluator.hpp"

#include <cmath>
#include <stdexcept>
#include <unordered_map>
#include <vector>

#include "qtest.hpp"

using quantfinlib::AlphaContext;
using quantfinlib::AlphaFactor;
using quantfinlib::BarSeries;
namespace factors = quantfinlib::factors;
namespace se = quantfinlib::signal_evaluator;

namespace {

constexpr int BARS = 200;
const std::vector<double> DRIFTS = {0.004, 0.003, 0.002, 0.001, -0.001, -0.002, -0.003, -0.004};

AlphaContext panel() {
    std::unordered_map<std::string, BarSeries> data;
    for (std::size_t s = 0; s < DRIFTS.size(); s++) {
        auto b = BarSeries::builder("S" + std::to_string(s));
        double close = 100;
        for (int i = 0; i < BARS; i++) {
            double open = close;
            close = 100 * std::pow(1 + DRIFTS[s], i + 1);
            b.add(i, open, std::max(open, close), std::min(open, close), close, 1000);
        }
        data.emplace("S" + std::to_string(s), b.build());
    }
    return AlphaContext::of(data);
}

/** Scores each symbol by its true drift -- the oracle factor. */
AlphaFactor oracle() {
    return AlphaFactor("ORACLE", [](const AlphaContext& ctx, int) {
        std::vector<double> s(static_cast<std::size_t>(ctx.symbolCount()));
        for (std::size_t i = 0; i < s.size(); i++) {
            int idx = std::stoi(ctx.symbols()[i].substr(1));
            s[i] = DRIFTS[static_cast<std::size_t>(idx)];
        }
        return s;
    });
}

}  // namespace

TEST(perfectSignalEarnsIcNearOne) {
    AlphaContext ctx = panel();
    se::Report r = se::evaluate(ctx, oracle(), 10, 5);
    EXPECT_NEAR(r.meanIc, 1.0, 1e-9);
    EXPECT_NEAR(r.icStd, 0.0, 1e-9);
    EXPECT_NEAR(r.hitRate, 1.0, 1e-9);
    EXPECT_NEAR(r.meanTurnover, 0.0, 1e-9);
    EXPECT_TRUE(r.observations > 10);
    EXPECT_TRUE(r.format().find("ORACLE") != std::string::npos);
    EXPECT_TRUE(static_cast<int>(r.icSeries.size()) == r.observations);
}

TEST(invertedSignalEarnsIcNearMinusOne) {
    AlphaContext ctx = panel();
    AlphaFactor inverted("INV", [](const AlphaContext& c, int index) {
        std::vector<double> s = oracle().scores(c, index);
        for (double& x : s) x = -x;
        return s;
    });
    se::Report r = se::evaluate(ctx, inverted, 10, 5);
    EXPECT_NEAR(r.meanIc, -1.0, 1e-9);
    EXPECT_NEAR(r.hitRate, 0.0, 1e-9);
}

TEST(flippingSignalShowsFullTurnover) {
    AlphaContext ctx = panel();
    AlphaFactor flipper("FLIP", [](const AlphaContext& c, int index) {
        std::vector<double> s = oracle().scores(c, index);
        double sign = ((index / 5) % 2 == 0) ? 1.0 : -1.0;
        for (double& x : s) x *= sign;
        return s;
    });
    se::Report r = se::evaluate(ctx, flipper, 10, 5);
    EXPECT_NEAR(r.meanTurnover, 1.0, 1e-9);
    EXPECT_NEAR(r.meanIc, 0.0, 0.05);
}

TEST(factorExposureDetectsDuplicatesAndInverses) {
    AlphaContext ctx = panel();
    AlphaFactor momentum = factors::momentum(20, 0);
    EXPECT_NEAR(se::factorExposure(ctx, oracle(), momentum, 30, 10), 1.0, 1e-9);
    AlphaFactor inverted("INV", [](const AlphaContext& c, int index) {
        std::vector<double> s = oracle().scores(c, index);
        for (double& x : s) x = -x;
        return s;
    });
    EXPECT_NEAR(se::factorExposure(ctx, oracle(), inverted, 30, 10), -1.0, 1e-9);
}

TEST(midranksHandleTiesExactly) {
    std::vector<double> scores = {5, 1, 1, 3};
    std::vector<double> fwd = {0.04, 0.01, 0.01, 0.03};
    EXPECT_NEAR(se::spearman(scores, fwd), 1.0, 1e-12);
    std::vector<double> fwd2 = {0.03, 0.01, 0.01, 0.04};
    double rho = se::spearman(scores, fwd2);
    EXPECT_TRUE(rho < 1.0 && rho > 0.5);
    std::vector<double> allTied = {2, 2, 2, 2};
    EXPECT_NEAR(se::spearman(allTied, fwd), 0.0, 1e-12);
}

TEST(quantileReturnsAreMonotoneForTheOracleWithExactMeans) {
    AlphaContext ctx = panel();
    se::QuantileReport r = se::quantileReturns(ctx, oracle(), 10, 5, 4);
    EXPECT_TRUE(r.quantiles == 4);
    EXPECT_TRUE(static_cast<int>(r.meanReturns.size()) == 4);
    EXPECT_TRUE(r.dates > 10);
    for (int q = 1; q < 4; q++) {
        EXPECT_TRUE(r.meanReturns[static_cast<std::size_t>(q)] > r.meanReturns[static_cast<std::size_t>(q - 1)]);
    }
    double bottom = ((std::pow(1 - 0.004, 5) - 1) + (std::pow(1 - 0.003, 5) - 1)) / 2;
    double top = ((std::pow(1 + 0.004, 5) - 1) + (std::pow(1 + 0.003, 5) - 1)) / 2;
    EXPECT_NEAR(r.meanReturns[0], bottom, 1e-12);
    EXPECT_NEAR(r.meanReturns[3], top, 1e-12);
    EXPECT_NEAR(r.spread(), top - bottom, 1e-12);
    EXPECT_TRUE(r.spread() > 0);
    int total = 0;
    for (int c : r.counts) total += c;
    EXPECT_TRUE(total == 8 * r.dates);
}

TEST(nanNamesAreExcludedFromTheQuantiles) {
    AlphaContext ctx = panel();
    AlphaFactor noBest("NOBEST", [](const AlphaContext& c, int index) {
        std::vector<double> s = oracle().scores(c, index);
        s[0] = std::nan("");
        return s;
    });
    se::QuantileReport r = se::quantileReturns(ctx, noBest, 10, 5, 4);
    EXPECT_NEAR(r.meanReturns[3], std::pow(1.003, 5) - 1, 1e-12);
    EXPECT_TRUE(r.meanReturns[3] < std::pow(1.004, 5) - 1);
}

TEST(quantileGatesRefuseNonsense) {
    AlphaContext ctx = panel();
    EXPECT_THROWS(se::quantileReturns(ctx, oracle(), 10, 5, 1));
    EXPECT_THROWS(se::quantileReturns(ctx, oracle(), 10, 0, 4));
    EXPECT_THROWS(se::quantileReturns(ctx, oracle(), -1, 5, 4));
    AlphaFactor sparse("SPARSE", [](const AlphaContext& c, int) {
        std::vector<double> s(static_cast<std::size_t>(c.symbolCount()), std::nan(""));
        s[0] = 1;
        s[1] = -1;
        return s;
    });
    EXPECT_THROWS(se::quantileReturns(ctx, sparse, 10, 5, 4));
}

TEST(nanScoresAreDroppedPairwiseNotFabricated) {
    AlphaContext ctx = panel();
    AlphaFactor sparse("SPARSE", [](const AlphaContext& c, int) {
        std::vector<double> s(static_cast<std::size_t>(c.symbolCount()), std::nan(""));
        s[0] = 1;
        s[1] = -1;
        return s;
    });
    EXPECT_THROWS(se::evaluate(ctx, sparse, 10, 5));
    EXPECT_THROWS(se::evaluate(ctx, oracle(), 10, 0));
}

QTEST_MAIN()
