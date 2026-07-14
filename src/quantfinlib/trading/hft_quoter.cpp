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
          lastQuoteNanos_(static_cast<std::size_t>(maxSymbols)) {
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
    auto i = static_cast<std::size_t>(symbolId);

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

    // Two sides through the risk gate. A rejected side is counted and
    // skipped; the other side may still quote (one-sided market).
    std::int64_t sizePerSide = quoteSize_[i];
    if (gateway_.submit(symbolId, Side::BUY, sizePerSide, bid, timestampNanos) <= 0) {
        rejectedSides_++;
    }
    if (gateway_.submit(symbolId, Side::SELL, sizePerSide, ask, timestampNanos) <= 0) {
        rejectedSides_++;
    }
    quoteUpdates_++;
    lastQuoteMid_[i] = price;
    lastQuoteNanos_[i] = timestampNanos;
}

}  // namespace quantfinlib
