#include "quantfinlib/fix/fix_session.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <stdexcept>

#include "quantfinlib/fix/file_session_store.hpp"

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
using native_socket_t = SOCKET;
constexpr native_socket_t kInvalidSocket = INVALID_SOCKET;
#else
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>
using native_socket_t = int;
constexpr native_socket_t kInvalidSocket = -1;
#endif

namespace quantfinlib {

namespace {

// ------------------------------------------------------------------
// Platform socket plumbing
// ------------------------------------------------------------------

void ensureSocketsInitialized() {
#ifdef _WIN32
    static std::once_flag flag;
    std::call_once(flag, [] {
        WSADATA data;
        WSAStartup(MAKEWORD(2, 2), &data);
    });
#endif
}

native_socket_t toNative(std::intptr_t handle) {
    return static_cast<native_socket_t>(handle);
}

std::intptr_t fromNative(native_socket_t s) {
    return static_cast<std::intptr_t>(s);
}

void closeNative(native_socket_t s) {
    if (s == kInvalidSocket) {
        return;
    }
#ifdef _WIN32
    ::closesocket(s);
#else
    ::close(s);
#endif
}

void setNoDelay(native_socket_t s) {
    int flag = 1;
    ::setsockopt(s, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&flag), sizeof(flag));
}

native_socket_t connectTo(const std::string& host, int port) {
    ensureSocketsInitialized();
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* res = nullptr;
    std::string portStr = std::to_string(port);
    if (::getaddrinfo(host.c_str(), portStr.c_str(), &hints, &res) != 0) {
        throw std::runtime_error("failed to resolve " + host);
    }
    native_socket_t sock = kInvalidSocket;
    for (addrinfo* p = res; p != nullptr; p = p->ai_next) {
        sock = ::socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (sock == kInvalidSocket) {
            continue;
        }
        if (::connect(sock, p->ai_addr, static_cast<int>(p->ai_addrlen)) == 0) {
            break;
        }
        closeNative(sock);
        sock = kInvalidSocket;
    }
    ::freeaddrinfo(res);
    if (sock == kInvalidSocket) {
        throw std::runtime_error("failed to connect to " + host + ":" + std::to_string(port));
    }
    setNoDelay(sock);
    return sock;
}

void writeBytes(native_socket_t sock, const std::vector<std::uint8_t>& payload) {
    std::size_t sent = 0;
    while (sent < payload.size()) {
        int n = ::send(sock, reinterpret_cast<const char*>(payload.data() + sent),
                       static_cast<int>(payload.size() - sent), 0);
        if (n <= 0) {
            throw std::runtime_error("socket send failed");
        }
        sent += static_cast<std::size_t>(n);
    }
}

bool isAdminType(const std::string& type) {
    return type == FixMessage::HEARTBEAT || type == FixMessage::TEST_REQUEST
           || type == FixMessage::RESEND_REQUEST || type == FixMessage::REJECT
           || type == FixMessage::SEQUENCE_RESET || type == FixMessage::LOGOUT
           || type == FixMessage::LOGON;
}

std::int64_t nowNanos() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
                   std::chrono::steady_clock::now().time_since_epoch())
            .count();
}

constexpr int ESTABLISH_TIMEOUT_SECONDS = 10;

}  // namespace

// ------------------------------------------------------------------
// FixServerSocket
// ------------------------------------------------------------------

FixServerSocket::FixServerSocket(int port) : fd_(fromNative(kInvalidSocket)), port_(port) {
    ensureSocketsInitialized();
    native_socket_t sock = ::socket(AF_INET, SOCK_STREAM, 0);
    if (sock == kInvalidSocket) {
        throw std::runtime_error("failed to create FIX server socket");
    }
    int opt = 1;
    ::setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&opt), sizeof(opt));
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(static_cast<std::uint16_t>(port));
    if (::bind(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        closeNative(sock);
        throw std::runtime_error("failed to bind FIX server socket");
    }
    if (::listen(sock, 16) != 0) {
        closeNative(sock);
        throw std::runtime_error("failed to listen on FIX server socket");
    }
    socklen_t len = sizeof(addr);
    ::getsockname(sock, reinterpret_cast<sockaddr*>(&addr), &len);
    port_ = ntohs(addr.sin_port);
    fd_ = fromNative(sock);
}

