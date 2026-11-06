#include "quantfinlib/crb/crb_pnl_ledger.hpp"

#include <cmath>
#include <limits>
#include <stdexcept>

namespace quantfinlib {

void CrbPnlLedger::onInternalized(double internalizedNotional, double halfSpreadBps,
                                  double improvementBps) {
    if (!(internalizedNotional >= 0) || internalizedNotional == std::numeric_limits<double>::infinity()) {
        throw std::invalid_argument("internalizedNotional must be >= 0 and finite");
    }
    if (!(halfSpreadBps > 0) || halfSpreadBps == std::numeric_limits<double>::infinity()) {
        throw std::invalid_argument("halfSpreadBps must be positive and finite");
    }
    if (!(improvementBps >= 0) || improvementBps == std::numeric_limits<double>::infinity()) {
        throw std::invalid_argument("improvementBps must be >= 0 and finite");
    }
    if (improvementBps > halfSpreadBps) {
        throw std::invalid_argument(
            "improvement exceeds the half spread -- the desk would be paying clients to trade");
    }
    if (internalizedNotional == 0) {
        return;
    }
    spreadCaptured_ += internalizedNotional * (halfSpreadBps - improvementBps) / 1e4;
    improvementPaid_ += internalizedNotional * improvementBps / 1e4;
    internalizations_++;
}

void CrbPnlLedger::onDecision(const InternalizationEngine::Decision& decision,
                              double halfSpreadBps) {
    onInternalized(std::fabs(decision.internalized), halfSpreadBps, decision.improvementBps);
}

void CrbPnlLedger::markInventory(double inventoryPnl) {
    if (!std::isfinite(inventoryPnl)) {
        throw std::invalid_argument("inventoryPnl must be finite");
    }
    inventoryPnl_ = inventoryPnl;
}

void CrbPnlLedger::onHedge(double notional, double costBps) {
    if (!(costBps >= 0) || costBps == std::numeric_limits<double>::infinity()) {
        throw std::invalid_argument("costBps must be >= 0 and finite");
    }
    if (!std::isfinite(notional)) {
        throw std::invalid_argument("notional must be finite");
    }
    double n = std::fabs(notional);
    if (n == 0) {
        return;
    }
    hedgeCost_ += n * costBps / 1e4;
    hedges_++;
}

void CrbPnlLedger::onRoute(double notional, const crb_router::Allocation& allocation) {
    if (!(notional >= 0) || notional == std::numeric_limits<double>::infinity()) {
        throw std::invalid_argument("notional must be >= 0 and finite");
    }
    routerCost_ += notional * allocation.expectedCostBps / 1e4;
}

}  // namespace quantfinlib
