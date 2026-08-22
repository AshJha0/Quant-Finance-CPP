// Phase 5B execution tests: the precomputed schedule designs (TWAP, VWAP,
// POV, Implementation Shortfall, WMR fixing). Pins are ported 1:1 from the
// Java reference tests (ExecutionTest, PovAndIsSchedulerTest,
// WmrFixingSchedulerTest) so the arithmetic can never silently drift.
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

#include "qtest.hpp"
#include "quantfinlib/execution/implementation_shortfall_scheduler.hpp"
#include "quantfinlib/execution/pov_tracker.hpp"
#include "quantfinlib/execution/slice.hpp"
#include "quantfinlib/execution/twap_scheduler.hpp"
#include "quantfinlib/execution/vwap_scheduler.hpp"
#include "quantfinlib/execution/wmr_fixing_scheduler.hpp"
#include "quantfinlib/microstructure/almgren_chriss.hpp"

using namespace quantfinlib;

namespace {
AlmgrenChriss::Params isParams(double lambda) {
    return AlmgrenChriss::Params(100000, 1.0, 10, 0.5, 1e-5, 1e-6, lambda);
}
}  // namespace

// -------------------------------------------------------------------- TWAP

TEST(twapSplitsEvenlyAndPreservesTotal) {
    std::vector<Slice> slices = twap_scheduler::schedule(1000, 60000, 6);
    EXPECT_TRUE(slices.size() == 6);
    std::int64_t total = 0;
    for (const Slice& s : slices) {
        total += s.quantity;
        EXPECT_TRUE(std::abs(s.quantity - 1000 / 6) <= 1);
    }
    EXPECT_TRUE(total == 1000);
    EXPECT_TRUE(slices.front().offsetMillis == 0);
    EXPECT_TRUE(slices.back().offsetMillis == 50000);
}

TEST(randomizedTwapPreservesTotalAndVaries) {
    std::vector<Slice> slices = twap_scheduler::scheduleRandomized(10000, 60000, 8, 0.3, 42);
    std::int64_t total = 0;
    bool uneven = false;
    for (const Slice& s : slices) {
        total += s.quantity;
        if (std::abs(s.quantity - 1250) > 1) {
            uneven = true;
        }
    }
    EXPECT_TRUE(total == 10000);
    EXPECT_TRUE(uneven);
}

TEST(twapOverflowGuardRejectsPathologicalHorizons) {
    EXPECT_THROWS(twap_scheduler::schedule(0, 1000, 1));
    EXPECT_THROWS(twap_scheduler::schedule(100, -1, 4));
    EXPECT_THROWS(twap_scheduler::schedule(100, std::numeric_limits<std::int64_t>::max() / 2, 4));
}

// -------------------------------------------------------------------- VWAP

TEST(vwapAllocatesProportionallyToProfile) {
    std::vector<double> profile = {1, 3};
    std::vector<Slice> slices = vwap_scheduler::schedule(1000, profile, 20000);
    EXPECT_TRUE(slices[0].quantity == 250);
    EXPECT_TRUE(slices[1].quantity == 750);
    EXPECT_TRUE(slices[1].offsetMillis == 10000);
}

TEST(proportionalAllocationHandlesRounding) {
    std::vector<double> w = {1, 1, 1};
    std::vector<std::int64_t> alloc = vwap_scheduler::allocateProportionally(100, w);
    EXPECT_TRUE(alloc[0] + alloc[1] + alloc[2] == 100);
    for (std::int64_t a : alloc) {
        EXPECT_TRUE(a >= 33 && a <= 34);
    }
}

TEST(allocateProportionallyRejectsNaNWeightInsteadOfSilentUniformSplit) {
    std::vector<double> w = {1, std::numeric_limits<double>::quiet_NaN(), 1};
    EXPECT_THROWS(vwap_scheduler::allocateProportionally(100, w));
}

TEST(allocateProportionallyRejectsInfiniteWeightInsteadOfSilentUniformSplit) {
    std::vector<double> w = {1, std::numeric_limits<double>::infinity(), 1};
    EXPECT_THROWS(vwap_scheduler::allocateProportionally(100, w));
}

TEST(vwapScheduleRejectsNaNInVolumeProfile) {
    std::vector<double> profile = {1, std::numeric_limits<double>::quiet_NaN(), 1};
    EXPECT_THROWS(vwap_scheduler::schedule(1000, profile, 20000));
}

// --------------------------------------------------------------------- POV

TEST(povChasesTargetParticipation) {
    PovTracker pov(10000, 0.10, 0, 1000);
    EXPECT_TRUE(pov.dueQuantity() == 0);
    pov.onMarketVolume(5000);  // target = 500
    EXPECT_TRUE(pov.dueQuantity() == 500);
    pov.onExecuted(500);
    EXPECT_TRUE(pov.dueQuantity() == 0);
    pov.onMarketVolume(3000);  // target = 800, executed 500
    EXPECT_TRUE(pov.dueQuantity() == 300);
    EXPECT_NEAR(pov.realizedParticipation(), 0.0625, 1e-12);
}

TEST(povRespectsSliceBoundsAndParentRemainder) {
    PovTracker pov(1000, 0.5, 100, 400);
    pov.onMarketVolume(150);  // behind by 75 < minSlice
    EXPECT_TRUE(pov.dueQuantity() == 0);
    pov.onMarketVolume(1850);  // behind by 1000 -> capped at 400
    EXPECT_TRUE(pov.dueQuantity() == 400);
    pov.onExecuted(900);
    pov.onMarketVolume(10000);  // behind, but only 100 left
    EXPECT_TRUE(pov.dueQuantity() == 100);
    pov.onExecuted(100);
    EXPECT_TRUE(pov.done());
    pov.onMarketVolume(10000);
    EXPECT_TRUE(pov.dueQuantity() == 0);
}

