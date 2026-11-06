#include "quantfinlib/trading/hft_quoter.hpp"

#include <cmath>
#include <limits>

namespace quantfinlib {

HftQuoter::HftQuoter(HftOrderGateway& gateway, int maxSymbols, const Config& defaults)
        : gateway_(gateway),
          riskGate_(gateway.riskGate()),
          quoteSize_(static_cast<std::size_t>(maxSymbols)),
          halfSpread_(static_cast<std::size_t>(maxSymbols)),
          skewPerUnit_(static_cast<std::size_t>(maxSymbols)),
          minRequoteIntervalNanos_(static_cast<std::size_t>(maxSymbols)),
          minMove_(static_cast<std::size_t>(maxSymbols)),
          grids_(static_cast<std::size_t>(maxSymbols)),
          lastQuoteMid_(static_cast<std::size_t>(maxSymbols),
                        std::numeric_limits<double>::quiet_NaN()),
          lastQuoteNanos_(static_cast<std::size_t>(maxSymbols)),
          bidOrder_(static_cast<std::size_t>(maxSymbols)),
          askOrder_(static_cast<std::size_t>(maxSymbols)),
          bidQuoted_(static_cast<std::size_t>(maxSymbols)),
          askQuoted_(static_cast<std::size_t>(maxSymbols)) {
    for (int i = 0; i < maxSymbols; i++) {
        apply(i, defaults);
    }
}

HftQuoter& HftQuoter::configureSymbol(int symbolId, const Config& config) {
    apply(symbolId, config);
    return *this;
}

void HftQuoter::apply(int symbolId, const Config& config) {
    auto i = static_cast<std::size_t>(symbolId);
    quoteSize_[i] = config.quoteSize;
    halfSpread_[i] = config.halfSpread;
    skewPerUnit_[i] = config.skewPerUnit;
    minRequoteIntervalNanos_[i] = config.minRequoteIntervalNanos;
    minMove_[i] = config.minMove;
    grids_[i] = config.tickSchedule;
}

void HftQuoter::onTick(int symbolId, double price, double /*size*/,
                       std::int64_t timestampNanos) {
    if (symbolId < 0 || static_cast<std::size_t>(symbolId) >= quoteSize_.size()) {
        badTicks_++;
        return;
    }
    auto i = static_cast<std::size_t>(symbolId);
    if (!std::isfinite(price) || price <= 0) {
        badTicks_++;
        pullQuotes(symbolId, timestampNanos);
        return;
    }

    // Conflation: suppress unless the mid moved or the interval expired.
    double lastMid = lastQuoteMid_[i];
    if (!std::isnan(lastMid) && std::fabs(price - lastMid) < minMove_[i]
            && timestampNanos - lastQuoteNanos_[i] < minRequoteIntervalNanos_[i]) {
        suppressedUpdates_++;
        return;
    }

    // Inventory skew: long inventory shades both sides down to attract
    // buyers of our excess; short shades up. Spread width is preserved.
    std::int64_t position = riskGate_.position(symbolId);
    double skew = -static_cast<double>(position) * skewPerUnit_[i];
    double bid = price - halfSpread_[i] + skew;
    double ask = price + halfSpread_[i] + skew;

    // Grid snap toward passivity: bid down, ask up -- never through mid.
    const TickSizeSchedule* grid = grids_[i].get();
    if (grid != nullptr) {
        bid = grid->roundDownClamped(bid);
        ask = grid->roundUpClamped(ask);
    }

    // A rejected side is counted and skipped; the other side may still quote
    // (one-sided market). In a rising market the ask moves first, so the bid
    // never amends through our own stale ask; a falling market needs no swap
    // because the bid already moves first.
    std::int64_t sizePerSide = quoteSize_[i];
    bool askFirst = !std::isnan(lastMid) && price > lastMid;
    if (askFirst) {
        quoteSide(askOrder_[i], askQuoted_[i], symbolId, Side::SELL, sizePerSide, ask,
                  timestampNanos);
    }
    quoteSide(bidOrder_[i], bidQuoted_[i], symbolId, Side::BUY, sizePerSide, bid, timestampNanos);
    if (!askFirst) {
        quoteSide(askOrder_[i], askQuoted_[i], symbolId, Side::SELL, sizePerSide, ask,
                  timestampNanos);
    }
    quoteUpdates_++;
    lastQuoteMid_[i] = price;
    lastQuoteNanos_[i] = timestampNanos;
}

void HftQuoter::quoteSide(std::int64_t& order, double& quoted, int symbolId, Side side,
                          std::int64_t size, double price, std::int64_t timestampNanos) {
    if (order != 0 && gateway_.leavesQuantity(order) > 0) {
        if (price == quoted) {
            return;   // already working there
        }
        std::int64_t result = gateway_.replace(order, size, price, timestampNanos);
        if (result > 0) {
            quoted = price;
        } else if (result == -HftRiskGate::REJECT_ORDER_STATE) {
            pendingSides_++;
        } else {
            rejectedSides_++;
        }
        return;
    }
    std::int64_t id = gateway_.submit(symbolId, side, size, price, timestampNanos);
    if (id > 0) {
        order = id;
        quoted = price;
    } else {
        order = 0;
        rejectedSides_++;
    }
}

void HftQuoter::pullQuotes(int symbolId, std::int64_t timestampNanos) {
    auto i = static_cast<std::size_t>(symbolId);
    if (bidOrder_[i] != 0) {
        gateway_.cancel(bidOrder_[i], timestampNanos);
    }
    if (askOrder_[i] != 0) {
        gateway_.cancel(askOrder_[i], timestampNanos);
    }
    lastQuoteMid_[i] = std::numeric_limits<double>::quiet_NaN();
}

}  // namespace quantfinlib
