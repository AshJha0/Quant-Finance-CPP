#include "quantfinlib/fx/ndf.hpp"

#include <map>
#include <stdexcept>

namespace quantfinlib {

namespace {

/**
 * Fixing lags (local business days between fixing and settlement) for the
 * common NDF currencies; PTAX fixes one day before settlement, the Asian
 * fixings two. Unlisted currencies default to 2.
 */
const std::map<std::string, int>& fixingLagTable() {
    static const std::map<std::string, int> table = {
        {"INR", 2}, {"KRW", 2}, {"TWD", 2}, {"IDR", 2}, {"MYR", 2},
        {"PHP", 1}, {"CNY", 2}, {"VND", 2}, {"BRL", 1}, {"CLP", 1},
    };
    return table;
}

}  // namespace

Ndf::Ndf(CurrencyPair pair, double baseNotional, double contractRate,
         LocalDate fixingDate, LocalDate settlementDate)
    : pair_(std::move(pair)),
      baseNotional_(baseNotional),
      contractRate_(contractRate),
      fixingDate_(fixingDate),
      settlementDate_(settlementDate) {
    if (contractRate_ <= 0 || baseNotional_ == 0) {
        throw std::invalid_argument("contractRate must be > 0 and notional non-zero");
    }
    if (!fixingDate_.isBefore(settlementDate_)) {
        throw std::invalid_argument("fixing must precede settlement");
    }
}

int Ndf::fixingLagDays(const std::string& currency) {
    const auto& table = fixingLagTable();
    auto it = table.find(currency);
    return it == table.end() ? 2 : it->second;
}

Ndf Ndf::of(const CurrencyPair& pair, const LocalDate& tradeDate, const std::string& tenor,
            double contractRate, double baseNotional) {
    LocalDate settlement = pair.tenorDate(tradeDate, tenor);
    LocalDate fixing = pair.quoteCalendar().subtractBusinessDays(
        settlement, fixingLagDays(pair.quote()));
    return Ndf(pair, baseNotional, contractRate, fixing, settlement);
}

Ndf Ndf::of(CurrencyPair pair, double baseNotional, double contractRate,
            LocalDate fixingDate, LocalDate settlementDate) {
    return Ndf(std::move(pair), baseNotional, contractRate, fixingDate, settlementDate);
}

double Ndf::settlementAmount(double fixingRate) const {
    if (fixingRate <= 0) {
        throw std::invalid_argument("fixingRate must be > 0: " + std::to_string(fixingRate));
    }
    return baseNotional_ * (fixingRate - contractRate_) / fixingRate;
}

double Ndf::markToMarket(const SwapPointsCurve& current) const {
    LocalDate spot = current.spotDate();
    double forwardEstimate = fixingDate_.isAfter(spot)
                             ? current.outright(fixingDate_)
                             : current.outright(spot);
    return settlementAmount(forwardEstimate);
}

double Ndf::markToMarket(const SwapPointsCurve& current, const YieldCurve& baseDiscount) const {
    double t = static_cast<double>(LocalDate::daysBetween(current.spotDate(), settlementDate_)) / 365.0;
    return markToMarket(current) * (t > 0 ? baseDiscount.discountFactor(t) : 1.0);
}

std::string Ndf::toString() const {
    return pair_.symbol() + " NDF " + std::to_string(baseNotional_) + " " + pair_.base()
           + " @" + std::to_string(contractRate_) + " fix " + fixingDate_.toString()
           + " settle " + settlementDate_.toString();
}

}  // namespace quantfinlib
