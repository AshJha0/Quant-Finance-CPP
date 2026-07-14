// Shared helpers for the FixSession loopback tests (fix_session_test.cpp,
// fix_reset_seq_num_test.cpp, fix_persistence_test.cpp,
// fix_protocol_extras_test.cpp) -- not a test file itself (excluded from the
// tests/*_test.cpp glob), just a CountDownLatch/CopyOnWriteArrayList
// analogue so each translation unit doesn't reinvent them.
#pragma once

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "qtest.hpp"

namespace fix_test {

/** CountDownLatch(1) analogue: signal once, any number of waiters. */
class CountdownFlag {
public:
    void countDown() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            done_ = true;
        }
        cv_.notify_all();
    }

    bool await(int seconds) {
        std::unique_lock<std::mutex> lock(mutex_);
        return cv_.wait_for(lock, std::chrono::seconds(seconds), [this] { return done_; });
    }

private:
    std::mutex mutex_;
    std::condition_variable cv_;
    bool done_ = false;
};

/** CopyOnWriteArrayList analogue: a mutex-guarded vector. */
template <typename T>
class SyncList {
public:
    void add(T v) {
        std::lock_guard<std::mutex> lock(mutex_);
        v_.push_back(std::move(v));
    }

    std::size_t size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return v_.size();
    }

    T first() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return v_.front();
    }

    T get(std::size_t i) const {
        std::lock_guard<std::mutex> lock(mutex_);
        return v_.at(i);
    }

    std::vector<T> snapshot() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return v_;
    }

private:
    mutable std::mutex mutex_;
    std::vector<T> v_;
};

}  // namespace fix_test

/** Polls `cond` (a re-evaluatable boolean expression) until true or a 5s timeout. */
#define AWAIT_TRUE(cond, message)                                                              \
    do {                                                                                        \
        auto qfx_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);        \
        while (!(cond)) {                                                                       \
            if (std::chrono::steady_clock::now() > qfx_deadline) {                              \
                qtest::fail(__FILE__, __LINE__, (message));                                      \
                break;                                                                           \
            }                                                                                    \
            std::this_thread::sleep_for(std::chrono::milliseconds(10));                          \
        }                                                                                        \
    } while (0)
