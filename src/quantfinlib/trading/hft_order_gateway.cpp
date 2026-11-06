#include "quantfinlib/trading/hft_order_gateway.hpp"

#include <algorithm>
#include <chrono>
#include <limits>

#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
#include <immintrin.h>
#define QFL_SPIN_PAUSE() _mm_pause()
#else
#define QFL_SPIN_PAUSE() ((void)0)
#endif

namespace quantfinlib {

namespace {

constexpr int kVersionShift = 40;
constexpr std::int64_t kOrderIdMask = (std::int64_t{1} << kVersionShift) - 1;
constexpr std::int64_t kMaxVersion = (std::int64_t{1} << 23) - 1;   // ClOrdID stays positive

std::int64_t clOrdIdOf(std::int64_t orderId, std::int64_t version) {
    return orderId | (version << kVersionShift);
}

std::int64_t roundUpPow2(int requested) {
    std::int64_t size = 4;
    while (size < requested) {
        size <<= 1;
    }
    return size;
}

constexpr std::size_t kSweepBlock = 64;

constexpr double kNoBuy = 0.0;   // prices are positive, so nothing crosses these
constexpr double kNoSell = std::numeric_limits<double>::infinity();

constexpr std::uint8_t kNew = static_cast<std::uint8_t>(VenueMessage::Type::NEW);
constexpr std::uint8_t kCancel = static_cast<std::uint8_t>(VenueMessage::Type::CANCEL);
constexpr std::uint8_t kReplace = static_cast<std::uint8_t>(VenueMessage::Type::REPLACE);

}  // namespace

class HftOrderGateway::SpinGuard {
public:
    explicit SpinGuard(std::atomic_flag& flag) : flag_(flag) {
        while (flag_.test_and_set(std::memory_order_acquire)) {
            QFL_SPIN_PAUSE();
        }
    }
    ~SpinGuard() { flag_.clear(std::memory_order_release); }

private:
    std::atomic_flag& flag_;
};

HftOrderGateway::HftOrderGateway(int ringCapacity, HftRiskGate& riskGate, bool busySpin,
                                 int maxOpenOrders)
        : ring_(ringCapacity),
          riskGate_(riskGate),
          busySpin_(busySpin),
          slots_(static_cast<std::size_t>(roundUpPow2(maxOpenOrders))),
          symbolHeads_(static_cast<std::size_t>(riskGate.symbolCapacity()), -1),
          highestBuy_(static_cast<std::size_t>(riskGate.symbolCapacity()), kNoBuy),
          lowestSell_(static_cast<std::size_t>(riskGate.symbolCapacity()), kNoSell),
          workingBuys_(static_cast<std::size_t>(riskGate.symbolCapacity())),
          workingSells_(static_cast<std::size_t>(riskGate.symbolCapacity())),
          slotMask_(roundUpPow2(maxOpenOrders) - 1) {
    sweptEpoch_ = riskGate.killEpoch();
}

std::int64_t HftOrderGateway::steadyNanos() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
                   std::chrono::steady_clock::now().time_since_epoch())
            .count();
}

void HftOrderGateway::addOrderListener(OrderListener listener) {
    std::lock_guard<std::mutex> lock(setupMutex_);
    orderListenerStore_.push_back(std::move(listener));
    const OrderVec* cur = listeners_.load(std::memory_order_acquire);
    auto next = std::make_unique<OrderVec>(cur ? *cur : OrderVec{});
    next->push_back(&orderListenerStore_.back());
    listeners_.store(next.get(), std::memory_order_release);
    orderLists_.push_back(std::move(next));
}

void HftOrderGateway::addVenueListener(VenueListener listener) {
    std::lock_guard<std::mutex> lock(setupMutex_);
    venueListenerStore_.push_back(std::move(listener));
    const VenueVec* cur = venueListeners_.load(std::memory_order_acquire);
    auto next = std::make_unique<VenueVec>(cur ? *cur : VenueVec{});
    next->push_back(&venueListenerStore_.back());
    venueListeners_.store(next.get(), std::memory_order_release);
    venueLists_.push_back(std::move(next));
}