FixServerSocket::~FixServerSocket() {
    close();
}

void FixServerSocket::close() {
    if (fd_ != fromNative(kInvalidSocket)) {
        closeNative(toNative(fd_));
        fd_ = fromNative(kInvalidSocket);
    }
}

// ------------------------------------------------------------------
// Config
// ------------------------------------------------------------------

FixSession::Config::Config(std::string senderCompId_, std::string targetCompId_,
                           int heartbeatSeconds_)
        : senderCompId(std::move(senderCompId_)),
          targetCompId(std::move(targetCompId_)),
          heartbeatSeconds(heartbeatSeconds_) {
    if (heartbeatSeconds < 1) {
        throw std::invalid_argument("heartbeat must be >= 1s");
    }
}

FixSession::Config FixSession::Config::withCredentials(std::string username_,
                                                       std::string password_) const {
    Config c = *this;
    c.username = std::move(username_);
    c.password = std::move(password_);
    return c;
}

FixSession::Config FixSession::Config::withResetOnLogon() const {
    Config c = *this;
    c.resetOnLogon = true;
    return c;
}

// ------------------------------------------------------------------
// Construction / establishment
// ------------------------------------------------------------------

FixSession::FixSession(std::intptr_t socket, Config config, Listener& listener, bool acceptor,
                       std::shared_ptr<FixSessionStore> store)
        : socket_(socket),
          config_(std::move(config)),
          listener_(listener),
          acceptor_(acceptor),
          store_(std::move(store)),
          sentResetLogon_(!acceptor && config_.resetOnLogon) {
    lastSentNanos_.store(nowNanos());
    lastReceivedNanos_.store(nowNanos());
    // An initiator configured for reset-on-logon starts BOTH counters at 1
    // before any thread runs (its Logon goes out as 34=1, 141=Y) and
    // persists the reset so a crash mid-handshake cannot resurrect the
    // abandoned numbers.
    if (sentResetLogon_) {
        store_->saveOutgoingSeq(1);
        store_->saveIncomingSeq(1);
    }
    outgoingSeq_ = store_->nextOutgoingSeq();
    expectedIncomingSeq_.store(store_->expectedIncomingSeq());
}

FixSession::~FixSession() {
    close();
    if (readerThread_.joinable()) {
        readerThread_.join();
    }
    if (heartbeatThread_.joinable()) {
        heartbeatThread_.join();
    }
}

std::unique_ptr<FixSession> FixSession::initiate(const std::string& host, int port,
                                                 const Config& config, Listener& listener) {
    return initiate(host, port, config, listener, std::make_shared<InMemorySessionStore>());
}

std::unique_ptr<FixSession> FixSession::initiate(const std::string& host, int port,
                                                 const Config& config, Listener& listener,
                                                 std::shared_ptr<FixSessionStore> store) {
    native_socket_t sock = connectTo(host, port);
    std::unique_ptr<FixSession> session(
            new FixSession(fromNative(sock), config, listener, false, std::move(store)));
    session->readerThread_ = std::thread(&FixSession::readerLoop, session.get());
    session->heartbeatThread_ = std::thread(&FixSession::heartbeatLoop, session.get());

    FixMessage::Builder logon = FixMessage::builder(FixMessage::LOGON)
                                         .field(FixMessage::ENCRYPT_METHOD, std::string("0"))
                                         .field(FixMessage::HEART_BT_INT,
                                                static_cast<std::int64_t>(config.heartbeatSeconds));
    if (config.resetOnLogon) {
        logon.field(FixMessage::RESET_SEQ_NUM_FLAG, std::string("Y"));
    }
    if (config.username.has_value()) {
        logon.field(FixMessage::USERNAME, *config.username);
    }
    if (config.password.has_value()) {
        logon.field(FixMessage::PASSWORD, *config.password);
    }
    session->send(logon);
    session->awaitEstablished();
    return session;
}

