#include "quantfinlib/pricing/autocallable.hpp"

#include <cmath>
#include <limits>
#include <stdexcept>

#include "quantfinlib/pricing/java_random.hpp"

namespace quantfinlib {

namespace {
constexpr double kInf = std::numeric_limits<double>::infinity();
}  // namespace

Autocallable::Autocallable(double notional,
                           const std::vector<double>& observationYears,
                           double autocallBarrier, double couponBarrier,
                           double knockInBarrier, double couponPerPeriod,
                           bool memoryCoupons)
    : notional_(notional),
      observationYears_(observationYears),
      autocallBarrier_(autocallBarrier),
      couponBarrier_(couponBarrier),
      knockInBarrier_(knockInBarrier),
      couponPerPeriod_(couponPerPeriod),
      memoryCoupons_(memoryCoupons) {
    // !(x > 0) rejects NaN as well as non-positives: a NaN term must fail
    // HERE, not surface as a NaN price later. The knock-in must sit at or
    // below the autocall (protection above the early-redemption trigger is
    // unreachable); its relation to the COUPON barrier is deliberately
    // free — structures place it on either side.
    if (!(notional > 0) || notional == kInf
            || observationYears.empty()
            || !(autocallBarrier > 0) || autocallBarrier == kInf
            || !(couponBarrier > 0) || !(knockInBarrier > 0)
            || couponBarrier > autocallBarrier || knockInBarrier > autocallBarrier
            || !(couponPerPeriod >= 0) || couponPerPeriod == kInf) {
        throw std::invalid_argument("invalid autocallable terms");
    }
    double prev = 0;
    for (double t : observationYears) {
        if (!(t > prev)) {
            throw std::invalid_argument(
                    "observation times must be strictly increasing and positive");
        }
        prev = t;
    }
}

double Autocallable::price(double spot, double initial, double vol, double rate,
                           double divYield, int paths, std::int64_t seed) const {
    if (!(spot > 0) || !(initial > 0) || paths < 1
            || spot == kInf || initial == kInf
            || !(vol >= 0) || vol == kInf
            || !std::isfinite(rate) || !std::isfinite(divYield)) {
        throw std::invalid_argument("invalid market inputs");
    }
    std::size_t n = observationYears_.size();
    std::vector<double> drift(n);
    std::vector<double> volStep(n);
    std::vector<double> discount(n);
    double prevT = 0;
    for (std::size_t i = 0; i < n; i++) {
        double dt = observationYears_[i] - prevT;
        drift[i] = (rate - divYield - 0.5 * vol * vol) * dt;
        volStep[i] = vol * std::sqrt(dt);
        discount[i] = std::exp(-rate * observationYears_[i]);
        prevT = observationYears_[i];
    }

    JavaRandom rnd(seed);
    std::vector<double> gaussians(n);
    double sum = 0;
    for (int p = 0; p < paths; p++) {
        for (std::size_t i = 0; i < n; i++) {
            gaussians[i] = rnd.nextGaussian();
        }
        sum += pathValue(spot, initial, drift, volStep, discount, gaussians, +1);
        sum += pathValue(spot, initial, drift, volStep, discount, gaussians, -1);
    }
    return sum / (2.0 * paths);
}

double Autocallable::pathValue(double spot, double initial,
                               const std::vector<double>& drift,
                               const std::vector<double>& volStep,
                               const std::vector<double>& discount,
                               const std::vector<double>& gaussians,
                               int sign) const {
    double s = spot;
    double value = 0;
    double missedCoupons = 0;
    double coupon = couponPerPeriod_ * notional_;
    std::size_t n = observationYears_.size();
    for (std::size_t i = 0; i < n; i++) {
        s *= std::exp(drift[i] + sign * volStep[i] * gaussians[i]);
        double level = s / initial;
        double df = discount[i];

        if (level >= autocallBarrier_) {
            // Early redemption: notional + this coupon + any memory.
            value += df * (notional_ + coupon + (memoryCoupons_ ? missedCoupons : 0));
            return value;
        }
        if (level >= couponBarrier_) {
            value += df * (coupon + (memoryCoupons_ ? missedCoupons : 0));
            missedCoupons = 0;
        } else {
            missedCoupons += coupon;
        }
        if (i == n - 1) {
            // Maturity without autocall: protected unless knocked in.
            value += df * (level >= knockInBarrier_ ? notional_ : notional_ * level);
        }
    }
    return value;
}

double Autocallable::notional() const {
    return notional_;
}

int Autocallable::observations() const {
    return static_cast<int>(observationYears_.size());
}

}  // namespace quantfinlib
