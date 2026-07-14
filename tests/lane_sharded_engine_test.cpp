// Port of the Java ShardedTradingEngine behavior pins: single- and
// multi-shard symbol registration (a cross co-located on two shards),
// localId() resolving the right per-shard dense id (and throwing for a
// shard that doesn't host the symbol), register-after-start throwing, and
// publish() fanning a tick to every hosting shard and being observable via
// each shard's own bus.
#include "quantfinlib/trading/sharded_trading_engine.hpp"

#include <atomic>
#include <chrono>
#include <memory>
#include <thread>

#include "qtest.hpp"

using quantfinlib::HftRiskGate;
using quantfinlib::ShardedTradingEngine;

namespace {
auto gateFactory = [](int) { return std::make_unique<HftRiskGate>(8); };
}

TEST(singleShardSymbolRoutesToThatShardOnly) {
    ShardedTradingEngine engine(2, 32, 16, 8, false, gateFactory);
    int handle = engine.registerSymbol("EURUSD", {0});
    EXPECT_TRUE(engine.localId(handle, 0) == 0);
    EXPECT_THROWS(engine.localId(handle, 1));
}

TEST(multiShardSymbolRegistersOnBothAndFansOutOnPublish) {
    ShardedTradingEngine engine(2, 64, 16, 8, false, gateFactory);
    int cross = engine.registerSymbol("EURUSD_SYNTH", {0, 1});
    EXPECT_TRUE(engine.localId(cross, 0) == 0);
    EXPECT_TRUE(engine.localId(cross, 1) == 0);

    std::atomic<int> shard0Ticks{0}, shard1Ticks{0};
    engine.bus(0).subscribeAll(
            [&](int, double, double, std::int64_t) { shard0Ticks.fetch_add(1); });
    engine.bus(1).subscribeAll(
            [&](int, double, double, std::int64_t) { shard1Ticks.fetch_add(1); });

    engine.start();
    EXPECT_TRUE(engine.publish(cross, 1.1, 1e6, 1));
    for (int i = 0; i < 1000 && engine.processedCount() < 2; i++) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    engine.close();

    EXPECT_TRUE(shard0Ticks.load() == 1);
    EXPECT_TRUE(shard1Ticks.load() == 1);
    EXPECT_TRUE(engine.processedCount() == 2);
}

TEST(registerAfterStartThrows) {
    ShardedTradingEngine engine(1, 32, 16, 8, false, gateFactory);
    engine.registerSymbol("A", {0});
    engine.start();
    EXPECT_THROWS(engine.registerSymbol("B", {0}));
    engine.close();
}

TEST(registerWithEmptyShardsThrows) {
    ShardedTradingEngine engine(1, 32, 16, 8, false, gateFactory);
    EXPECT_THROWS(engine.registerSymbol("A", {}));
}

TEST(shardCountAndGatesExposeEveryShard) {
    ShardedTradingEngine engine(3, 32, 16, 8, false, gateFactory);
    EXPECT_TRUE(engine.shardCount() == 3);
    auto gates = engine.gates();
    EXPECT_TRUE(gates.size() == 3);
    EXPECT_TRUE(&engine.gate(0) == gates[0]);
    EXPECT_TRUE(&engine.gate(1) == gates[1]);
    EXPECT_TRUE(&engine.gate(2) == gates[2]);
}

TEST(deliveredCountAggregatesAcrossShards) {
    ShardedTradingEngine engine(2, 32, 16, 8, false, gateFactory);
    int a = engine.registerSymbol("A", {0});
    int b = engine.registerSymbol("B", {1});
    engine.start();
    engine.gateway(0).submit(engine.localId(a, 0), quantfinlib::Side::BUY, 1, 1.0, 1);
    engine.gateway(1).submit(engine.localId(b, 1), quantfinlib::Side::BUY, 1, 1.0, 1);
    for (int i = 0; i < 1000 && engine.deliveredCount() < 2; i++) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    engine.close();
    EXPECT_TRUE(engine.deliveredCount() == 2);
}

QTEST_MAIN()
