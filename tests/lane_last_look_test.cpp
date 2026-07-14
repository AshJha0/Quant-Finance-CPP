// Port of the Java LastLookGate behavior pins: acceptance is a symmetric
// band around the quoted price (direction of the move never changes
// accept/reject, only the maker/taker-protective classification), and the
// FX Global Code Principle 17 symmetry property -- a maker-sells request
// and a maker-buys request facing the OPPOSITE move sign get IDENTICAL
// accept/reject outcomes, so a truly symmetric gate produces
// maker-protective and taker-protective rejects in comparable measure
// across a balanced 50/50 stream of favorable/adverse moves.
#include "quantfinlib/trading/last_look_gate.hpp"

#include <cmath>

#include "qtest.hpp"

using quantfinlib::LastLookGate;

TEST(constructorValidatesArguments) {
    EXPECT_THROWS(LastLookGate(0));
    EXPECT_THROWS(LastLookGate(-0.0001));
    EXPECT_THROWS(LastLookGate(std::nan("")));
}

TEST(acceptsWithinToleranceRejectsBeyondInEitherDirection) {
    LastLookGate gate(0.0001);
    EXPECT_TRUE(gate.accept(true, 1.1000, 1.1000));     // no move
    EXPECT_TRUE(gate.accept(true, 1.1000, 1.1001));     // +1 pip, within tol
    EXPECT_TRUE(gate.accept(true, 1.1000, 1.0999));     // -1 pip, within tol
    EXPECT_TRUE(!gate.accept(true, 1.1000, 1.1002));    // beyond tol, up
    EXPECT_TRUE(!gate.accept(true, 1.1000, 1.0998));    // beyond tol, down
    EXPECT_TRUE(gate.accepts() == 3);
    EXPECT_TRUE(gate.rejects() == 2);
}

TEST(classificationDependsOnDirectionRelativeToMakerSide) {
    LastLookGate gate(0.0001);
    // Maker SELLS (taker buys): a RISING fair hurts the maker.
    EXPECT_TRUE(!gate.accept(true, 1.1000, 1.1005));
    EXPECT_TRUE(gate.makerProtectiveRejects() == 1);
    EXPECT_TRUE(gate.takerProtectiveRejects() == 0);

    // Maker SELLS: a FALLING fair favors the maker -> taker-protective.
    EXPECT_TRUE(!gate.accept(true, 1.1000, 1.0990));
    EXPECT_TRUE(gate.makerProtectiveRejects() == 1);
    EXPECT_TRUE(gate.takerProtectiveRejects() == 1);

    // Maker BUYS (taker sells): the classification flips relative to the move.
    EXPECT_TRUE(!gate.accept(false, 1.1000, 1.0990));   // falling hurts a buying maker
    EXPECT_TRUE(gate.makerProtectiveRejects() == 2);
    EXPECT_TRUE(!gate.accept(false, 1.1000, 1.1005));   // rising favors a buying maker
    EXPECT_TRUE(gate.takerProtectiveRejects() == 2);
}

TEST(symmetricGateProducesRoughlyEqualMakerAndTakerProtectiveRejectsOver5050Moves) {
    // A balanced stream: half the requests see the fair move against the
    // maker, half in the maker's favor, both always beyond tolerance and
    // both maker-sells and maker-buys represented -- the symmetric gate
    // must classify rejects roughly 50/50 (this is the disclosure
    // signature the Java doc says distinguishes symmetric from asymmetric
    // last look).
    LastLookGate gate(0.0001);
    constexpr int N = 2000;
    for (int i = 0; i < N; i++) {
        bool makerSells = (i % 2) == 0;
        bool moveUp = (i % 4) < 2;   // alternate up/down in pairs
        double fair = 1.1000 + (moveUp ? 0.0010 : -0.0010);
        gate.accept(makerSells, 1.1000, fair);
    }
    EXPECT_TRUE(gate.rejects() == N);
    EXPECT_TRUE(gate.accepts() == 0);
    // Exactly balanced by construction (deterministic alternation).
    EXPECT_TRUE(gate.makerProtectiveRejects() == gate.takerProtectiveRejects());
    EXPECT_NEAR(gate.rejectRate(), 1.0, 1e-12);
}

TEST(rejectRateIsNanBeforeAnyDecision) {
    LastLookGate gate(0.0001);
    EXPECT_TRUE(std::isnan(gate.rejectRate()));
}

QTEST_MAIN()
