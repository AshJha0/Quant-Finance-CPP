// Port of com.quantfinlib.risk.CreditAndLimitsTest (Java), minus the
// PreTradeLimitChecker cases (execution-infra class, Phase 2):
// counterparty exposure with netting, settlement (Herstatt) risk, and
// concentration metrics. All cases are deterministic hand arithmetic.
#include <string>
#include <vector>

#include "qtest.hpp"
#include "quantfinlib/risk/concentration_risk.hpp"
#include "quantfinlib/risk/counterparty_exposure_tracker.hpp"
#include "quantfinlib/risk/settlement_risk_analyzer.hpp"

using quantfinlib::CounterpartyExposureTracker;
namespace concentration_risk = quantfinlib::concentration_risk;
namespace settlement = quantfinlib::settlement_risk_analyzer;
using Trade = CounterpartyExposureTracker::CounterpartyTrade;
using Leg = settlement::SettlementLeg;

// ---- Counterparty exposure -----------------------------------------

TEST(currentExposureNetsWithinCounterparty) {
    CounterpartyExposureTracker tracker;
    tracker.addTrade({"BANK_A", "FX_FWD", 1'000'000, 50'000, 0.5})
           .addTrade({"BANK_A", "FX_SWAP", 2'000'000, -30'000, 2})
           .addTrade({"BANK_B", "FX_FWD", 500'000, -10'000, 0.25});

    EXPECT_NEAR(20'000, tracker.currentExposure("BANK_A"), 1e-9);  // 50k - 30k
    EXPECT_NEAR(0, tracker.currentExposure("BANK_B"), 1e-9);       // floored at 0
}

TEST(pfeUsesTenorAddOns) {
    CounterpartyExposureTracker tracker;
    tracker.addTrade({"BANK_A", "FX_FWD", 1'000'000, 0, 0.5})  // 1%
           .addTrade({"BANK_A", "FX_SWAP", 1'000'000, 0, 3})   // 5%
           .addTrade({"BANK_A", "XCCY", 1'000'000, 0, 7});     // 7.5%

    // No positive MTM -> NGR = 1, full add-on: 10k + 50k + 75k.
    EXPECT_NEAR(10'000 + 50'000 + 75'000,
                tracker.potentialFutureExposure("BANK_A"), 1e-9);
    EXPECT_NEAR(tracker.potentialFutureExposure("BANK_A"),
                tracker.totalExposure("BANK_A"), 1e-9);
    auto all = tracker.allExposures();
    bool hasBankA = false;
    for (const auto& e : all) {
        if (e.first == "BANK_A") {
            hasBankA = true;
        }
    }
    EXPECT_TRUE(hasBankA);
}

TEST(ngrGivesNettingRelief) {
    // Hedged netting set: +50k / -50k MTM. Current exposure nets to 0,
    // gross positive MTM is 50k, so NGR = 0 and the add-on multiplier is
    // the (0.4 + 0.6 * 0) = 0.4 floor — the CEM netting benefit.
    CounterpartyExposureTracker tracker;
    tracker.addTrade({"BANK_C", "FX_FWD", 1'000'000, 50'000, 0.5})
           .addTrade({"BANK_C", "FX_FWD", 1'000'000, -50'000, 0.5});
    double grossAddOn = 2 * 1'000'000 * 0.01;  // both tenors < 1y: 1%
    EXPECT_NEAR(0.4 * grossAddOn, tracker.potentialFutureExposure("BANK_C"), 1e-9);
}

// ---- Settlement (Herstatt) risk ---------------------------------------

TEST(herstattExposureOnlyWhenPayingFirst) {
    std::vector<Leg> legs = {
            // Pay EUR at 07:00 CET, receive USD at 15:00 NY: Herstatt window.
            {"BANK_A", "EUR", 1'000'000, 700, "USD", 1'085'000, 1500},
            // Receive first: no Herstatt exposure.
            {"BANK_A", "USD", 500'000, 1600, "EUR", 460'000, 900},
            {"BANK_B", "EUR", 200'000, 700, "USD", 217'000, 1500}};

    auto exposure = settlement::herstattExposure(legs);
    double bankA = 0;
    double bankB = 0;
    for (const auto& e : exposure) {
        if (e.first == "BANK_A") {
            bankA = e.second;
        } else if (e.first == "BANK_B") {
            bankB = e.second;
        }
    }
    EXPECT_NEAR(1'085'000, bankA, 1e-9);
    EXPECT_NEAR(217'000, bankB, 1e-9);
}

TEST(peakExposureTracksOverlappingWindows) {
    std::vector<Leg> legs = {
            {"BANK_A", "EUR", 0, 100, "USD", 500'000, 300},
            {"BANK_A", "EUR", 0, 200, "USD", 400'000, 250},
            // Third window opens only after the first two have settled.
            {"BANK_A", "EUR", 0, 400, "USD", 300'000, 500}};

    // Between t=200 and t=250 both early legs are outstanding: 900k peak.
    EXPECT_NEAR(900'000, settlement::peakExposure(legs, "BANK_A"), 1e-9);
}

// ---- Concentration ------------------------------------------------------

TEST(herfindahlAndEffectivePositions) {
    std::vector<double> equal = {25, 25, 25, 25};
    EXPECT_NEAR(0.25, concentration_risk::herfindahlIndex(equal), 1e-12);
    EXPECT_NEAR(4.0, concentration_risk::effectivePositions(equal), 1e-9);

    std::vector<double> concentrated = {97, 1, 1, 1};
    EXPECT_TRUE(concentration_risk::herfindahlIndex(concentrated) > 0.9);
    EXPECT_NEAR(0.98, concentration_risk::topNShare(concentrated, 2), 1e-9);
}

TEST(limitBreachesFlagOversizedGroups) {
    concentration_risk::GroupedExposures byCcy = {
            {"USD", 600.0}, {"EUR", 300.0}, {"JPY", 100.0}};
    auto breaches = concentration_risk::limitBreaches(byCcy, 0.5);
    EXPECT_TRUE(breaches.size() == 1 && breaches[0] == "USD");
    auto shares = concentration_risk::shares(byCcy);
    EXPECT_TRUE(shares[0].first == "USD");
    EXPECT_NEAR(0.6, shares[0].second, 1e-12);
}

QTEST_MAIN()
