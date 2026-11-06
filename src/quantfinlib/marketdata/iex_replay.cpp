#include "quantfinlib/marketdata/iex_replay.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "quantfinlib/orderbook/side.hpp"

namespace quantfinlib {

namespace {

// A shadow ladder in whole cents up to $5,000. An order off it is counted and
// left out.
constexpr int kMaxCents = 500000;
constexpr int kShadowOrders = 1 << 20;
constexpr std::int64_t kNotionalOrder = 100;
constexpr std::uint8_t kSaleExtendedHours = 0x40;
constexpr std::uint8_t kSaleSinglePriceCross = 0x08;

struct Fill {
    std::int64_t maker;
    std::int64_t quantity;
};

void onFill(void* context, std::int64_t maker, std::int64_t, int, std::int64_t quantity,
            std::int64_t) {
    static_cast<std::vector<Fill>*>(context)->push_back({maker, quantity});
}

// The order system events must arrive in, from the DEEP specification.
int systemRank(char code) {
    switch (code) {
        case 'O': return 1;
        case 'S': return 2;
        case 'R': return 3;
        case 'M': return 4;
        case 'E': return 5;
        case 'C': return 6;
        default: return 0;
    }
}

}  // namespace

struct IexReplay::Shadow {
    HftOrderBook book{0, kMaxCents, kShadowOrders};
    std::unordered_map<std::int64_t, std::int64_t> ids;  // feed reference -> book id
    std::vector<Fill> fills;
};

struct IexReplay::Symbol {
    IexLevelBook deep;
    IexOrderBook plus;
    bool usesOrders = false;
    bool open = false;             // updates since the last finished event
    std::int64_t eventTs = -1;
    std::int64_t lastTs = 0;
    IexBbo bbo;                    // as of the last finished event
    IexBbo logged;
    bool tracked = false;
    std::unique_ptr<Shadow> shadow;
    QueuePositionEstimator queue;
    std::int64_t queuePrice = 0;
};

IexReplay::IexReplay(std::vector<std::string> tracked, std::FILE* bboLog) : bboLog_(bboLog) {
    for (const std::string& s : tracked) {
        tracked_.push_back(symbolKey(s));
    }
}

IexReplay::~IexReplay() = default;

std::uint64_t IexReplay::symbolKey(const std::string& symbol) {
    char padded[8];
    std::memset(padded, ' ', sizeof padded);
    std::memcpy(padded, symbol.data(), std::min(symbol.size(), sizeof padded));
    std::uint64_t key = 0;
    std::memcpy(&key, padded, sizeof key);
    return key;
}

IexReplay::Symbol& IexReplay::symbol(std::uint64_t key) {
    std::unique_ptr<Symbol>& slot = symbols_[key];
    if (!slot) {
        slot = std::make_unique<Symbol>();
        slot->tracked = std::find(tracked_.begin(), tracked_.end(), key) != tracked_.end();
    }
    return *slot;
}

const IexLevelBook& IexReplay::levels(const Symbol& s) const {
    return s.usesOrders ? s.plus.levels() : s.deep;
}

void IexReplay::finishEvent(Symbol& s, std::uint64_t key) {
    s.open = false;
    s.bbo = levels(s).bbo();
    ++checks_.events;
    if (s.bbo.crossed()) {
        ++checks_.crossedEvents;
    } else if (s.bbo.locked()) {
        ++checks_.lockedEvents;
    }
    if (!s.tracked) {
        return;
    }
    if (bboLog_ != nullptr && !(s.bbo == s.logged)) {
        BboRecord record{s.eventTs, key, s.bbo};
        std::fwrite(&record, sizeof record, 1, bboLog_);
        s.logged = s.bbo;
    }
    if (s.shadow) {
        ++checks_.hftBboCompared;
        const HftOrderBook& b = s.shadow->book;
        IexBbo got;
        if (b.bestBidTick() != HftOrderBook::NO_BID) {
            got.bid = static_cast<std::int64_t>(b.bestBidTick()) * 100;
            got.bidSize = b.bestBidSize();
        }
        if (b.bestAskTick() != HftOrderBook::NO_ASK) {
            got.ask = static_cast<std::int64_t>(b.bestAskTick()) * 100;
            got.askSize = b.bestAskSize();
        }
        if (!(got == s.bbo)) {
            ++checks_.hftBboMismatches;
        }
    }
    if (!s.usesOrders) {
        stepQueue(s);
    }
}

// A notional 100-share order joins the back of the best bid and is followed
// until it reaches the front or its level goes. The feed's level size never
// includes it, so the estimator is given displayed size plus the notional order.
void IexReplay::stepQueue(Symbol& s) {
    QueuePositionEstimator& q = s.queue;
    if (q.active()) {
        std::int64_t size = s.deep.size(true, s.queuePrice);
        if (size == 0) {
            ++checks_.queueLevelGone;
            q.close();
        } else {
            q.onLevelResize(static_cast<std::int64_t>(size) + kNotionalOrder);
            ++checks_.queueUpdates;
            double ahead = q.sharesAhead();
            double progress = q.queueProgress();
            if (!std::isfinite(ahead) || ahead < 0 || ahead > static_cast<double>(size) ||
                !(progress >= 0 && progress <= 1)) {
                ++checks_.queueInvariantFailures;
            }
            if (ahead <= 0) {
                ++checks_.queueDrained;
                q.close();
            }
        }
    }
    if (!q.active() && s.bbo.bid != 0) {
        q.join(static_cast<std::int64_t>(s.bbo.bidSize), kNotionalOrder);
        s.queuePrice = s.bbo.bid;
        ++checks_.queueJoins;
    }
}

void IexReplay::onPrint(const Symbol& s, const IexMessage& m) {
    ++checks_.prints;
    checks_.printShares += m.size;
    checks_.printIdSum += static_cast<std::uint64_t>(m.tradeId);
    if (s.bbo.bid == 0 || s.bbo.ask == 0) {
        ++checks_.printsOneSided;
    } else if (m.price >= s.bbo.bid && m.price <= s.bbo.ask) {
        ++checks_.printsInside;
    } else if (regularHours_ && (m.flags & (kSaleExtendedHours | kSaleSinglePriceCross)) == 0) {
        ++checks_.printsOutside;
    } else {
        ++checks_.printsOutsideOther;
    }
}

void IexReplay::shadowAdd(Symbol& s, std::int64_t ref, const IexOrderBook::Order& order) {
    if (order.price % 100 != 0 || order.price / 100 > kMaxCents) {
        ++checks_.hftNonCentSkipped;
        return;
    }
    std::int64_t id = s.shadow->book.submitPostOnly(order.buy ? Side::BUY : Side::SELL,
                                                    static_cast<int>(order.price / 100),
                                                    order.size, 0);
    ++checks_.hftOrders;
    if (id < 0) {
        ++checks_.hftRejects;
        return;
    }
    s.shadow->ids[ref] = id;
}

void IexReplay::shadowDrop(Symbol& s, std::int64_t ref) {
    auto it = s.shadow->ids.find(ref);
    if (it != s.shadow->ids.end()) {
        s.shadow->book.cancel(it->second);
        s.shadow->ids.erase(it);
    }
}

// Makes every shadow order match the feed again after a fill took the wrong one.
void IexReplay::shadowRepair(Symbol& s) {
    Shadow& h = *s.shadow;
    for (auto it = h.ids.begin(); it != h.ids.end();) {
        if (s.plus.find(it->first) == nullptr) {
            h.book.cancel(it->second);
            it = h.ids.erase(it);
        } else {
            ++it;
        }
    }
    for (const auto& [ref, order] : s.plus.orders()) {
        auto it = h.ids.find(ref);
        if (it != h.ids.end()) {
            if (h.book.openQuantity(it->second) == order.size) {
                continue;
            }
            h.book.cancel(it->second);
            h.ids.erase(it);
        }
        shadowAdd(s, ref, order);
    }
}

void IexReplay::shadowExecute(Symbol& s, std::int64_t ref, const IexOrderBook::Order& before,
                              std::uint32_t size) {
    Shadow& h = *s.shadow;
    auto it = h.ids.find(ref);
    if (it == h.ids.end()) {
        return;
    }
    int tick = static_cast<int>(before.price / 100);
    int touch = before.buy ? h.book.bestBidTick() : h.book.bestAskTick();
    if (touch != tick) {
        shadowRepair(s);
        return;
    }
    h.fills.clear();
    h.book.submitIoc(before.buy ? Side::SELL : Side::BUY, tick, size, 0);
    if (h.fills.size() == 1 && h.fills[0].maker == it->second) {
        ++checks_.hftExecAtFront;
        if (s.plus.find(ref) == nullptr) {
            h.ids.erase(it);
        }
    } else {
        ++checks_.hftExecNotAtFront;
        shadowRepair(s);
    }
}

void IexReplay::onExecuted(Symbol& s, const IexMessage& m) {
    const IexOrderBook::Order* found = s.plus.find(m.orderId);
    if (found == nullptr) {
        s.plus.execute(m.orderId, m.size);  // counts the unknown order
        return;
    }
    IexOrderBook::Order before = *found;
    IexBbo touch = s.plus.levels().bbo();
    if (before.price == (before.buy ? touch.bid : touch.ask)) {
        ++checks_.execAtTouch;
    } else {
        ++checks_.execBehindTouch;
    }
    if (before.price != m.price) {
        ++checks_.execPriceDiffers;
    }
    if (s.plus.execute(m.orderId, m.size) && s.shadow) {
        shadowExecute(s, m.orderId, before, m.size);
    }
}

void IexReplay::onMessage(const IexMessage& m) {
    if (m.type == 'S') {
        ++checks_.systemEvents;
        char code = static_cast<char>(m.flags);
        if (systemRank(code) == 0 || systemRank(code) <= systemRank(systemState_)) {
            ++checks_.systemOutOfOrder;
        }
        systemState_ = code;
        if (code == 'R') {
            regularHours_ = true;
        } else if (code == 'M') {
            regularHours_ = false;
        }
        return;
    }
    if (m.type == 'H') {
        ++checks_.tradingStatus;
    }
    bool deep = m.type == '8' || m.type == '5';
    bool orders = m.type == 'a' || m.type == 'M' || m.type == 'R' || m.type == 'L' ||
                  m.type == 'C';
    if (!deep && !orders && m.type != 'T' && m.type != 'B') {
        return;
    }
    Symbol& s = symbol(m.symbol);
    if (m.timestamp < s.lastTs) {
        ++checks_.timestampRegressions;
    } else {
        s.lastTs = m.timestamp;
    }
    if (orders && !s.usesOrders) {
        s.usesOrders = true;
        if (s.tracked) {
            s.shadow = std::make_unique<Shadow>();
            s.shadow->book.tradeSink(onFill, &s.shadow->fills);
        }
    }
    // DEEP+ has no event flag: a new timestamp closes the previous event.
    if (s.usesOrders && s.open && m.timestamp != s.eventTs) {
        finishEvent(s, m.symbol);
    }
    switch (m.type) {
        case '8': case '5':
            if (!s.deep.set(m.type == '8', m.price, m.size) && m.size == 0) {
                ++checks_.removeMissingLevel;
            }
            s.eventTs = m.timestamp;
            s.open = true;
            if ((m.flags & 1) != 0) {
                finishEvent(s, m.symbol);
            }
            return;
        case 'T':
            onPrint(s, m);
            if (s.tracked && !s.usesOrders && s.queue.active() && m.price == s.queuePrice) {
                s.queue.onTrade(static_cast<std::int64_t>(m.size));
            }
            return;
        case 'B':
            ++checks_.tradeBreaks;
            return;
        case 'a':
            if (s.plus.add(m.orderId, m.flags == '8', m.price, m.size) && s.shadow) {
                shadowAdd(s, m.orderId, *s.plus.find(m.orderId));
            }
            break;
        case 'M': {
            const IexOrderBook::Order* found = s.plus.find(m.orderId);
            bool same = found != nullptr && found->price == m.price && found->size == m.size;
            bool keep = (m.flags & 1) != 0;
            checks_.modifyKeptPriority += keep ? 1 : 0;
            if (s.plus.modify(m.orderId, m.price, m.size) && s.shadow && !same) {
                // HftOrderBook has no reduce in place, so a modelled modify goes to
                // the back even when the venue kept priority.
                checks_.hftPriorityLossModifies += keep ? 1 : 0;
                shadowDrop(s, m.orderId);
                shadowAdd(s, m.orderId, *s.plus.find(m.orderId));
            }
            break;
        }
        case 'R':
            if (s.plus.remove(m.orderId) && s.shadow) {
                shadowDrop(s, m.orderId);
            }
            break;
        case 'L':
            onExecuted(s, m);
            onPrint(s, m);
            break;
        default:  // 'C'
            ++checks_.clearBook;
            s.plus.clear();
            if (s.shadow) {
                shadowRepair(s);
            }
            break;
    }
    s.eventTs = m.timestamp;
    s.open = true;
}

void IexReplay::onGap(std::int64_t) {
    // Books are kept so the counters show the damage; any gap voids the book checks.
    ++gapsSeen_;
}

void IexReplay::finish() {
    for (auto& [key, s] : symbols_) {
        if (s->open) {
            if (s->usesOrders) {
                finishEvent(*s, key);
            } else {
                ++checks_.incompleteAtEnd;
            }
        }
        if (!levels(*s).empty()) {
            ++checks_.restingAtEnd;
        }
    }
}

IexOrderBook::Stats IexReplay::orderBookStats() const {
    IexOrderBook::Stats sum;
    for (const auto& [key, s] : symbols_) {
        const IexOrderBook::Stats& one = s->plus.stats();
        sum.unknownOrders += one.unknownOrders;
        sum.duplicateOrders += one.duplicateOrders;
        sum.oversizeExecutions += one.oversizeExecutions;
        sum.negativeLevels += one.negativeLevels;
        sum.refused += one.refused;
    }
    return sum;
}

}  // namespace quantfinlib