std::unique_ptr<FixSession> FixSession::accept(FixServerSocket& server, const Config& config,
                                               Listener& listener) {
    return accept(server, config, listener, std::make_shared<InMemorySessionStore>());
}

std::unique_ptr<FixSession> FixSession::accept(FixServerSocket& server, const Config& config,
                                               Listener& listener,
                                               std::shared_ptr<FixSessionStore> store) {
    ensureSocketsInitialized();
    native_socket_t client = ::accept(toNative(server.fd_), nullptr, nullptr);
    if (client == kInvalidSocket) {
        throw std::runtime_error("FIX server accept failed");
    }
    setNoDelay(client);
    std::unique_ptr<FixSession> session(
            new FixSession(fromNative(client), config, listener, true, std::move(store)));
    session->readerThread_ = std::thread(&FixSession::readerLoop, session.get());
    session->heartbeatThread_ = std::thread(&FixSession::heartbeatLoop, session.get());
    session->awaitEstablished();
    return session;
}

void FixSession::awaitEstablished() {
    if (!awaitLogon(ESTABLISH_TIMEOUT_SECONDS)) {
        close();
        throw std::runtime_error("FIX logon timed out after " + std::to_string(ESTABLISH_TIMEOUT_SECONDS) + "s");
    }
}

// ------------------------------------------------------------------
// Latches
// ------------------------------------------------------------------

bool FixSession::awaitLogon(int timeoutSeconds) {
    std::unique_lock<std::mutex> lock(logonMutex_);
    return logonCv_.wait_for(lock, std::chrono::seconds(timeoutSeconds), [this] { return logonDone_; });
}

void FixSession::countDownLogon() {
    {
        std::lock_guard<std::mutex> lock(logonMutex_);
        logonDone_ = true;
    }
    logonCv_.notify_all();
}

bool FixSession::awaitLogout(int timeoutSeconds) {
    std::unique_lock<std::mutex> lock(logoutMutex_);
    return logoutCv_.wait_for(lock, std::chrono::seconds(timeoutSeconds), [this] { return logoutDone_; });
}

void FixSession::countDownLogout() {
    {
        std::lock_guard<std::mutex> lock(logoutMutex_);
        logoutDone_ = true;
    }
    logoutCv_.notify_all();
}

// ------------------------------------------------------------------
// Application messages
// ------------------------------------------------------------------

std::string FixSession::sendNewOrderSingle(const std::string& clOrdId, const std::string& symbol,
                                           Side side, std::int64_t quantity, double limitPrice,
                                           char timeInForce) {
    requireEstablished();
    FixMessage::Builder b = FixMessage::builder(FixMessage::NEW_ORDER_SINGLE)
                                     .field(FixMessage::CL_ORD_ID, clOrdId)
                                     .field(FixMessage::SYMBOL, symbol)
                                     .field(FixMessage::SIDE, side == Side::BUY ? '1' : '2')
                                     .field(FixMessage::TRANSACT_TIME, utcTimestampNow())
                                     .field(FixMessage::ORDER_QTY, quantity);
    if (std::isnan(limitPrice)) {
        b.field(FixMessage::ORD_TYPE, NewOrderSingle::ORD_TYPE_MARKET);
    } else {
        b.field(FixMessage::ORD_TYPE, NewOrderSingle::ORD_TYPE_LIMIT)
                .field(FixMessage::PRICE, limitPrice);
    }
    b.field(FixMessage::TIME_IN_FORCE, timeInForce);
    send(b);
    return clOrdId;
}

void FixSession::sendExecutionReport(const ExecutionReport& report) {
    requireEstablished();
    FixMessage::Builder b = report.toBuilder();
    b.field(FixMessage::TRANSACT_TIME, utcTimestampNow());
    send(b);
}

