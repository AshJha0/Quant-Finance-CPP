// Correctness replay of an IEX DEEP or DEEP+ capture. It counts what the feed
// did and whether rebuilt books stayed sound. It measures no edge or profit.
//
// A symbol's book is sampled once per feed event, because the book may look
// crossed in the middle of one: on DEEP an event ends at the update with the
// Event Flags bit set, on DEEP+ at the next timestamp for that symbol.
//
// Tracked symbols additionally get, on DEEP+, a shadow HftOrderBook driven by the
// real orders, and on DEEP, a QueuePositionEstimator following a notional order.
#pragma once

#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "quantfinlib/marketdata/iex_book.hpp"
#include "quantfinlib/marketdata/iex_tp_decoder.hpp"
#include "quantfinlib/microstructure/queue_position_estimator.hpp"
#include "quantfinlib/orderbook/hft_order_book.hpp"

namespace quantfinlib {

class IexReplay : public IexTpDecoder::Handler {
public:
    /** One BBO change of a tracked symbol, as written to the log. */
    struct BboRecord {
        std::int64_t timestamp;
        std::uint64_t symbol;
        IexBbo bbo;
    };

    struct Checks {
        std::uint64_t events = 0;
        std::uint64_t crossedEvents = 0;
        std::uint64_t lockedEvents = 0;
        std::uint64_t removeMissingLevel = 0;
        std::uint64_t timestampRegressions = 0;
        std::uint64_t incompleteAtEnd = 0;      // DEEP: updates after the last complete flag
        std::uint64_t restingAtEnd = 0;         // symbols with a non-empty book at the end
        // Prints against the BBO of the last finished event.
        std::uint64_t prints = 0;
        std::uint64_t printShares = 0;
        std::uint64_t printIdSum = 0;           // wraps; only compared between feeds
        std::uint64_t printsInside = 0;
        std::uint64_t printsOutside = 0;        // outside the BBO, regular hours
        std::uint64_t printsOutsideOther = 0;   // outside the BBO, other sessions or crosses
        std::uint64_t printsOneSided = 0;
        std::uint64_t tradeBreaks = 0;
        // DEEP+ executions against the rebuilt book.
        std::uint64_t execAtTouch = 0;
        std::uint64_t execBehindTouch = 0;
        std::uint64_t execPriceDiffers = 0;
        std::uint64_t modifyKeptPriority = 0;
        std::uint64_t clearBook = 0;
        // Status messages.
        std::uint64_t systemEvents = 0;
        std::uint64_t systemOutOfOrder = 0;
        std::uint64_t tradingStatus = 0;
        // Shadow HftOrderBook.
        std::uint64_t hftOrders = 0;
        std::uint64_t hftNonCentSkipped = 0;
        std::uint64_t hftRejects = 0;
        std::uint64_t hftExecAtFront = 0;
        std::uint64_t hftExecNotAtFront = 0;
        std::uint64_t hftPriorityLossModifies = 0;
        std::uint64_t hftBboCompared = 0;
        std::uint64_t hftBboMismatches = 0;
        // Queue estimator workload.
        std::uint64_t queueJoins = 0;
        std::uint64_t queueUpdates = 0;
        std::uint64_t queueDrained = 0;
        std::uint64_t queueLevelGone = 0;
        std::uint64_t queueInvariantFailures = 0;
    };

    /** @param bboLog optional; tracked symbols' BBO changes go here. Not owned. */
    explicit IexReplay(std::vector<std::string> tracked = {}, std::FILE* bboLog = nullptr);
    ~IexReplay() override;

    void onMessage(const IexMessage& message) override;
    void onGap(std::int64_t lostMessages) override;

    /** Closes open events and counts what is left. Call once, at end of input. */
    void finish();

    const Checks& checks() const { return checks_; }
    /** Sum of the per-symbol DEEP+ book counters. */
    IexOrderBook::Stats orderBookStats() const;
    std::uint64_t symbols() const { return symbols_.size(); }
    std::uint64_t gapsSeen() const { return gapsSeen_; }

    /** The 8 padded bytes of a symbol as a key, as in IexMessage::symbol. */
    static std::uint64_t symbolKey(const std::string& symbol);

private:
    struct Shadow;
    struct Symbol;

    Symbol& symbol(std::uint64_t key);
    const IexLevelBook& levels(const Symbol& s) const;
    void finishEvent(Symbol& s, std::uint64_t key);
    void onPrint(const Symbol& s, const IexMessage& m);
    void onExecuted(Symbol& s, const IexMessage& m);
    void shadowAdd(Symbol& s, std::int64_t ref, const IexOrderBook::Order& order);
    void shadowDrop(Symbol& s, std::int64_t ref);
    void shadowRepair(Symbol& s);
    void shadowExecute(Symbol& s, std::int64_t ref, const IexOrderBook::Order& before,
                       std::uint32_t size);
    void stepQueue(Symbol& s);

    std::vector<std::uint64_t> tracked_;
    std::FILE* bboLog_;
    std::unordered_map<std::uint64_t, std::unique_ptr<Symbol>> symbols_;
    Checks checks_;
    std::uint64_t gapsSeen_ = 0;
    char systemState_ = 0;
    bool regularHours_ = false;
};

}  // namespace quantfinlib
