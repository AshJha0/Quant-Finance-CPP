// Port of com.quantfinlib.fx.FixingRisk (Java): analytics for benchmark-
// fixing exposure — the risk concentrated in the short calculation window
// of an official fix (WM/R 4pm London, the RBI reference rate an NDF
// settles on, an equity closing auction). A book that must trade AT the
// fix cannot execute at a single instant: it works the order through the
// window and receives roughly the window TWAP/VWAP, while its liability
// references the fix print. windowTwap/windowVwap measure what execution
// achieves; slippageVsFix the realized tracking difference in pips;
// trackingErrorStd the ex-ante 1-sigma of that difference from diffusion
// alone (sigma^2*T/3, the classic TWAP-vs-close result); participationRate
// the share of window volume. All static, allocation-free.
#pragma once

#include <vector>

#include "quantfinlib/fx/currency_pair.hpp"

namespace quantfinlib {
namespace fixing_risk {

/** Time-weighted average of window prices (equally spaced observations). */
double windowTwap(const std::vector<double>& prices);

/** Volume-weighted average of window prices. */
double windowVwap(const std::vector<double>& prices, const std::vector<double>& sizes);

/** Realized slippage of an achieved price vs the fix print, in pips (signed, buy side). */
double slippageVsFix(const CurrencyPair& pair, double achievedPrice, double fixPrice);

/**
 * Ex-ante 1-sigma tracking error (price terms) between a fix print and a
 * uniform execution across the window, from diffusion alone: for
 * arithmetic Brownian motion with per-sqrt-minute vol sigma, the variance
 * of (fix - TWAP) over a window of length T minutes ending at the fix is
 * sigma^2*T/3.
 *
 * @param volPerSqrtMinute price vol per sqrt-minute (e.g. daily vol / sqrt(24*60))
 * @param windowMinutes    fix calculation window length
 */
double trackingErrorStd(double volPerSqrtMinute, double windowMinutes);

/**
 * Order size as a fraction of expected window volume - above ~20% the
 * order moves the fix it is trying to match, and impact (not tracking
 * noise) dominates.
 */
double participationRate(double orderQty, double expectedWindowVolume);

}  // namespace fixing_risk
}  // namespace quantfinlib
