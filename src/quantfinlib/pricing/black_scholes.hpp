// Port of com.quantfinlib.pricing.BlackScholes (Java): Black-Scholes-Merton
// pricing and Greeks with a continuous carry yield q (dividend yield for
// equities, foreign rate for FX — Garman-Kohlhagen). All rates and vol are
// annualized; theta per year; vega/rho per 1.00 change. OptionType lives at
// namespace scope because every other pricing class shares it.
#pragma once

namespace quantfinlib {

enum class OptionType { CALL, PUT };

inline int sign(OptionType type) {
    return type == OptionType::CALL ? 1 : -1;
}

namespace black_scholes {

/** Full Greek set for one option (Java record BlackScholes.Greeks). */
struct Greeks {
    double price;
    double delta;
    double gamma;
    double vega;
    double theta;
    double rho;
};

double price(OptionType type, double spot, double strike,
             double rate, double carry, double vol, double timeYears);

double delta(OptionType type, double spot, double strike,
             double rate, double carry, double vol, double timeYears);

double gamma(double spot, double strike, double rate, double carry,
             double vol, double timeYears);

/** Per 1.00 change in volatility (divide by 100 for per-vol-point). */
double vega(double spot, double strike, double rate, double carry,
            double vol, double timeYears);

/** Per year (divide by 365 for per-calendar-day). */
double theta(OptionType type, double spot, double strike,
             double rate, double carry, double vol, double timeYears);

/** Per 1.00 change in the domestic rate. */
double rho(OptionType type, double spot, double strike,
           double rate, double carry, double vol, double timeYears);

Greeks greeks(OptionType type, double spot, double strike,
              double rate, double carry, double vol, double timeYears);

/**
 * Implied volatility by bisection. Returns NaN when the price is not
 * attainable inside the [1e-4, 5.0] vol bracket (below intrinsic, or above
 * the maximum BS price) — a stale or rounded market price must surface as
 * "no vol", not silently come back as the 500% search bound.
 */
double impliedVol(OptionType type, double marketPrice, double spot,
                  double strike, double rate, double carry, double timeYears);

double intrinsic(OptionType type, double spot, double strike);

}  // namespace black_scholes
}  // namespace quantfinlib
