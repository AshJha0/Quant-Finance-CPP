#include "quantfinlib/trading/fix_venue_bridge.hpp"

#include <charconv>
#include <cmath>
#include <stdexcept>

namespace quantfinlib {

namespace {

/** Digits only; anything else is 0, which no order carries. */
std::int64_t parseClOrdId(const std::string& text) {
    std::int64_t value = 0;
    const char* end = text.data() + text.size();
    auto [ptr, ec] = std::from_chars(text.data(), end, value);
    return ec == std::errc() && ptr == end && !text.empty() && value > 0 ? value : 0;
}

/** A whole, positive quantity that fits; 0 otherwise. */
std::int64_t wholeQuantity(double quantity) {
    if (!(quantity >= 1.0) || quantity >= 9.0e15 || std::floor(quantity) != quantity) {
        return 0;
    }
    return static_cast<std::int64_t>(quantity);
}

}  // namespace

FixVenueBridge::FixVenueBridge(HftOrderGateway& gateway, std::vector<std::string> symbols,
                               Config config)
        : gateway_(gateway), symbols_(std::move(symbols)), config_(config) {
    if (config_.execIdWindow == 0) {
        throw std::invalid_argument("execIdWindow must be > 0");
    }
    for (std::size_t i = 0; i < symbols_.size(); i++) {
        if (!symbolIds_.emplace(symbols_[i], static_cast<int>(i)).second) {
            throw std::invalid_argument("duplicate symbol " + symbols_[i]);
        }
    }
    gateway_.addVenueListener([this](const VenueMessage& m) { onVenueMessage(m); });
}

FixVenueBridge::~FixVenueBridge() {
    gateway_.stop();
}

void FixVenueBridge::detach() {
    linkLost();
}

std::size_t FixVenueBridge::unsentCancelCount() const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    return unsentCancels_.size();
}

void FixVenueBridge::fault(std::atomic<std::int64_t>& counter) {
    counter.fetch_add(1, std::memory_order_acq_rel);
    gateway_.riskGate().trip(HftRiskGate::KILL_GATEWAY);
}

void FixVenueBridge::linkLost() {
    {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        session_ = nullptr;
        connected_.store(false, std::memory_order_release);
    }
    gateway_.riskGate().trip(HftRiskGate::KILL_VENUE_LINK);
    gateway_.cancelAll(HftOrderGateway::steadyNanos());
}

void FixVenueBridge::sendCancel(FixSession& session, const VenueMessage& m) {
    bool sent = false;
    try {
        session.sendOrderCancelRequest(std::to_string(m.clOrdId), std::to_string(m.origClOrdId),
                                       symbols_.at(static_cast<std::size_t>(m.symbolId)), m.side,
                                       m.quantity);
        // send() swallows a failure and disconnects, so returning proves nothing.
        sent = session.isEstablished();
    } catch (const std::runtime_error&) {
        // Session went down between the check and the send.
    }
    if (!sent) {
        unsentCancels_.push_back(m);
    }
}

void FixVenueBridge::onVenueMessage(const VenueMessage& m) {
    std::unique_lock<std::recursive_mutex> lock(mutex_);
    FixSession* session = session_ != nullptr && session_->isEstablished() ? session_ : nullptr;
    if (m.type == VenueMessage::Type::CANCEL) {
        if (session == nullptr) {
            unsentCancels_.push_back(m);
        } else {
            sendCancel(*session, m);
        }
        return;
    }
    if (session == nullptr) {
        lock.unlock();
        neverSent_.fetch_add(1, std::memory_order_acq_rel);
        if (m.type == VenueMessage::Type::NEW) {
            gateway_.onOrderReject(m.clOrdId);
        } else {
            gateway_.onReplaceReject(m.clOrdId);
        }
        return;
    }
    const std::string& symbol = symbols_.at(static_cast<std::size_t>(m.symbolId));
    if (m.type == VenueMessage::Type::NEW) {
        session->sendNewOrderSingle(std::to_string(m.clOrdId), symbol, m.side, m.quantity, m.price,
                                    config_.timeInForce);
    } else {
        session->sendOrderCancelReplace(std::to_string(m.clOrdId), std::to_string(m.origClOrdId),
                                        symbol, m.side, m.quantity, m.price, config_.timeInForce);
    }
    if (!session->isEstablished()) {
        inDoubt_.fetch_add(1, std::memory_order_acq_rel);
    }
}

