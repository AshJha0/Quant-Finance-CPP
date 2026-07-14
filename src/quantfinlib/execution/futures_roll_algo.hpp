// Port of com.quantfinlib.execution.FuturesRollAlgo: the futures roll --
// move from the expiring front contract to the back over the roll window,
// following the LIQUIDITY MIGRATION rather than fighting it. The algo
// tracks a migration curve -- the cumulative fraction of open interest
// that has moved by each day -- and rolls in step:
// target(day) = round(position * curve[day]); due = target - rolled.
//
// The default curve is the classic roll S-shape (slow start, concentrated
// middle, complete before the final day's scramble). Each day's due
// quantity executes as a CALENDAR SPREAD -- sell front / buy back for a
// long -- which is exactly a SpreadExecutionAlgo with ratio 1 and the
// calendar spread's own legging cap. Deterministic, single-threaded,
// research/warm lane. IllegalArgumentException maps to
// std::invalid_argument.
#pragma once

#include <cstdint>
#include <vector>

namespace quantfinlib {

class FuturesRollAlgo {
public:
    /**
     * @param positionContracts contracts to roll (positive; direction is
     *                          the caller's ticket), > 0
     * @param cumulativeMigration cumulative fraction migrated by end of
     *                            each roll day: non-decreasing, in [0, 1]
     *                            (a zero early entry = nothing due yet),
     *                            final entry exactly 1
     */
    FuturesRollAlgo(std::int64_t positionContracts, const std::vector<double>& cumulativeMigration);

    /**
     * The classic S-curve over `days` roll days: slow start, concentrated
     * middle, fully complete at the end -- smoothstep 3x^2 - 2x^3 sampled
     * at each day's close.
     */
    static std::vector<double> defaultMigration(int days);

    /**
     * Contracts due on `day` (0-based): the migration target minus what
     * has already rolled. Falling behind earlier days simply makes later
     * days' due larger -- the roll always catches up to the curve.
     */
    std::int64_t dueOnDay(int day) const;

    /** Records rolled contracts (calendar-spread fills). */
    void onRolled(std::int64_t contracts);

    std::int64_t rolled() const { return rolled_; }
    std::int64_t remaining() const { return position_ - rolled_; }
    bool done() const { return rolled_ == position_; }
    int rollDays() const { return static_cast<int>(cumulativeCurve_.size()); }

private:
    std::int64_t position_;
    std::vector<double> cumulativeCurve_;
    std::int64_t rolled_ = 0;
};

}  // namespace quantfinlib
