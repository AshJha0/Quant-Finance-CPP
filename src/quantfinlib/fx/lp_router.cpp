#include "quantfinlib/fx/lp_router.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace quantfinlib {

namespace {
constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();
}

LpRouter::LpRouter(const FxTierBook& book, const LpScorecard& card, double maxRejectRate)
    : LpRouter(book, card, maxRejectRate, 0) {}

LpRouter::LpRouter(const FxTierBook& book, const LpScorecard& card, double maxRejectRate,
                   double holdUrgencyBpsPerMs)
    : book_(&book),
      card_(&card),
      maxRejectRate_(maxRejectRate),
      holdUrgencyBpsPerMs_(holdUrgencyBpsPerMs),
      lastQuotedPrice_(kNaN),
      lastExpectedPrice_(kNaN),
      routeCount_(0),
      vetoCount_(0) {
    if (book.lpCount() != card.lpCount()) {
        throw std::invalid_argument("book and scorecard LP counts differ");
    }
    if (maxRejectRate <= 0 || maxRejectRate > 1) {
        throw std::invalid_argument("maxRejectRate must be in (0,1]");
    }
    if (holdUrgencyBpsPerMs < 0) {
        throw std::invalid_argument("holdUrgencyBpsPerMs must be >= 0");
    }
}

int LpRouter::route(bool buy, double size) {
    routeCount_++;
    lastQuotedPrice_ = kNaN;
    lastExpectedPrice_ = kNaN;
    int bestLp = -1;
    double bestExpected = 0;
    double bestQuoted = 0;
    int lps = book_->lpCount();
    for (int lp = 0; lp < lps; lp++) {
        double rate = card_->rejectRate(lp);
        if (rate > maxRejectRate_) {
            vetoCount_++;
            continue;
        }
        double quoted = book_->fullAmountPrice(lp, buy, size);
        double penalty = rate * std::max(card_->postRejectMarkout(lp), 0.0);
        if (holdUrgencyBpsPerMs_ > 0) {
            penalty += quoted * (card_->avgHoldNanos(lp) / 1e6) * holdUrgencyBpsPerMs_ / 1e4;
        }
        double expected = buy ? quoted + penalty : quoted - penalty;
        if (!std::isfinite(expected)) {
            continue;
        }
        if (bestLp == -1 || (buy ? expected < bestExpected : expected > bestExpected)) {
            bestLp = lp;
            bestExpected = expected;
            bestQuoted = quoted;
        }
    }
    if (bestLp >= 0) {
        lastQuotedPrice_ = bestQuoted;
        lastExpectedPrice_ = bestExpected;
    }
    return bestLp;
}

}  // namespace quantfinlib
