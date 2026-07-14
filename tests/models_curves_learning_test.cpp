// Hand-pinned regressions for the seasonality-curve and online-learning
// classes: the per-bucket seeding fix in VolatilityCurve/SpreadForecaster
// (contrasted with VolumeCurve's session-level seed), AlphaEnsemble's
// non-renormalized IC weighting, and OnlineAlphaLearner's
// predict-then-train prequential IC gate.
#include "quantfinlib/microstructure/alpha_ensemble.hpp"
#include "quantfinlib/microstructure/day_type_profiles.hpp"
#include "quantfinlib/microstructure/online_alpha_learner.hpp"
#include "quantfinlib/microstructure/spread_forecaster.hpp"
#include "quantfinlib/microstructure/volatility_curve.hpp"
#include "quantfinlib/microstructure/volume_curve.hpp"

#include <cmath>
#include <vector>

#include "qtest.hpp"

using namespace quantfinlib;

// ------------------------------------------------------------------
// VolumeCurve: SESSION-LEVEL seed (a zero bucket IS a real observation)
// ------------------------------------------------------------------

TEST(volumeCurveSeedsTheWholeProfileAtOnceOnFirstRollDay) {
    VolumeCurve v(3, 0.5);
    // Day 1: only bucket 0 traded; buckets 1 and 2 are genuinely zero.
    v.onVolume(0, 900);
    v.rollDay();
    // The whole profile seeds directly from day 1 -- including the zero
    // buckets, which are real "no prints" observations, not missing data.
    EXPECT_NEAR(v.profileVolume(0), 900, 1e-9);
    EXPECT_NEAR(v.profileVolume(1), 0.0, 1e-9);
    EXPECT_NEAR(v.profileVolume(2), 0.0, 1e-9);
    EXPECT_TRUE(v.daysLearned() == 1);

    // Day 2: bucket 1 finally trades. Because seeding is SESSION-level (not
    // per-bucket), bucket 1 now EWMA-blends from its 0 seed instead of
    // taking day 2's value directly.
    v.onVolume(0, 900);
    v.onVolume(1, 400);
    v.rollDay();
    EXPECT_NEAR(v.profileVolume(1), 0.5 * (400 - 0.0), 1e-9);  // ramped from 0, not seeded at 400
}

// ------------------------------------------------------------------
// VolatilityCurve: PER-BUCKET seed regression
// ------------------------------------------------------------------

TEST(volatilityCurveSeedsEachBucketIndependentlyOnItsFirstObservation) {
    VolatilityCurve v(3, 0.5);
    // Day 1: only bucket 0 observed.
    v.onVol(0, 0.002);
    v.rollDay();
    EXPECT_NEAR(v.baseline(0), 0.002, 1e-12);
    EXPECT_NEAR(v.baseline(1), 0.0, 1e-12);  // never observed: stays 0, not EWMA-ramped

    // Day 2: bucket 1 is observed for the FIRST time. Per-bucket seeding
    // means it takes today's value directly (its own first observation),
    // NOT an EWMA blend from the 0 it never earned.
    v.onVol(0, 0.0022);
    v.onVol(1, 0.005);
    v.rollDay();
    EXPECT_NEAR(v.baseline(1), 0.005, 1e-12);  // seeded directly, not 0.5*(0.005-0)=0.0025
    // Bucket 0, which HAS a prior baseline, blends normally.
    EXPECT_NEAR(v.baseline(0), 0.002 + 0.5 * (0.0022 - 0.002), 1e-12);
}

TEST(volatilityCurveRegimeIsNeutralUntilLearnedOrOnBadInput) {
    VolatilityCurve v(2, 0.1);
    EXPECT_NEAR(v.regime(0, 0.01), 0.0, 0.0);  // no baseline yet: neutral, not "extreme"
    v.onVol(0, 0.001);
    v.rollDay();
    EXPECT_NEAR(v.regime(0, 0.001), 0.0, 1e-9);          // at baseline: calm
    EXPECT_NEAR(v.regime(0, 0.002), 1.0, 1e-9);          // 2x baseline: clamps to the ceiling
    EXPECT_NEAR(v.regime(0, std::nan("")), 0.0, 0.0);    // bad reading reads as neutral
}

// ------------------------------------------------------------------
// SpreadForecaster: PER-BUCKET seed regression (same fix as VolatilityCurve)
// ------------------------------------------------------------------