std::string FixSession::sendOrderCancelRequest(const std::string& clOrdId,
                                               const std::string& origClOrdId,
                                               const std::string& symbol, Side side,
                                               std::int64_t quantity) {
    requireEstablished();
    send(FixMessage::builder(FixMessage::ORDER_CANCEL_REQUEST)
                 .field(FixMessage::ORIG_CL_ORD_ID, origClOrdId)
                 .field(FixMessage::CL_ORD_ID, clOrdId)
                 .field(FixMessage::SYMBOL, symbol)
                 .field(FixMessage::SIDE, side == Side::BUY ? '1' : '2')
                 .field(FixMessage::TRANSACT_TIME, utcTimestampNow())
                 .field(FixMessage::ORDER_QTY, quantity));
    return clOrdId;
}

std::string FixSession::sendOrderCancelReplace(const std::string& clOrdId,
                                               const std::string& origClOrdId,
                                               const std::string& symbol, Side side,
                                               std::int64_t quantity, double limitPrice,
                                               char timeInForce) {
    requireEstablished();
    FixMessage::Builder b = FixMessage::builder(FixMessage::ORDER_CANCEL_REPLACE_REQUEST)
                                     .field(FixMessage::ORIG_CL_ORD_ID, origClOrdId)
                                     .field(FixMessage::CL_ORD_ID, clOrdId)
                                     .field(FixMessage::SYMBOL, symbol)
                                     .field(FixMessage::SIDE, side == Side::BUY ? '1' : '2')
                                     .field(FixMessage::TRANSACT_TIME, utcTimestampNow())
                                     .field(FixMessage::ORDER_QTY, quantity);
    if (std::isnan(limitPrice)) {
        b.field(FixMessage::ORD_TYPE, NewOrderSingle::ORD_TYPE_MARKET);
    } else {
        b.field(FixMessage::ORD_TYPE, NewOrderSingle::ORD_TYPE_LIMIT)
                .field(FixMessage::PRICE, limitPrice);
    }
    b.field(FixMessage::TIME_IN_FORCE, timeInForce);
    send(b);
    return clOrdId;
}

void FixSession::sendReject(std::int64_t refSeqNum, const std::string& text) {
    FixMessage::Builder b =
            FixMessage::builder(FixMessage::REJECT).field(FixMessage::REF_SEQ_NUM, refSeqNum);
    if (!text.empty()) {
        std::string cleaned = text;
        std::replace(cleaned.begin(), cleaned.end(), FixMessage::SOH, ' ');
        b.field(FixMessage::TEXT, cleaned);
    }
    send(b);
}

void FixSession::logout() {
    if (!established_.load()) {
        close();
        return;
    }
    logoutSent_.store(true);
    send(FixMessage::builder(FixMessage::LOGOUT));
    awaitLogout(2);
    close();
}

void FixSession::close() {
    running_.store(false);
    established_.store(false);
    closeNative(toNative(socket_));
    if (auto* fileStore = dynamic_cast<FileSessionStore*>(store_.get())) {
        fileStore->close();
    }
}

// ------------------------------------------------------------------
// Session machinery
// ------------------------------------------------------------------

void FixSession::send(FixMessage::Builder builder) {
    try {
        std::lock_guard<std::recursive_mutex> lock(writeMutex_);
        std::int64_t seq = outgoingSeq_++;
        std::string sendingTime = utcTimestampNow();
        if (!isAdminType(builder.msgType())) {
            store_->storeMessage(seq, FixSessionStore::StoredMessage{builder.msgType(),
                                                                      builder.body(), sendingTime});
        }
        store_->saveOutgoingSeq(outgoingSeq_);
        writeBytes(toNative(socket_),
                   builder.encode(config_.senderCompId, config_.targetCompId, seq, sendingTime));
        lastSentNanos_.store(nowNanos());
    } catch (const std::exception& e) {
        disconnect(std::string("send failed: ") + e.what());
    }
}

