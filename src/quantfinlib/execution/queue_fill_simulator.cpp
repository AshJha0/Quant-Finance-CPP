#include "quantfinlib/execution/queue_fill_simulator.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace quantfinlib {

namespace {

bool usablePrice(double p) { return std::isfinite(p) && p > 0; }

/** SplitMix64 (Steele, Lea, Flood 2014): integer only, so identical on every platform. */
std::uint64_t splitMix(std::uint64_t& state) {
    std::uint64_t z = (state += 0x9E3779B97F4A7C15ULL);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

double bps(double amount, double notional) {
    return notional > 0 ? amount / notional * 1e4 : std::nan("");
}

}  // namespace

double QueueFillSimulator::MarkoutBucket::markoutBps() const { return bps(markout, notional); }

double QueueFillSimulator::MarkoutBucket::adverseMoveBps() const { return bps(adverseMove, notional); }

QueueFillSimulator::QueueFillSimulator(const Config& config) : config_(config), rngState_(config.seed) {
    if (config.orderLatencyNanos < 0 || config.orderLatencyJitterNanos < 0
        || config.marketDataLatencyNanos < 0 || config.markoutHorizonNanos <= 0
        || config.depthRecoveryNanos < 0) {
        throw std::invalid_argument("invalid fill simulator config");
    }
}

std::int64_t QueueFillSimulator::latency() {
    std::int64_t jitter = 0;
    if (config_.orderLatencyJitterNanos > 0) {
        auto span = static_cast<std::uint64_t>(config_.orderLatencyJitterNanos) + 1;
        jitter = static_cast<std::int64_t>(splitMix(rngState_) % span);
    }
    return config_.orderLatencyNanos + jitter;
}

void QueueFillSimulator::advanceTo(std::int64_t nowNanos) {
    if (nowNanos < clock_) {
        throw std::invalid_argument("simulator time cannot go back");
    }
    deliverDue(nowNanos);
    clock_ = nowNanos;
}

void QueueFillSimulator::deliverDue(std::int64_t upToNanos) {
    while (!pending_.empty() && pending_.begin()->first.first <= upToNanos) {
        std::int64_t nanos = pending_.begin()->first.first;
        Pending p = pending_.begin()->second;
        pending_.erase(pending_.begin());
        auto it = orders_.find(p.orderId);
        if (it == orders_.end()) {
            continue;  // finished while this message was in flight
        }
        if (p.isCancel) {
            finishCancelled(it->second, nanos);
        } else {
            arrive(it->second, nanos);
        }
        eraseFinished();
    }
}

void QueueFillSimulator::eraseFinished() {
    for (auto it = orders_.begin(); it != orders_.end();) {
        if (it->second.remaining == 0) {
            arrivalOf_.erase(it->first);
            it = orders_.erase(it);
        } else {
            ++it;
        }
    }
}

std::int64_t QueueFillSimulator::displayedLeft(Side takerSide, std::int64_t nanos) {
    if (!book_) {
        return 0;
    }
    bool buy = takerSide == Side::BUY;
    Taken& taken = buy ? takenAsk_ : takenBid_;
    double touch = buy ? book_->ask : book_->bid;
    if (taken.price != touch || nanos - taken.lastNanos >= config_.depthRecoveryNanos) {
        taken = Taken{touch, 0, nanos};
    }
    return std::max<std::int64_t>(0, (buy ? book_->askSize : book_->bidSize) - taken.quantity);
}

void QueueFillSimulator::noteTaken(Side takerSide, std::int64_t quantity, std::int64_t nanos) {
    Taken& taken = takerSide == Side::BUY ? takenAsk_ : takenBid_;
    taken.quantity += quantity;
    taken.lastNanos = nanos;
}

void QueueFillSimulator::takeDisplayed(Order& order, double fillPrice, std::int64_t nanos, bool passive) {
    std::int64_t quantity = std::min(order.remaining, displayedLeft(order.side, nanos));
    if (quantity > 0) {
        noteTaken(order.side, quantity, nanos);
        order.queue.onOwnFill(quantity);
        recordFill(order, fillPrice, quantity, nanos, passive);
    }
}

void QueueFillSimulator::arrive(Order& order, std::int64_t nanos) {
    order.arrived = true;
    bool market = std::isnan(order.limit);
    if (book_) {
        double touch = order.side == Side::BUY ? book_->ask : book_->bid;
        bool crosses = market || (order.side == Side::BUY ? touch <= order.limit : touch >= order.limit);
        if (crosses) {
            takeDisplayed(order, touch, nanos, false);
        }
    }
    if (order.remaining == 0) {
        return;
    }
    if (market) {
        finishCancelled(order, nanos);
    } else {
        updateQueue(order);
    }
}

std::int64_t QueueFillSimulator::ownSizeAt(Side side, double price, std::int64_t exceptId) const {
    std::int64_t total = 0;
    for (const auto& [id, o] : orders_) {
        if (id != exceptId && o.arrived && o.side == side && o.limit == price) {
            total += o.remaining;
        }
    }
    return total;
}

void QueueFillSimulator::updateQueue(Order& order) {
    if (!book_ || !order.arrived || std::isnan(order.limit)) {
        return;
    }
    bool buy = order.side == Side::BUY;
    double touch = buy ? book_->bid : book_->ask;
    bool behindTouch = buy ? touch > order.limit : touch < order.limit;
    if (behindTouch) {
        return;  // depth at this price is not on the tape; the queue waits
    }
    std::int64_t displayed = touch == order.limit ? (buy ? book_->bidSize : book_->askSize) : 0;
    if (order.queue.active()) {
        order.queue.onLevelResize(displayed + ownSizeAt(order.side, order.limit, -1));
    } else {
        // Our earlier orders at this price are ahead of this one.
        std::int64_t ownAhead = 0;
        for (const auto& [id, o] : orders_) {
            if (id < order.id && o.arrived && o.side == order.side && o.limit == order.limit) {
                ownAhead += o.remaining;
            }
        }
        order.queue.join(displayed + ownAhead, order.remaining);
    }
}

void QueueFillSimulator::sweepCrossed(std::int64_t nanos) {
    for (auto& [id, o] : orders_) {
        if (!o.arrived || std::isnan(o.limit)) {
            continue;
        }
        bool crossed = o.side == Side::BUY ? book_->ask <= o.limit : book_->bid >= o.limit;
        if (crossed) {
            // The level was run over, so nothing is ahead of us any more.
            o.queue.clearAhead();
            takeDisplayed(o, o.limit, nanos, true);
        }
        if (o.remaining > 0) {
            updateQueue(o);
        }
    }
}

void QueueFillSimulator::onBook(std::int64_t nanos, double bid, std::int64_t bidSize, double ask,
                                std::int64_t askSize) {
    if (!usablePrice(bid) || !usablePrice(ask) || bid > ask || bidSize < 0 || askSize < 0) {
        throw std::invalid_argument("invalid book update");
    }
    advanceTo(nanos);
    book_ = Book{nanos, bid, bidSize, ask, askSize};
    history_.push_back(*book_);
    while (history_.size() >= 2 && history_[1].timestampNanos <= nanos - config_.marketDataLatencyNanos) {
        history_.pop_front();
    }
    sweepCrossed(nanos);
    eraseFinished();
    resolveMarks(nanos);
}

void QueueFillSimulator::onTrade(std::int64_t nanos, double price, std::int64_t quantity) {
    advanceTo(nanos);
    if (!book_) {
        return;
    }
    // Quote rule (Lee and Ready 1991) without the tick test: a print at the
    // mid says nothing about who was hit.
    double mid = book_->mid();
    if (price < mid || (price == mid && price <= book_->bid)) {
        onTrade(nanos, price, quantity, Side::SELL);
    } else if (price > mid) {
        onTrade(nanos, price, quantity, Side::BUY);
    }
}

void QueueFillSimulator::onTrade(std::int64_t nanos, double price, std::int64_t quantity, Side aggressor) {
    if (!usablePrice(price) || quantity <= 0) {
        throw std::invalid_argument("invalid trade");
    }
    advanceTo(nanos);
    Side restingSide = opposite(aggressor);
    bool buy = restingSide == Side::BUY;
    std::vector<Order*> eligible;
    for (auto& [id, o] : orders_) {
        bool reached = buy ? price <= o.limit : price >= o.limit;  // false for a market order
        if (o.arrived && o.side == restingSide && reached) {
            eligible.push_back(&o);
        }
    }
    // Price priority, then time: stable sort on a list already in id order.
    std::stable_sort(eligible.begin(), eligible.end(), [buy](const Order* a, const Order* b) {
        return buy ? a->limit > b->limit : a->limit < b->limit;
    });
    std::int64_t budget = quantity;
    for (Order* o : eligible) {
        bool through = o->limit != price;
        if (!o->queue.active()) {
            if (!through) {
                continue;  // a print at a price we hold no place at
            }
            o->queue.join(0, o->remaining);
        }
        if (through) {
            o->queue.clearAhead();
        }
        auto reach = static_cast<std::int64_t>(
                std::floor(static_cast<double>(quantity) - o->queue.sharesAhead()));
        std::int64_t fill = std::max<std::int64_t>(0, std::min({o->remaining, reach, budget}));
        o->queue.onTrade(quantity - fill);
        if (fill > 0) {
            budget -= fill;
            o->queue.onOwnFill(fill);
            recordFill(*o, o->limit, fill, nanos, true);
        }
    }
    eraseFinished();
}

void QueueFillSimulator::recordFill(Order& order, double price, std::int64_t quantity, std::int64_t nanos,
                                    bool passive) {
    order.remaining -= quantity;
    Report r;
    r.type = ReportType::FILL;
    r.orderId = order.id;
    r.side = order.side;
    r.price = price;
    r.quantity = quantity;
    r.exchangeNanos = nanos;
    r.reportNanos = nanos + config_.orderLatencyNanos;
    r.passive = passive;
    reports_.push_back(r);
    marks_.push_back(OpenMark{nanos + config_.markoutHorizonNanos, order.side, price, book_ ? book_->mid() : price,
                              quantity, passive});
    adverse_.unmarkedQuantity += quantity;
}

void QueueFillSimulator::finishCancelled(Order& order, std::int64_t nanos) {
    Report r;
    r.type = ReportType::CANCELLED;
    r.orderId = order.id;
    r.side = order.side;
    r.quantity = order.remaining;
    r.exchangeNanos = nanos;
    r.reportNanos = nanos + config_.orderLatencyNanos;
    reports_.push_back(r);
    order.remaining = 0;
    order.queue.close();
}

void QueueFillSimulator::resolveMarks(std::int64_t nanos) {
    double mid = book_->mid();
    while (!marks_.empty() && marks_.front().dueNanos <= nanos) {
        const OpenMark& m = marks_.front();
        MarkoutBucket& bucket = m.passive ? adverse_.passive : adverse_.aggressive;
        double qty = static_cast<double>(m.quantity);
        int sgn = sign(m.side);
        bucket.quantity += m.quantity;
        bucket.notional += m.price * qty;
        bucket.markout += sgn * (mid - m.price) * qty;
        bucket.adverseMove += sgn * (mid - m.midAtFill) * qty;
        adverse_.unmarkedQuantity -= m.quantity;
        marks_.pop_front();
    }
}

std::int64_t QueueFillSimulator::send(std::int64_t nowNanos, Side side, std::int64_t quantity, double limit) {
    if (quantity <= 0) {
        throw std::invalid_argument("quantity must be > 0");
    }
    advanceTo(nowNanos);
    std::int64_t id = nextId_++;
    Order order;
    order.id = id;
    order.side = side;
    order.limit = limit;
    order.remaining = quantity;
    orders_.emplace(id, std::move(order));
    std::int64_t arrival = nowNanos + latency();
    arrivalOf_[id] = arrival;
    pending_.emplace(std::make_pair(arrival, nextSeq_++), Pending{false, id});
    deliverDue(nowNanos);
    return id;
}

std::int64_t QueueFillSimulator::submitLimit(std::int64_t nowNanos, Side side, std::int64_t quantity,
                                             double limitPrice) {
    if (!usablePrice(limitPrice)) {
        throw std::invalid_argument("limit price must be finite and > 0");
    }
    return send(nowNanos, side, quantity, limitPrice);
}

std::int64_t QueueFillSimulator::submitMarket(std::int64_t nowNanos, Side side, std::int64_t quantity) {
    return send(nowNanos, side, quantity, std::numeric_limits<double>::quiet_NaN());
}

bool QueueFillSimulator::cancel(std::int64_t nowNanos, std::int64_t orderId) {
    advanceTo(nowNanos);
    if (orders_.find(orderId) == orders_.end()) {
        return false;
    }
    // Jitter must not let a cancel overtake its own order.
    std::int64_t arrival = std::max(nowNanos + latency(), arrivalOf_.at(orderId));
    pending_.emplace(std::make_pair(arrival, nextSeq_++), Pending{true, orderId});
    deliverDue(nowNanos);
    return true;
}

std::vector<QueueFillSimulator::Report> QueueFillSimulator::drainReports(std::int64_t nowNanos) {
    std::vector<Report> out;
    while (!reports_.empty() && reports_.front().reportNanos <= nowNanos) {
        out.push_back(reports_.front());
        reports_.pop_front();
    }
    return out;
}

std::optional<QueueFillSimulator::Book> QueueFillSimulator::visibleBook(std::int64_t nowNanos) const {
    std::int64_t cutoff = nowNanos - config_.marketDataLatencyNanos;
    for (auto it = history_.rbegin(); it != history_.rend(); ++it) {
        if (it->timestampNanos <= cutoff) {
            return *it;
        }
    }
    return std::nullopt;
}

double QueueFillSimulator::sharesAhead(std::int64_t orderId) const {
    auto it = orders_.find(orderId);
    return it != orders_.end() && it->second.queue.active() ? it->second.queue.sharesAhead() : std::nan("");
}

}  // namespace quantfinlib
