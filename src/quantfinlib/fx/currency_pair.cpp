#include "quantfinlib/fx/currency_pair.hpp"

#include <cctype>
#include <cmath>
#include <stdexcept>

namespace quantfinlib {

namespace {
std::string upper(const std::string& s) {
    std::string out = s;
    for (char& c : out) {
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    return out;
}
}  // namespace

CurrencyPair::CurrencyPair(std::string base, std::string quote, double pipSize,
                           int pricePrecision, int spotLagDays,
                           BusinessCalendar baseCalendar, BusinessCalendar quoteCalendar)
    : base_(std::move(base)),
      quote_(std::move(quote)),
      pipSize_(pipSize),
      pricePrecision_(pricePrecision),
      spotLagDays_(spotLagDays),
      baseCalendar_(std::move(baseCalendar)),
      quoteCalendar_(std::move(quoteCalendar)),
      // Both centers as ONE calendar (holiday union): every roll convention
      // delegates to rates::BusinessCalendar, so the market rules live in
      // exactly one place.
      jointCalendar_(baseCalendar_.unionWith(quoteCalendar_)) {}

CurrencyPair CurrencyPair::of(const std::string& pair) {
    if (pair.length() != 6) {
        throw std::invalid_argument("pair must be 6 letters, e.g. EURUSD: " + pair);
    }
    return of(pair.substr(0, 3), pair.substr(3, 3));
}

CurrencyPair CurrencyPair::of(const std::string& base, const std::string& quote) {
    std::string b = upper(base);
    std::string q = upper(quote);
    // JPY-quoted pairs trade to 3 decimals; everything else to 5.
    double pip = q == "JPY" ? 0.01 : 0.0001;
    int precision = q == "JPY" ? 3 : 5;
    int lag = spotLagFor(b, q);
    BusinessCalendar weekends = BusinessCalendar::weekendsOnly();
    return CurrencyPair(b, q, pip, precision, lag, weekends, weekends);
}

CurrencyPair CurrencyPair::custom(const std::string& base, const std::string& quote,
                                  double pipSize, int pricePrecision, int spotLagDays,
                                  BusinessCalendar baseCalendar,
                                  BusinessCalendar quoteCalendar) {
    if (pipSize <= 0 || spotLagDays < 0) {
        throw std::invalid_argument("pipSize must be > 0 and spotLagDays >= 0");
    }
    return CurrencyPair(upper(base), upper(quote), pipSize, pricePrecision, spotLagDays,
                        std::move(baseCalendar), std::move(quoteCalendar));
}

CurrencyPair CurrencyPair::withCalendars(BusinessCalendar baseCalendar,
                                         BusinessCalendar quoteCalendar) const {
    return CurrencyPair(base_, quote_, pipSize_, pricePrecision_, spotLagDays_,
                        std::move(baseCalendar), std::move(quoteCalendar));
}

int CurrencyPair::spotLagFor(const std::string& base, const std::string& quote) {
    std::string pair = base + quote;
    // T+1 market exceptions; every other pair settles T+2.
    if (pair == "USDCAD" || pair == "USDTRY" || pair == "USDRUB" || pair == "USDPHP") {
        return 1;
    }
    return 2;
}

double CurrencyPair::round(double price) const {
    double scale = std::pow(10.0, pricePrecision_);
    return std::floor(price * scale + 0.5) / scale;
}

LocalDate CurrencyPair::tenorDate(const LocalDate& tradeDate, const std::string& tenor) const {
    std::string t = upper(tenor);
    // Trim (Java String.trim on the uppercased tenor).
    while (!t.empty() && std::isspace(static_cast<unsigned char>(t.front()))) {
        t.erase(t.begin());
    }
    while (!t.empty() && std::isspace(static_cast<unsigned char>(t.back()))) {
        t.pop_back();
    }
    LocalDate spot = spotDate(tradeDate);
    if (t == "ON") {
        return addJointBusinessDays(tradeDate, 1);
    }
    if (t == "TN") {
        return addJointBusinessDays(tradeDate, 2);
    }
    if (t == "SN") {
        return addJointBusinessDays(spot, 1);
    }
    if (t.length() < 2) {
        throw std::invalid_argument("unsupported tenor: " + tenor);
    }
    int n;
    try {
        std::size_t used = 0;
        n = std::stoi(t.substr(0, t.length() - 1), &used);
        if (used != t.length() - 1) {
            throw std::invalid_argument("trailing garbage");
        }
    } catch (const std::exception&) {
        throw std::invalid_argument("unsupported tenor: " + tenor);
    }
    char unit = t.back();
    switch (unit) {
        // Short dates roll forward only: crossing month-end is expected.
        case 'D':
            return jointCalendar_.roll(spot.plusDays(n), BusinessCalendar::Roll::FOLLOWING);
        case 'W':
            return jointCalendar_.roll(spot.plusWeeks(n), BusinessCalendar::Roll::FOLLOWING);
        case 'M':
            return monthTenor(spot, n);
        case 'Y':
            return monthTenor(spot, n * 12);
        default:
            throw std::invalid_argument("unsupported tenor: " + tenor);
    }
}

LocalDate CurrencyPair::monthTenor(const LocalDate& spot, int months) const {
    // End-end rule first (spot on the last business day of its month pins
    // the forward to the last business day of the target month), otherwise
    // modified-following.
    LocalDate target = spot.plusMonths(months);
    if (spot == lastJointBusinessDayOfMonth(spot)) {
        return lastJointBusinessDayOfMonth(target);
    }
    return jointCalendar_.roll(target, BusinessCalendar::Roll::MODIFIED_FOLLOWING);
}

LocalDate CurrencyPair::lastJointBusinessDayOfMonth(const LocalDate& anyDayInMonth) const {
    return jointCalendar_.roll(anyDayInMonth.atEndOfMonth(), BusinessCalendar::Roll::PRECEDING);
}

}  // namespace quantfinlib
