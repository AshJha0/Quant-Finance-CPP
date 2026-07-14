// Port of com.quantfinlib.pricing.StructuredNotes (Java): STRUCTURED
// NOTES priced by DECOMPOSITION into vanilla pieces. Reverse convertible
// = bond + fat coupon - (par/K) puts. Capital-protected note = protected
// floor + participation of the ATM call upside (participationFor inverts
// for the rate a budget affords). Discount certificate = the covered
// call, S*e^{-qT} - call(cap). Deltas come from the same decompositions.
// Values are per note of face par (certificate: per unit of underlying).
#pragma once

namespace quantfinlib {
namespace structured_notes {

/**
 * Fair value of a vanilla reverse convertible of face par. couponRate is
 * the total coupon for the LIFE of the note (0.09 = 9% paid at maturity);
 * shares delivered are worth S_T/K * par when S_T < K.
 */
double reverseConvertible(double par, double couponRate,
                          double spot, double strike, double rate,
                          double carry, double vol, double timeYears);

/** Delta of the reverse convertible: short put makes the holder LONG the stock. */
double reverseConvertibleDelta(double par, double spot, double strike,
                               double rate, double carry, double vol,
                               double timeYears);

/**
 * Fair value of a capital-protected note: protection of par floored, plus
 * participation of the underlying's upside from spot.
 */
double capitalProtectedNote(double par, double protection, double participation,
                            double spot, double rate, double carry,
                            double vol, double timeYears);

/**
 * The participation rate a given ISSUE PRICE affords:
 * (issuePrice - protection * par * DF) / ((par/S0) * call).
 */
double participationFor(double par, double protection, double issuePrice,
                        double spot, double rate, double carry,
                        double vol, double timeYears);

/**
 * Fair value per unit of underlying of a discount certificate capped at
 * cap: the covered call S e^{-qT} - call(cap).
 */
double discountCertificate(double spot, double cap, double rate,
                           double carry, double vol, double timeYears);

/** Delta of the discount certificate: long stock, short call. */
double discountCertificateDelta(double spot, double cap, double rate,
                                double carry, double vol, double timeYears);

}  // namespace structured_notes
}  // namespace quantfinlib
