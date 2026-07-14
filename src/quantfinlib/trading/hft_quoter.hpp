// Port of com.quantfinlib.trading.HftQuoter (Java): streaming two-sided
// quoter on the fast lane -- the market-making loop: tick in -> mid ->
// inventory skew -> tick-grid snap -> two orders out through the
// HftRiskGate and HftOrderGateway, with zero allocation per tick.
//
// Quote construction per tick (all primitive arithmetic):
//   skew = -position * skewPerUnit          (long inventory shades DOWN)
//   bid  = mid - halfSpread + skew           -> rounded DOWN to the grid
//   ask  = mid + halfSpread + skew           -> rounded UP   to the grid
//
// Inventory comes straight from the risk gate's position (updated by fills
// via HftRiskGate::onFill), so the skew loop closes without any extra
// state. Both sides still pass the gate's own checks -- a quoter cannot
// out-trade its risk limits.
//
// Conflation: venues throttle quote updates and stale requotes waste the
// wire: a re-quote is suppressed unless the mid moved at least minMove OR
// minRequoteIntervalNanos elapsed. Suppression is counted, never silent.
//
// Register onTick as a TickListener on the bus (runs on the consumer
// thread), exactly like a strategy. Single-threaded by construction; the
// per-symbol state arrays are indexed by dense symbol id.
//
// Mixed books: half-spreads, skews, conflation thresholds and tick grids
// are per-instrument quantities (a EURUSD half-spread is ~100x too tight
// for USDJPY), so the constructor's config is only the default -- override
// per symbol with configureSymbol and one quoter serves the whole book.
#pragma once

#include <cstdint>
#include <limits>
#include <memory>
#include <stdexcept>
#include <vector>

#include "quantfinlib/microstructure/tick_size_schedule.hpp"
#include "quantfinlib/trading/hft_order_gateway.hpp"
#include "quantfinlib/trading/hft_risk_gate.hpp"

namespace quantfinlib {

class HftQuoter {
public:
    /**
     * Quoting parameters. halfSpread/skewPerUnit/minMove are in price
     * terms; tickSchedule is optional grid snapping (nullptr = quote raw
     * prices).
     */
    struct Config {
        std::int64_t quoteSize;
        double halfSpread;
        double skewPerUnit;
        std::int64_t minRequoteIntervalNanos;
        double minMove;
        std::shared_ptr<const TickSizeSchedule> tickSchedule;

        Config(std::int64_t quoteSize_, double halfSpread_, double skewPerUnit_,
               std::int64_t minRequoteIntervalNanos_, double minMove_,
               std::shared_ptr<const TickSizeSchedule> tickSchedule_)
                : quoteSize(quoteSize_),
                  halfSpread(halfSpread_),
                  skewPerUnit(skewPerUnit_),
                  minRequoteIntervalNanos(minRequoteIntervalNanos_),
                  minMove(minMove_),
                  tickSchedule(std::move(tickSchedule_)) {
            if (quoteSize <= 0 || halfSpread <= 0 || skewPerUnit < 0
                    || minRequoteIntervalNanos < 0 || minMove < 0) {
                throw std::invalid_argument(
                        "quoteSize and halfSpread must be > 0; skew/conflation params >= 0");
            }
        }

        /** Minimal config: size and half-spread, no skew, no conflation, no grid. */
        static Config of(std::int64_t quoteSize, double halfSpread) {
            return Config(quoteSize, halfSpread, 0, 0, 0, nullptr);
        }

        Config withSkewPerUnit(double skew) const {
            return Config(quoteSize, halfSpread, skew, minRequoteIntervalNanos, minMove,
                          tickSchedule);
        }

        /**
         * Suppresses a re-quote only when the mid moved less than
         * minMovePrice AND the last quote is younger than minIntervalNanos
         * -- BOTH gates must pass to suppress.
         *
         * Pitfall: minIntervalNanos = 0 therefore disables conflation
         * entirely (nothing is ever "younger than 0 ns"), it does NOT mean
         * "suppress on move alone". For purely move-gated conflation --
         * the usual choice for derived crosses -- use withMinMove.
         */
        Config withConflation(std::int64_t minIntervalNanos, double minMovePrice) const {
            return Config(quoteSize, halfSpread, skewPerUnit, minIntervalNanos, minMovePrice,
                          tickSchedule);
        }

        /**
         * Purely move-gated conflation: re-quote only when the mid has
         * moved at least minMovePrice, regardless of age (the interval
         * gate is set effectively infinite).
         */
        Config withMinMove(double minMovePrice) const {
            return Config(quoteSize, halfSpread, skewPerUnit,
                          std::numeric_limits<std::int64_t>::max() / 4, minMovePrice,
                          tickSchedule);
        }

        Config withTickSchedule(std::shared_ptr<const TickSizeSchedule> schedule) const {
            return Config(quoteSize, halfSpread, skewPerUnit, minRequoteIntervalNanos, minMove,
                          std::move(schedule));
        }
    };

    HftQuoter(HftOrderGateway& gateway, int maxSymbols, const Config& defaults);

    /**
     * Per-symbol quoting parameters (cold path -- call at setup or on
     * config updates from the same thread that quotes, or before start).
     */
    HftQuoter& configureSymbol(int symbolId, const Config& config);

    /**
     * The hot path: one tick -> (possibly) one two-sided quote. Runs on
     * the bus consumer thread; no allocation, no locks, no boxing.
     */
    void onTick(int symbolId, double price, double size, std::int64_t timestampNanos);

    /** Quote updates actually sent (each is up to two orders). */
    std::int64_t quoteUpdates() const { return quoteUpdates_; }

    /** Updates suppressed by conflation. */
    std::int64_t suppressedUpdates() const { return suppressedUpdates_; }

    /** Individual sides refused by the risk gate or a full ring. */
    std::int64_t rejectedSides() const { return rejectedSides_; }

private:
    void apply(int symbolId, const Config& config);

    HftOrderGateway& gateway_;
    HftRiskGate& riskGate_;

    std::vector<std::int64_t> quoteSize_;
    std::vector<double> halfSpread_;
    std::vector<double> skewPerUnit_;
    std::vector<std::int64_t> minRequoteIntervalNanos_;
    std::vector<double> minMove_;
    std::vector<std::shared_ptr<const TickSizeSchedule>> grids_;

    std::vector<double> lastQuoteMid_;      // NaN = never quoted
    std::vector<std::int64_t> lastQuoteNanos_;

    std::int64_t quoteUpdates_ = 0;
    std::int64_t suppressedUpdates_ = 0;
    std::int64_t rejectedSides_ = 0;
};

}  // namespace quantfinlib
