// Port of com.quantfinlib.fix.FixSession (Java): FIX 4.4 session over TCP --
// initiator or acceptor -- implementing the session protocol needed to
// trade: Logon handshake, Heartbeats with TestRequest probing and staleness
// disconnect, sequence-number tracking with gap detection, Logout handshake,
// and the application flow NewOrderSingle out / ExecutionReport in (or the
// reverse, on the venue side). Zero dependencies beyond the platform socket
// API (winsock2 on Windows, BSD sockets elsewhere).
//
// Gap recovery: inbound gaps trigger a ResendRequest(2) and out-of-order
// messages are dropped until redelivered; inbound ResendRequests are
// serviced from an in-session message store -- application messages are
// replayed with PossDupFlag(43)/OrigSendingTime(122) and admin runs are
// coalesced into SequenceReset-GapFill(4). Duplicates (PossDup below the
// expected seqnum) are suppressed; a too-low seqnum without PossDup
// disconnects the session.
//
// Persistence: sessions default to an in-memory FixSessionStore (sequence
// numbers reset per connection); pass a FileSessionStore to the
// initiate/accept overloads for sequence-number continuation across
// restarts, with ResendRequests serviced from messages sent before the
// reconnect.
//
// ResetSeqNumFlag(141): Config::withResetOnLogon() makes the initiator send
// 141=Y and restart both counters at 1; receiving a Logon with 141=Y (and
// 34=1, per spec) resets the inbound expectation to 1 and -- on the side
// that did not initiate the reset -- the outbound counter too, persisted,
// with the acceptor confirming 141=Y back.
#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

#include "quantfinlib/fix/execution_report.hpp"
#include "quantfinlib/fix/fix_decoder.hpp"
#include "quantfinlib/fix/fix_message.hpp"
#include "quantfinlib/fix/fix_session_store.hpp"
#include "quantfinlib/fix/new_order_single.hpp"
#include "quantfinlib/fix/order_cancel_replace_request.hpp"
#include "quantfinlib/fix/order_cancel_request.hpp"
#include "quantfinlib/orderbook/side.hpp"

namespace quantfinlib {

/**
 * Minimal TCP listening socket -- the C++ analogue of java.net.ServerSocket
 * used with FixSession::accept. The native handle is kept out of this
 * header (opaque std::intptr_t) so callers never need winsock2.h.
 */
class FixServerSocket {
public:
    /** Binds 127.0.0.1:port (port 0 = OS-assigned ephemeral port) and starts listening. */
    explicit FixServerSocket(int port = 0);
    ~FixServerSocket();

    FixServerSocket(const FixServerSocket&) = delete;
    FixServerSocket& operator=(const FixServerSocket&) = delete;

    int port() const {
        return port_;
    }

    void close();

private:
    friend class FixSession;

    std::intptr_t fd_;
    int port_;
};

class FixSession {
public:
    struct Config {
        std::string senderCompId;
        std::string targetCompId;
        int heartbeatSeconds;
        std::optional<std::string> username;
        std::optional<std::string> password;
        bool resetOnLogon = false;

        /** Throws std::invalid_argument if heartbeatSeconds < 1. */
        Config(std::string senderCompId_, std::string targetCompId_, int heartbeatSeconds_);

        /** Adds Username(553)/Password(554) to the initiator's Logon. */
        Config withCredentials(std::string username_, std::string password_) const;

        /**
         * Initiator sends ResetSeqNumFlag(141)=Y on Logon and restarts BOTH
         * sequence counters at 1 (persisted) -- the standard clean-slate
         * reconnect when the two sides' stores have diverged beyond repair.
         */
        Config withResetOnLogon() const;
    };

    /** Session callbacks; invoked on the session's reader thread. */
    class Listener {
    public:
        virtual ~Listener() = default;

        virtual void onLogon(FixSession& session) {
            (void)session;
        }

        virtual void onLogout(FixSession& session) {
            (void)session;
        }

        virtual void onExecutionReport(FixSession& session, const ExecutionReport& report) {
            (void)session;
            (void)report;
        }

        virtual void onNewOrderSingle(FixSession& session, const NewOrderSingle& order) {
            (void)session;
            (void)order;
        }

        virtual void onOrderCancelRequest(FixSession& session, const OrderCancelRequest& request) {
            (void)session;
            (void)request;
        }

        virtual void onOrderCancelReplace(FixSession& session,
                                          const OrderCancelReplaceRequest& request) {
            (void)session;
            (void)request;
        }

        /** Session-level Reject (35=3) received from the peer. */
        virtual void onReject(FixSession& session, std::int64_t refSeqNum,
                              const std::string& text) {
            (void)session;
            (void)refSeqNum;
            (void)text;
        }

        /** Heartbeats, test requests and any unrecognized message types. */
        virtual void onMessage(FixSession& session, const FixMessage& message) {
            (void)session;
            (void)message;
        }

        virtual void onSequenceGap(std::int64_t expectedSeqNum, std::int64_t receivedSeqNum) {
            (void)expectedSeqNum;
            (void)receivedSeqNum;
        }

        /** This session serviced a peer's ResendRequest for [beginSeqNo, endSeqNo]. */
        virtual void onResendServed(std::int64_t beginSeqNo, std::int64_t endSeqNo) {
            (void)beginSeqNo;
            (void)endSeqNo;
        }

