#include "quantfinlib/fix/fix_session.hpp"

#include <algorithm>
#include <cerrno>
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

// A peer FIN must surface as a send error, never as SIGPIPE killing the
// process. macOS has no MSG_NOSIGNAL and uses SO_NOSIGPIPE instead.
#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
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

void configureSocket(native_socket_t s, int sendTimeoutMillis) {
#ifdef _WIN32
    DWORD timeout = static_cast<DWORD>(sendTimeoutMillis);
#else
    timeval timeout{};
    timeout.tv_sec = sendTimeoutMillis / 1000;
    timeout.tv_usec = (sendTimeoutMillis % 1000) * 1000;
#endif
    ::setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&timeout),
                 sizeof(timeout));
#ifdef SO_NOSIGPIPE
    int on = 1;
    ::setsockopt(s, SOL_SOCKET, SO_NOSIGPIPE, &on, sizeof(on));
#endif
}

/**
 * SO_SNDTIMEO bounds each blocked send call; the deadline bounds the whole
 * frame against a peer that drains a few bytes at a time. A timeout leaves a
 * partial frame on the wire, so the caller must treat it as fatal.
 */
void writeBytes(native_socket_t sock, const std::vector<std::uint8_t>& payload,
                int timeoutMillis) {
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMillis);
    std::size_t sent = 0;
    while (sent < payload.size()) {
        int n = ::send(sock, reinterpret_cast<const char*>(payload.data() + sent),
                       static_cast<int>(payload.size() - sent), MSG_NOSIGNAL);
        if (n <= 0) {
#ifndef _WIN32
            if (n < 0 && errno == EINTR) {
                continue;
            }
#endif
            throw std::runtime_error("socket send failed or timed out");
        }
        sent += static_cast<std::size_t>(n);
        if (sent < payload.size() && std::chrono::steady_clock::now() >= deadline) {
            throw std::runtime_error("socket send timed out");
        }
    }
}