void HftOrderGateway::enableDeadMan(std::int64_t timeoutNanos, std::int64_t nowNanos) {
    lastHeartbeat_.store(nowNanos, std::memory_order_release);
    deadManTimeout_.store(timeoutNanos > 0 ? timeoutNanos : 0, std::memory_order_release);
}

bool HftOrderGateway::checkDeadMan(std::int64_t nowNanos) {
    std::int64_t timeout = deadManTimeout_.load(std::memory_order_acquire);
    if (timeout == 0 || nowNanos - lastHeartbeat_.load(std::memory_order_acquire) <= timeout) {
        return false;
    }
    riskGate_.trip(HftRiskGate::KILL_DEAD_MAN);
    return true;
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

HftOrderGateway::Slot* HftOrderGateway::find(std::int64_t clOrdId) {
    if (clOrdId <= 0) {
        return nullptr;
    }
    std::int64_t orderId = clOrdId & kOrderIdMask;
    Slot& slot = slots_[static_cast<std::size_t>(orderId & slotMask_)];
    if (slot.orderId != orderId || (clOrdId >> kVersionShift) > slot.version) {
        return nullptr;
    }
    return &slot;
}

const HftOrderGateway::Slot* HftOrderGateway::find(std::int64_t clOrdId) const {
    return const_cast<HftOrderGateway*>(this)->find(clOrdId);
}

bool HftOrderGateway::crossesOwnOrder(int symbolId, Side side, std::int64_t quantity, double price,
                                      const Slot* self) {
    auto sym = static_cast<std::size_t>(symbolId);
    if (side == Side::BUY ? price < lowestSell_[sym] : price > highestBuy_[sym]) {
        return false;
    }
    for (int i = symbolHeads_[static_cast<std::size_t>(symbolId)]; i >= 0;) {
        const Slot& other = slots_[static_cast<std::size_t>(i)];
        i = other.next;
        if (&other == self || other.side == side) {
            continue;
        }
        // Until the venue answers, a pending replace could rest at either price.
        for (int leg = 0; leg < (other.replacePending ? 2 : 1); leg++) {
            double resting = leg == 0 ? other.price : other.pendingPrice;
            bool crosses = side == Side::BUY ? price >= resting : price <= resting;
            if (crosses
                    && (selfTradeHook_ == nullptr
                        || !selfTradeHook_(selfTradeContext_,
                                           SelfTradeCandidate{symbolId, side, quantity, price,
                                                              other.orderId, resting}))) {
                return true;
            }
        }
    }
    return false;
}

void HftOrderGateway::widenBound(int symbolId, Side side, double price) {
    auto sym = static_cast<std::size_t>(symbolId);
    if (side == Side::BUY) {
        highestBuy_[sym] = std::max(highestBuy_[sym], price);
    } else {
        lowestSell_[sym] = std::min(lowestSell_[sym], price);
    }
}

void HftOrderGateway::link(int index) {
    Slot& slot = slots_[static_cast<std::size_t>(index)];
    int& head = symbolHeads_[static_cast<std::size_t>(slot.symbolId)];
    slot.prev = -1;
    slot.next = head;
    if (head >= 0) {
        slots_[static_cast<std::size_t>(head)].prev = index;
    }
    head = index;
    auto sym = static_cast<std::size_t>(slot.symbolId);
    (slot.side == Side::BUY ? workingBuys_ : workingSells_)[sym]++;
    widenBound(slot.symbolId, slot.side, slot.price);
}

void HftOrderGateway::finish(Slot& slot) {
    riskGate_.releaseOpen(slot.symbolId, slot.side, slot.reserved);
    if (slot.prev >= 0) {
        slots_[static_cast<std::size_t>(slot.prev)].next = slot.next;
    } else {
        symbolHeads_[static_cast<std::size_t>(slot.symbolId)] = slot.next;
    }
    if (slot.next >= 0) {
        slots_[static_cast<std::size_t>(slot.next)].prev = slot.prev;
    }
    auto sym = static_cast<std::size_t>(slot.symbolId);
    if (slot.side == Side::BUY) {
        if (--workingBuys_[sym] == 0) {
            highestBuy_[sym] = kNoBuy;
        }
    } else if (--workingSells_[sym] == 0) {
        lowestSell_[sym] = kNoSell;
    }
    slot = Slot{};
    live_.fetch_sub(1, std::memory_order_acq_rel);
}

void HftOrderGateway::resolveReplace(Slot& slot, bool accepted) {
    if (accepted) {
        slot.quantity = slot.pendingQuantity;
        slot.price = slot.pendingPrice;
    }
    slot.replacePending = false;
    std::int64_t target = std::max<std::int64_t>(0, slot.quantity - slot.filled);
    if (slot.reserved > target) {
        riskGate_.releaseOpen(slot.symbolId, slot.side, slot.reserved - target);
        slot.reserved = target;
    }
    if (slot.filled >= slot.quantity) {
        finish(slot);
    }
}

std::int64_t HftOrderGateway::submit(int symbolId, Side side, std::int64_t quantity, double price,
                                     std::int64_t timestampNanos) {
    SpinGuard guard(lock_);
    // Read first. A kill landing after this is not reduce-only just because
    // it beat the order to the table: the order passed as a normal one, and
    // delivery must stop it.
    bool reduceOnly = riskGate_.isKilled();
    int code = riskGate_.reserve(symbolId, side, quantity, price);
    if (code != HftRiskGate::OK) {
        return -code;
    }
    if (crossesOwnOrder(symbolId, side, quantity, price, nullptr)) {
        riskGate_.releaseOpen(symbolId, side, quantity);
        riskGate_.countRejection(HftRiskGate::REJECT_SELF_TRADE);
        return -HftRiskGate::REJECT_SELF_TRADE;
    }
    if (live_.load(std::memory_order_relaxed) > slotMask_ || nextId_ + slotMask_ >= kOrderIdMask) {
        riskGate_.releaseOpen(symbolId, side, quantity);
        riskGate_.countRejection(HftRiskGate::REJECT_ORDER_TABLE);
        return -HftRiskGate::REJECT_ORDER_TABLE;
    }
    std::int64_t id = nextId_;
    while (slots_[static_cast<std::size_t>(id & slotMask_)].orderId != 0) {
        id++;   // bounded: the table has a free slot
    }
    if (!ring_.publishMessage(kNew, id, 0, symbolId, side, quantity, price, timestampNanos)) {
        riskGate_.releaseOpen(symbolId, side, quantity);
        ringFull_.fetch_add(1, std::memory_order_acq_rel);
        return 0;
    }
    int index = static_cast<int>(id & slotMask_);
    Slot& slot = slots_[static_cast<std::size_t>(index)];
    slot.orderId = id;
    slot.symbolId = symbolId;
    slot.side = side;
    slot.quantity = quantity;
    slot.reserved = quantity;
    slot.price = price;
    slot.state = State::QUEUED;
    slot.reduceOnly = reduceOnly;
    link(index);
    live_.fetch_add(1, std::memory_order_acq_rel);
    nextId_ = id + 1;
    submitted_.fetch_add(1, std::memory_order_acq_rel);
    return id;
}

std::int64_t HftOrderGateway::cancel(std::int64_t orderId, std::int64_t timestampNanos) {
    SpinGuard guard(lock_);
    Slot* slot = find(orderId);
    if (slot == nullptr || slot->cancelPending || slot->version >= kMaxVersion) {
        riskGate_.countRejection(HftRiskGate::REJECT_ORDER_STATE);
        return -HftRiskGate::REJECT_ORDER_STATE;
    }
    if (slot->state == State::QUEUED) {
        finish(*slot);
        return orderId;
    }
    std::int64_t orig = clOrdIdOf(slot->orderId, slot->ackedVersion);
    std::int64_t clOrdId = clOrdIdOf(slot->orderId, slot->version + 1);
    if (!ring_.publishMessage(kCancel, clOrdId, orig, slot->symbolId, slot->side, slot->quantity,
                              slot->price, timestampNanos)) {
        ringFull_.fetch_add(1, std::memory_order_acq_rel);
        return 0;
    }
    slot->version++;
    slot->cancelPending = true;
    return clOrdId;
}

std::int64_t HftOrderGateway::replace(std::int64_t orderId, std::int64_t newLeaves,
                                      double newPrice, std::int64_t timestampNanos) {
    SpinGuard guard(lock_);
    Slot* slot = find(orderId);
    if (slot == nullptr || slot->state != State::LIVE || slot->replacePending
            || slot->cancelPending || slot->version >= kMaxVersion) {
        riskGate_.countRejection(HftRiskGate::REJECT_ORDER_STATE);
        return -HftRiskGate::REJECT_ORDER_STATE;
    }
    if (riskGate_.isKilled() && !slot->reduceOnly) {
        riskGate_.countRejection(HftRiskGate::REJECT_KILLED);
        return -HftRiskGate::REJECT_KILLED;
    }
    if (newLeaves <= 0) {
        riskGate_.countRejection(HftRiskGate::REJECT_QUANTITY);
        return -HftRiskGate::REJECT_QUANTITY;
    }
    std::int64_t total = slot->filled + newLeaves;
    std::int64_t delta = std::max<std::int64_t>(0, total - slot->quantity);
    int code = riskGate_.reserveDelta(slot->symbolId, slot->side, newLeaves, delta, newPrice);
    if (code != HftRiskGate::OK) {
        return -code;
    }
    if (crossesOwnOrder(slot->symbolId, slot->side, newLeaves, newPrice, slot)) {
        riskGate_.releaseOpen(slot->symbolId, slot->side, delta);
        riskGate_.countRejection(HftRiskGate::REJECT_SELF_TRADE);
        return -HftRiskGate::REJECT_SELF_TRADE;
    }
    std::int64_t orig = clOrdIdOf(slot->orderId, slot->ackedVersion);
    std::int64_t clOrdId = clOrdIdOf(slot->orderId, slot->version + 1);
    if (!ring_.publishMessage(kReplace, clOrdId, orig, slot->symbolId, slot->side, total, newPrice,
                              timestampNanos)) {
        riskGate_.releaseOpen(slot->symbolId, slot->side, delta);
        ringFull_.fetch_add(1, std::memory_order_acq_rel);
        return 0;
    }
    slot->version++;
    slot->reserved += delta;
    slot->pendingQuantity = total;
    slot->pendingPrice = newPrice;
    widenBound(slot->symbolId, slot->side, newPrice);
    slot->replacePending = true;
    return clOrdId;
}

int HftOrderGateway::sweep(std::int64_t timestampNanos, bool skipReduceOnly) {
    int acted = 0;
    // The kill is already latched, so an order landing behind the sweep is
    // reduce-only or gets stopped at delivery. Nothing needs the whole table
    // held at once.
    for (std::size_t base = 0; base < slots_.size(); base += kSweepBlock) {
        SpinGuard guard(lock_);
        std::size_t end = std::min(slots_.size(), base + kSweepBlock);
        for (std::size_t i = base; i < end; i++) {
            Slot& slot = slots_[i];
            if (slot.orderId == 0 || slot.cancelPending || (skipReduceOnly && slot.reduceOnly)) {
                continue;
            }
            if (slot.state == State::QUEUED) {
                finish(slot);
                blocked_.fetch_add(1, std::memory_order_acq_rel);
                acted++;
                continue;
            }
            std::int64_t orig = clOrdIdOf(slot.orderId, slot.ackedVersion);
            std::int64_t clOrdId = clOrdIdOf(slot.orderId, slot.version + 1);
            if (!ring_.publishMessage(kCancel, clOrdId, orig, slot.symbolId, slot.side,
                                      slot.quantity, slot.price, timestampNanos)) {
                ringFull_.fetch_add(1, std::memory_order_acq_rel);
                sweepPending_.store(true, std::memory_order_release);
                continue;
            }
            slot.version++;
            slot.cancelPending = true;
            acted++;
        }
    }
    return acted;
}

int HftOrderGateway::cancelAll(std::int64_t timestampNanos) {
    return sweep(timestampNanos, false);
}

bool HftOrderGateway::onOrderAck(std::int64_t clOrdId) {
    SpinGuard guard(lock_);
    Slot* slot = find(clOrdId);
    if (slot == nullptr || slot->state != State::SENT) {
        unmatched_.fetch_add(1, std::memory_order_acq_rel);
        return false;
    }
    slot->state = State::LIVE;
    return true;
}

bool HftOrderGateway::onOrderReject(std::int64_t clOrdId) {
    SpinGuard guard(lock_);
    Slot* slot = find(clOrdId);
    if (slot == nullptr || slot->state != State::SENT) {
        unmatched_.fetch_add(1, std::memory_order_acq_rel);
        return false;
    }
    finish(*slot);
    return true;
}

bool HftOrderGateway::onCancelAck(std::int64_t clOrdId) {
    SpinGuard guard(lock_);
    Slot* slot = find(clOrdId);
    if (slot == nullptr || !slot->cancelPending) {
        unmatched_.fetch_add(1, std::memory_order_acq_rel);
        return false;
    }
    finish(*slot);
    return true;
}

bool HftOrderGateway::onCancelReject(std::int64_t clOrdId) {
    SpinGuard guard(lock_);
    Slot* slot = find(clOrdId);
    if (slot == nullptr || !slot->cancelPending) {
        unmatched_.fetch_add(1, std::memory_order_acq_rel);
        return false;
    }
    slot->cancelPending = false;
    return true;
}

bool HftOrderGateway::onReplaceAck(std::int64_t clOrdId) {
    SpinGuard guard(lock_);
    Slot* slot = find(clOrdId);
    if (slot == nullptr || !slot->replacePending) {
        unmatched_.fetch_add(1, std::memory_order_acq_rel);
        return false;
    }
    slot->ackedVersion = clOrdId >> kVersionShift;
    resolveReplace(*slot, true);
    return true;
}

bool HftOrderGateway::onReplaceReject(std::int64_t clOrdId) {
    SpinGuard guard(lock_);
    Slot* slot = find(clOrdId);
    if (slot == nullptr || !slot->replacePending) {
        unmatched_.fetch_add(1, std::memory_order_acq_rel);
        return false;
    }
    resolveReplace(*slot, false);
    return true;
}

bool HftOrderGateway::onOrderOut(std::int64_t clOrdId) {
    SpinGuard guard(lock_);
    Slot* slot = find(clOrdId);
    if (slot == nullptr || slot->state == State::QUEUED) {
        unmatched_.fetch_add(1, std::memory_order_acq_rel);
        return false;
    }
    finish(*slot);
    return true;
}

bool HftOrderGateway::onFill(std::int64_t clOrdId, int symbolId, Side side,
                             std::int64_t quantity) {
    SpinGuard guard(lock_);
    Slot* slot = find(clOrdId);
    if (slot == nullptr || slot->state == State::QUEUED || slot->symbolId != symbolId
            || slot->side != side || quantity <= 0) {
        // Book it if the report is usable at all: the exposure is real.
        riskGate_.onFill(symbolId, side, quantity);
        unsolicited_.fetch_add(1, std::memory_order_acq_rel);
        riskGate_.trip(HftRiskGate::KILL_GATEWAY);
        return false;
    }
    std::int64_t limit = slot->replacePending ? std::max(slot->quantity, slot->pendingQuantity)
                                              : slot->quantity;
    bool overfilled = quantity > limit - slot->filled;
    std::int64_t released = std::min(quantity, slot->reserved);
    riskGate_.onOrderFill(symbolId, side, quantity, released);
    slot->reserved -= released;
    slot->filled += quantity;
    if (overfilled) {
        overfills_.fetch_add(1, std::memory_order_acq_rel);
        riskGate_.trip(HftRiskGate::KILL_GATEWAY);
    }
    if (slot->filled >= slot->quantity && !slot->replacePending) {
        finish(*slot);
    }
    return !overfilled;
}

std::int64_t HftOrderGateway::leavesQuantity(std::int64_t orderId) const {
    SpinGuard guard(lock_);
    const Slot* slot = find(orderId);
    return slot == nullptr ? 0 : std::max<std::int64_t>(0, slot->quantity - slot->filled);
}

std::int64_t HftOrderGateway::filledQuantity(std::int64_t clOrdId) const {
    SpinGuard guard(lock_);
    const Slot* slot = find(clOrdId);
    return slot == nullptr ? -1 : slot->filled;
}

bool HftOrderGateway::issued(std::int64_t clOrdId) const {
    SpinGuard guard(lock_);
    if (clOrdId <= 0) {
        return false;
    }
    std::int64_t orderId = clOrdId & kOrderIdMask;
    if (orderId == 0 || orderId >= nextId_) {
        return false;
    }
    const Slot& slot = slots_[static_cast<std::size_t>(orderId & slotMask_)];
    return slot.orderId != orderId || (clOrdId >> kVersionShift) <= slot.version;
}

void HftOrderGateway::onDrained(std::uint8_t type, std::int64_t clOrdId, std::int64_t refId,
                                int symbolId, Side side, std::int64_t quantity, double price,
                                std::int64_t timestampNanos) {
    {
        SpinGuard guard(lock_);
        Slot* slot = find(clOrdId);
        if (slot == nullptr) {
            return;   // ended while queued: never goes on the wire
        }
        bool stopped = riskGate_.isKilled() && !slot->reduceOnly;
        if (type == kNew) {
            if (slot->state != State::QUEUED) {
                return;
            }
            if (stopped) {
                finish(*slot);
                blocked_.fetch_add(1, std::memory_order_acq_rel);
                return;
            }
            slot->state = State::SENT;
        } else if (type == kReplace) {
            if (!slot->replacePending) {
                return;
            }
            if (stopped) {
                resolveReplace(*slot, false);
                return;
            }
        }
    }
    try {
        if (type == kNew) {
            delivered_.fetch_add(1, std::memory_order_acq_rel);
            const OrderVec* ls = listeners_.load(std::memory_order_acquire);
            if (ls != nullptr) {
                for (const OrderListener* l : *ls) {
                    (*l)(clOrdId, symbolId, side, quantity, price, timestampNanos);
                }
            }
        }
        const VenueVec* vs = venueListeners_.load(std::memory_order_acquire);
        if (vs != nullptr) {
            VenueMessage message{static_cast<VenueMessage::Type>(type), clOrdId, refId, symbolId,
                                 side, quantity, price, timestampNanos};
            for (const VenueListener* l : *vs) {
                (*l)(message);
            }
        }
    } catch (...) {
        // What the venue saw is now unknown: stop and let an operator look.
        listenerFailures_.fetch_add(1, std::memory_order_acq_rel);
        riskGate_.trip(HftRiskGate::KILL_GATEWAY);
    }
}

int HftOrderGateway::pollOnce() {
    std::uint64_t epoch = riskGate_.killEpoch();
    if (epoch != sweptEpoch_ || sweepPending_.exchange(false, std::memory_order_acq_rel)) {
        sweptEpoch_ = epoch;
        sweep(steadyNanos(), true);
    }
    if (deadManTimeout_.load(std::memory_order_relaxed) != 0) {
        checkDeadMan(steadyNanos());
    }
    return ring_.drainMessagesTo(
            [this](std::uint8_t type, std::int64_t clOrdId, std::int64_t refId, int symbolId,
                   Side side, std::int64_t quantity, double price, std::int64_t timestampNanos) {
                onDrained(type, clOrdId, refId, symbolId, side, quantity, price, timestampNanos);
            },
            256);
}

void HftOrderGateway::venueLoop() {
    while (true) {
        int n = pollOnce();
        if (n == 0) {
            if (!running_.load(std::memory_order_acquire) && ring_.isEmpty()
                    && !sweepPending_.load(std::memory_order_acquire)) {
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
