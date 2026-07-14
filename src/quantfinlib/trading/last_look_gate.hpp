// Port of com.quantfinlib.trading.LastLookGate (Java): maker-side symmetric
// last-look price check -- the mechanism FX liquidity providers apply to
// incoming deal requests, implemented the way the FX Global Code
// (Principle 17) says it must be: symmetric. At the end of the hold window
// the quoted price is compared to the current fair price, and the request
// is rejected when the market has moved beyond the tolerance in EITHER
// direction -- protecting the maker from being picked off, without
// free-optioning the taker.
//
// This class is the decision arithmetic plus its disclosure statistics.
// The hold window itself belongs to the caller's timer/session machinery.
// Zero allocation, single writer.
#pragma once

#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>

namespace quantfinlib {

class LastLookGate {
public:
    /**
     * @param tolerance maximum |current fair - quoted| move, in price units
     *                  (e.g. 0.0001 = 1 pip on EURUSD), beyond which the
     *                  request is rejected -- in both directions
     */
    explicit LastLookGate(double tolerance) : tolerance_(tolerance) {
        if (!(tolerance > 0) || std::isnan(tolerance)) {
            throw std::invalid_argument("tolerance must be positive");
        }
    }

    /**
     * The decision at the end of the hold: accept iff the fair price is
     * still within tolerance of the quote. Symmetric -- the direction of
     * the move never changes the outcome, only the statistics.
     *
     * @param makerSells  true when the taker is buying (maker sells at the quote)
     * @param quotedPrice the price the maker showed
     * @param currentFair the maker's current fair value (e.g. composite mid)
     */
    bool accept(bool makerSells, double quotedPrice, double currentFair) {
        double move = currentFair - quotedPrice;
        if (std::fabs(move) <= tolerance_) {
            accepts_++;
            return true;
        }
        rejects_++;
        // Classification only (the decision is already made): a maker who
        // sells is hurt by a rising fair; falling fair means the reject
        // "protected" a taker who would have overpaid.
        bool hurtsMaker = makerSells ? move > 0 : move < 0;
        if (hurtsMaker) {
            makerProtectiveRejects_++;
        } else {
            takerProtectiveRejects_++;
        }
        return false;
    }

    std::int64_t accepts() const { return accepts_; }
    std::int64_t rejects() const { return rejects_; }

    /** Rejects where the move was against the maker (the classic pick-off). */
    std::int64_t makerProtectiveRejects() const { return makerProtectiveRejects_; }

    /**
     * Rejects where the move favored the maker -- a symmetric gate produces
     * these in roughly equal measure; their absence in an LP's disclosures
     * is the signature of asymmetric last look.
     */
    std::int64_t takerProtectiveRejects() const { return takerProtectiveRejects_; }

    /** Reject fraction of all decisions (NaN before any decision). */
    double rejectRate() const {
        std::int64_t total = accepts_ + rejects_;
        return total == 0 ? std::numeric_limits<double>::quiet_NaN()
                           : static_cast<double>(rejects_) / static_cast<double>(total);
    }

private:
    const double tolerance_;
    std::int64_t accepts_ = 0;
    std::int64_t rejects_ = 0;
    std::int64_t makerProtectiveRejects_ = 0;
    std::int64_t takerProtectiveRejects_ = 0;
};

}  // namespace quantfinlib