/** "YYYYMMDD-HH:MM:SS[.sss]" as seconds since the epoch; nullopt if malformed. */
std::optional<std::int64_t> parseUtcSeconds(const std::string& t) {
    if (t.size() < 17 || t[8] != '-' || t[11] != ':' || t[14] != ':') {
        return std::nullopt;
    }
    auto number = [&t](std::size_t from, std::size_t count, int& out) {
        out = 0;
        for (std::size_t i = from; i < from + count; ++i) {
            if (t[i] < '0' || t[i] > '9') {
                return false;
            }
            out = out * 10 + (t[i] - '0');
        }
        return true;
    };
    int year = 0, month = 0, day = 0, hour = 0, minute = 0, second = 0;
    if (!number(0, 4, year) || !number(4, 2, month) || !number(6, 2, day) || !number(9, 2, hour)
        || !number(12, 2, minute) || !number(15, 2, second)) {
        return std::nullopt;
    }
    using namespace std::chrono;
    year_month_day ymd{std::chrono::year(year), std::chrono::month(static_cast<unsigned>(month)),
                       std::chrono::day(static_cast<unsigned>(day))};
    if (!ymd.ok() || hour > 23 || minute > 59 || second > 60) {
        return std::nullopt;
    }
    return sys_days(ymd).time_since_epoch().count() * 86400LL + hour * 3600LL + minute * 60LL
           + second;
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
// Enough for any Logon; the full frame limit applies once a peer is logged on.
constexpr std::int64_t PRE_LOGON_MAX_BODY = 4096;
// Isolated line noise is ignored; a stream of it is not a live FIX peer.
constexpr int MAX_CONSECUTIVE_GARBLED = 16;

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
    std::intptr_t fd = fd_.exchange(fromNative(kInvalidSocket));
    if (fd != fromNative(kInvalidSocket)) {
#ifndef _WIN32
        ::shutdown(toNative(fd), SHUT_RDWR);  // wakes a thread blocked in accept
#endif
        closeNative(toNative(fd));
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
    if (senderCompId.find(FixMessage::SOH) != std::string::npos
            || targetCompId.find(FixMessage::SOH) != std::string::npos) {
        throw std::invalid_argument("CompID contains SOH");
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

FixSession::Config FixSession::Config::withSendTimeout(int millis) const {
    if (millis < 1) {
        throw std::invalid_argument("send timeout must be >= 1ms");
    }
    Config c = *this;
    c.sendTimeoutMillis = millis;
    return c;
}

FixSession::Config FixSession::Config::withMaxOrderReplayAge(int seconds) const {
    if (seconds < 0) {
        throw std::invalid_argument("replay age must be >= 0");
    }
    Config c = *this;
    c.maxOrderReplayAgeSeconds = seconds;
    return c;
}

FixSession::Config FixSession::Config::withSequenceResetMode() const {
    Config c = *this;
    c.allowSequenceResetMode = true;
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
    configureSocket(toNative(socket_), config_.sendTimeoutMillis);
    decoder_.setMaxBodyLength(PRE_LOGON_MAX_BODY);
    lastSentNanos_.store(nowNanos());
    lastReceivedNanos_.store(nowNanos());
    // An initiator configured for reset-on-logon starts BOTH counters at 1
    // before any thread runs (its Logon goes out as 34=1, 141=Y) and
    // persists the reset so a crash mid-handshake cannot resurrect the
    // abandoned numbers or their stored messages.
    if (sentResetLogon_) {
        store_->reset();
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
    // Only now can nothing be using the descriptor or the store.
    closeNative(toNative(socket_));
    if (auto* fileStore = dynamic_cast<FileSessionStore*>(store_.get())) {
        fileStore->close();
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
    native_socket_t client = ::accept(toNative(server.fd_.load()), nullptr, nullptr);
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
    std::lock_guard<std::mutex> lock(logonMutex_);
    if (!logonSucceeded_) {
        throw std::runtime_error("FIX logon failed: " + failureReason_);
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

void FixSession::sendOrderCancelReject(const OrderCancelReject& reject) {
    requireEstablished();
    FixMessage::Builder b = reject.toBuilder();
    b.field(FixMessage::TRANSACT_TIME, utcTimestampNow());
    send(b);
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
    shutdownSocket();
    {
        std::lock_guard<std::mutex> lock(stopMutex_);
    }
    stopCv_.notify_all();
}

void FixSession::shutdownSocket() {
    if (socketShutdown_.exchange(true)) {
        return;
    }
    // shutdown, not close: the reader may be inside recv on this descriptor,
    // and a closed number can be reused by another thread's open.
#ifdef _WIN32
    ::shutdown(toNative(socket_), SD_BOTH);
#else
    ::shutdown(toNative(socket_), SHUT_RDWR);
#endif
}

// ------------------------------------------------------------------
// Session machinery
// ------------------------------------------------------------------

void FixSession::sendLocked(const FixMessage::Builder& builder) {
    std::int64_t seq = outgoingSeq_++;
    std::string sendingTime = utcTimestampNow();
    if (!isAdminType(builder.msgType())) {
        store_->storeMessage(seq, FixSessionStore::StoredMessage{builder.msgType(), builder.body(),
                                                                  sendingTime});
    }
    store_->saveOutgoingSeq(outgoingSeq_);
    writeBytes(toNative(socket_),
               builder.encode(config_.senderCompId, config_.targetCompId, seq, sendingTime),
               config_.sendTimeoutMillis);
    lastSentNanos_.store(nowNanos());
}

void FixSession::send(FixMessage::Builder builder) {
    if (!running_.load()) {
        return;
    }
    try {
        std::lock_guard<std::recursive_mutex> lock(writeMutex_);
        sendLocked(builder);
    } catch (const std::exception& e) {
        disconnect(std::string("send failed: ") + e.what());
    }
}

void FixSession::trySend(const FixMessage::Builder& builder) {
    try {
        std::unique_lock<std::recursive_mutex> lock(writeMutex_, std::try_to_lock);
        if (lock.owns_lock()) {
            sendLocked(builder);
        }
        // Lock held: another send is in flight, which is traffic in itself
        // and is bounded by the send timeout.
    } catch (const std::exception& e) {
        disconnect(std::string("send failed: ") + e.what());
    }
}

void FixSession::writeReplay(const FixMessage::Builder& builder, std::int64_t seqNum,
                            const std::optional<std::string>& origSendingTime) {
    std::lock_guard<std::recursive_mutex> lock(writeMutex_);
    writeBytes(toNative(socket_),
               builder.encode(config_.senderCompId, config_.targetCompId, seqNum,
                              utcTimestampNow(), true, origSendingTime),
               config_.sendTimeoutMillis);
    lastSentNanos_.store(nowNanos());
}

void FixSession::forceOutgoingSeq(std::int64_t seqNum) {
    std::lock_guard<std::recursive_mutex> lock(writeMutex_);
    outgoingSeq_ = seqNum;
}

void FixSession::readerLoop() {
    std::vector<std::uint8_t> chunk(4096);
    int garbled = 0;
    try {
        while (running_.load()) {
            int n = ::recv(toNative(socket_), reinterpret_cast<char*>(chunk.data()),
                          static_cast<int>(chunk.size()), 0);
            if (n <= 0) {
                if (running_.load()) {
                    disconnect(n == 0 ? "peer closed connection" : "read failed");
                }
                return;
            }
            decoder_.feed(chunk.data(), 0, static_cast<std::size_t>(n));
            while (running_.load()) {
                std::optional<FixMessage> message;
                try {
                    message = decoder_.poll();
                } catch (const std::invalid_argument& e) {
                    // The frame was consumed but failed its checksum or body
                    // length. The spec says ignore it: the next good message
                    // reads as a gap and recovers it. Before Logon there is
                    // no session worth protecting.
                    if (!established_.load() || ++garbled > MAX_CONSECUTIVE_GARBLED) {
                        disconnect(std::string("garbled message: ") + e.what());
                        return;
                    }
                    continue;
                }
                if (!message.has_value()) {
                    break;
                }
                garbled = 0;
                // Only valid frames count as life: a peer dribbling junk
                // must not keep the watchdog quiet.
                lastReceivedNanos_.store(nowNanos());
                handle(*message);
            }
        }
    } catch (const std::exception& e) {
        if (running_.load()) {
            disconnect(std::string("protocol error: ") + e.what());
        }
    }
}

void FixSession::protocolFailure(const std::string& reason) {
    // Never answer a peer that has not logged on: it would burn a sequence
    // number on behalf of an unknown counterparty.
    if (established_.load()) {
        logoutSent_.store(true);
        send(FixMessage::builder(FixMessage::LOGOUT).field(FixMessage::TEXT, reason));
    }
    disconnect(reason);
}

void FixSession::tooLow(std::int64_t expected, std::int64_t seq) {
    protocolFailure("MsgSeqNum too low: expected " + std::to_string(expected) + " got "
                    + std::to_string(seq) + " without PossDupFlag");
}

void FixSession::requestResend(std::int64_t expected, std::int64_t received) {
    listener_.onSequenceGap(expected, received);
    resendTriggerSeq_ = std::max(resendTriggerSeq_, received);
    gapFillWindow_ = true;
    if (!resendPending_) {
        resendPending_ = true;
        send(FixMessage::builder(FixMessage::RESEND_REQUEST)
                     .field(FixMessage::BEGIN_SEQ_NO, expected)
                     .field(FixMessage::END_SEQ_NO, static_cast<std::int64_t>(0)));
    }
}

void FixSession::handle(const FixMessage& m) {
    if (!m.has(FixMessage::MSG_TYPE) || !m.has(FixMessage::MSG_SEQ_NUM)
        || !m.has(FixMessage::SENDER_COMP_ID) || !m.has(FixMessage::TARGET_COMP_ID)) {
        protocolFailure("required header field missing");
        return;
    }
    std::int64_t seq = m.getLong(FixMessage::MSG_SEQ_NUM);
    const std::string& type = m.msgType();
    if (seq < 1) {
        protocolFailure("MsgSeqNum must be positive");
        return;
    }
    // Their sender is our target and vice versa. Checked before anything
    // else acts on the message, sequence state included.
    if (m.getString(FixMessage::SENDER_COMP_ID) != config_.targetCompId
        || m.getString(FixMessage::TARGET_COMP_ID) != config_.senderCompId) {
        protocolFailure("CompID problem");
        return;
    }
    const bool possDup = m.getString(FixMessage::POSS_DUP_FLAG, "N") == "Y";
    if (established_.load()) {
        if (type == FixMessage::LOGON) {
            protocolFailure("Logon received on an established session");
            return;
        }
    } else {
        if (type != FixMessage::LOGON) {
            protocolFailure("first message must be Logon");
            return;
        }
        if (acceptor_ && !listener_.authenticateLogon(m)) {
            protocolFailure("Logon refused");
            return;
        }
    }

    // SequenceReset carries sequencing semantics itself; handle before validation.
    if (type == FixMessage::SEQUENCE_RESET) {
        handleSequenceReset(m, seq);
        return;
    }
    // Logon with ResetSeqNumFlag(141)=Y -- which the spec requires to carry
    // 34=1 -- restarts both sides at 1. Apply BEFORE seqnum validation: the
    // whole point is that the peer's fresh seq 1 would otherwise read as
    // "too low" against a durable store and disconnect the session the
    // flag exists to save.
    bool resetApplied = false;
    if (type == FixMessage::LOGON && m.getString(FixMessage::RESET_SEQ_NUM_FLAG, "N") == "Y") {
        if (seq != 1 || possDup) {
            protocolFailure("ResetSeqNumFlag requires MsgSeqNum 1 without PossDupFlag");
            return;
        }
        applyLogonSeqReset();
        resetApplied = true;
    }

    std::int64_t expected = expectedIncomingSeq_.load();
    bool inSequence = false;
    if (seq > expected) {
        // Gap: ask for a resend and drop this message -- it will be redelivered.
        requestResend(expected, seq);
        // Session-critical messages still act despite the gap -- including a
        // peer's own RESEND_REQUEST, so gap recovery on both sides can
        // proceed concurrently instead of one side stalling the other's.
        if (!(type == FixMessage::LOGON || type == FixMessage::LOGOUT
              || type == FixMessage::RESEND_REQUEST)) {
            return;
        }
    } else if (seq < expected) {
        if (possDup && established_.load()) {
            return;  // duplicate from a resend overlap: suppress silently
        }
        tooLow(expected, seq);
        return;
    } else {
        inSequence = true;
        if (!possDup) {
            gapFillWindow_ = false;  // live traffic: any resend reply has finished
        }
        expectedIncomingSeq_.store(seq + 1);
        if (resendPending_ && seq + 1 > resendTriggerSeq_) {
            resendPending_ = false;  // gap fully recovered
        }
    }

    try {
        dispatch(m, seq, resetApplied);
    } catch (const std::invalid_argument& e) {
        // Well-sequenced but a field is bad: a session Reject, not a disconnect.
        if (!established_.load()) {
            throw;
        }
        sendReject(seq, e.what());
    }
    // Persist only after the application has the message. A crash in
    // between re-requests it; the reverse order would lose a fill for good.
    if (inSequence) {
        store_->saveIncomingSeq(seq + 1);
    }
}

void FixSession::dispatch(const FixMessage& m, std::int64_t seq, bool resetApplied) {
    const std::string& type = m.msgType();
    if (type == FixMessage::LOGON) {
        if (m.getLong(FixMessage::HEART_BT_INT) < 1) {
            throw std::invalid_argument("HeartBtInt must be positive");
        }
        if (acceptor_) {
            FixMessage::Builder reply =
                    FixMessage::builder(FixMessage::LOGON)
                            .field(FixMessage::ENCRYPT_METHOD, std::string("0"))
                            .field(FixMessage::HEART_BT_INT,
                                   static_cast<std::int64_t>(config_.heartbeatSeconds));
            if (resetApplied) {
                // Confirm the reset: our reply is the peer's proof that
                // its 141=Y was honored, sent as 34=1.
                reply.field(FixMessage::RESET_SEQ_NUM_FLAG, std::string("Y"));
            }
            send(reply);
        }
        decoder_.setMaxBodyLength(FixDecoder::MAX_BODY_LENGTH);
        established_.store(true);
        {
            std::lock_guard<std::mutex> lock(logonMutex_);
            logonSucceeded_ = true;
        }
        countDownLogon();
        // Raw message too, so acceptors can inspect Username/Password (553/554).
        listener_.onMessage(*this, m);
        listener_.onLogon(*this);
    } else if (type == FixMessage::HEARTBEAT) {
        if (testRequestPending_.load()
            && m.getString(FixMessage::TEST_REQ_ID, "")
                       == "probe-" + std::to_string(testRequestId_.load())) {
            testRequestPending_.store(false);
        }
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
        close();
    } else if (type == FixMessage::RESEND_REQUEST) {
        handleResendRequest(m, seq);
    } else if (type == FixMessage::REJECT) {
        listener_.onReject(*this, m.has(FixMessage::REF_SEQ_NUM) ? m.getLong(FixMessage::REF_SEQ_NUM) : 0,
                           m.getString(FixMessage::TEXT, ""));
    } else if (type == FixMessage::EXECUTION_REPORT) {
        dispatchApp(m, seq, [this, &m] {
            listener_.onExecutionReport(*this, ExecutionReport::fromMessage(m));
        });
    } else if (type == FixMessage::ORDER_CANCEL_REJECT) {
        dispatchApp(m, seq, [this, &m] {
            listener_.onOrderCancelReject(*this, OrderCancelReject::fromMessage(m));
        });
    } else if (type == FixMessage::NEW_ORDER_SINGLE) {
        dispatchApp(m, seq,
                    [this, &m] { listener_.onNewOrderSingle(*this, NewOrderSingle::fromMessage(m)); });
    } else if (type == FixMessage::ORDER_CANCEL_REQUEST) {
        dispatchApp(m, seq, [this, &m] {
            listener_.onOrderCancelRequest(*this, OrderCancelRequest::fromMessage(m));
        });
    } else if (type == FixMessage::ORDER_CANCEL_REPLACE_REQUEST) {
        dispatchApp(m, seq, [this, &m] {
            listener_.onOrderCancelReplace(*this, OrderCancelReplaceRequest::fromMessage(m));
        });
    } else {
        listener_.onMessage(*this, m);
    }
}

void FixSession::dispatchApp(const FixMessage& m, std::int64_t seq,
                             const std::function<void()>& dispatch) {
    try {
        dispatch();
    } catch (const std::exception& e) {
        listener_.onApplicationFault(*this, m, e.what());
        sendReject(seq, e.what());
    }
}

void FixSession::handleSequenceReset(const FixMessage& m, std::int64_t seq) {
    std::int64_t expected = expectedIncomingSeq_.load();
    const bool gapFill = m.getString(FixMessage::GAP_FILL_FLAG, "N") == "Y";
    const bool possDup = m.getString(FixMessage::POSS_DUP_FLAG, "N") == "Y";

    if (!gapFill) {
        // Reset mode ignores MsgSeqNum by definition, which makes it a way to
        // skip fills with no record, so it needs an explicit opt-in.
        if (!config_.allowSequenceResetMode) {
            sendReject(seq, "SequenceReset-Reset not accepted");
            return;
        }
        std::int64_t newSeqNo = 0;
        try {
            newSeqNo = m.getLong(FixMessage::NEW_SEQ_NO);
        } catch (const std::invalid_argument& e) {
            sendReject(seq, e.what());
            return;
        }
        if (newSeqNo < expected) {
            sendReject(seq, "attempt to lower sequence number");
        } else if (newSeqNo > expected) {
            listener_.onSequenceGap(expected, newSeqNo);
            advanceIncoming(newSeqNo);
        }
        return;
    }

    // GapFill stands in for real messages, so it obeys MsgSeqNum like one.
    if (seq > expected) {
        requestResend(expected, seq);
        return;
    }
    if (seq < expected) {
        if (!possDup) {
            tooLow(expected, seq);
        }
        return;  // PossDup replay of an old gap-fill
    }
    std::int64_t newSeqNo = 0;
    try {
        newSeqNo = m.getLong(FixMessage::NEW_SEQ_NO);
    } catch (const std::invalid_argument& e) {
        sendReject(seq, e.what());
        return;
    }
    if (!gapFillWindow_ || !possDup) {
        sendReject(seq, "GapFill outside a resend");
        return;
    }
    if (newSeqNo <= seq) {
        sendReject(seq, "GapFill NewSeqNo must exceed MsgSeqNum");
        return;
    }
    advanceIncoming(newSeqNo);
}

void FixSession::applyLogonSeqReset() {
    if (!sentResetLogon_) {
        std::lock_guard<std::recursive_mutex> lock(writeMutex_);
        outgoingSeq_ = 1;
        store_->reset();
    } else {
        // This side's store was cleared at construction, and its Logon has
        // already taken 34=1.
        store_->saveIncomingSeq(1);
    }
    resendPending_ = false;
    gapFillWindow_ = false;
    resendTriggerSeq_ = 0;
    expectedIncomingSeq_.store(1);
}

void FixSession::advanceIncoming(std::int64_t newExpected) {
    expectedIncomingSeq_.store(newExpected);
    store_->saveIncomingSeq(newExpected);
    if (resendPending_ && newExpected > resendTriggerSeq_) {
        resendPending_ = false;  // gap fully recovered
    }
}

bool FixSession::isStaleOrder(const FixSessionStore::StoredMessage& stored) const {
    if (stored.msgType != FixMessage::NEW_ORDER_SINGLE
        && stored.msgType != FixMessage::ORDER_CANCEL_REQUEST
        && stored.msgType != FixMessage::ORDER_CANCEL_REPLACE_REQUEST) {
        return false;  // execution reports must always be recoverable
    }
    std::optional<std::int64_t> sent = parseUtcSeconds(stored.sendingTime);
    if (!sent.has_value()) {
        return true;
    }
    std::int64_t now = std::chrono::duration_cast<std::chrono::seconds>(
                               std::chrono::system_clock::now().time_since_epoch())
                               .count();
    return now - *sent > config_.maxOrderReplayAgeSeconds;
}

void FixSession::handleResendRequest(const FixMessage& m, std::int64_t requestSeq) {
    std::int64_t begin = m.getLong(FixMessage::BEGIN_SEQ_NO);
    std::int64_t endRequested = m.getLong(FixMessage::END_SEQ_NO);
    std::int64_t end = 0;
    try {
        // The lock spans the whole replay so that no live message can take a
        // sequence number inside the run; `end` is capped at what was really
        // sent and every send is time-bounded, so the hold is too.
        std::lock_guard<std::recursive_mutex> lock(writeMutex_);
        std::int64_t last = outgoingSeq_ - 1;
        if (begin < 1 || begin > last || endRequested < 0
            || (endRequested != 0 && endRequested < begin)) {
            sendReject(requestSeq, "invalid ResendRequest range");
            return;
        }
        end = endRequested == 0 ? last : std::min(endRequested, last);
        std::int64_t gapFillStart = -1;
        for (std::int64_t seq = begin; seq <= end; ++seq) {
            std::optional<FixSessionStore::StoredMessage> stored = store_->retrieve(seq);
            if (!stored.has_value() || isStaleOrder(*stored)) {
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
    } catch (const std::exception& e) {
        disconnect(std::string("resend failed: ") + e.what());
        return;
    }
    listener_.onResendServed(begin, end);
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
        {
            std::unique_lock<std::mutex> lock(stopMutex_);
            stopCv_.wait_for(lock, std::chrono::milliseconds(100),
                             [this] { return !running_.load(); });
        }
        if (!established_.load() || !running_.load()) {
            continue;
        }
        std::int64_t now = nowNanos();
        // Staleness first and with no lock: a writer stuck on a dead peer
        // must not be able to delay this decision.
        std::int64_t silence = now - lastReceivedNanos_.load();
        if (silence >= intervalNanos * 5 / 2) {
            disconnect("heartbeat timeout after " + std::to_string(silence / 1'000'000) + "ms of silence");
            return;
        }
        if (now - lastSentNanos_.load() >= intervalNanos) {
            trySend(FixMessage::builder(FixMessage::HEARTBEAT));
        }
        // Re-probe if the last TestRequest went unanswered for an interval.
        if (silence >= intervalNanos * 3 / 2
            && (!testRequestPending_.load() || now - testRequestSentNanos_ >= intervalNanos)) {
            testRequestId_.store(now);
            testRequestSentNanos_ = now;
            testRequestPending_.store(true);
            trySend(FixMessage::builder(FixMessage::TEST_REQUEST)
                            .field(FixMessage::TEST_REQ_ID, "probe-" + std::to_string(now)));
        }
    }
}

void FixSession::disconnect(const std::string& reason) {
    bool expected = false;
    if (!disconnectNotified_.compare_exchange_strong(expected, true)) {
        return;
    }
    close();
    {
        std::lock_guard<std::mutex> lock(logonMutex_);
        if (!logonSucceeded_) {
            failureReason_ = reason;
        }
    }
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
