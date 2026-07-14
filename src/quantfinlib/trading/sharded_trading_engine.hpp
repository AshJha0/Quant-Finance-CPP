// Port of com.quantfinlib.trading.ShardedTradingEngine (Java): horizontal
// scaling as shipped machinery, not a recipe: N independent
// bus -> risk gate -> order gateway stacks (one consumer core and one venue
// core per shard) behind a single symbol-routing facade. The scaling model
// is shared-nothing: shards never touch each other's state, so aggregate
// capacity is per-shard throughput x shard count.
//
// Symbol routing rules:
//  - registerSymbol(symbol, shards) assigns a symbol to one or MORE shards
//    -- multi-shard registration is the cross co-location tool: a synthetic
//    cross must live where both its legs tick, and duplicating a leg's feed
//    into a second shard costs one extra ring publish;
//  - publish() fans a tick to every shard hosting the symbol -- zero
//    allocation on the hot path, single producer thread (or one producer
//    per disjoint symbol set);
//  - strategies/quoters attach per shard via bus(shard) exactly as they
//    would to a standalone bus -- the shard is invisible to them.
//
// What sharding deliberately does NOT solve: firm-wide risk. Each shard's
// HftRiskGate sees only its own symbols; put a GlobalRiskAggregator over
// gates() for cross-shard exposure caps.
#pragma once

#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "quantfinlib/marketdata/hft_market_data_bus.hpp"
#include "quantfinlib/trading/hft_order_gateway.hpp"
#include "quantfinlib/trading/hft_risk_gate.hpp"

namespace quantfinlib {

class ShardedTradingEngine {
public:
    /**
     * @param shardCount         independent stacks (2 threads each: consumer + venue)
     * @param busRingCapacity    tick ring per shard (power of two)
     * @param orderRingCapacity  order ring per shard (power of two)
     * @param maxSymbolsPerShard dense-id capacity per shard
     * @param busySpin           spin-wait consumers (latency) vs park (cores)
     * @param gateFactory        builds each shard's risk gate (limits per shard)
     * @throws std::invalid_argument if shardCount <= 0
     */
    ShardedTradingEngine(int shardCount, int busRingCapacity, int orderRingCapacity,
                         int maxSymbolsPerShard, bool busySpin,
                         const std::function<std::unique_ptr<HftRiskGate>(int)>& gateFactory);

    ~ShardedTradingEngine() { close(); }

    ShardedTradingEngine(const ShardedTradingEngine&) = delete;
    ShardedTradingEngine& operator=(const ShardedTradingEngine&) = delete;

    /**
     * Registers a symbol on the given shard(s); returns the global handle
     * used with publish(). Setup path -- call before start().
     * @throws std::runtime_error if already started
     * @throws std::invalid_argument if shards is empty
     */
    int registerSymbol(const std::string& symbol, const std::vector<int>& shards);

    /**
     * The symbol's dense id within one of its shards (for subscriptions).
     * @throws std::invalid_argument if not hosted on that shard
     */
    int localId(int handle, int shard) const;

    /** Starts every shard (subscribe strategies via bus() first). */
    void start();

    /**
     * The producer hot path: fans one tick to every shard hosting the
     * symbol. Zero allocation. Returns false when ANY hosting shard's ring
     * was full (that shard missed the tick; counted on its bus).
     */
    bool publish(int handle, double price, double size, std::int64_t timestampNanos);

    int shardCount() const { return static_cast<int>(buses_.size()); }

    /** Shard components, for wiring listeners/quoters and observability. */
    HftMarketDataBus& bus(int shard) const { return *buses_[static_cast<std::size_t>(shard)]; }

    HftRiskGate& gate(int shard) const { return *gates_[static_cast<std::size_t>(shard)]; }

    HftOrderGateway& gateway(int shard) const {
        return *gateways_[static_cast<std::size_t>(shard)];
    }

    /** All gates -- the input to a GlobalRiskAggregator. */
    std::vector<HftRiskGate*> gates() const;

    /** Ticks processed across all shards. */
    std::int64_t processedCount() const;

    /** Orders delivered to venue listeners across all shards. */
    std::int64_t deliveredCount() const;

    void close();

private:
    std::vector<std::unique_ptr<HftMarketDataBus>> buses_;
    std::vector<std::unique_ptr<HftRiskGate>> gates_;
    std::vector<std::unique_ptr<HftOrderGateway>> gateways_;

    // Routing tables, indexed by the global symbol handle returned from
    // registerSymbol: which shards host it, and its dense id in each.
    std::vector<std::vector<int>> shardsOf_;
    std::vector<std::vector<int>> localIdOf_;
    std::vector<std::string> names_;
    bool started_ = false;
};

}  // namespace quantfinlib
