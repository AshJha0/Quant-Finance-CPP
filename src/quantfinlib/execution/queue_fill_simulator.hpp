// Queue-based fill simulator for one instrument, driven by a top-of-book and
// trade tape. It replaces "filled at the touch" with what the tape supports:
//
//  - A passive limit joins behind the size displayed at its price. The queue
//    moves on trades and on pro-rata cancels (QueuePositionEstimator), and
//    only volume that reaches the order fills it, so partial fills are normal.
//  - An aggressive order takes what is displayed at the touch and no more.
//    Only level one is known, so the tape never justifies a deeper sweep:
//    a market remainder is cancelled, a limit remainder rests.
//  - Depth taken stays taken at that price for depthRecoveryNanos. A shadow
//    order leaves no mark on the recorded tape, so without this a strategy
//    could lift the same displayed size on every update.
//  - Orders and cancels reach the venue orderLatency (plus seeded jitter)
//    after they are sent, and the market keeps moving meanwhile: a cancel can
//    lose the race to a fill. visibleBook() is the book as the strategy sees
//    it, marketDataLatency old. Reports come back one order latency late.
//  - Every fill is marked to the mid markoutHorizonNanos later, split passive
//    and aggressive, so adverse selection shows up instead of hiding in P&L.
//
// Conservative choices. A limit behind the touch has no known queue, so it
// joins at the back of the level when the touch reaches it, giving up any
// earlier time priority. A resting order the market crosses fills at its own
// limit, not at the better opposite quote. A print at the mid with no stated
// aggressor fills nothing. Prices compare exactly: feed them from one tick
// grid.
//
// All times are event times on one clock; nothing reads a wall clock. The
// only randomness is latency jitter, from a seeded generator with integer
// arithmetic, so a run repeats bit for bit on any platform. Not thread safe.
#pragma once

#include <cstdint>
#include <deque>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "quantfinlib/microstructure/queue_position_estimator.hpp"
#include "quantfinlib/orderbook/side.hpp"

namespace quantfinlib {

struct QueueFillConfig {
    std::int64_t orderLatencyNanos = 500'000;       // assumed one-way, sender to venue
    std::int64_t orderLatencyJitterNanos = 0;       // uniform extra in [0, jitter]
    std::int64_t marketDataLatencyNanos = 100'000;  // assumed venue to strategy
    std::int64_t markoutHorizonNanos = 1'000'000'000;
    std::int64_t depthRecoveryNanos = 1'000'000'000;
    std::uint64_t seed = 1;

    /** No latency: for tests and for replaying a tape already shifted by latency. */
    static QueueFillConfig zeroLatency() {
        QueueFillConfig c;
        c.orderLatencyNanos = 0;
        c.marketDataLatencyNanos = 0;
        return c;
    }
};

class QueueFillSimulator {
public:
    using Config = QueueFillConfig;

    struct Book {
        std::int64_t timestampNanos = 0;
        double bid = 0;
        std::int64_t bidSize = 0;
        double ask = 0;
        std::int64_t askSize = 0;

        double mid() const { return (bid + ask) / 2; }
    };

    enum class ReportType { FILL, CANCELLED };

    struct Report {
        ReportType type = ReportType::FILL;
        std::int64_t orderId = 0;
        Side side = Side::BUY;
        double price = 0;              // FILL only
        std::int64_t quantity = 0;     // filled, or the remainder cancelled
        std::int64_t exchangeNanos = 0;
        std::int64_t reportNanos = 0;  // when the sender learns of it
        bool passive = false;
    };

    /** Mark-out totals for one kind of fill, over fills whose horizon has passed. */
    struct MarkoutBucket {
        std::int64_t quantity = 0;
        double notional = 0;
        double markout = 0;      // sum of sign * (mid at horizon - fill price) * qty
        double adverseMove = 0;  // sum of sign * (mid at horizon - mid at fill) * qty

        /** Signed return of the fill at the horizon; NaN with nothing marked. */
        double markoutBps() const;
        /** The market's move after the fill, spread excluded; negative is adverse. */
        double adverseMoveBps() const;
    };

    struct AdverseSelection {
        MarkoutBucket passive;
        MarkoutBucket aggressive;
        std::int64_t unmarkedQuantity = 0;  // horizon not reached yet
    };

