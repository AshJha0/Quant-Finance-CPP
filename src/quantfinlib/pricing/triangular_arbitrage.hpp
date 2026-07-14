// Port of com.quantfinlib.pricing.TriangularArbitrage (Java): triangular
// arbitrage checks over three related FX pairs, using dealable bid/ask
// quotes (not mids), so a positive result is executable edge before fees.
// Conventions: ab is the price of one unit of A in B, bc of one B in C,
// ac of one A in C — e.g. A=EUR, B=USD, C=JPY: ab=EURUSD, bc=USDJPY,
// ac=EURJPY.
#pragma once

#include <stdexcept>
#include <string>

namespace quantfinlib {
namespace triangular_arbitrage {

struct Quote {
    double bid;
    double ask;

    Quote(double bid_, double ask_) : bid(bid_), ask(ask_) {
        if (ask < bid) {
            throw std::invalid_argument(
                    "crossed quote: bid " + std::to_string(bid)
                    + " > ask " + std::to_string(ask));
        }
    }

    double mid() const { return (bid + ask) / 2; }
};

/**
 * Best executable round-trip edge in basis points (positive = arbitrage):
 * path 1 buys A synthetically via B (ab.ask * bc.ask) and sells it directly
 * at ac.bid; path 2 buys A directly at ac.ask and sells it via B at
 * ab.bid * bc.bid.
 */
double arbitrageBps(const Quote& ab, const Quote& bc, const Quote& ac);

/** True when the executable edge exceeds thresholdBps (e.g. costs). */
bool exists(const Quote& ab, const Quote& bc, const Quote& ac,
            double thresholdBps);

/** The no-arbitrage cross rate implied by the two leg mids. */
double impliedCrossMid(const Quote& ab, const Quote& bc);

}  // namespace triangular_arbitrage
}  // namespace quantfinlib