void FixVenueBridge::onLogon(FixSession& session) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    session_ = &session;
    connected_.store(true, std::memory_order_release);
    std::deque<VenueMessage> pending;
    pending.swap(unsentCancels_);
    for (const VenueMessage& m : pending) {
        sendCancel(session, m);
    }
}

void FixVenueBridge::onLogout(FixSession&) {
    linkLost();
}

void FixVenueBridge::onDisconnect(const std::string&) {
    linkLost();
}

bool FixVenueBridge::firstSighting(const std::string& execId) {
    if (!seenExecIds_.insert(execId).second) {
        return false;
    }
    execIdOrder_.push_back(execId);
    if (execIdOrder_.size() > config_.execIdWindow) {
        seenExecIds_.erase(execIdOrder_.front());
        execIdOrder_.pop_front();
    }
    return true;
}

int FixVenueBridge::symbolIdOf(const std::string& symbol) const {
    auto it = symbolIds_.find(symbol);
    return it == symbolIds_.end() ? -1 : it->second;
}

void FixVenueBridge::handleFill(const ExecutionReport& report, std::int64_t clOrdId) {
    if (!firstSighting(report.execId)) {
        duplicates_.fetch_add(1, std::memory_order_acq_rel);
        return;
    }
    std::int64_t quantity = wholeQuantity(report.lastQty);
    if (quantity == 0) {
        fault(faults_);   // cannot be booked as a whole lot; the kill stops the rest
        return;
    }
    std::int64_t before = gateway_.filledQuantity(clOrdId);
    // Always through the gateway: it books the exposure even when it trips.
    gateway_.onFill(clOrdId, symbolIdOf(report.symbol), report.side, quantity);
    if (before >= 0 && static_cast<double>(before + quantity) != report.cumQty) {
        fault(cumMismatch_);
    }
}

void FixVenueBridge::onExecutionReport(FixSession&, const ExecutionReport& report) {
    std::int64_t clOrdId = parseClOrdId(report.clOrdId);
    bool known = clOrdId != 0 && gateway_.issued(clOrdId);
    if (!known) {
        fault(unknown_);
    }
    switch (report.execType) {
        case ExecutionReport::EXEC_TYPE_TRADE:
            handleFill(report, clOrdId);
            return;
        case ExecutionReport::EXEC_TYPE_PENDING_NEW:
        case ExecutionReport::EXEC_TYPE_PENDING_CANCEL:
        case ExecutionReport::EXEC_TYPE_PENDING_REPLACE:
        case ExecutionReport::EXEC_TYPE_RESTATED:
        case ExecutionReport::EXEC_TYPE_ORDER_STATUS:
            return;   // no state change here
        default:
            break;
    }
    if (!known) {
        return;
    }
    switch (report.execType) {
        case ExecutionReport::EXEC_TYPE_NEW:
            gateway_.onOrderAck(clOrdId);
            break;
        case ExecutionReport::EXEC_TYPE_REJECTED:
            gateway_.onOrderReject(clOrdId);
            break;
        case ExecutionReport::EXEC_TYPE_REPLACED:
            gateway_.onReplaceAck(clOrdId);
            break;
        case ExecutionReport::EXEC_TYPE_CANCELED:
        case ExecutionReport::EXEC_TYPE_EXPIRED:
        case ExecutionReport::EXEC_TYPE_DONE_FOR_DAY:
            gateway_.onOrderOut(clOrdId);
            break;
        default:
            fault(faults_);   // trade bust, correction, or a type we do not know
            break;
    }
}

void FixVenueBridge::onOrderCancelReject(FixSession&, const OrderCancelReject& reject) {
    std::int64_t clOrdId = parseClOrdId(reject.clOrdId);
    if (clOrdId == 0 || !gateway_.issued(clOrdId)) {
        fault(unknown_);
        return;
    }
    if (reject.responseTo == OrderCancelReject::RESPONSE_TO_CANCEL) {
        gateway_.onCancelReject(clOrdId);
    } else {
        gateway_.onReplaceReject(clOrdId);
    }
}

void FixVenueBridge::onApplicationFault(FixSession&, const FixMessage&, const std::string&) {
    fault(faults_);
}

void FixVenueBridge::onReject(FixSession&, std::int64_t, const std::string&) {
    fault(faults_);
}

void FixVenueBridge::onMessage(FixSession&, const FixMessage& message) {
    if (message.msgType() == FixMessage::BUSINESS_MESSAGE_REJECT) {
        fault(faults_);
    }
}

}  // namespace quantfinlib