void FixSession::writeReplay(const FixMessage::Builder& builder, std::int64_t seqNum,
                            const std::optional<std::string>& origSendingTime) {
    std::lock_guard<std::recursive_mutex> lock(writeMutex_);
    writeBytes(toNative(socket_),
               builder.encode(config_.senderCompId, config_.targetCompId, seqNum,
                              utcTimestampNow(), true, origSendingTime));
    lastSentNanos_.store(nowNanos());
}

void FixSession::forceOutgoingSeq(std::int64_t seqNum) {
    std::lock_guard<std::recursive_mutex> lock(writeMutex_);
    outgoingSeq_ = seqNum;
}

void FixSession::readerLoop() {
    std::vector<std::uint8_t> chunk(4096);
    try {
        while (running_.load()) {
            int n = ::recv(toNative(socket_), reinterpret_cast<char*>(chunk.data()),
                          static_cast<int>(chunk.size()), 0);
            if (n == 0) {
                disconnect("peer closed connection");
                return;
            }
            if (n < 0) {
                if (running_.load()) {
                    disconnect("read failed");
                }
                return;
            }
            lastReceivedNanos_.store(nowNanos());
            testRequestPending_.store(false);
            decoder_.feed(chunk.data(), 0, static_cast<std::size_t>(n));
            std::optional<FixMessage> message;
            while ((message = decoder_.poll()).has_value()) {
                handle(*message);
            }
        }
    } catch (const std::exception& e) {
        if (running_.load()) {
            disconnect(std::string("protocol error: ") + e.what());
        }
    }
}

