#include "quantfinlib/pricing/barrier_option.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>

#include "quantfinlib/util/math_utils.hpp"

namespace quantfinlib {
namespace barrier_option {

namespace {

double n(double x) {
    return math_utils::normCdf(x);
}

// (H/S)^p * N(x), safe when the power overflows as N underflows at low vol.
double powN(double logHs, double p, double x) {
    return math_utils::expTimesNormCdf(p * logHs, x);
}

void requireAlive(double spot, double barrier, bool down) {
    if (!(spot > 0) || !(barrier > 0)) {
        throw std::invalid_argument("spot, strike, barrier must be > 0");
    }
    if (down ? barrier >= spot : barrier <= spot) {
        throw std::invalid_argument(std::string(down ? "down" : "up") + " barrier "
                                    + std::to_string(barrier) + " already breached at spot "
                                    + std::to_string(spot));
    }
}

}  // namespace

double price(OptionType type, Direction direction, Knock knock, double spot, double strike,
             double barrier, double rebate, double rate, double carry, double vol,
             double timeYears) {
    if (!(spot > 0) || !(strike > 0) || !(barrier > 0) || !std::isfinite(spot)
            || !std::isfinite(strike) || !std::isfinite(barrier)) {
        throw std::invalid_argument("spot, strike, barrier must be positive and finite");
    }
    if (!(rebate >= 0) || !std::isfinite(rebate) || !std::isfinite(rate)
            || !std::isfinite(carry) || !(vol >= 0) || !std::isfinite(vol)
            || std::isnan(timeYears)) {
        throw std::invalid_argument("invalid rebate, rate, carry or vol");
    }
    bool down = direction == Direction::DOWN;
    bool in = knock == Knock::IN;
    bool breached = down ? spot <= barrier : spot >= barrier;
    if (breached) {
        return in ? black_scholes::price(type, spot, strike, rate, carry, vol, timeYears)
                  : rebate;
    }
    if (timeYears <= 0) {
        return in ? rebate : black_scholes::intrinsic(type, spot, strike);
    }
    if (vol <= 0) {
        throw std::invalid_argument("vol must be > 0 before expiry");
    }
    double t = timeYears;
    double b = rate - carry;
    double sig2 = vol * vol;
    double sq = vol * std::sqrt(t);
    double mu = (b - 0.5 * sig2) / sig2;
    double phi = sign(type);
    double eta = down ? 1 : -1;
    double logHs = std::log(barrier / spot);
    double x1 = std::log(spot / strike) / sq + (1 + mu) * sq;
    double x2 = -logHs / sq + (1 + mu) * sq;
    double y1 = std::log(barrier * barrier / (spot * strike)) / sq + (1 + mu) * sq;
    double y2 = logHs / sq + (1 + mu) * sq;
    double sFwd = spot * std::exp((b - rate) * t);
    double kDf = strike * std::exp(-rate * t);
    double a = phi * sFwd * n(phi * x1) - phi * kDf * n(phi * x1 - phi * sq);
    double bb = phi * sFwd * n(phi * x2) - phi * kDf * n(phi * x2 - phi * sq);
    double c = phi * sFwd * powN(logHs, 2 * (mu + 1), eta * y1)
            - phi * kDf * powN(logHs, 2 * mu, eta * y1 - eta * sq);
    double d = phi * sFwd * powN(logHs, 2 * (mu + 1), eta * y2)
            - phi * kDf * powN(logHs, 2 * mu, eta * y2 - eta * sq);
    double e = 0;
    double f = 0;
    if (rebate != 0) {
        double disc = mu * mu + 2 * rate / sig2;
        if (disc < 0) {
            throw std::invalid_argument(
                    "mu^2 + 2 rate / vol^2 < 0: no closed-form rebate at this negative rate");
        }
        double lam = std::sqrt(disc);
        double z = logHs / sq + lam * sq;
        e = rebate * std::exp(-rate * t)
                * (n(eta * x2 - eta * sq) - powN(logHs, 2 * mu, eta * y2 - eta * sq));
        f = rebate * (powN(logHs, mu + lam, eta * z)
                      + powN(logHs, mu - lam, eta * z - 2 * eta * lam * sq));
    }
    bool call = type == OptionType::CALL;
    bool xAboveH = strike > barrier;
    double v;
    if (in) {
        if (call && down) {
            v = xAboveH ? c + e : a - bb + d + e;
        } else if (call) {
            v = xAboveH ? a + e : bb - c + d + e;
        } else if (down) {
            v = xAboveH ? bb - c + d + e : a + e;
        } else {
            v = xAboveH ? a - bb + d + e : c + e;
        }
    } else {
        if (call && down) {
            v = xAboveH ? a - c + f : bb - d + f;
        } else if (call) {
            v = xAboveH ? f : a - bb + c - d + f;
        } else if (down) {
            v = xAboveH ? a - bb + c - d + f : f;
        } else {
            v = xAboveH ? bb - d + f : a - c + f;
        }
    }
    // Cancellation in the reverse cases can leave a few ulps below zero.
    return std::max(v, 0.0);
}

double downAndInCall(double spot, double strike, double barrier,
                     double rate, double carry, double vol, double timeYears) {
    requireAlive(spot, barrier, true);
    return price(OptionType::CALL, Direction::DOWN, Knock::IN, spot, strike, barrier, 0, rate,
                 carry, vol, timeYears);
}

double downAndOutCall(double spot, double strike, double barrier,
                      double rate, double carry, double vol, double timeYears) {
    requireAlive(spot, barrier, true);
    return price(OptionType::CALL, Direction::DOWN, Knock::OUT, spot, strike, barrier, 0, rate,
                 carry, vol, timeYears);
}

double upAndInPut(double spot, double strike, double barrier,
                  double rate, double carry, double vol, double timeYears) {
    requireAlive(spot, barrier, false);
    return price(OptionType::PUT, Direction::UP, Knock::IN, spot, strike, barrier, 0, rate,
                 carry, vol, timeYears);
}

double upAndOutPut(double spot, double strike, double barrier,
                   double rate, double carry, double vol, double timeYears) {
    requireAlive(spot, barrier, false);
    return price(OptionType::PUT, Direction::UP, Knock::OUT, spot, strike, barrier, 0, rate,
                 carry, vol, timeYears);
}

}  // namespace barrier_option
}  // namespace quantfinlib
