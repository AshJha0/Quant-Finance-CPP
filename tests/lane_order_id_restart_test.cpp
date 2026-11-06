// ClOrdIDs must not repeat when a session restarts.
#include "qtest.hpp"
#include "quantfinlib/trading/hft_order_gateway.hpp"
#include "quantfinlib/trading/hft_risk_gate.hpp"

using namespace quantfinlib;

namespace {
void configure(HftRiskGate& gate) {
    gate.maxOrderQuantity(1000).maxOrderNotional(1e9).maxPositionQuantity(100000).priceCollarPct(0.5);
    gate.setReferencePrice(0, 100.0);
}
}  // namespace

TEST(aRestartedGatewayContinuesFromThePersistedId) {
    HftRiskGate gate(1);
    configure(gate);
    std::int64_t persisted;
    std::int64_t last;
    {
        HftOrderGateway gw(1 << 6, gate, false);
        gw.submit(0, Side::BUY, 1, 100.0, 1);
        last = gw.submit(0, Side::BUY, 1, 100.0, 2);
        persisted = gw.nextOrderId();
    }
    HftOrderGateway restarted(1 << 6, gate, false);
    restarted.startOrderIdsAt(persisted);
    EXPECT_TRUE(restarted.submit(0, Side::BUY, 1, 100.0, 3) == last + 1);
}

TEST(idsCannotMoveOnceAnOrderIsOut) {
    HftRiskGate gate(1);
    configure(gate);
    HftOrderGateway gw(1 << 6, gate, false);
    gw.submit(0, Side::BUY, 1, 100.0, 1);
    EXPECT_THROWS(gw.startOrderIdsAt(1000));
}

TEST(idsBelowTheStartWereNeverIssuedHere) {
    HftRiskGate gate(1);
    configure(gate);
    HftOrderGateway gw(1 << 6, gate, false);
    gw.startOrderIdsAt(5000);
    EXPECT_THROWS(gw.startOrderIdsAt(0));
    std::int64_t id = gw.submit(0, Side::BUY, 1, 100.0, 1);
    EXPECT_TRUE(id == 5000);
    EXPECT_TRUE(gw.issued(id));
    EXPECT_TRUE(!gw.issued(4999));
}

QTEST_MAIN()
