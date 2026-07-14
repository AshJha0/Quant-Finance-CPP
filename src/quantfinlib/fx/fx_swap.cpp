#include "quantfinlib/fx/fx_swap.hpp"

#include <cctype>
#include <stdexcept>

namespace quantfinlib {

FxSwap::FxSwap(CurrencyPair pair, double baseNotional, LocalDate nearDate, double nearRate,
               LocalDate farDate, double farRate)
    : pair_(std::move(pair)),
      baseNotional_(baseNotional),
      nearDate_(nearDate),
      nearRate_(nearRate),
      farDate_(farDate),
      farRate_(farRate) {
    if (!farDate_.isAfter(nearDate_)) {
        throw std::invalid_argument("far date must be after near date");
    }
    if (nearRate_ <= 0 || farRate_ <= 0 || baseNotional_ == 0) {
        throw std::invalid_argument("rates must be > 0 and notional non-zero");
    }
}

FxSwap FxSwap::of(CurrencyPair pair, double baseNotional,
                   LocalDate nearDate, double nearRate,
                   LocalDate farDate, double farRate) {
    return FxSwap(std::move(pair), baseNotional, nearDate, nearRate, farDate, farRate);
}

LocalDate FxSwap::dateOf(const SwapPointsCurve& curve, const std::string& tenor) {
    const CurrencyPair& pair = curve.pair();
    return pair.tenorDate(pair.tradeDateForSpot(curve.spotDate()), tenor);
}

FxSwap FxSwap::atMarket(const SwapPointsCurve& curve, const std::string& nearTenor,
                        const std::string& farTenor, double baseNotional) {
    std::string upperNear = nearTenor;
    for (char& c : upperNear) {
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    bool spotNear = upperNear == "SPOT";
    LocalDate near = spotNear ? curve.spotDate() : dateOf(curve, nearTenor);
    double nearRate = spotNear ? curve.spotRate() : curve.outright(near);
    LocalDate far = dateOf(curve, farTenor);
    return FxSwap(curve.pair(), baseNotional, near, nearRate, far, curve.outright(far));
}

double FxSwap::legPnl(const LocalDate& legDate, double legRate, int sign,
                      const SwapPointsCurve& current) const {
    if (legDate.isBefore(current.spotDate())) {
        return 0;  // settled: realized cash, not MTM
    }
    double forward = legDate == current.spotDate()
                     ? current.spotRate()
                     : current.outright(legDate);
    return sign * baseNotional_ * (forward - legRate);
}

double FxSwap::markToMarket(const SwapPointsCurve& current) const {
    return legPnl(nearDate_, nearRate_, +1, current) + legPnl(farDate_, farRate_, -1, current);
}

double FxSwap::markToMarket(const SwapPointsCurve& current, const YieldCurve& quoteDiscount) const {
    double mtm = 0;
    if (!nearDate_.isBefore(current.spotDate())) {
        double tNear = static_cast<double>(LocalDate::daysBetween(current.spotDate(), nearDate_)) / 365.0;
        mtm += legPnl(nearDate_, nearRate_, +1, current) * (tNear > 0 ? quoteDiscount.discountFactor(tNear) : 1.0);
    }
    if (!farDate_.isBefore(current.spotDate())) {
        double tFar = static_cast<double>(LocalDate::daysBetween(current.spotDate(), farDate_)) / 365.0;
        mtm += legPnl(farDate_, farRate_, -1, current) * (tFar > 0 ? quoteDiscount.discountFactor(tFar) : 1.0);
    }
    return mtm;
}

double FxSwap::rollCost(const CurrencyPair& pair, double baseNotional, double tomNextPips) {
    return baseNotional * pair.priceFromPips(tomNextPips);
}

std::string FxSwap::toString() const {
    return pair_.symbol() + " swap " + std::to_string(baseNotional_) + " " + pair_.base()
           + " " + nearDate_.toString() + "@" + std::to_string(nearRate_)
           + " / " + farDate_.toString() + "@" + std::to_string(farRate_);
}

}  // namespace quantfinlib
