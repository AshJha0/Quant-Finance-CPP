// Port of com.quantfinlib.trading.AutoHedger (Java): live position-band
// auto-hedger on the fast lane -- watches the risk gate's live position on
// every tick and fires a flattening order through the HftOrderGateway the
// moment the band is breached.
//
//   |position| > positionBand  ->  submit opposite-side order for the excess
//                                   over the band (hedge back TO the band)
//
// Hedging to the band edge rather than to flat is deliberate: it removes
// the limit breach with the smallest order, avoids ping-ponging around
// zero, and matches how dealer books actually run inventory.
//
// Positions only move when fills confirm, so the hedger counts what it has
// in flight: one working hedge per symbol, whose unfilled quantity comes off
// the excess. A hedge that already covers the excess sends nothing new. If
// the excess changes, or the cooldown (minHedgeIntervalNanos) has passed and
// the price moved, the working hedge is amended in place rather than joined
// by a second one. Back inside the band, a working hedge is cancelled.
//
// Register onTick as a TickListener on the bus (consumer thread), after
// the strategy/quoter listeners so it sees the same tick they acted on.
// Zero allocation per tick.
#pragma once

#include <cstdint>
#include <limits>
#include <stdexcept>
#include <vector>

#include "quantfinlib/trading/hft_order_gateway.hpp"
#include "quantfinlib/trading/hft_risk_gate.hpp"

namespace quantfinlib {

class AutoHedger {
public:
    /**
     * @param positionBand          absolute position tolerated without hedging (> 0)
     * @param minHedgeIntervalNanos cooldown between hedges per symbol (>= 0)
     */
    AutoHedger(HftOrderGateway& gateway, int maxSymbols, std::int64_t positionBand,
              std::int64_t minHedgeIntervalNanos);

    /** The hot path: band check per tick; an order only on breach. */
    void onTick(int symbolId, double price, double size, std::int64_t timestampNanos);

    /** Hedge orders accepted onto the wire. */
    std::int64_t hedgesSubmitted() const { return hedgesSubmitted_; }

    /** Hedge attempts refused (risk gate or full ring) -- monitor this. */
    std::int64_t hedgesRejected() const { return hedgesRejected_; }

    /** Working hedges amended to a new size or price. */
    std::int64_t hedgesAmended() const { return hedgesAmended_; }

    /** Ticks where a breach was already covered by quantity in flight. */
    std::int64_t hedgesCovered() const { return hedgesCovered_; }

private:
    /** Sentinel: no hedge sent yet -- the cooldown must not apply. */
    static constexpr std::int64_t NEVER = std::numeric_limits<std::int64_t>::min();

    HftOrderGateway& gateway_;
    HftRiskGate& riskGate_;
    const std::int64_t positionBand_;
    const std::int64_t minHedgeIntervalNanos_;

    std::vector<std::int64_t> lastHedgeNanos_;   // NEVER until the first hedge
    std::vector<std::int64_t> hedgeOrder_;       // working hedge, 0 = none
    std::vector<Side> hedgeSide_;
    std::vector<double> hedgePrice_;

    std::int64_t hedgesSubmitted_ = 0;
    std::int64_t hedgesRejected_ = 0;
    std::int64_t hedgesAmended_ = 0;
    std::int64_t hedgesCovered_ = 0;
};

}  // namespace quantfinlib
