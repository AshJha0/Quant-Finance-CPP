// The live link between HftOrderGateway and a FIX session. Register one
// bridge as the session's listener: venue messages leave as NewOrderSingle,
// OrderCancelRequest and OrderCancelReplaceRequest, and ExecutionReport and
// OrderCancelReject come back as the gateway's on* calls.
//
//     FixVenueBridge bridge(gateway, {"AAPL", "MSFT"});   // index = symbol id
//     auto session = FixSession::initiate(host, port, config, bridge, store);
//     gateway.start();
//
// Report handling. Anything this side cannot account for latches KILL_GATEWAY:
//   - a ClOrdID that does not parse or was never issued;
//   - a fill that overfills, disagrees on symbol or side, or whose CumQty
//     does not match our count (a fill went missing);
//   - a fractional or non-positive fill quantity, a trade bust or correction,
//     an ExecType we do not know, an unreadable message, a session Reject or
//     BusinessMessageReject, which may mean an order did not go where we think.
// A non-fill report for an order that has finished only counts: resends make
// those normal. Fills are deduplicated on ExecID over a bounded window,
// because the session delivers at least once.
//
// Losing the link. A disconnect or logout latches KILL_VENUE_LINK and cancels
// everything. Nothing is released on the strength of that: the venue may
// still be working the orders, so reservations stay until it says otherwise.
// Cancels that could not go out wait and leave on the next Logon. The kill
// stays latched across the reconnect; an operator resets it. A new order or
// replace depends on how far it got:
//   no session        never sent, so refused back to the gateway;
//   send interrupted  venue state unknown: the order stays open and reserved,
//                     and is counted as in doubt.
// The session store can replay a NEW after reconnect, which is why an
// interrupted send is not treated as a refusal.
//
// Not covered: no OrderStatusRequest or mass status, so an order the venue
// never saw stays reserved until restart. ExecID history is not persisted.
//
// Lifetimes: the gateway outlives the bridge and the bridge outlives every
// session using it. After a local FixSession::close() call detach() before
// destroying the session. The destructor stops the gateway, whose venue
// thread would otherwise call into a dead bridge; do not poll it afterwards.
#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "quantfinlib/fix/fix_session.hpp"
#include "quantfinlib/trading/hft_order_gateway.hpp"

namespace quantfinlib {

class FixVenueBridge final : public FixSession::Listener {
public:
    struct Config {
        char timeInForce = NewOrderSingle::TIF_DAY;
        std::size_t execIdWindow = std::size_t{1} << 16;
    };

    /** @param symbols wire symbol by gateway symbol id */
    FixVenueBridge(HftOrderGateway& gateway, std::vector<std::string> symbols, Config config);
    FixVenueBridge(HftOrderGateway& gateway, std::vector<std::string> symbols)
            : FixVenueBridge(gateway, std::move(symbols), Config{}) {}
    ~FixVenueBridge() override;

    FixVenueBridge(const FixVenueBridge&) = delete;
    FixVenueBridge& operator=(const FixVenueBridge&) = delete;

    /** Forgets the session, with the same consequences as losing the link. */
    void detach();

    bool connected() const { return connected_.load(std::memory_order_acquire); }

    /** Reports that latched a kill because they named no order of ours. */
    std::int64_t unknownClOrdIdCount() const { return unknown_.load(std::memory_order_acquire); }
    std::int64_t duplicateFillCount() const { return duplicates_.load(std::memory_order_acquire); }
    std::int64_t cumQtyMismatchCount() const {
        return cumMismatch_.load(std::memory_order_acquire);
    }
    /** Unreadable messages, bad quantities, unknown ExecTypes, session rejects. */
    std::int64_t faultCount() const { return faults_.load(std::memory_order_acquire); }
    /** NEW or REPLACE refused locally: no session, so never on the wire. */
    std::int64_t neverSentCount() const { return neverSent_.load(std::memory_order_acquire); }
    /** Orders whose send was cut off: the venue may or may not have them. */
    std::int64_t inDoubtCount() const { return inDoubt_.load(std::memory_order_acquire); }
    /** Cancels waiting for the next Logon. */
    std::size_t unsentCancelCount() const;

    // FixSession::Listener
    void onLogon(FixSession& session) override;
    void onLogout(FixSession& session) override;
    void onExecutionReport(FixSession& session, const ExecutionReport& report) override;
    void onOrderCancelReject(FixSession& session, const OrderCancelReject& reject) override;
    void onApplicationFault(FixSession& session, const FixMessage& message,
                            const std::string& reason) override;
    void onReject(FixSession& session, std::int64_t refSeqNum, const std::string& text) override;
    void onMessage(FixSession& session, const FixMessage& message) override;
    void onDisconnect(const std::string& reason) override;

private:
    void onVenueMessage(const VenueMessage& message);
    void sendCancel(FixSession& session, const VenueMessage& message);
    void handleFill(const ExecutionReport& report, std::int64_t clOrdId);
    bool firstSighting(const std::string& execId);
    int symbolIdOf(const std::string& symbol) const;
    void fault(std::atomic<std::int64_t>& counter);
    void linkLost();

    HftOrderGateway& gateway_;
    const std::vector<std::string> symbols_;
    std::unordered_map<std::string, int> symbolIds_;
    const Config config_;

    // Recursive: a failed send disconnects on the sending thread, which
    // arrives back in onDisconnect holding this.
    mutable std::recursive_mutex mutex_;
    FixSession* session_ = nullptr;           // under mutex_
    std::deque<VenueMessage> unsentCancels_;  // under mutex_
    std::atomic<bool> connected_{false};

    // Reader thread only; one session at a time.
    std::unordered_set<std::string> seenExecIds_;
    std::deque<std::string> execIdOrder_;

    std::atomic<std::int64_t> unknown_{0};
    std::atomic<std::int64_t> duplicates_{0};
    std::atomic<std::int64_t> cumMismatch_{0};
    std::atomic<std::int64_t> faults_{0};
    std::atomic<std::int64_t> neverSent_{0};
    std::atomic<std::int64_t> inDoubt_{0};
};

}  // namespace quantfinlib
