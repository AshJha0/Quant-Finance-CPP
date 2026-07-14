#include "quantfinlib/fx/lp_scorecard.hpp"

#include <cmath>
#include <stdexcept>

namespace quantfinlib {

LpScorecard::LpScorecard(int lpCount, double alpha, std::int64_t markoutHorizonNanos)
    : lpCount_(lpCount),
      alpha_(alpha),
      markoutHorizonNanos_(markoutHorizonNanos),
      attempts_(lpCount, 0),
      fills_(lpCount, 0),
      rejects_(lpCount, 0),
      rejectRate_(lpCount, 0.0),
      holdNanosEwma_(lpCount, 0.0),
      effSpreadEwma_(lpCount, 0.0),
      markoutEwma_(lpCount, 0.0),
      markoutCount_(lpCount, 0),
      pendingSide_(static_cast<std::size_t>(lpCount) * PENDING_RING, 0),
      pendingMid_(static_cast<std::size_t>(lpCount) * PENDING_RING, 0.0),
      pendingTs_(static_cast<std::size_t>(lpCount) * PENDING_RING, 0),
      pendingCursor_(lpCount, 0),
      pendingCount_(0),
      maturedMarkouts_(0) {
    if (lpCount < 1 || alpha <= 0 || alpha > 1 || markoutHorizonNanos <= 0) {
        throw std::invalid_argument("need lpCount >= 1, alpha in (0,1], horizon > 0");
    }
}

LpScorecard::LpScorecard(int lpCount) : LpScorecard(lpCount, 0.05, 100'000'000LL) {}

void LpScorecard::onFill(int lp, bool buy, double price, double midAtRequest,
                         std::int64_t holdNanos) {
    attempts_[lp]++;
    fills_[lp]++;
    rejectRate_[lp] += alpha_ * (0 - rejectRate_[lp]);
    holdNanosEwma_[lp] += alpha_ * (static_cast<double>(holdNanos) - holdNanosEwma_[lp]);
    double eff = buy ? price - midAtRequest : midAtRequest - price;
    if (std::isfinite(eff)) {
        effSpreadEwma_[lp] += alpha_ * (eff - effSpreadEwma_[lp]);
    }
}

void LpScorecard::onReject(int lp, bool buy, double midAtRequest, std::int64_t timestampNanos,
                           std::int64_t holdNanos) {
    attempts_[lp]++;
    rejects_[lp]++;
    rejectRate_[lp] += alpha_ * (1 - rejectRate_[lp]);
    holdNanosEwma_[lp] += alpha_ * (static_cast<double>(holdNanos) - holdNanosEwma_[lp]);
    if (!std::isfinite(midAtRequest)) {
        return;
    }
    int slot = lp * PENDING_RING + pendingCursor_[lp];
    if (pendingSide_[slot] == 0) {
        pendingCount_++;
    }
    pendingSide_[slot] = static_cast<std::int8_t>(buy ? 1 : -1);
    pendingMid_[slot] = midAtRequest;
    pendingTs_[slot] = timestampNanos;
    pendingCursor_[lp] = static_cast<std::uint8_t>((pendingCursor_[lp] + 1) % PENDING_RING);
}

void LpScorecard::onMid(double mid, std::int64_t timestampNanos) {
    if (pendingCount_ == 0 || !std::isfinite(mid)) {
        return;
    }
    int n = lpCount_ * PENDING_RING;
    for (int slot = 0; slot < n; slot++) {
        if (pendingSide_[slot] != 0 && timestampNanos - pendingTs_[slot] >= markoutHorizonNanos_) {
            int lp = slot / PENDING_RING;
            double move = pendingSide_[slot] * (mid - pendingMid_[slot]);
            markoutEwma_[lp] = markoutCount_[lp] == 0
                              ? move
                              : markoutEwma_[lp] + alpha_ * (move - markoutEwma_[lp]);
            markoutCount_[lp]++;
            pendingSide_[slot] = 0;
            pendingCount_--;
            maturedMarkouts_++;
        }
    }
}

}  // namespace quantfinlib
