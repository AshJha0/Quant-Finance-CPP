#include "quantfinlib/marketdata/hft_market_data_bus.hpp"

#include <chrono>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>

#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
#include <immintrin.h>
#define QFL_SPIN_PAUSE() _mm_pause()
#else
#define QFL_SPIN_PAUSE() ((void)0)
#endif

namespace quantfinlib {

namespace {
int checkedMaxSymbols(int maxSymbols) {
    if (maxSymbols <= 0) {
        throw std::invalid_argument("maxSymbols must be positive");
    }
    return maxSymbols;
}
}  // namespace

HftMarketDataBus::HftMarketDataBus(int ringCapacity, int maxSymbols, bool busySpin)
        : ring_(ringCapacity),
          maxSymbols_(checkedMaxSymbols(maxSymbols)),
          busySpin_(busySpin),
          latestPrice_(new std::atomic<double>[static_cast<std::size_t>(maxSymbols)]),
          symbolListeners_(static_cast<std::size_t>(maxSymbols)) {
    double nan = std::numeric_limits<double>::quiet_NaN();
    for (int i = 0; i < maxSymbols; i++) {
        latestPrice_[static_cast<std::size_t>(i)].store(nan, std::memory_order_relaxed);
        symbolListeners_[static_cast<std::size_t>(i)].store(nullptr, std::memory_order_relaxed);
    }
}

HftMarketDataBus::~HftMarketDataBus() {
    stop();
    for (auto& slot : symbolListeners_) {
        delete slot.load(std::memory_order_acquire);
    }
    delete globalListeners_.load(std::memory_order_acquire);
}

int HftMarketDataBus::registerSymbol(const std::string& symbol) {
    int id = registry_.registerSymbol(symbol);
    if (id >= maxSymbols_) {
        throw std::runtime_error("maxSymbols (" + std::to_string(maxSymbols_) + ") exceeded");
    }
    return id;
}

void HftMarketDataBus::addListener(std::atomic<const ListenerList*>& slot,
                                   TickListener listener) {
    if (!listener) {
        throw std::invalid_argument("listener is empty");
    }
    std::lock_guard<std::mutex> lock(setupMutex_);
    const ListenerList* cur = slot.load(std::memory_order_acquire);
    auto next = std::make_unique<ListenerList>();
    next->reserve((cur ? cur->size() : 0) + 1);
    if (cur) {
        *next = *cur;
    }
    retired_.reserve(retired_.size() + 1);
    listeners_.push_back(std::move(listener));
    next->push_back(&listeners_.back());
    slot.store(next.release(), std::memory_order_release);
    if (cur) {
        retired_.emplace_back(cur);
    }
}

void HftMarketDataBus::subscribe(int symbolId, TickListener listener) {
    if (!validSymbol(symbolId)) {
        throw std::out_of_range("symbolId " + std::to_string(symbolId) + " out of range");
    }
    addListener(symbolListeners_[static_cast<std::size_t>(symbolId)], std::move(listener));
}

void HftMarketDataBus::subscribeAll(TickListener listener) {
    addListener(globalListeners_, std::move(listener));
}

void HftMarketDataBus::start() {
    std::lock_guard<std::mutex> lock(lifecycleMutex_);
    if (running_.load(std::memory_order_acquire)) {
        return;
    }
    running_.store(true, std::memory_order_release);
    consumer_ = std::thread([this] { consumeLoop(); });
}

void HftMarketDataBus::stop() {
    std::lock_guard<std::mutex> lock(lifecycleMutex_);
    running_.store(false, std::memory_order_release);
    if (consumer_.joinable()) {
        consumer_.join();
    }
    // No consumer exists now and none can start while we hold the lifecycle
    // lock, so nothing can still be reading a retired list.
    std::lock_guard<std::mutex> setup(setupMutex_);
    retired_.clear();
}

bool HftMarketDataBus::publish(int symbolId, double price, double size,
                                std::int64_t timestampNanos) {
    if (!validSymbol(symbolId)) {
        invalidSymbol_.store(invalidSymbol_.load(std::memory_order_relaxed) + 1,
                             std::memory_order_relaxed);
        return false;
    }
    bool ok = ring_.publish(symbolId, price, size, timestampNanos);
    if (!ok) {
        ringFull_.store(ringFull_.load(std::memory_order_relaxed) + 1,
                        std::memory_order_relaxed);
    }
    return ok;
}

void HftMarketDataBus::invoke(const ListenerList& list, int symbolId, double price, double size,
                              std::int64_t timestampNanos) {
    for (const TickListener* l : list) {
        try {
            (*l)(symbolId, price, size, timestampNanos);
        } catch (...) {
            listenerErrors_.store(listenerErrors_.load(std::memory_order_relaxed) + 1,
                                  std::memory_order_relaxed);
        }
    }
}

void HftMarketDataBus::dispatch(int symbolId, double price, double size,
                                 std::int64_t timestampNanos) {
    latestPrice_[static_cast<std::size_t>(symbolId)].store(price, std::memory_order_relaxed);
    const ListenerList* ls =
            symbolListeners_[static_cast<std::size_t>(symbolId)].load(std::memory_order_acquire);
    if (ls != nullptr) {
        invoke(*ls, symbolId, price, size, timestampNanos);
    }
    const ListenerList* gs = globalListeners_.load(std::memory_order_acquire);
    if (gs != nullptr) {
        invoke(*gs, symbolId, price, size, timestampNanos);
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
    if (!validSymbol(symbolId)) {
        return std::numeric_limits<double>::quiet_NaN();
    }
    return latestPrice_[static_cast<std::size_t>(symbolId)].load(std::memory_order_relaxed);
}

}  // namespace quantfinlib
