// Port of com.quantfinlib.fx.SyntheticCross (Java): direct-versus-synthetic
// cross execution arithmetic. An FX cross (EURJPY) can be dealt directly or
// replicated through its liquid legs (buy EURUSD, buy USDJPY), and the
// cheaper route changes with every quote. Spread composition is the whole
// point: a synthetic buy pays the ask on both legs (MULTIPLY: askA x askB)
// or the ask of one and the bid of the other (DIVIDE: askA / bidB) — two
// half-spreads against one on the direct route. Static, primitives-only,
// zero allocation.
//
// DEVIATION FROM JAVA: the Java Op enum lives on com.quantfinlib.fx.CrossRateEngine,
// which is skipped in this port (it depends on the HftMarketDataBus/TickListener
// streaming infrastructure, out of scope). Op is relocated here, at namespace
// scope, since SyntheticCross is its only in-scope consumer.
#pragma once

namespace quantfinlib {

/** How the two leg prices compose into the cross (see CrossRateEngine.Op in Java). */
enum class CrossOp {
    /** A/B x B/C = A/C (shared middle currency). */
    MULTIPLY,
    /** A/C / B/C = A/B (shared quote currency). */
    DIVIDE
};

namespace synthetic_cross {

/**
 * All-in synthetic ASK (cost to BUY the cross via the legs).
 * MULTIPLY (A/B x B/C = A/C): buy both legs -> askA x askB.
 * DIVIDE (A/C / B/C = A/B): buy leg A, sell leg B -> askA / bidB.
 */
double syntheticAsk(CrossOp op, double bidA, double askA, double bidB, double askB);

/**
 * All-in synthetic BID (proceeds of SELLING the cross via the legs):
 * the mirror of syntheticAsk.
 */
double syntheticBid(CrossOp op, double bidA, double askA, double bidB, double askB);

/**
 * Savings per unit of buying synthetically instead of directly
 * (positive = the legs are cheaper). NaN when either route is unpriced —
 * where "unpriced" means NaN, zero, or negative — so an unquoted book can
 * never masquerade as an attractive route, including via the divide-by-zero
 * infinity a raw syntheticBid would produce.
 */
double buySavings(double directAsk, CrossOp op, double bidA, double askA,
                  double bidB, double askB);

/** Mirror: extra proceeds per unit of selling via the legs (positive = legs win). */
double sellSavings(double directBid, CrossOp op, double bidA, double askA,
                   double bidB, double askB);

/** True when buying through the legs beats the direct ask (NaN-safe: false). */
bool buySyntheticWins(double directAsk, CrossOp op, double bidA, double askA,
                      double bidB, double askB);

/** True when selling through the legs beats the direct bid (NaN-safe: false). */
bool sellSyntheticWins(double directBid, CrossOp op, double bidA, double askA,
                       double bidB, double askB);

}  // namespace synthetic_cross
}  // namespace quantfinlib
