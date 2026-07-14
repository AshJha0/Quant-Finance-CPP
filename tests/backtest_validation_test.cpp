// Port of ValidationRobustnessTest.java (PurgedKFold + OverfitProbability
// sections), the SharpeValidation tests of ValidationTest.java, and the
// minTrackRecordLength round trip of AlphaResearchRoundTest.java. All
// pins are hand-derivable index arithmetic or closed-form identities;
// the deflated-Sharpe trial noise comes from a seeded Box-Muller instead
// of Java's nextGaussian (property assertions only).
#include <cmath>
#include <vector>

#include "qtest.hpp"
#include "quantfinlib/backtest/validation/overfit_probability.hpp"
#include "quantfinlib/backtest/validation/purged_k_fold.hpp"
#include "quantfinlib/backtest/validation/sharpe_validation.hpp"
#include "quantfinlib/backtest/validation/splittable_random.hpp"
#include "quantfinlib/util/math_utils.hpp"

using quantfinlib::SplittableRandom;
namespace pkf = quantfinlib::purged_k_fold;
namespace ofp = quantfinlib::overfit_probability;
namespace sv = quantfinlib::sharpe_validation;
namespace mu = quantfinlib::math_utils;

// ------------------------------------------------------------ PurgedKFold

TEST(purgedSplitsMatchHandDerivedIndexArithmetic) {
    // n=20, k=4, labelHorizon=2, embargo=1. Fold 1 tests [5,10):
    // head = [0, 5-2) = {0,1,2}; tail = [10+2+1, 20) = {13..19}.
    std::vector<pkf::Split> splits = pkf::splits(20, 4, 2, 1);
    EXPECT_TRUE(splits.size() == 4);

    const pkf::Split& f1 = splits[1];
    EXPECT_TRUE(f1.testFrom == 5);
    EXPECT_TRUE(f1.testTo == 10);
    EXPECT_TRUE(f1.trainIndices == (std::vector<int>{0, 1, 2, 13, 14, 15, 16, 17, 18, 19}));

    // Fold 0 has no head at all: train = [0+5+2+1, 20) = {8..19}.
    const pkf::Split& f0 = splits[0];
    EXPECT_TRUE(f0.trainIndices
                == (std::vector<int>{8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19}));

    // Test ranges partition [0, 20) with no gaps.
    int covered = 0;
    for (const pkf::Split& s : splits) {
        covered += s.testTo - s.testFrom;
    }
    EXPECT_TRUE(covered == 20);
}

TEST(zeroHorizonZeroEmbargoReducesToPlainKFold) {
    // With nothing to purge, train must be the exact complement of test.
    for (const pkf::Split& s : pkf::splits(12, 3, 0, 0)) {
        EXPECT_TRUE(static_cast<int>(s.trainIndices.size())
                    == 12 - (s.testTo - s.testFrom));
        for (int i : s.trainIndices) {
            EXPECT_TRUE(i < s.testFrom || i >= s.testTo);
        }
    }
}

TEST(purgedKFoldRefusesDegenerateSetups) {
    EXPECT_THROWS(pkf::splits(20, 1, 0, 0));
    EXPECT_THROWS(pkf::splits(5, 3, 0, 0));
    EXPECT_THROWS(pkf::splits(20, 4, -1, 0));
    // Horizon so wide the purge eats ALL training data must throw, not
    // silently return an empty training set.
    EXPECT_THROWS(pkf::splits(8, 2, 10, 0));
}

// ----------------------------------------------------- OverfitProbability

TEST(genuineSkillScoresPboZero) {
    // Variant 0 beats variant 1 in EVERY period: whatever half is
    // in-sample, the winner is 0 and it ranks 2 of 2 out of sample.
    // omega = 2/3 on every one of C(4,2)=6 splits -> lambda = ln 2 > 0.
    std::vector<std::vector<double>> r(16, std::vector<double>(2));
    for (int t = 0; t < 16; t++) {
        r[static_cast<std::size_t>(t)][0] = 0.01;
        r[static_cast<std::size_t>(t)][1] = -0.01;
    }
    ofp::Result res = ofp::cscv(r, 4, [](const std::vector<double>& s) { return mu::mean(s); });
    EXPECT_TRUE(res.combinations == 6);
    EXPECT_NEAR(res.pbo, 0.0, 0.0);
    for (double lambda : res.logits) {
        EXPECT_NEAR(lambda, std::log(2.0), 1e-12);
    }
}

TEST(regimeFlippingNoiseScoresPboOne) {
    // Variant 0 wins the first half of time, variant 1 (its mirror) the
    // second half. Whatever blocks you train on, the winner does no
    // better than tie out of sample: every logit <= 0, PBO = 1. +/-1.0 so
    // mixed-block means cancel EXACTLY in floating point.
    std::vector<std::vector<double>> r(16, std::vector<double>(2));
    for (int t = 0; t < 16; t++) {
        r[static_cast<std::size_t>(t)][0] = t < 8 ? 1.0 : -1.0;
        r[static_cast<std::size_t>(t)][1] = -r[static_cast<std::size_t>(t)][0];
    }
    ofp::Result res = ofp::cscv(r, 4, [](const std::vector<double>& s) { return mu::mean(s); });
    EXPECT_TRUE(res.combinations == 6);
    EXPECT_NEAR(res.pbo, 1.0, 0.0);
}

