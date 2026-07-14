// Port of crb.CrbBookTest (Java): central risk book booking, cross-product
// netting, greeks rollup, and the risk report. Persistence
// (writeState/readState) and the hedge-universe registry-growth test are
// covered by crb_universe_scenario_test.cpp's non-persistence subset; see
// central_risk_book.hpp for the documented persistence deviation. Pins and
// tolerances unchanged.
#include <cmath>
#include <limits>
#include <string>
#include <vector>

#include "qtest.hpp"
#include "quantfinlib/crb/central_risk_book.hpp"
#include "quantfinlib/crb/crb_hedge_universe.hpp"
#include "quantfinlib/pricing/black_scholes.hpp"
#include "quantfinlib/util/math_utils.hpp"

using quantfinlib::CentralRiskBook;
using quantfinlib::CrbHedgeUniverse;
using quantfinlib::OptionType;
namespace black_scholes = quantfinlib::black_scholes;
namespace math_utils = quantfinlib::math_utils;

TEST(cashEquityNetsAcrossDesks) {
    CentralRiskBook book;
    book.bookCashEquity("cash-desk", "AAPL", 10'000, 150);
    book.bookCashEquity("etf-desk", "AAPL", -8'000, 150);
    EXPECT_NEAR(300'000, book.exposure("EQ:AAPL"), 1e-9);
    EXPECT_NEAR(2'700'000, book.grossExposure("EQ:AAPL"), 1e-9);
    EXPECT_NEAR(1'500'000, book.deskExposure("cash-desk", "EQ:AAPL"), 1e-9);
    EXPECT_NEAR(-1'200'000, book.deskExposure("etf-desk", "EQ:AAPL"), 1e-9);
    EXPECT_NEAR(1 - 300'000.0 / 2'700'000, book.nettingEfficiency(), 1e-12);
    EXPECT_TRUE(book.flowsBooked() == 2);
    EXPECT_NEAR(0, book.exposure("EQ:MSFT"), 0.0);
}

TEST(equityOptionDeltaNetsAgainstCashShares) {
    double spot = 100, strike = 105, r = 0.03, q = 0.01, vol = 0.25, t = 0.5;
    CentralRiskBook book;
    book.bookCashEquity("cash-desk", "XYZ", -5'000, spot);
    book.bookEquityOption("vol-desk", "XYZ", OptionType::CALL, 100, 100, spot, strike, r, q, vol, t);

    double delta = black_scholes::delta(OptionType::CALL, spot, strike, r, q, vol, t);
    double gamma = black_scholes::gamma(spot, strike, r, q, vol, t);
    double vega = black_scholes::vega(spot, strike, r, q, vol, t);
    EXPECT_NEAR(-500'000 + 10'000 * delta * spot, book.exposure("EQ:XYZ"), 1e-6);
    EXPECT_NEAR(10'000 * gamma * spot * spot / 100, book.exposure("EQGAMMA:XYZ"), 1e-9);
    EXPECT_NEAR(10'000 * vega / 100, book.exposure("EQVEGA:XYZ"), 1e-9);

    CentralRiskBook putBook;
    putBook.bookEquityOption("vol-desk", "XYZ", OptionType::PUT, 50, 100, spot, strike, r, q, vol, t);
    EXPECT_TRUE(putBook.exposure("EQ:XYZ") < 0);
    EXPECT_TRUE(putBook.exposure("EQGAMMA:XYZ") > 0);
}

TEST(fxSpotDecomposesToCurrencyLegsAndNetsAcrossPairs) {
    CentralRiskBook book;
    book.bookFxSpot("fx-desk", "EURUSD", 10'000'000, 1.10);
    EXPECT_NEAR(10'000'000, book.exposure("CCY:EUR"), 1e-6);
    EXPECT_NEAR(-11'000'000, book.exposure("CCY:USD"), 1e-6);

    book.bookFxSpot("spot-desk-2", "USDJPY", 5'000'000, 150);
    EXPECT_NEAR(-6'000'000, book.exposure("CCY:USD"), 1e-6);
    EXPECT_NEAR(-750'000'000, book.exposure("CCY:JPY"), 1e-6);
}

TEST(fxSwapCarriesPointsRiskWithZeroBaseDelta) {
    CentralRiskBook book;
    book.bookFxSwap("fwd-desk", "EURUSD", 20'000'000, 1.1000, 1.1050);
    EXPECT_NEAR(0, book.exposure("CCY:EUR"), 1e-9);
    EXPECT_NEAR(20'000'000 * 0.005, book.exposure("CCY:USD"), 1e-6);
    EXPECT_NEAR(-20'000'000, book.exposure("FXPOINTS:EURUSD"), 1e-9);
    book.bookFxSpot("spot-desk", "EURUSD", 1'000'000, 1.10);
    EXPECT_NEAR(1'000'000, book.exposure("CCY:EUR"), 1e-9);
}

TEST(ndfCarriesFullDeltaUntilFixingAndTracksPendingNotional) {
    CentralRiskBook book;
    book.bookNdf("em-desk", "USDINR", 5'000'000, 84.0);
    EXPECT_NEAR(5'000'000, book.exposure("CCY:USD"), 1e-6);
    EXPECT_NEAR(-420'000'000, book.exposure("CCY:INR"), 1e-6);
    EXPECT_NEAR(5'000'000, book.pendingFixing("USDINR"), 1e-9);
    book.bookNdf("em-desk", "USDINR", -2'000'000, 84.0);
    EXPECT_NEAR(3'000'000, book.exposure("CCY:USD"), 1e-6);
    EXPECT_NEAR(7'000'000, book.pendingFixing("USDINR"), 1e-9);
    EXPECT_NEAR(0, book.pendingFixing("USDBRL"), 1e-12);

    book.settleFixing("USDINR", 3'000'000);
    EXPECT_NEAR(4'000'000, book.pendingFixing("USDINR"), 1e-9);
    book.settleFixing("USDINR", 4'000'000);
    EXPECT_NEAR(0, book.pendingFixing("USDINR"), 1e-12);
    EXPECT_THROWS(book.settleFixing("USDINR", 1));
}

TEST(fxOptionDeltaNetsAgainstSpotOnTheCurrencyLegs) {
    double s = 1.10, k = 1.12, rd = 0.05, rf = 0.03, vol = 0.10, t = 0.25;
    CentralRiskBook book;
    book.bookFxSpot("fx-desk", "EURUSD", -6'000'000, s);
    book.bookFxOption("fx-opt-desk", "EURUSD", OptionType::CALL, 10'000'000, s, k, rd, rf, vol, t);

    double delta = black_scholes::delta(OptionType::CALL, s, k, rd, rf, vol, t);
    double vega = black_scholes::vega(s, k, rd, rf, vol, t);
    EXPECT_NEAR(-6'000'000 + 10'000'000 * delta, book.exposure("CCY:EUR"), 1e-4);
    EXPECT_NEAR(10'000'000 * vega / 100, book.exposure("FXVEGA:EURUSD"), 1e-9);
    EXPECT_TRUE(book.exposure("FXGAMMA:EURUSD") > 0);
}

TEST(reportPricesTheDiversificationBenefitOfNetting) {
    CentralRiskBook book;
    book.bookCashEquity("desk-a", "SPY", 20'000, 500);
    book.bookCashEquity("desk-b", "SPY", -16'000, 500);
    double dailyVar = 4e-4;
    std::vector<std::vector<double>> cov = {{dailyVar}};
    CentralRiskBook::CrbReport report = book.report(cov, 0.99);

    double z = math_utils::normInv(0.99);
    double sigma = std::sqrt(dailyVar);
    EXPECT_NEAR(z * 2'000'000 * sigma, report.var, 1e-6);
    EXPECT_NEAR(z * 18'000'000 * sigma, report.standaloneDeskVar, 1e-6);
    EXPECT_NEAR(z * 16'000'000 * sigma, report.diversificationBenefit, 1e-6);
    EXPECT_TRUE(report.es > report.var);
    EXPECT_NEAR(book.nettingEfficiency(), report.nettingEfficiency, 1e-12);
    EXPECT_NEAR(18'000'000, report.grossExposure, 1e-6);
    EXPECT_NEAR(2'000'000, report.netExposure, 1e-6);
}

TEST(gatesRejectGarbageLoudly) {
    CentralRiskBook book;
    EXPECT_THROWS(book.bookCashEquity("d", "X", std::nan(""), 100));
    EXPECT_THROWS(book.bookCashEquity("d", "X", 100, 0));
    EXPECT_THROWS(book.bookCashEquity(" ", "X", 100, 100));
    EXPECT_THROWS(book.bookFxSpot("d", "EUR", 1e6, 1.1));
    EXPECT_THROWS(book.bookFxSwap("d", "EURUSD", 1e6, 1.1, std::numeric_limits<double>::infinity()));
    EXPECT_THROWS(book.bookEquityOption("d", "X", OptionType::CALL, 1, 100, 100, 100, 0.02, 0, -0.2, 1));
    book.bookCashEquity("d", "X", 100, 100);
    EXPECT_THROWS(book.report(std::vector<std::vector<double>>(2, std::vector<double>(2, 0.0)), 0.99));

    long flows = book.flowsBooked();
    double eq = book.exposure("EQ:Y");
    EXPECT_THROWS(book.bookEquityOption("d", "Y", OptionType::CALL, 1, 100, 100, 100, 0.03,
                                       std::numeric_limits<double>::infinity(), 0.2, 1));
    EXPECT_TRUE(book.flowsBooked() == flows);
    EXPECT_NEAR(eq, book.exposure("EQ:Y"), 0.0);
}

TEST(hedgeOnlyFactorsNeverBreakTheBookLevelViews) {
    CentralRiskBook book;
    book.bookFxSpot("fx-desk", "EURUSD", 10'000'000, 1.10);
    double efficiencyBefore = book.nettingEfficiency();
    CrbHedgeUniverse universe(book.factors());
    for (int i = 0; i < 20; i++) {
        universe.addSingleFactor("HEDGE-" + std::to_string(i), "HEDGEFACTOR:" + std::to_string(i), 1);
    }
    EXPECT_NEAR(efficiencyBefore, book.nettingEfficiency(), 1e-12);
    int n = book.factors().size();
    std::vector<std::vector<double>> cov(n, std::vector<double>(n, 0.0));
    for (int f = 0; f < n; f++) {
        cov[f][f] = 1e-4;
    }
    CentralRiskBook::CrbReport report = book.report(cov, 0.99);
    EXPECT_TRUE(report.var > 0);
    EXPECT_TRUE(static_cast<int>(book.netExposures().size()) == n);
}

QTEST_MAIN()
