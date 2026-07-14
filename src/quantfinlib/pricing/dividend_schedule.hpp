// Port of com.quantfinlib.pricing.DividendSchedule (Java): discrete
// (cash) dividends for equity derivatives — the escrowed dividend model.
// The PV of all dividends with ex-dates before expiry is stripped from
// spot and the remainder diffuses lognormally:
//   S* = S - sum d_i e^{-r t_i} (t_i <= T),  F = S* e^{(r - borrow) T}.
// Borrow cost enters exactly like a continuous yield. Instances are
// immutable; all queries allocation-free.
#pragma once

#include <vector>

#include "quantfinlib/pricing/black_scholes.hpp"

namespace quantfinlib {

class DividendSchedule {
public:
    /** Empty schedule (no dividends): forwards collapse to the yield-free case. */
    static const DividendSchedule& NONE();

    /**
     * @param exTimesYears ex-dividend times in years from valuation, ascending
     * @param amounts      cash amounts per share, aligned with the times
     */
    static DividendSchedule of(const std::vector<double>& exTimesYears,
                               const std::vector<double>& amounts);

    /** Present value of all dividends with ex-dates on or before horizonYears. */
    double presentValue(double rate, double horizonYears) const;

    /** Escrowed spot: what actually diffuses once dividend PV is stripped. */
    double adjustedSpot(double spot, double rate, double horizonYears) const;

    /** Equity forward with discrete dividends and a continuous borrow fee. */
    double forward(double spot, double rate, double borrow, double horizonYears) const;

    /**
     * European price under the escrowed model: Black-Scholes on the adjusted
     * spot, with borrow as the carry. With no dividends and no borrow this
     * is exactly the plain Black-Scholes price.
     */
    double europeanPrice(OptionType type, double spot, double strike, double rate,
                         double borrow, double vol, double timeYears) const;

    int count() const;

private:
    DividendSchedule(std::vector<double> exTimes, std::vector<double> amounts);

    std::vector<double> exTimes_;  // years from valuation, ascending
    std::vector<double> amounts_;  // cash per share
};

}  // namespace quantfinlib
