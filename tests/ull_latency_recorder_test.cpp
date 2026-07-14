// Port of LatencyRecorderTest + HiccupMonitorTest (Java): the log-linear
// histogram's percentile error bound (~6-7% worst case at 16 sub-buckets
// per power of two), exact bucketing below 16, negative clamping, the
// summary formatting, and a short live HiccupMonitor run.
#include "quantfinlib/util/latency_recorder.hpp"

#include <chrono>
#include <string>
#include <thread>

#include "quantfinlib/util/hiccup_monitor.hpp"
#include "qtest.hpp"

using quantfinlib::HiccupMonitor;
using quantfinlib::LatencyRecorder;

TEST(percentilesWithinBucketResolution) {
    LatencyRecorder r;
    for (std::int64_t v = 1; v <= 10000; v++) {
        r.record(v);
    }
    EXPECT_TRUE(r.count() == 10000);
    EXPECT_TRUE(r.min() == 1);
    EXPECT_TRUE(r.max() == 10000);
    EXPECT_NEAR(r.mean(), 5000.5, 0.01);
    // Log-linear buckets: ~6-7% worst-case quantile error.
    EXPECT_NEAR(static_cast<double>(r.percentile(0.50)), 5000.0, 5000 * 0.07);
    EXPECT_NEAR(static_cast<double>(r.percentile(0.99)), 9900.0, 9900 * 0.07);
    EXPECT_TRUE(r.percentile(0.999) <= r.max());
    // Percentiles are clamped into [min, max] and monotone in p.
    EXPECT_TRUE(r.percentile(0.0) >= r.min());
    EXPECT_TRUE(r.percentile(1.0) <= r.max());
    EXPECT_TRUE(r.percentile(0.50) <= r.percentile(0.90));
    EXPECT_TRUE(r.percentile(0.90) <= r.percentile(0.99));
}

TEST(smallExactValuesAndReset) {
    LatencyRecorder r;
    for (int i = 0; i < 10; i++) {
        r.record(7);
    }
    EXPECT_TRUE(r.percentile(0.5) == 7);  // values < 16 are bucketed exactly
    EXPECT_TRUE(r.max() == 7);
    r.reset();
    EXPECT_TRUE(r.count() == 0);
    EXPECT_TRUE(r.max() == 0);
    EXPECT_TRUE(r.min() == 0);
    EXPECT_NEAR(r.mean(), 0.0, 0.0);
    EXPECT_TRUE(r.percentile(0.5) == 0);
}

TEST(negativeClampedAndSummaryFormats) {
    LatencyRecorder r;
    r.record(-5);
    r.record(500);
    r.record(2500000);
    std::string s = r.summary();
    EXPECT_TRUE(s.find("n=3") != std::string::npos);
    EXPECT_TRUE(s.find("ms") != std::string::npos);  // 2.5ms max formatted in ms
    EXPECT_TRUE(r.min() == 0);                        // -5 clamped to 0
}

TEST(bucketIndexMatchesTheJavaLayout) {
    // Hand-pins of the HdrHistogram-style index math (SUB_BITS = 4).
    EXPECT_TRUE(LatencyRecorder::bucketIndex(0) == 0);
    EXPECT_TRUE(LatencyRecorder::bucketIndex(15) == 15);
    // 16 = 2^4, exp=4, sub=(16>>0)&15=0 -> 64.
    EXPECT_TRUE(LatencyRecorder::bucketIndex(16) == 64);
    // 1000: exp=9, sub=(1000>>5)&15=(31)&15=15 -> 9*16+15 = 159.
    EXPECT_TRUE(LatencyRecorder::bucketIndex(1000) == 159);
}

TEST(hiccupMonitorSamplesExcessSleepTime) {
    EXPECT_THROWS(HiccupMonitor(50000));  // < 100us resolution refused
    HiccupMonitor mon(1000000);           // 1 ms resolution
    mon.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(80));
    mon.close();
    EXPECT_TRUE(mon.samples() > 0);
    EXPECT_TRUE(mon.maxHiccupNanos() >= 0);
    std::string s = mon.summary();
    EXPECT_TRUE(s.find("platform hiccups (res=1ms):") != std::string::npos);
}

QTEST_MAIN()
