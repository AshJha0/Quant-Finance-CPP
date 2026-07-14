#include "quantfinlib/marketdata/hft_market_data_bus.hpp"

#include <chrono>
#include <cmath>
#include <limits>
#include <stdexcept>

#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
#include <immintrin.h>
#define QFL_SPIN_PAUSE() _mm_pause()
#else
#define QFL_SPIN_PAUSE() ((void)0)
#endif

namespace quantfinlib {

HftMarketDataBus::HftMarketDataBus(int ringCapacity, int maxSymbols, bool busySpin)
        : ring_(ringCapacity),
          maxSymbols_(maxSymbols),
          busySpin_(busySpin),
          latestPrice_(new std::atomic<double>[static_cast<std::size_t>(maxSymbols)]),
          symbolListeners_(static_cast<std::size_t>(maxSymbols)) {
    double nan = std::numeric_limits<double>::quiet_NaN();
    for (int i = 0; i < maxSymbols; i++) {
        latestPrice_[static_cast<std::size_t>(i)].store(nan, std::memory_order_relaxed);
    }
}

int HftMarketDataBus::registerSymbol(const std::string& symbol) {
    int id = registry_.registerSymbol(symbol);
    if (id >= maxSymbols_) {
        throw std::runtime_error("maxSymbols (" + std::to_string(maxSymbols_) + ") exceeded");
    }
    return id;
}

void HftMarketDataBus::subscribe(int symbolId, TickListener listener) {
    std::lock_guard<std::mutex> lock(setupMutex_);
    auto& slot = symbolListeners_[static_cast<std::size_t>(symbolId)];
    ListenerVecPtr cur = slot.load(std::memory_order_acquire);
    auto next = std::make_shared<ListenerVec>(cur ? *cur : ListenerVec{});
    next->push_back(std::move(listener));
    slot.store(std::move(next), std::memory_order_release);
}

void HftMarketDataBus::subscribeAll(TickListener listener) {
    std::lock_guard<std::mutex> lock(setupMutex_);
    ListenerVecPtr cur = globalListeners_.load(std::memory_order_acquire);
    auto next = std::make_shared<ListenerVec>(cur ? *cur : ListenerVec{});
    next->push_back(std::move(listener));
    globalListeners_.store(std::move(next), std::memory_order_release);
}

void HftMarketDataBus::start() {
    std::lock_guard<std::mutex> lock(setupMutex_);
    if (running_.load(std::memory_order_acquire)) {
        return;
    }
    running_.store(true, std::memory_order_release);
    consumer_ = std::thread([this] { consumeLoop(); });
}

void HftMarketDataBus::stop() {
    std::thread toJoin;
    {
        std::lock_guard<std::mutex> lock(setupMutex_);
        running_.store(false, std::memory_order_release);
        toJoin = std::move(consumer_);
    }
    if (toJoin.joinable()) {
        toJoin.join();
    }
}

bool HftMarketDataBus::publish(int symbolId, double price, double size,
                                std::int64_t timestampNanos) {
    bool ok = ring_.publish(symbolId, price, size, timestampNanos);
    if (!ok) {
        ringFull_++;
    }
    return ok;
}

void HftMarketDataBus::dispatch(int symbolId, double price, double size,
                                 std::int64_t timestampNanos) {
    latestPrice_[static_cast<std::size_t>(symbolId)].store(price, std::memory_order_relaxed);
    ListenerVecPtr ls = symbolListeners_[static_cast<std::size_t>(symbolId)].load(
            std::memory_order_acquire);
    if (ls) {
        for (const auto& l : *ls) {
            l(symbolId, price, size, timestampNanos);
        }
    }
    ListenerVecPtr gs = globalListeners_.load(std::memory_order_acquire);
    if (gs) {
        for (const auto& l : *gs) {
            l(symbolId, price, size, timestampNanos);
        }
    }
}

void HftMarketDataBus::consumeLoop() {
    auto dispatcher = [this](int symbolId, double price, double size,
                              std::int64_t timestampNanos) {
        dispatch(symbolId, price, size, timestampNanos);
    };
    while (true) {
        int n = ring_.drainTo(dispatcher, 1024);
        if (n > 0) {
            processed_.store(processed_.load(std::memory_order_relaxed) + n,
                              std::memory_order_release);
        } else {
            if (!running_.load(std::memory_order_acquire) && ring_.isEmpty()) {
                return;
            }
            if (busySpin_) {
                QFL_SPIN_PAUSE();
            } else {
                std::this_thread::sleep_for(std::chrono::nanoseconds(200));
            }
        }
    }
}

double HftMarketDataBus::latestPrice(int symbolId) const {
    return latestPrice_[static_cast<std::size_t>(symbolId)].load(std::memory_order_relaxed);
}

}  // namespace quantfinlib