void FixSession::handle(const FixMessage& m) {
    std::int64_t seq = m.getLong(FixMessage::MSG_SEQ_NUM);
    const std::string& type = m.msgType();

    // SequenceReset carries sequencing semantics itself; handle before validation.
    if (type == FixMessage::SEQUENCE_RESET) {
        handleSequenceReset(m);
        return;
    }
    // Logon with ResetSeqNumFlag(141)=Y -- which the spec requires to carry
    // 34=1 -- restarts both sides at 1. Apply BEFORE seqnum validation: the
    // whole point is that the peer's fresh seq 1 would otherwise read as
    // "too low" against a durable store and disconnect the session the
    // flag exists to save.
    if (type == FixMessage::LOGON && seq == 1
        && m.getString(FixMessage::RESET_SEQ_NUM_FLAG, "N") == "Y"
        && m.getString(FixMessage::POSS_DUP_FLAG, "N") != "Y") {
        applyLogonSeqReset();
    }

    std::int64_t expected = expectedIncomingSeq_.load();
    if (seq > expected) {
        // Gap: ask for a resend and drop this message -- it will be redelivered.
        listener_.onSequenceGap(expected, seq);
        resendTriggerSeq_ = std::max(resendTriggerSeq_, seq);
        if (!resendPending_) {
            resendPending_ = true;
            send(FixMessage::builder(FixMessage::RESEND_REQUEST)
                         .field(FixMessage::BEGIN_SEQ_NO, expected)
                         .field(FixMessage::END_SEQ_NO, static_cast<std::int64_t>(0)));
        }
        // Session-critical messages still act despite the gap.
        if (!(type == FixMessage::LOGON || type == FixMessage::LOGOUT)) {
            return;
        }
    } else if (seq < expected) {
        if (m.getString(FixMessage::POSS_DUP_FLAG, "N") == "Y") {
            return;  // duplicate from a resend overlap: suppress silently
        }
        disconnect("MsgSeqNum too low: expected " + std::to_string(expected) + " got "
                   + std::to_string(seq) + " without PossDupFlag");
        return;
    } else {
        advanceIncoming(seq + 1);
    }

    if (type == FixMessage::LOGON) {
        if (!established_.load()) {
            if (acceptor_) {
                FixMessage::Builder reply =
                        FixMessage::builder(FixMessage::LOGON)
                                .field(FixMessage::ENCRYPT_METHOD, std::string("0"))
                                .field(FixMessage::HEART_BT_INT,
                                       static_cast<std::int64_t>(config_.heartbeatSeconds));
                if (m.getString(FixMessage::RESET_SEQ_NUM_FLAG, "N") == "Y") {
                    // Confirm the reset: our reply is the peer's proof that
                    // its 141=Y was honored, sent as 34=1.
                    reply.field(FixMessage::RESET_SEQ_NUM_FLAG, std::string("Y"));
                }
                send(reply);
            }
            established_.store(true);
            countDownLogon();
            // Raw message too, so acceptors can inspect Username/Password (553/554).
            listener_.onMessage(*this, m);
            listener_.onLogon(*this);
        }
    } else if (type == FixMessage::HEARTBEAT) {
        listener_.onMessage(*this, m);
    } else if (type == FixMessage::TEST_REQUEST) {
        FixMessage::Builder reply = FixMessage::builder(FixMessage::HEARTBEAT);
        if (m.has(FixMessage::TEST_REQ_ID)) {
            reply.field(FixMessage::TEST_REQ_ID, m.getString(FixMessage::TEST_REQ_ID));
        }
        send(reply);
        listener_.onMessage(*this, m);
    } else if (type == FixMessage::LOGOUT) {
        if (!logoutSent_.load()) {
            send(FixMessage::builder(FixMessage::LOGOUT));  // confirm
        }
        established_.store(false);
        countDownLogout();
        listener_.onLogout(*this);
        running_.store(false);
        closeNative(toNative(socket_));
    } else if (type == FixMessage::RESEND_REQUEST) {
        handleResendRequest(m);
    } else if (type == FixMessage::REJECT) {
        listener_.onReject(*this, m.has(FixMessage::REF_SEQ_NUM) ? m.getLong(FixMessage::REF_SEQ_NUM) : 0,
                           m.getString(FixMessage::TEXT, ""));
    } else if (type == FixMessage::EXECUTION_REPORT) {
        dispatchApp(seq, [this, &m] {
            listener_.onExecutionReport(*this, ExecutionReport::fromMessage(m));
        });
    } else if (type == FixMessage::NEW_ORDER_SINGLE) {
        dispatchApp(seq,
                    [this, &m] { listener_.onNewOrderSingle(*this, NewOrderSingle::fromMessage(m)); });
    } else if (type == FixMessage::ORDER_CANCEL_REQUEST) {
        dispatchApp(seq, [this, &m] {
            listener_.onOrderCancelRequest(*this, OrderCancelRequest::fromMessage(m));
        });
    } else if (type == FixMessage::ORDER_CANCEL_REPLACE_REQUEST) {
        dispatchApp(seq, [this, &m] {
            listener_.onOrderCancelReplace(*this, OrderCancelReplaceRequest::fromMessage(m));
        });
    } else {
        listener_.onMessage(*this, m);
    }
}

void FixSession::dispatchApp(std::int64_t seq, const std::function<void()>& dispatch) {
    try {
        dispatch();
    } catch (const std::exception& e) {
        sendReject(seq, e.what());
    }
}

void FixSession::handleSequenceReset(const FixMessage& m) {
    std::int64_t newSeqNo = m.getLong(FixMessage::NEW_SEQ_NO);
    if (newSeqNo > expectedIncomingSeq_.load()) {
        advanceIncoming(newSeqNo);
    }
    // Stale resets (PossDup replays of old gap-fills) are ignored.
}

void FixSession::applyLogonSeqReset() {
    if (!sentResetLogon_) {
        std::lock_guard<std::recursive_mutex> lock(writeMutex_);
        outgoingSeq_ = 1;
        store_->saveOutgoingSeq(outgoingSeq_);
    }
    resendPending_ = false;
    resendTriggerSeq_ = 0;
    expectedIncomingSeq_.store(1);
    store_->saveIncomingSeq(1);
}

void FixSession::advanceIncoming(std::int64_t newExpected) {
    expectedIncomingSeq_.store(newExpected);
    store_->saveIncomingSeq(newExpected);
    if (resendPending_ && newExpected > resendTriggerSeq_) {
        resendPending_ = false;  // gap fully recovered
    }
}

