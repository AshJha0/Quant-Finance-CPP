// Port of the HftRiskGate behavior pins (Java HftOrderPathTest gate
// sections): every rejection reason, the reduce-vs-flip position rule
// ((new ^ cur) < 0 detects sign flips), the NaN reference-price collar
// rejection, halt and the gate-wide kill switch — plus a two-thread onFill
// exact-sum test proving the atomic position add loses no updates (the
// C++ mirror of Java's VarHandle getAndAdd discipline).
#include "quantfinlib/trading/hft_risk_gate.hpp"

#include <cstdint>
#include <thread>

#include "qtest.hpp"

using quantfinlib::HftRiskGate;
using quantfinlib::Side;

TEST(cleanOrderPassesAndRejectionReasonsPin) {
    HftRiskGate gate(4);
    gate.maxOrderQuantity(1000)
        .maxOrderNotional(1e6)
        .maxPositionQuantity(5000)
        .priceCollarPct(0.02);
    gate.setReferencePrice(0, 100.0);

    EXPECT_TRUE(gate.check(0, Side::BUY, 500, 100.0) == HftRiskGate::OK);

    // Quantity: zero, negative and over-cap all pin REJECT_QUANTITY.
    EXPECT_TRUE(gate.check(0, Side::BUY, 0, 100.0) == HftRiskGate::REJECT_QUANTITY);
    EXPECT_TRUE(gate.check(0, Side::BUY, -5, 100.0) == HftRiskGate::REJECT_QUANTITY);
    EXPECT_TRUE(gate.check(0, Side::BUY, 1001, 100.0) == HftRiskGate::REJECT_QUANTITY);

    // Notional: 999 * 1200 > 1e6.
    EXPECT_TRUE(gate.check(0, Side::BUY, 999, 1200.0) == HftRiskGate::REJECT_NOTIONAL);

    // Collar: 2% around 100 -> 102.01 rejected, 101.9 passes.
    EXPECT_TRUE(gate.check(0, Side::BUY, 10, 102.01) == HftRiskGate::REJECT_PRICE_COLLAR);
    EXPECT_TRUE(gate.check(0, Side::BUY, 10, 101.9) == HftRiskGate::OK);
    EXPECT_TRUE(gate.check(0, Side::SELL, 10, 97.99) == HftRiskGate::REJECT_PRICE_COLLAR);

    // Rejection counters kept per reason.
    EXPECT_TRUE(gate.rejectionCount(HftRiskGate::REJECT_QUANTITY) == 3);
    EXPECT_TRUE(gate.rejectionCount(HftRiskGate::REJECT_NOTIONAL) == 1);
    EXPECT_TRUE(gate.rejectionCount(HftRiskGate::REJECT_PRICE_COLLAR) == 2);
    EXPECT_TRUE(gate.rejectionCount(HftRiskGate::OK) == 0);
}

TEST(nanReferencePriceRejectsUnlessTheCheckIsOff) {
    // Review finding C4: this test used to pin the skip as intended. Symbol 1
    // never got a reference price, so the order cannot be collared and must
    // be refused; the opt-out has to be explicit.
    HftRiskGate gate(4);
    gate.maxOrderQuantity(100).maxOrderNotional(1e9).maxPositionQuantity(100)
        .priceCollarPct(0.0001);  // brutally tight collar
    EXPECT_TRUE(gate.check(1, Side::BUY, 10, 12345.0) == HftRiskGate::REJECT_PRICE_COLLAR);
    HftRiskGate noCollar(4);
    noCollar.maxOrderQuantity(100).maxOrderNotional(1e12).maxPositionQuantity(100);
    noCollar.setReferencePrice(0, 100.0);
    EXPECT_TRUE(noCollar.check(0, Side::BUY, 10, 1e9) == HftRiskGate::REJECT_PRICE_COLLAR);
    noCollar.disableReferenceCheck();
    EXPECT_TRUE(noCollar.check(0, Side::BUY, 10, 1e9) == HftRiskGate::OK);
    EXPECT_TRUE(gate.referencePrice(1) != gate.referencePrice(1));  // still NaN
}

TEST(positionCheckAllowsReduceButNotFlip) {
    HftRiskGate gate(2);
    gate.maxOrderQuantity(10000).maxOrderNotional(1e9).maxPositionQuantity(1000)
        .disableReferenceCheck();

    // Book two 800-lot buy fills: position 1600, already over the cap.
    gate.onFill(0, Side::BUY, 800);
    gate.onFill(0, Side::BUY, 800);
    EXPECT_TRUE(gate.position(0) == 1600);

    // Another buy projects 1700: rejected (grows the breach).
    EXPECT_TRUE(gate.check(0, Side::BUY, 100, 10.0) == HftRiskGate::REJECT_POSITION);
    // A 500-lot sell projects 1100 — still over cap, but REDUCING and
    // same-sign: the de-risking trade must pass.
    EXPECT_TRUE(gate.check(0, Side::SELL, 500, 10.0) == HftRiskGate::OK);
    // A 3000-lot sell projects -1400: smaller in magnitude than 1600 but
    // it FLIPS the sign through zero — a brand-new over-cap short wearing
    // a hedge's clothes. Rejected.
    EXPECT_TRUE(gate.check(0, Side::SELL, 3000, 10.0) == HftRiskGate::REJECT_POSITION);
    // Flattening entirely (1600 sell, projects 0) passes.
    EXPECT_TRUE(gate.check(0, Side::SELL, 1600, 10.0) == HftRiskGate::OK);

    // Mirror on the short side.
    gate.onFill(1, Side::SELL, 1600);
    EXPECT_TRUE(gate.position(1) == -1600);
    EXPECT_TRUE(gate.check(1, Side::SELL, 100, 10.0) == HftRiskGate::REJECT_POSITION);
    EXPECT_TRUE(gate.check(1, Side::BUY, 500, 10.0) == HftRiskGate::OK);
    EXPECT_TRUE(gate.check(1, Side::BUY, 3000, 10.0) == HftRiskGate::REJECT_POSITION);
}