TEST(povValidatesItsParameters) {
    EXPECT_THROWS(PovTracker(0, 0.1, 0, 10));
    EXPECT_THROWS(PovTracker(100, 0, 0, 10));
    EXPECT_THROWS(PovTracker(100, 1.5, 0, 10));
    EXPECT_THROWS(PovTracker(100, 0.1, 20, 10));
}

// ------------------------------------------------- Implementation Shortfall

TEST(isScheduleSumsExactlyAndFrontLoadsWithUrgency) {
    std::vector<Slice> risky = implementation_shortfall_scheduler::schedule(isParams(1e-4), 3600000);
    EXPECT_TRUE(risky.size() == 10);
    std::int64_t total = 0;
    for (const Slice& s : risky) {
        total += s.quantity;
    }
    EXPECT_TRUE(total == 100000);
    EXPECT_TRUE(risky[0].quantity > risky[9].quantity);
    EXPECT_TRUE(risky[0].offsetMillis == 0);
    EXPECT_TRUE(risky[9].offsetMillis == 3600000LL * 9 / 10);
}

TEST(zeroRiskAversionDegradesToTwap) {
    std::vector<Slice> twap = implementation_shortfall_scheduler::schedule(isParams(0), 1000000);
    std::int64_t first = twap[0].quantity;
    for (const Slice& s : twap) {
        EXPECT_TRUE(std::abs(s.quantity - first) <= 1);
    }
}

TEST(frontLoadCalibrationHitsTheRequestedFraction) {
    AlmgrenChriss::Params base = isParams(0);
    double lambda = implementation_shortfall_scheduler::riskAversionForFrontLoad(base, 0.30);
    AlmgrenChriss::Trajectory t = AlmgrenChriss::optimalTrajectory(base.withRiskAversion(lambda));
    EXPECT_NEAR(t.trades[0] / 100000, 0.30, 1e-3);
    EXPECT_THROWS(implementation_shortfall_scheduler::riskAversionForFrontLoad(base, 0.05));
}

TEST(sinhOverflowFailsLoudlyInsteadOfSilentlyDegrading) {
    // A huge lambda overflows sinh in the AC trajectory (NaN holdings):
    // kappa*T ~ 840, past sinh's ~710 overflow point. schedule() must
    // throw, not spin an O(parentShares) loop emitting a garbage schedule.
    EXPECT_THROWS(implementation_shortfall_scheduler::schedule(isParams(1e40), 1000000));
    // The calibrator, by contrast, must survive probing overflowing
    // lambdas (NaN reads as "front-loads more than enough") and still
    // land on the requested fraction.
    double lambda = implementation_shortfall_scheduler::riskAversionForFrontLoad(isParams(0), 0.90);
    AlmgrenChriss::Trajectory t = AlmgrenChriss::optimalTrajectory(isParams(0).withRiskAversion(lambda));
    EXPECT_NEAR(t.trades[0] / 100000, 0.90, 1e-2);
}

// -------------------------------------------------------------- WMR fixing

TEST(wmrSlicesSpanTheWindowEvenlyAndSumExactly) {
    std::vector<Slice> s = wmr_fixing_scheduler::schedule(10000000, 20);
    EXPECT_TRUE(s.size() == 20);
    std::int64_t total = 0;
    for (const Slice& x : s) {
        total += x.quantity;
    }
    EXPECT_TRUE(total == 10000000);
    EXPECT_TRUE(s[0].offsetMillis == 0);
    EXPECT_TRUE(s[19].offsetMillis == wmr_fixing_scheduler::WINDOW_MILLIS * 19 / 20);
    std::int64_t first = s[0].quantity;
    for (const Slice& x : s) {
        EXPECT_TRUE(std::abs(x.quantity - first) <= 1);
    }
}

TEST(wmrCustomWindowsAreRespected) {
    std::vector<Slice> s = wmr_fixing_scheduler::schedule(100, 60000, 4);
    EXPECT_TRUE(s[0].offsetMillis == 0);
    EXPECT_TRUE(s[1].offsetMillis == 15000);
    EXPECT_TRUE(s[3].offsetMillis == 45000);
}

TEST(wmrZeroQuantityChildrenAndOverflowOffsetsAreRejected) {
    EXPECT_THROWS(wmr_fixing_scheduler::schedule(3, 10));
    EXPECT_THROWS(wmr_fixing_scheduler::schedule(100, std::numeric_limits<std::int64_t>::max() / 2, 4));
    EXPECT_THROWS(wmr_fixing_scheduler::schedule(0, 10));
    EXPECT_THROWS(wmr_fixing_scheduler::schedule(100, 0, 5));
    EXPECT_THROWS(wmr_fixing_scheduler::schedule(100, 1000, 0));
}

TEST(wmrScheduleIsExactlyATwapSchedule) {
    // The class's whole claim is "TWAP-in-window IS neutral replication":
    // pin the delegation so the two can never drift.
    std::vector<Slice> a = twap_scheduler::schedule(1000003, 300000, 7);
    std::vector<Slice> b = wmr_fixing_scheduler::schedule(1000003, 300000, 7);
    EXPECT_TRUE(a.size() == b.size());
    for (std::size_t i = 0; i < a.size(); i++) {
        EXPECT_TRUE(a[i].offsetMillis == b[i].offsetMillis);
        EXPECT_TRUE(a[i].quantity == b[i].quantity);
    }
}

QTEST_MAIN()
