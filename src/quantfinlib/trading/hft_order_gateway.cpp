#include "quantfinlib/trading/hft_order_gateway.hpp"

#include <chrono>

#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
#include <immintrin.h>
#define QFL_SPIN_PAUSE() _mm_pause()
#else
#define QFL_SPIN_PAUSE() ((void)0)
#endif

namespace quantfinlib {

HftOrderGateway::HftOrderGateway(int ringCapacity, HftRiskGate& riskGate, bool busySpin)
        : ring_(ringCapacity), riskGate_(riskGate), busySpin_(busySpin) {}

void HftOrderGateway::addOrderListener(OrderListener listener) {
    std::lock_guard<std::mutex> lock(setupMutex_);
    ListenerVecPtr cur = listeners_.load(std::memory_order_acquire);
    auto next = std::make_shared<ListenerVec>(cur ? *cur : ListenerVec{});
    next->push_back(std::move(listener));
    listeners_.store(std::move(next), std::memory_order_release);
}

void HftOrderGateway::start() {
    std::lock_guard<std::mutex> lock(setupMutex_);
    if (running_.load(std::memory_order_acquire)) {
        return;
    }
    running_.store(true, std::memory_order_release);
    venueThread_ = std::thread([this] { venueLoop(); });
}

void HftOrderGateway::stop() {
    std::thread toJoin;
    {
        std::lock_guard<std::mutex> lock(setupMutex_);
        running_.store(false, std::memory_order_release);
        toJoin = std::move(venueThread_);
    }
    if (toJoin.joinable()) {
        toJoin.join();
    }
}

std::int64_t HftOrderGateway::submit(int symbolId, Side side, std::int64_t quantity, double price,
                                     std::int64_t timestampNanos) {
    int code = riskGate_.check(symbolId, side, quantity, price);
    if (code != HftRiskGate::OK) {
        return -code;
    }
    std::int64_t id = nextId_;
    if (!ring_.publish(id, symbolId, side, quantity, price, timestampNanos)) {
        ringFull_++;
        return 0;
    }
    nextId_ = id + 1;
    submitted_++;
    return id;
}

void HftOrderGateway::dispatch(std::int64_t orderId, int symbolId, Side side,
                                std::int64_t quantity, double price,
                                std::int64_t timestampNanos) {
    ListenerVecPtr ls = listeners_.load(std::memory_order_acquire);
    if (ls) {
        for (const auto& l : *ls) {
            l(orderId, symbolId, side, quantity, price, timestampNanos);
        }
    }
}

void HftOrderGateway::venueLoop() {
    auto dispatcher = [this](std::int64_t orderId, int symbolId, Side side,
                              std::int64_t quantity, double price, std::int64_t timestampNanos) {
        dispatch(orderId, symbolId, side, quantity, price, timestampNanos);
    };
    while (true) {
        int n = ring_.drainTo(dispatcher, 256);
        if (n > 0) {
            delivered_.store(delivered_.load(std::memory_order_relaxed) + n,
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

}  // namespace quantfinlib