TEST(sharpeObjectiveHandlesFlatSeriesAndCountsCombinations) {
    // 3 variants, 8 blocks: C(8,4) = 70 combinations. Variant 2 is flat
    // (zero variance) — the Sharpe objective scores it 0 rather than
    // dividing by zero.
    std::vector<std::vector<double>> r(32, std::vector<double>(3));
    for (int t = 0; t < 32; t++) {
        r[static_cast<std::size_t>(t)][0] = ((t * 7 + 3) % 5 - 2) / 100.0;
        r[static_cast<std::size_t>(t)][1] = ((t * 11 + 1) % 7 - 3) / 100.0;
        r[static_cast<std::size_t>(t)][2] = 0.0;
    }
    ofp::Result res = ofp::cscvSharpe(r, 8);
    EXPECT_TRUE(res.combinations == 70);
    EXPECT_TRUE(res.pbo >= 0 && res.pbo <= 1);
}

TEST(cscvValidatesItsInputs) {
    std::vector<std::vector<double>> ok(16, std::vector<double>(2, 0.0));
    EXPECT_THROWS(ofp::cscvSharpe(ok, 5));                                    // odd
    EXPECT_THROWS(ofp::cscvSharpe(ok, 18));                                   // > cap
    EXPECT_THROWS(ofp::cscvSharpe(
            std::vector<std::vector<double>>(16, std::vector<double>(1, 0.0)), 4));
    EXPECT_THROWS(ofp::cscvSharpe(
            std::vector<std::vector<double>>(6, std::vector<double>(2, 0.0)), 4));
    std::vector<std::vector<double>> ragged = {{0.1, 0.2}, {0.1}};
    EXPECT_THROWS(ofp::cscvSharpe(ragged, 4));
    std::vector<std::vector<double>> nanMat(16, std::vector<double>(2, 0.0));
    nanMat[3][1] = std::nan("");
    EXPECT_THROWS(ofp::cscvSharpe(nanMat, 4));
}

// ------------------------------------------------------- SharpeValidation

TEST(probabilisticSharpeBehavesSanely) {
    // Strong Sharpe over a long track vs zero benchmark: near certainty.
    double high = sv::probabilisticSharpe(0.15, 0, 1000, 0, 3);
    EXPECT_TRUE(high > 0.99);
    // Same Sharpe over a tiny track: much less confident.
    double low = sv::probabilisticSharpe(0.15, 0, 20, 0, 3);
    EXPECT_TRUE(low < high);
    // Negative skew and fat tails reduce confidence.
    double uglyTails = sv::probabilisticSharpe(0.15, 0, 1000, -1.5, 8);
    EXPECT_TRUE(uglyTails < high);
    EXPECT_THROWS(sv::probabilisticSharpe(0.15, 0, 1, 0, 3));
}

TEST(deflatedSharpeAppliesMultipleTestingHaircut) {
    // 100 trials whose Sharpes are pure noise around zero (Box-Muller on
    // the seeded SplittableRandom; Java drew nextGaussian — the
    // assertions are properties of the haircut, not of the stream).
    SplittableRandom rnd(9);
    std::vector<double> trials(100);
    for (std::size_t i = 0; i < trials.size(); i++) {
        double u1 = rnd.nextDouble();
        double u2 = rnd.nextDouble();
        if (u1 <= 0) {
            u1 = 1e-300;
        }
        trials[i] = 0.05 * std::sqrt(-2 * std::log(u1))
                * std::cos(2 * 3.141592653589793 * u2);
    }
    double observed = 0.10;  // the "winner"
    double psr = sv::probabilisticSharpe(observed, 0, 500, 0, 3);
    double dsr = sv::deflatedSharpe(observed, trials, 500, 0, 3);
    // Deflation must cost confidence versus the naive zero benchmark.
    EXPECT_TRUE(dsr < psr);
    EXPECT_TRUE(sv::expectedMaxSharpe(100, 0.0025) > 0);
    EXPECT_THROWS(sv::expectedMaxSharpe(1, 0.0025));
}

TEST(minTrackRecordLengthRoundTripsThroughProbabilisticSharpe) {
    double sr = 0.1;  // per period
    double n = sv::minTrackRecordLength(sr, 0, -0.5, 4, 0.95);
    EXPECT_TRUE(n > 100 && n < 1000);  // a plausible track record
    // The round trip: AT the computed length, PSR clears the bar...
    EXPECT_TRUE(sv::probabilisticSharpe(sr, 0, static_cast<int>(std::ceil(n)), -0.5, 4)
                >= 0.95 - 1e-3);
    // ...and meaningfully short of it, it does not.
    EXPECT_TRUE(sv::probabilisticSharpe(sr, 0, static_cast<int>(n * 0.7), -0.5, 4) < 0.95);
    // More confidence demands a longer record, always.
    EXPECT_TRUE(sv::minTrackRecordLength(sr, 0, -0.5, 4, 0.99) > n);
    // No record length proves an edge the record does not show.
    EXPECT_TRUE(std::isinf(sv::minTrackRecordLength(0.0, 0.1, 0, 3, 0.95)));
    EXPECT_THROWS(sv::minTrackRecordLength(0.1, 0, 0, 3, 1.0));
}

QTEST_MAIN()
