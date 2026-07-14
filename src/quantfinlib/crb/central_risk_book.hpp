// Port of com.quantfinlib.crb.CentralRiskBook (Java): the central risk
// book — one netted view of the firm's market risk across desks and
// products. Every instrument decomposes into a COMMON risk-factor space at
// booking time: an FX-option delta nets against a spot position, an
// equity-option delta nets against cash shares, and opposite flows from
// two desks cancel before anyone pays a spread to the street.
//
// Factor naming and units (query via exposure()):
//   EQ:<sym>       — equity delta in book-currency notional
//   EQGAMMA:<sym> / EQVEGA:<sym> — dollar gamma per 1% move, vega per vol point
//   CCY:<ccy>      — currency exposure in NATIVE units of that currency
//   FXPOINTS:<pair>— forward-points risk of swaps
//   FXGAMMA:<pair> / FXVEGA:<pair> — dollar gamma per 1%, vega per vol point
//
// Sign convention: positive quantity/notional = the BOOK is long. This is
// a RISK ledger, not a cash ledger. Deterministic, single-threaded.
//
// DEVIATION FROM JAVA: writeState/readState (persist.Checkpoint section
// serialization) are NOT ported — see fx/lp_scorecard.hpp for the
// rationale (no C++ persist.Checkpoint port; out of this task's scope).
#pragma once

#include <map>
#include <set>
#include <string>
#include <vector>

#include "quantfinlib/crb/factor_registry.hpp"
#include "quantfinlib/pricing/black_scholes.hpp"

namespace quantfinlib {

class CentralRiskBook {
public:
    /**
     * @param grossExposure    sum over factors of gross booked notional
     * @param netExposure      sum over factors of |netted| notional
     * @param nettingEfficiency 1 - net/gross
     * @param var              delta-normal VaR of the netted book
     * @param es               delta-normal ES of the netted book
     * @param standaloneDeskVar sum of per-desk standalone VaRs
     * @param diversificationBenefit standaloneDeskVar - var
     */
    struct CrbReport {
        double grossExposure;
        double netExposure;
        double nettingEfficiency;
        double var;
        double es;
        double standaloneDeskVar;
        double diversificationBenefit;
    };

    CentralRiskBook() = default;

    /** Cash equity: qty shares (signed) at price. */
    void bookCashEquity(const std::string& desk, const std::string& symbol, double qty,
                       double price);

    /**
     * Listed equity option: contracts signed, multiplier shares per
     * contract (100 for US listed). Greeks come from black_scholes with
     * the house q-convention (carry = dividend yield).
     */
    void bookEquityOption(const std::string& desk, const std::string& symbol, OptionType type,
                         double contracts, double multiplier, double spot, double strike,
                         double rate, double carry, double vol, double timeYears);

    /**
     * FX spot on pair ("EURUSD"): buy baseNotional of the base currency
     * (signed) at rate. Decomposes into the two CURRENCY legs.
     */
    void bookFxSpot(const std::string& desk, const std::string& pair, double baseNotional,
                   double rate);

    /**
     * FX swap (buy-sell base for positive notional): near leg at nearRate,
     * far leg back at farRate. Base-currency legs cancel EXACTLY; what
     * remains is the quote-currency cash-flow imbalance and the
     * forward-POINTS risk.
     */
    void bookFxSwap(const std::string& desk, const std::string& pair, double baseNotional,
                   double nearRate, double farRate);

    /**
     * NDF: buy baseNotional of base forward at fwdRate. Full currency
     * delta on both legs, with the non-deliverable notional tracked per
     * pair.
     */
    void bookNdf(const std::string& desk, const std::string& pair, double baseNotional,
               double fwdRate);

    /**
     * FX option via Garman-Kohlhagen (carry = foreign rate): baseNotional
     * signed (long calls on base).
     */
    void bookFxOption(const std::string& desk, const std::string& pair, OptionType type,
                     double baseNotional, double spotRate, double strike, double domesticRate,
                     double foreignRate, double vol, double timeYears);

    /** Net exposure on a factor (0 for a factor never booked). */
    double exposure(const std::string& factor) const;

    /** Gross (sum of |flow|) on a factor — what the desks did severally. */
    double grossExposure(const std::string& factor) const;

    /** One desk's net contribution to a factor. */
    double deskExposure(const std::string& desk, const std::string& factor) const;

    /** Net exposures over all factors, indexed by registry id. */
    std::vector<double> netExposures() const;

    /**
     * How much risk the netting destroyed before anyone hedged:
     * 1 - sum|net| / sum(gross).
     */
    double nettingEfficiency() const;

    /** Non-deliverable notional still awaiting its fixing, per pair. */
    double pendingFixing(const std::string& pair) const;

    /**
     * Releases notional (positive, gross) of pending fixing after the
     * fixing occurs. Over-settling throws.
     */
    void settleFixing(const std::string& pair, double notional);

    long flowsBooked() const { return flowsBooked_; }

    FactorRegistry& factors() { return registry_; }
    const FactorRegistry& factors() const { return registry_; }

    std::set<std::string> desks() const;

    /**
     * The book-level risk report. covariance is over the factor space in
     * REGISTRY ORDER (factors().name(i)) with entries in (factor-return)^2
     * units matching each factor's exposure units.
     */
    CrbReport report(const std::vector<std::vector<double>>& covariance, double confidence) const;

private:
    void add(const std::string& desk, const std::string& factor, double amount);

    FactorRegistry registry_;
    std::vector<double> net_ = std::vector<double>(16, 0.0);
    std::vector<double> gross_ = std::vector<double>(16, 0.0);
    std::map<std::string, std::vector<double>> deskNet_;
    std::map<std::string, double> pendingFixings_;
    long flowsBooked_ = 0;
};

}  // namespace quantfinlib
