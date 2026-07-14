#include "quantfinlib/pricing/vanna_volga.hpp"

#include <cmath>
#include <stdexcept>

namespace quantfinlib {

VannaVolga::VannaVolga(const std::vector<double>& strikes,
                       const std::vector<double>& vols, double rate,
                       double carry, double timeYears)
    : rate_(rate), carry_(carry), timeYears_(timeYears) {
    if (strikes.size() != 3 || vols.size() != 3) {
        throw std::invalid_argument("exactly three pillars required");
    }
    if (!(strikes[0] < strikes[1] && strikes[1] < strikes[2])) {
        throw std::invalid_argument("strikes must be strictly ascending");
    }
    for (double v : vols) {
        if (v <= 0) {
            throw std::invalid_argument("vols must be > 0");
        }
    }
    if (timeYears <= 0) {
        throw std::invalid_argument("timeYears must be > 0");
    }
    for (int i = 0; i < 3; i++) {
        strikes_[i] = strikes[i];
        vols_[i] = vols[i];
    }
}

VannaVolga VannaVolga::ofPillars(const std::vector<double>& strikes,
                                 const std::vector<double>& vols, double rate,
                                 double carry, double timeYears) {
    if (strikes.size() == 5) {
        // Five-pillar smile (10-delta wings present): vanna-volga uses the
        // classic 25-delta triple — indices 1, 2, 3.
        return VannaVolga({strikes[1], strikes[2], strikes[3]},
                          {vols[1], vols[2], vols[3]}, rate, carry, timeYears);
    }
    return VannaVolga(strikes, vols, rate, carry, timeYears);
}

double VannaVolga::price(OptionType type, double spot, double strike) const {
    if (spot <= 0 || strike <= 0) {
        throw std::invalid_argument("spot and strike must be > 0");
    }
    double atm = vols_[1];
    double base = black_scholes::price(type, spot, strike, rate_, carry_, atm,
                                       timeYears_);
    double vegaK = black_scholes::vega(spot, strike, rate_, carry_, atm,
                                       timeYears_);
    double adjustment = 0;
    for (int i = 0; i < 3; i++) {
        double vegaI = black_scholes::vega(spot, strikes_[i], rate_, carry_, atm,
                                           timeYears_);
        // Pillar hedge cost: market vol vs flat ATM vol at the pillar.
        // OptionType is irrelevant to the vol difference (put-call parity
        // makes call and put vega identical); use calls throughout.
        double marketI = black_scholes::price(OptionType::CALL, spot, strikes_[i],
                                              rate_, carry_, vols_[i], timeYears_);
        double flatI = black_scholes::price(OptionType::CALL, spot, strikes_[i],
                                            rate_, carry_, atm, timeYears_);
        adjustment += weight(i, strike, vegaK, vegaI) * (marketI - flatI);
    }
    return base + adjustment;
}

double VannaVolga::impliedVol(double spot, double strike) const {
    // Calls are numerically safest OTM-forward and equivalent by parity.
    double p = price(OptionType::CALL, spot, strike);
    return black_scholes::impliedVol(OptionType::CALL, p, spot, strike, rate_,
                                     carry_, timeYears_);
}

double VannaVolga::weight(int i, double k, double vegaK, double vegaI) const {
    double x = std::log(k);
    double x1 = std::log(strikes_[0]);
    double x2 = std::log(strikes_[1]);
    double x3 = std::log(strikes_[2]);
    double num;
    double den;
    switch (i) {
        case 0:
            num = (x2 - x) * (x3 - x);
            den = (x2 - x1) * (x3 - x1);
            break;
        case 1:
            num = (x - x1) * (x3 - x);
            den = (x2 - x1) * (x3 - x2);
            break;
        default:
            num = (x - x1) * (x - x2);
            den = (x3 - x1) * (x3 - x2);
            break;
    }
    return (vegaK / vegaI) * (num / den);
}

}  // namespace quantfinlib
