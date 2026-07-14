#include "quantfinlib/trading/sharded_trading_engine.hpp"

#include <stdexcept>

namespace quantfinlib {

ShardedTradingEngine::ShardedTradingEngine(
        int shardCount, int busRingCapacity, int orderRingCapacity, int maxSymbolsPerShard,
        bool busySpin, const std::function<std::unique_ptr<HftRiskGate>(int)>& gateFactory) {
    if (shardCount <= 0) {
        throw std::invalid_argument("shardCount must be > 0");
    }
    buses_.reserve(static_cast<std::size_t>(shardCount));
    gates_.reserve(static_cast<std::size_t>(shardCount));
    gateways_.reserve(static_cast<std::size_t>(shardCount));
    for (int s = 0; s < shardCount; s++) {
        buses_.push_back(
                std::make_unique<HftMarketDataBus>(busRingCapacity, maxSymbolsPerShard, busySpin));
        gates_.push_back(gateFactory(s));
        gateways_.push_back(
                std::make_unique<HftOrderGateway>(orderRingCapacity, *gates_[static_cast<std::size_t>(s)],
                                                   busySpin));
    }
}

int ShardedTradingEngine::registerSymbol(const std::string& symbol,
                                         const std::vector<int>& shards) {
    if (started_) {
        throw std::runtime_error("register symbols before start()");
    }
    if (shards.empty()) {
        throw std::invalid_argument("symbol needs at least one shard");
    }
    std::vector<int> ids;
    ids.reserve(shards.size());
    for (int shard : shards) {
        ids.push_back(buses_[static_cast<std::size_t>(shard)]->registerSymbol(symbol));
    }
    int handle = static_cast<int>(names_.size());
    names_.push_back(symbol);
    shardsOf_.push_back(shards);
    localIdOf_.push_back(std::move(ids));
    return handle;
}

int ShardedTradingEngine::localId(int handle, int shard) const {
    const std::vector<int>& shards = shardsOf_[static_cast<std::size_t>(handle)];
    for (std::size_t i = 0; i < shards.size(); i++) {
        if (shards[i] == shard) {
            return localIdOf_[static_cast<std::size_t>(handle)][i];
        }
    }
    throw std::invalid_argument(names_[static_cast<std::size_t>(handle)] + " is not hosted on shard "
                                 + std::to_string(shard));
}

void ShardedTradingEngine::start() {
    if (started_) {
        return;
    }
    for (auto& gw : gateways_) {
        gw->start();
    }
    for (auto& bus : buses_) {
        bus->start();
    }
    started_ = true;
}

bool ShardedTradingEngine::publish(int handle, double price, double size,
                                   std::int64_t timestampNanos) {
    const std::vector<int>& shards = shardsOf_[static_cast<std::size_t>(handle)];
    const std::vector<int>& ids = localIdOf_[static_cast<std::size_t>(handle)];
    bool all = true;
    for (std::size_t i = 0; i < shards.size(); i++) {
        all = buses_[static_cast<std::size_t>(shards[i])]->publish(ids[i], price, size,
                                                                     timestampNanos)
              && all;
    }
    return all;
}

std::vector<HftRiskGate*> ShardedTradingEngine::gates() const {
    std::vector<HftRiskGate*> out;
    out.reserve(gates_.size());
    for (auto& g : gates_) {
        out.push_back(g.get());
    }
    return out;
}

std::int64_t ShardedTradingEngine::processedCount() const {
    std::int64_t total = 0;
    for (auto& bus : buses_) {
        total += bus->processedCount();
    }
    return total;
}

std::int64_t ShardedTradingEngine::deliveredCount() const {
    std::int64_t total = 0;
    for (auto& gw : gateways_) {
        total += gw->deliveredCount();
    }
    return total;
}

void ShardedTradingEngine::close() {
    for (auto& bus : buses_) {
        bus->close();
    }
    for (auto& gw : gateways_) {
        gw->close();
    }
}

}  // namespace quantfinlib