        virtual void onDisconnect(const std::string& reason) {
            (void)reason;
        }
    };

    ~FixSession();
    FixSession(const FixSession&) = delete;
    FixSession& operator=(const FixSession&) = delete;

    /** Connects, sends Logon, and blocks until the handshake completes. Throws std::runtime_error. */
    static std::unique_ptr<FixSession> initiate(const std::string& host, int port,
                                                 const Config& config, Listener& listener);

    /** As above with a durable session store. */
    static std::unique_ptr<FixSession> initiate(const std::string& host, int port,
                                                 const Config& config, Listener& listener,
                                                 std::shared_ptr<FixSessionStore> store);

    /** Accepts one connection, awaits the peer's Logon, replies, and returns established. */
    static std::unique_ptr<FixSession> accept(FixServerSocket& server, const Config& config,
                                               Listener& listener);

    /** As above with a durable session store. */
    static std::unique_ptr<FixSession> accept(FixServerSocket& server, const Config& config,
                                               Listener& listener,
                                               std::shared_ptr<FixSessionStore> store);

    // ------------------------------------------------------------------
    // Application messages
    // ------------------------------------------------------------------

    /**
     * Sends a NewOrderSingle. limitPrice = NaN sends a market order.
     * Returns the ClOrdID for correlation with execution reports.
     */
    std::string sendNewOrderSingle(const std::string& clOrdId, const std::string& symbol,
                                    Side side, std::int64_t quantity, double limitPrice,
                                    char timeInForce);

    /** Venue side: sends an execution report for a received order. */
    void sendExecutionReport(const ExecutionReport& report);

    /** Sends an OrderCancelRequest (35=F) for a working order. */
    std::string sendOrderCancelRequest(const std::string& clOrdId, const std::string& origClOrdId,
                                        const std::string& symbol, Side side,
                                        std::int64_t quantity);

    /** Sends an OrderCancelReplaceRequest (35=G); limitPrice = NaN = market. */
    std::string sendOrderCancelReplace(const std::string& clOrdId, const std::string& origClOrdId,
                                        const std::string& symbol, Side side,
                                        std::int64_t quantity, double limitPrice,
                                        char timeInForce);

    /** Sends a session-level Reject (35=3) referring to an inbound message. */
    void sendReject(std::int64_t refSeqNum, const std::string& text);

    /** Initiates the Logout handshake and closes the session. */
    void logout();

    bool isEstablished() const {
        return established_.load();
    }

    /** Next incoming sequence number this session expects (for diagnostics/tests). */
    std::int64_t expectedIncomingSeqNum() const {
        return expectedIncomingSeq_.load();
    }

    void close();

    // ------------------------------------------------------------------
    // Test hooks -- package-private in the Java source; C++ has no
    // equivalent access control, so these are public and documented as
    // internal to the session/test harness.
    // ------------------------------------------------------------------

    void send(FixMessage::Builder builder);

    /** Skips outgoing sequence numbers to provoke a gap on the peer. */
    void forceOutgoingSeq(std::int64_t seqNum);

private:
    FixSession(std::intptr_t socket, Config config, Listener& listener, bool acceptor,
               std::shared_ptr<FixSessionStore> store);

    void awaitEstablished();
    void writeReplay(const FixMessage::Builder& builder, std::int64_t seqNum,
                      const std::optional<std::string>& origSendingTime);
    void readerLoop();
    void handle(const FixMessage& m);
    void dispatchApp(std::int64_t seq, const std::function<void()>& dispatch);
    void handleSequenceReset(const FixMessage& m);
    void applyLogonSeqReset();
    void advanceIncoming(std::int64_t newExpected);
    void handleResendRequest(const FixMessage& m);
    void sendGapFill(std::int64_t seq, std::int64_t newSeqNo);
    void heartbeatLoop();
    void disconnect(const std::string& reason);
    void requireEstablished() const;

    bool awaitLogon(int timeoutSeconds);
    void countDownLogon();
    bool awaitLogout(int timeoutSeconds);
    void countDownLogout();

    static std::string utcTimestampNow();

    std::intptr_t socket_;
    Config config_;
    Listener& listener_;
    bool acceptor_;
    std::recursive_mutex writeMutex_;   // recursive: handleResendRequest re-enters via writeReplay
    FixDecoder decoder_;

    std::mutex logonMutex_;
    std::condition_variable logonCv_;
    bool logonDone_ = false;

    std::mutex logoutMutex_;
    std::condition_variable logoutCv_;
    bool logoutDone_ = false;

    std::atomic<bool> disconnectNotified_{false};

    std::atomic<bool> running_{true};
    std::atomic<bool> established_{false};
    std::atomic<bool> logoutSent_{false};
    std::atomic<std::int64_t> lastSentNanos_{0};
    std::atomic<std::int64_t> lastReceivedNanos_{0};
    std::atomic<bool> testRequestPending_{false};

    std::shared_ptr<FixSessionStore> store_;
    std::int64_t outgoingSeq_ = 0;              // guarded by writeMutex_
    std::atomic<std::int64_t> expectedIncomingSeq_{0};  // written by reader thread only
    bool resendPending_ = false;             // reader thread only
    std::int64_t resendTriggerSeq_ = 0;      // reader thread only
    const bool sentResetLogon_;              // this side initiated the 141=Y reset

    std::thread readerThread_;
    std::thread heartbeatThread_;
};

}  // namespace quantfinlib