TEST(haltAndKillSwitchesGateEverything) {
    HftRiskGate gate(2);
    gate.maxOrderQuantity(100).maxOrderNotional(1e9).maxPositionQuantity(100)
        .disableReferenceCheck();
    gate.halt(0, true);
    EXPECT_TRUE(gate.check(0, Side::BUY, 1, 1.0) == HftRiskGate::REJECT_HALTED);
    EXPECT_TRUE(gate.check(1, Side::BUY, 1, 1.0) == HftRiskGate::OK);  // per-symbol
    gate.halt(0, false);
    EXPECT_TRUE(gate.check(0, Side::BUY, 1, 1.0) == HftRiskGate::OK);

    // Kill is gate-wide and checked FIRST (even a halted symbol reports KILLED).
    gate.halt(0, true);
    gate.kill(true);
    EXPECT_TRUE(gate.isKilled());
    EXPECT_TRUE(gate.check(0, Side::BUY, 1, 1.0) == HftRiskGate::REJECT_KILLED);
    EXPECT_TRUE(gate.check(1, Side::BUY, 1, 1.0) == HftRiskGate::REJECT_KILLED);
    gate.kill(false);
    EXPECT_TRUE(!gate.isKilled());
    EXPECT_TRUE(gate.check(1, Side::BUY, 1, 1.0) == HftRiskGate::OK);
    EXPECT_TRUE(gate.symbolCapacity() == 2);
}

TEST(reasonNamesAreStable) {
    EXPECT_TRUE(HftRiskGate::reasonName(HftRiskGate::OK) == "OK");
    EXPECT_TRUE(HftRiskGate::reasonName(HftRiskGate::REJECT_QUANTITY) == "MAX_ORDER_QTY");
    EXPECT_TRUE(HftRiskGate::reasonName(HftRiskGate::REJECT_NOTIONAL) == "MAX_NOTIONAL");
    EXPECT_TRUE(HftRiskGate::reasonName(HftRiskGate::REJECT_POSITION) == "MAX_POSITION");
    EXPECT_TRUE(HftRiskGate::reasonName(HftRiskGate::REJECT_PRICE_COLLAR) == "PRICE_COLLAR");
    EXPECT_TRUE(HftRiskGate::reasonName(HftRiskGate::REJECT_HALTED) == "HALTED");
    EXPECT_TRUE(HftRiskGate::reasonName(HftRiskGate::REJECT_KILLED) == "KILLED");
    EXPECT_TRUE(HftRiskGate::reasonName(HftRiskGate::REJECT_PRICE) == "INVALID_PRICE");
    EXPECT_TRUE(HftRiskGate::reasonName(HftRiskGate::REJECT_SYMBOL) == "UNKNOWN_SYMBOL");
    EXPECT_TRUE(HftRiskGate::reasonName(HftRiskGate::REJECT_SELF_TRADE) == "SELF_TRADE");
    EXPECT_TRUE(HftRiskGate::reasonName(HftRiskGate::REJECT_ORDER_TABLE) == "ORDER_TABLE_FULL");
    EXPECT_TRUE(HftRiskGate::reasonName(HftRiskGate::REJECT_ORDER_STATE) == "ORDER_STATE");
    EXPECT_TRUE(HftRiskGate::reasonName(42) == "UNKNOWN(42)");
}

TEST(concurrentOnFillFromTwoThreadsLosesNoUpdate) {
    // The venue-ack path is multi-source: two threads hammer onFill on the
    // same symbol and the atomic add must land every update exactly once.
    // Thread A books +1 x 200k, thread B books -1 x 120k: the final
    // position must be exactly 80k — a plain long[] would lose updates.
    HftRiskGate gate(1);
    constexpr int BUYS = 200000;
    constexpr int SELLS = 120000;
    std::thread buyer([&gate] {
        for (int i = 0; i < BUYS; i++) {
            gate.onFill(0, Side::BUY, 1);
        }
    });
    std::thread seller([&gate] {
        for (int i = 0; i < SELLS; i++) {
            gate.onFill(0, Side::SELL, 1);
        }
    });
    buyer.join();
    seller.join();
    EXPECT_TRUE(gate.position(0) == BUYS - SELLS);
}

QTEST_MAIN()