TEST(spreadForecasterSeedsEachBucketIndependently) {
    SpreadForecaster f(2, 0.5, 5'000'000'000LL);
    f.onSpread(0, 0.02, 0);
    f.rollDay();
    EXPECT_NEAR(f.baseline(0), 0.02, 1e-12);
    EXPECT_NEAR(f.baseline(1), 0.0, 1e-12);

    // Bucket 1's first-ever observation, on day 2: seeds directly.
    f.onSpread(0, 0.021, 1'000'000'000LL);
    f.onSpread(1, 0.05, 2'000'000'000LL);
    f.rollDay();
    EXPECT_NEAR(f.baseline(1), 0.05, 1e-12);  // not 0.5*(0.05-0)=0.025
}

TEST(spreadForecasterForecastsLastObservedBeforeAnyBaseline) {
    SpreadForecaster f;
    EXPECT_TRUE(std::isnan(f.forecast(0, 0)));  // nothing observed at all
    f.onSpread(5, 0.03, 0);
    EXPECT_NEAR(f.forecast(5, 0), 0.03, 1e-12);  // no rollDay yet: degrades to last observed
}

// ------------------------------------------------------------------
// AlphaEnsemble
// ------------------------------------------------------------------

TEST(alphaEnsembleWeightsAreNotRenormalized) {
    AlphaEnsemble ens(2, 0.5);
    // Component 0's value fed at call i is scored (as the PREVIOUS
    // snapshot) against the return realized at call i+1, so a "perfect
    // predictor" must equal ret(i+1), not ret(i) -- since ret alternates
    // sign every call, that predictor value is simply -ret(i). Component 1
    // is unrelated noise.
    for (int i = 0; i < 40; i++) {
        double ret = (i % 2 == 0) ? 0.01 : -0.01;
        double predictor = -ret;  // == ret(i+1), the next call's realized return
        std::vector<double> values = {predictor, (i % 3 == 0) ? 0.01 : -0.01};
        ens.onObservation(values, ret);
    }
    double ic0 = ens.componentIC(0);
    EXPECT_TRUE(ic0 > 0.9);  // component 0 is essentially a perfect predictor
    // Feed a small value for component 0 only; combined() must reflect
    // ic0 * value, NOT a renormalized (sum-to-one) blend.
    std::vector<double> probe = {0.1, 0.0};
    double combined = ens.combined(probe);
    EXPECT_NEAR(combined, ic0 * 0.1, 1e-6);
}

TEST(alphaEnsembleIsSilentBeforeOneIcMemory) {
    AlphaEnsemble ens(1, 0.5);  // memory ~= 1/0.5 = 2 samples
    ens.onObservation({1.0}, 0.01);  // only a snapshot -- no scored observation yet
    EXPECT_TRUE(ens.samples() == 0);
    EXPECT_NEAR(ens.combined({1.0}), 0.0, 0.0);
}

// ------------------------------------------------------------------
// OnlineAlphaLearner
// ------------------------------------------------------------------

TEST(onlineAlphaLearnerLearnsAPositiveWeightOnAPredictiveFeature) {
    OnlineAlphaLearner learner(0.1, 1e-4, 0.05);
    // Feature 0 perfectly predicts the return; train many steps.
    for (int i = 0; i < 300; i++) {
        double f0 = (i % 2 == 0) ? 1.0 : -1.0;
        double ret = 0.01 * f0;
        learner.train(f0, 0.0, 0.0, 0.0, ret);
    }
    EXPECT_TRUE(learner.weight(0) > 0);
    EXPECT_TRUE(learner.outOfSampleIC() > 0.5);
    // Gated output should now emit a nonzero, correctly-signed signal.
    double pred = learner.normalizedPrediction(1.0, 0.0, 0.0, 0.0);
    EXPECT_TRUE(pred > 0);
}

TEST(onlineAlphaLearnerIsSilentBeforeTrackRecordOrPositiveIC) {
    OnlineAlphaLearner learner;
    // Untrained: normalizedPrediction must be exactly 0 (no track record).
    EXPECT_NEAR(learner.normalizedPrediction(1.0, 1.0, 1.0, 1.0), 0.0, 0.0);
    // Non-finite inputs are skipped entirely -- no state change.
    learner.train(std::nan(""), 0, 0, 0, 0.01);
    EXPECT_TRUE(learner.samples() == 0);
}

// ------------------------------------------------------------------
// DayTypeProfiles
// ------------------------------------------------------------------

TEST(dayTypeProfilesBuildIndependentEagerlyConstructedCurves) {
    DayTypeProfiles<VolumeCurve> profiles(3, [](int) { return VolumeCurve(4, 0.2); });
    EXPECT_TRUE(profiles.dayTypes() == 3);
    profiles.profile(1).onVolume(0, 500);
    profiles.profile(1).rollDay();
    // Each day type is independent: type 0 never learned type 1's volume.
    EXPECT_NEAR(profiles.profile(1).profileVolume(0), 500, 1e-9);
    EXPECT_NEAR(profiles.profile(0).profileVolume(0), 0.0, 1e-9);
}

QTEST_MAIN()