    explicit QueueFillSimulator(const Config& config = Config());

    /** Moves the clock, delivering orders and cancels due by then. Time cannot go back. */
    void advanceTo(std::int64_t nowNanos);

    /**
     * A new top of book. Throws on a non-finite or non-positive price, a
     * crossed book, a negative size, or a time before the clock.
     */
    void onBook(std::int64_t nanos, double bid, std::int64_t bidSize, double ask, std::int64_t askSize);

    /**
     * A print of `quantity`. Report trades before the book update that
     * reflects them. This form infers the aggressor from the last book.
     */
    void onTrade(std::int64_t nanos, double price, std::int64_t quantity);
    void onTrade(std::int64_t nanos, double price, std::int64_t quantity, Side aggressor);

    /** Sends a limit order at nowNanos; returns its id. Throws on bad quantity or price. */
    std::int64_t submitLimit(std::int64_t nowNanos, Side side, std::int64_t quantity, double limitPrice);

    /** Sends a market order; whatever the touch cannot fill on arrival is cancelled. */
    std::int64_t submitMarket(std::int64_t nowNanos, Side side, std::int64_t quantity);

    /** Sends a cancel; false if the order is finished. The order may still fill before it lands. */
    bool cancel(std::int64_t nowNanos, std::int64_t orderId);

    /** Removes and returns the reports the sender knows about by nowNanos, in order. */
    std::vector<Report> drainReports(std::int64_t nowNanos);

    /** The latest book published at least marketDataLatency before nowNanos. */
    std::optional<Book> visibleBook(std::int64_t nowNanos) const;

    /** Shares estimated ahead of a resting order; NaN if it has no queue position. */
    double sharesAhead(std::int64_t orderId) const;

    const AdverseSelection& adverseSelection() const { return adverse_; }
    std::int64_t clockNanos() const { return clock_; }
    const Config& config() const { return config_; }

private:
    struct Order {
        std::int64_t id = 0;
        Side side = Side::BUY;
        double limit = 0;  // NaN = market
        std::int64_t remaining = 0;
        bool arrived = false;
        QueuePositionEstimator queue;
    };

    struct Pending {
        bool isCancel;
        std::int64_t orderId;
    };

    struct Taken {
        double price = 0;
        std::int64_t quantity = 0;
        std::int64_t lastNanos = 0;
    };

    struct OpenMark {
        std::int64_t dueNanos;
        Side side;
        double price;
        double midAtFill;
        std::int64_t quantity;
        bool passive;
    };

    std::int64_t send(std::int64_t nowNanos, Side side, std::int64_t quantity, double limit);
    std::int64_t latency();
    void deliverDue(std::int64_t upToNanos);
    void arrive(Order& order, std::int64_t nanos);
    void takeDisplayed(Order& order, double fillPrice, std::int64_t nanos, bool passive);
    std::int64_t displayedLeft(Side takerSide, std::int64_t nanos);
    void noteTaken(Side takerSide, std::int64_t quantity, std::int64_t nanos);
    void updateQueue(Order& order);
    void sweepCrossed(std::int64_t nanos);
    std::int64_t ownSizeAt(Side side, double price, std::int64_t exceptId) const;
    void recordFill(Order& order, double price, std::int64_t quantity, std::int64_t nanos, bool passive);
    void finishCancelled(Order& order, std::int64_t nanos);
    void eraseFinished();
    void resolveMarks(std::int64_t nanos);

    Config config_;
    std::uint64_t rngState_;
    std::int64_t clock_ = 0;
    std::int64_t nextId_ = 1;
    std::uint64_t nextSeq_ = 0;
    std::optional<Book> book_;
    std::deque<Book> history_;
    std::map<std::int64_t, Order> orders_;  // id order is arrival-send order
    std::map<std::int64_t, std::int64_t> arrivalOf_;
    std::map<std::pair<std::int64_t, std::uint64_t>, Pending> pending_;
    Taken takenBid_;
    Taken takenAsk_;
    std::deque<Report> reports_;
    std::deque<OpenMark> marks_;
    AdverseSelection adverse_;
};

}  // namespace quantfinlib
