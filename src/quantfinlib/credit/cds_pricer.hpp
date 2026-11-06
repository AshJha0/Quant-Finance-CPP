// Port of com.quantfinlib.credit.CdsPricer (Java): CDS pricing off a
// CreditCurve — the two legs, the par spread, and the upfront that
// post-2009 standardized contracts actually exchange. Per unit notional:
//   riskyAnnuity  = sum dt * DF(t_i) * Q(t_i)  +  accrual-on-default term
//   premiumLegPv  = spread * riskyAnnuity
//   protectionPv  = (1 - R) * sum DF(t_i) * (Q(t_{i-1}) - Q(t_i))
//   parSpread     = protectionPv / riskyAnnuity     (zero-upfront spread)
//   upfront(S_c)  = protectionPv - S_c * riskyAnnuity
// Positive upfront means the protection buyer pays points up front. The
// risky annuity is also the desk's "risky DV01". Same quarterly
// discretization as the bootstrap, stated there, with a final short period to
// a maturity off the grid. Not the ISDA standard model.
#pragma once

#include "quantfinlib/credit/credit_curve.hpp"
#include "quantfinlib/rates/yield_curve.hpp"

namespace quantfinlib {
namespace cds_pricer {

/** PV of 1bp-per-year premium stream per unit spread (the risky annuity / risky DV01 base). */
double riskyAnnuity(const CreditCurve& credit, const YieldCurve& discount,
                    double maturityYears);

/** PV of the premium leg at the given running spread. */
double premiumLegPv(const CreditCurve& credit, const YieldCurve& discount,
                    double spread, double maturityYears);

/** PV of the protection leg: (1-R) paid at default. */
double protectionLegPv(const CreditCurve& credit, const YieldCurve& discount,
                       double maturityYears);

/** The zero-upfront (par) spread for this maturity. */
double parSpread(const CreditCurve& credit, const YieldCurve& discount,
                 double maturityYears);

/**
 * Upfront points (per unit notional) the protection BUYER pays on a
 * contract with fixed running coupon `contractSpread`.
 */
double upfront(const CreditCurve& credit, const YieldCurve& discount,
               double contractSpread, double maturityYears);

}  // namespace cds_pricer
}  // namespace quantfinlib
