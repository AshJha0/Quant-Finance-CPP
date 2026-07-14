// Port of the alpha.AlphaValidationTest pins (Java): walk-forward picks
// the genuinely better candidate on training data and holds up out of
// sample (the leak-fixed forward-window regression), blocked CV shows
// regime consistency, the permutation test separates real signals from
// static luck, and sensitivity distinguishes plateaus from spikes.
#include "quantfinlib/alpha/alpha_context.hpp"
#include "quantfinlib/alpha/alpha_factor.hpp"
#include "quantfinlib/alpha/alpha_validation.hpp"
#include "quantfinlib/alpha/factors.hpp"
#include "quantfinlib/risk/java_random.hpp"

#include <cmath>
#include <unordered_map>
#include <vector>

#include "qtest.hpp"

using quantfinlib::AlphaContext;
using quantfinlib::AlphaFactor;
using quantfinlib::BarSeries;
namespace factors = quantfinlib::factors;
namespace av = quantfinlib::alpha_validation;

namespace {

constexpr int BARS = 300;
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

AlphaContext regimePanel() {
    std::unordered_map<std::string, BarSeries> data;
    for (std::size_t s = 0; s < DRIFTS.size(); s++) {
        auto b = BarSeries::builder("S" + std::to_string(s));
        double close = 100;
        for (int i = 0; i < BARS; i++) {
            double open = close;
            double drift = i < BARS / 2 ? DRIFTS[s] : -DRIFTS[s];
            close = open * (1 + drift);
            b.add(i, open, std::max(open, close), std::min(open, close), close, 1000);
        }
        data.emplace("S" + std::to_string(s), b.build());
    }
    return AlphaContext::of(data);
}

/** A candidate that anti-ranks the panel -- the trap walk-forward must avoid. */
AlphaFactor sabotage() {
    return AlphaFactor("SABOTAGE", [](const AlphaContext& ctx, int index) {
        std::vector<double> s = factors::momentum(20, 0).scores(ctx, index);
        for (double& x : s) x = -x;
        return s;
    });
}

}  // namespace

TEST(walkForwardPicksTheRealSignalInEveryFold) {
    AlphaContext ctx = panel();
    std::vector<AlphaFactor> candidates = {sabotage(), factors::momentum(20, 0)};
    av::WalkForwardResult r = av::walkForward(ctx, candidates, 5, 30, 80, 40);
    EXPECT_TRUE(r.folds.size() >= 3);
    for (const auto& fold : r.folds) {
        EXPECT_TRUE(fold.chosenFactor == "MOMENTUM(20-0)");
        EXPECT_TRUE(fold.outOfSampleIc > 0.9);
    }
    EXPECT_NEAR(r.efficiency(), 1.0, 0.05);
    EXPECT_TRUE(r.meanOutOfSampleIc > 0.9);
}

TEST(blockedCrossValidationShowsRegimeConsistency) {
    AlphaContext ctx = panel();
    av::CrossValidationResult r = av::crossValidate(ctx, factors::momentum(20, 0), 5, 30, 4);
    EXPECT_TRUE(r.blockIcs.size() == 4);
    EXPECT_NEAR(r.signConsistency(), 1.0, 1e-12);
    EXPECT_NEAR(r.meanIc, 1.0, 1e-9);
    EXPECT_TRUE(r.icStd < 1e-9);
    EXPECT_THROWS(av::crossValidate(ctx, factors::momentum(20, 0), 5, 30, 1));
}

TEST(permutationTestSeparatesSignalFromStaticLuck) {
    AlphaContext ctx = regimePanel();
    av::RobustnessResult real = av::monteCarloRobustness(ctx, factors::momentum(20, 0), 5, 30, 200, 42);
    EXPECT_TRUE(real.pValue < 0.05);
    EXPECT_TRUE(real.observedMeanIc > 0.7);

    quantfinlib::JavaRandom rng(7);
    std::vector<double> fixed(static_cast<std::size_t>(ctx.symbolCount()));
    for (double& x : fixed) x = rng.nextDouble() - 0.5;
    AlphaFactor luck("LUCK", [fixed](const AlphaContext&, int) { return fixed; });
    av::RobustnessResult lucky = av::monteCarloRobustness(ctx, luck, 5, 30, 200, 42);
    EXPECT_NEAR(lucky.pValue, 1.0, 1e-9);
    EXPECT_THROWS(av::monteCarloRobustness(ctx, luck, 5, 30, 5, 42));
}

TEST(trainingWindowsNeverReadTestReturns) {
    AlphaContext ctx = panel();
    std::vector<int> asked;
    AlphaFactor recording("REC", [&asked](const AlphaContext& c, int index) {
        asked.push_back(index);
        return factors::momentum(20, 0).scores(c, index);
    });
    int from = 30;
    int to = 90;
    int horizon = 25;
    av::meanIc(ctx, recording, from, to, horizon);
    EXPECT_TRUE(!asked.empty());
    for (int t : asked) {
        EXPECT_TRUE(t + horizon < to);
    }
}

TEST(walkForwardRefusesWhenNoCandidateScoresInTraining) {
    AlphaContext ctx = panel();
    std::vector<AlphaFactor> candidates = {factors::momentum(250, 0)};
    bool threw = false;
    try {
        av::walkForward(ctx, candidates, 5, 0, 80, 40);
    } catch (const std::invalid_argument& e) {
        threw = true;
        EXPECT_TRUE(std::string(e.what()).find("warm-up") != std::string::npos);
    }
    EXPECT_TRUE(threw);
}

TEST(sensitivitySweepShowsAPlateauNotASpike) {
    AlphaContext ctx = panel();
    std::vector<AlphaFactor> sweep = {factors::momentum(15, 0), factors::momentum(20, 0),
                                      factors::momentum(25, 0), factors::momentum(30, 0)};
    av::SensitivityResult r = av::parameterSensitivity(ctx, sweep, 5, 40);
    EXPECT_TRUE(r.meanIcs.size() == 4);
    for (double ic : r.meanIcs) {
        EXPECT_NEAR(ic, 1.0, 1e-9);
    }
    EXPECT_NEAR(r.worstNeighborDrop, 0.0, 1e-9);
    EXPECT_TRUE(r.best().rfind("MOMENTUM", 0) == 0);
    std::vector<AlphaFactor> single = {sweep[0]};
    EXPECT_THROWS(av::parameterSensitivity(ctx, single, 5, 40));
}

QTEST_MAIN()
