#include "quantfinlib/pricing/heston.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <stdexcept>

#include "quantfinlib/pricing/java_random.hpp"

namespace quantfinlib {
namespace heston {

namespace {

constexpr double kInf = std::numeric_limits<double>::infinity();

constexpr int BASE_INTEGRATION_STEPS = 4'096;  // Simpson: must be even
// Window sized for the 20%-vol / 1y reference; the integrand's decay
// scale is ~1/(sigma_eff*sqrtT), NOT parameter-free, so probability()
// stretches both the window and the step count for short-dated / low-vol
// inputs (a fixed 200 silently truncates a 1-week 4%-vol option's
// integral).
constexpr double BASE_INTEGRATION_LIMIT = 200;
constexpr double REFERENCE_SIGMA_SQRT_T = 0.2;
constexpr double MAX_STRETCH = 64;

void requireMarketInputs(double spot, double strike, double rate,
                         double divYield, double timeYears) {
    if (!(spot > 0) || !(strike > 0) || !(timeYears > 0)
            || spot == kInf || strike == kInf
            || !std::isfinite(rate) || !std::isfinite(divYield)
            || timeYears == kInf) {
        throw std::invalid_argument("invalid market inputs");
    }
}

/** Re{e^{iu x} phi(u)/(iu)} with the little-trap phi. All complex math inlined. */
double integrand(double u, double x, double t, const Params& p, bool first) {
    double kappa = p.kappa;
    double theta = p.theta;
    double sv = p.sigmaV;
    double rho = p.rho;
    double v0 = p.v0;
    // u' and b depend on which probability we are computing.
    double uSign = first ? 0.5 : -0.5;
    double b = first ? kappa - rho * sv : kappa;

    // d^2 = (b - i rho sigma u)^2 + sigma^2 (u^2 - 2i uSign u)
    double reA = b;
    double imA = -rho * sv * u;                       // (b - i rho sigma u)
    double reA2 = reA * reA - imA * imA;
    double imA2 = 2 * reA * imA;
    double reB = sv * sv * (u * u);                   // sigma^2 u^2 (real part add)
    double imB = sv * sv * (-2 * uSign * u);          // sigma^2 (-2i uSign u)
    double reD2 = reA2 + reB;
    double imD2 = imA2 + imB;
    // Complex sqrt of (reD2 + i imD2), principal branch — STABLE form:
    // reD2 = b^2 + sigma^2 u^2 (1 - rho^2) >= 0 always, so the real part is
    // the large component; the imaginary part MUST come from imD2/(2 reD),
    // not from sqrt((mod - re)/2), which underflows to 0 when
    // |imD2| << reD2 and silently zeroes the phase slope near u = 0 (a
    // first-node corruption worth ~0.5% of an ATM price — caught by the
    // BS-limit test).
    double modD2 = std::hypot(reD2, imD2);
    double reD = std::sqrt((modD2 + reD2) / 2);
    double imD = imD2 / (2 * reD);

    // g2 = (b - i rho sigma u - d)/(b - i rho sigma u + d) — the LITTLE-TRAP ratio
    double reNum = reA - reD;
    double imNum = imA - imD;
    double reDen = reA + reD;
    double imDen = imA + imD;
    double den2 = reDen * reDen + imDen * imDen;
    double reG = (reNum * reDen + imNum * imDen) / den2;
    double imG = (imNum * reDen - reNum * imDen) / den2;

    // e^{-d t}
    double expRe = std::exp(-reD * t);
    double reEdt = expRe * std::cos(-imD * t);
    double imEdt = expRe * std::sin(-imD * t);

    // 1 - g e^{-dt} and 1 - g
    double reOneMinusGe = 1 - (reG * reEdt - imG * imEdt);
    double imOneMinusGe = -(reG * imEdt + imG * reEdt);
    double reOneMinusG = 1 - reG;
    double imOneMinusG = -imG;

    // C = (kappa theta / sigma^2) [(b - i rho sigma u - d) t
    //     - 2 ln((1 - g e^{-dt})/(1 - g))]
    double ratioRe;
    double ratioIm;
    {
        double d2 = reOneMinusG * reOneMinusG + imOneMinusG * imOneMinusG;
        ratioRe = (reOneMinusGe * reOneMinusG + imOneMinusGe * imOneMinusG) / d2;
        ratioIm = (imOneMinusGe * reOneMinusG - reOneMinusGe * imOneMinusG) / d2;
    }
    double logModRatio = 0.5 * std::log(ratioRe * ratioRe + ratioIm * ratioIm);
    // Principal-branch arg is valid ONLY for the little-trap ratio with
    // Re(d) >= 0 (Lord & Kahl: its winding number is zero for all t) —
    // the 1993 g or a flipped d branch would need phase tracking here.
    double argRatio = std::atan2(ratioIm, ratioRe);
    double coef = kappa * theta / (sv * sv);
    double reC = coef * ((reA - reD) * t - 2 * logModRatio);
    double imC = coef * ((imA - imD) * t - 2 * argRatio);

    // D = ((b - i rho sigma u - d)/sigma^2) (1 - e^{-dt})/(1 - g e^{-dt})
    double reOneMinusEdt = 1 - reEdt;
    double imOneMinusEdt = -imEdt;
    double reFrac;
    double imFrac;
    {
        double d2 = reOneMinusGe * reOneMinusGe + imOneMinusGe * imOneMinusGe;
        reFrac = (reOneMinusEdt * reOneMinusGe + imOneMinusEdt * imOneMinusGe) / d2;
        imFrac = (imOneMinusEdt * reOneMinusGe - reOneMinusEdt * imOneMinusGe) / d2;
    }
    double reDD = ((reA - reD) * reFrac - (imA - imD) * imFrac) / (sv * sv);
    double imDD = ((reA - reD) * imFrac + (imA - imD) * reFrac) / (sv * sv);

    // phi = exp(C + D v0 + i u x)   (forward-measure form: no drift term)
    double reExp = reC + reDD * v0;
    double imExp = imC + imDD * v0 + u * x;
    double mod = std::exp(reExp);
    // integrand = Re{phi/(iu)} = Re{phi (-i)/u}; Re{(a+bi)(-i/u)} = b/u.
    return mod * std::sin(imExp) / u;
}

/**
 * P1 (delta-measure) or P2 (risk-neutral) via Simpson integration of the
 * little-trap characteristic function.
 */
double probability(double forward, double strike, double t,
                   const Params& p, bool first) {
    double logMoneyness = std::log(forward / strike);
    double sigmaEff = std::sqrt(std::max(p.v0, p.theta));
    double stretch = std::min(MAX_STRETCH,
            std::max(1.0, REFERENCE_SIGMA_SQRT_T / (sigmaEff * std::sqrt(t))));
    // Scale the step count with the window so resolution is preserved
    // (an even multiple of an even base stays Simpson-legal).
    int steps = BASE_INTEGRATION_STEPS * static_cast<int>(std::ceil(stretch));
    double h = BASE_INTEGRATION_LIMIT * stretch / steps;
    double sum = 0;
    for (int i = 0; i <= steps; i++) {
        double u = i * h + 1e-9;  // dodge the u=0 singularity of the integrand
        double weight = (i == 0 || i == steps) ? 1 : (i % 2 == 1 ? 4 : 2);
        sum += weight * integrand(u, logMoneyness, t, p, first);
    }
    return 0.5 + (h / 3) * sum / std::numbers::pi;
}

}  // namespace

Params::Params(double kappa_, double theta_, double sigmaV_, double rho_,
               double v0_)
    : kappa(kappa_), theta(theta_), sigmaV(sigmaV_), rho(rho_), v0(v0_) {
    if (!(kappa > 0) || !(theta > 0) || !(sigmaV > 0) || !(v0 > 0)
            || !(rho >= -1 && rho <= 1)
            || kappa == kInf || theta == kInf || sigmaV == kInf || v0 == kInf) {
        throw std::invalid_argument(
                "need kappa, theta, sigmaV, v0 > 0 (finite) and rho in [-1, 1]");
    }
}

double Params::feller() const {
    return 2 * kappa * theta / (sigmaV * sigmaV);
}

double call(double spot, double strike, double rate, double divYield,
            double timeYears, const Params& p) {
    requireMarketInputs(spot, strike, rate, divYield, timeYears);
    double forward = spot * std::exp((rate - divYield) * timeYears);
    double df = std::exp(-rate * timeYears);
    double p1 = probability(forward, strike, timeYears, p, true);
    double p2 = probability(forward, strike, timeYears, p, false);
    return df * (forward * p1 - strike * p2);
}

double put(double spot, double strike, double rate, double divYield,
           double timeYears, const Params& p) {
    double forward = spot * std::exp((rate - divYield) * timeYears);
    double df = std::exp(-rate * timeYears);
    return call(spot, strike, rate, divYield, timeYears, p)
            - df * (forward - strike);
}

double callMonteCarlo(double spot, double strike, double rate,
                      double divYield, double timeYears, const Params& p,
                      int steps, int paths, std::int64_t seed) {
    requireMarketInputs(spot, strike, rate, divYield, timeYears);
    if (steps < 1 || paths < 1) {
        throw std::invalid_argument("need steps >= 1 and paths >= 1");
    }
    JavaRandom rnd(seed);
    double dt = timeYears / steps;
    double sqrtDt = std::sqrt(dt);
    double rhoBar = std::sqrt(1 - p.rho * p.rho);
    double drift = (rate - divYield) * dt;
    double sum = 0;
    for (int path = 0; path < paths; path++) {
        double logS = std::log(spot);
        double v = p.v0;
        for (int i = 0; i < steps; i++) {
            double vPlus = std::max(v, 0.0);          // full truncation
            double z1 = rnd.nextGaussian();
            double z2 = p.rho * z1 + rhoBar * rnd.nextGaussian();
            logS += drift - 0.5 * vPlus * dt + std::sqrt(vPlus) * sqrtDt * z1;
            v += p.kappa * (p.theta - vPlus) * dt
                    + p.sigmaV * std::sqrt(vPlus) * sqrtDt * z2;
        }
        sum += std::max(std::exp(logS) - strike, 0.0);
    }
    return std::exp(-rate * timeYears) * sum / paths;
}

}  // namespace heston
}  // namespace quantfinlib
