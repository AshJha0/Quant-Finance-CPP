#include "quantfinlib/credit/cds_pricer.hpp"

#include <limits>
#include <stdexcept>
#include <string>

namespace quantfinlib {
namespace cds_pricer {

namespace {

void validate(double maturityYears) {
    if (!(maturityYears > 0)
            || maturityYears == std::numeric_limits<double>::infinity()) {
        throw std::invalid_argument("maturityYears must be positive and finite, got "
                                    + std::to_string(maturityYears));
    }
    // The legs are summed on the quarterly grid starting at gridStep();
    // a maturity below one grid step has NO coupon dates, so the annuity
    // and protection leg are both empty -- parSpread would be 0/0 = NaN.
    // Throw instead of leaking NaN (house rule).
    if (maturityYears < CreditCurve::gridStep()) {
        throw std::invalid_argument("maturityYears must be >= the pricing grid step "
                + std::to_string(CreditCurve::gridStep()) + ", got "
                + std::to_string(maturityYears));
    }
}

}  // namespace

double riskyAnnuity(const CreditCurve& credit, const YieldCurve& discount,
                    double maturityYears) {
    validate(maturityYears);
    double dt = CreditCurve::gridStep();
    double annuity = 0;
    double prevQ = 1;
    for (double t = dt; t <= maturityYears + 1e-12; t += dt) {
        double q = credit.survivalProbability(t);
        double df = discount.discountFactor(t);
        // Full period if it survives; half a period of accrual if it
        // defaults inside the period (the standard convention).
        annuity += dt * df * q + 0.5 * dt * df * (prevQ - q);
        prevQ = q;
    }
    return annuity;
}

double premiumLegPv(const CreditCurve& credit, const YieldCurve& discount,
                    double spread, double maturityYears) {
    if (!(spread > 0) || spread == std::numeric_limits<double>::infinity()) {
        throw std::invalid_argument("spread must be positive and finite, got "
                                    + std::to_string(spread));
    }
    return spread * riskyAnnuity(credit, discount, maturityYears);
}

double protectionLegPv(const CreditCurve& credit, const YieldCurve& discount,
                       double maturityYears) {
    validate(maturityYears);
    double dt = CreditCurve::gridStep();
    double pv = 0;
    double prevQ = 1;
    for (double t = dt; t <= maturityYears + 1e-12; t += dt) {
        double q = credit.survivalProbability(t);
        pv += discount.discountFactor(t) * (prevQ - q);
        prevQ = q;
    }
    return (1 - credit.recovery()) * pv;
}

double parSpread(const CreditCurve& credit, const YieldCurve& discount,
                 double maturityYears) {
    return protectionLegPv(credit, discount, maturityYears)
            / riskyAnnuity(credit, discount, maturityYears);
}

double upfront(const CreditCurve& credit, const YieldCurve& discount,
               double contractSpread, double maturityYears) {
    if (!(contractSpread > 0)
            || contractSpread == std::numeric_limits<double>::infinity()) {
        throw std::invalid_argument("contractSpread must be positive and finite, got "
                                    + std::to_string(contractSpread));
    }
    return protectionLegPv(credit, discount, maturityYears)
            - contractSpread * riskyAnnuity(credit, discount, maturityYears);
}

}  // namespace cds_pricer
}  // namespace quantfinlib