void FixSession::handleResendRequest(const FixMessage& m) {
    std::int64_t begin = m.getLong(FixMessage::BEGIN_SEQ_NO);
    std::int64_t endRequested = m.getLong(FixMessage::END_SEQ_NO);
    try {
        std::lock_guard<std::recursive_mutex> lock(writeMutex_);
        std::int64_t last = outgoingSeq_ - 1;
        std::int64_t end = endRequested == 0 ? last : std::min(endRequested, last);
        std::int64_t gapFillStart = -1;
        for (std::int64_t seq = begin; seq <= end; ++seq) {
            std::optional<FixSessionStore::StoredMessage> stored = store_->retrieve(seq);
            if (!stored.has_value()) {
                if (gapFillStart < 0) {
                    gapFillStart = seq;
                }
                continue;
            }
            if (gapFillStart >= 0) {
                sendGapFill(gapFillStart, seq);
                gapFillStart = -1;
            }
            writeReplay(FixMessage::Builder::restore(stored->msgType, stored->body), seq,
                        stored->sendingTime);
        }
        if (gapFillStart >= 0) {
            sendGapFill(gapFillStart, end + 1);
        }
        listener_.onResendServed(begin, end);
    } catch (const std::exception& e) {
        disconnect(std::string("resend failed: ") + e.what());
    }
}

void FixSession::sendGapFill(std::int64_t seq, std::int64_t newSeqNo) {
    writeReplay(FixMessage::builder(FixMessage::SEQUENCE_RESET)
                        .field(FixMessage::GAP_FILL_FLAG, std::string("Y"))
                        .field(FixMessage::NEW_SEQ_NO, newSeqNo),
                seq, std::nullopt);
}

void FixSession::heartbeatLoop() {
    std::int64_t intervalNanos = static_cast<std::int64_t>(config_.heartbeatSeconds) * 1'000'000'000LL;
    while (running_.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        if (!established_.load() || !running_.load()) {
            continue;
        }
        std::int64_t now = nowNanos();
        if (now - lastSentNanos_.load() >= intervalNanos) {
            send(FixMessage::builder(FixMessage::HEARTBEAT));
        }
        std::int64_t silence = now - lastReceivedNanos_.load();
        if (silence >= intervalNanos * 3 / 2 && !testRequestPending_.load()) {
            testRequestPending_.store(true);
            send(FixMessage::builder(FixMessage::TEST_REQUEST)
                         .field(FixMessage::TEST_REQ_ID, "probe-" + std::to_string(now)));
        }
        if (silence >= intervalNanos * 5 / 2) {
            disconnect("heartbeat timeout after " + std::to_string(silence / 1'000'000) + "ms of silence");
            return;
        }
    }
}

void FixSession::disconnect(const std::string& reason) {
    bool expected = false;
    if (!disconnectNotified_.compare_exchange_strong(expected, true)) {
        return;
    }
    close();
    countDownLogon();   // unblock anyone waiting on establishment
    countDownLogout();
    listener_.onDisconnect(reason);
}

void FixSession::requireEstablished() const {
    if (!established_.load()) {
        throw std::runtime_error("FIX session not established");
    }
}

std::string FixSession::utcTimestampNow() {
    using namespace std::chrono;
    auto now = system_clock::now();
    auto dp = floor<days>(now);
    year_month_day ymd{dp};
    auto tod = now - dp;
    auto hms = hh_mm_ss<milliseconds>(duration_cast<milliseconds>(tod));
    char buf[24];
    std::snprintf(buf, sizeof(buf), "%04d%02d%02d-%02d:%02d:%02d.%03d",
                 static_cast<int>(ymd.year()), static_cast<unsigned>(ymd.month()),
                 static_cast<unsigned>(ymd.day()), static_cast<int>(hms.hours().count()),
                 static_cast<int>(hms.minutes().count()), static_cast<int>(hms.seconds().count()),
                 static_cast<int>(hms.subseconds().count()));
    return std::string(buf);
}

}  // namespace quantfinlib
