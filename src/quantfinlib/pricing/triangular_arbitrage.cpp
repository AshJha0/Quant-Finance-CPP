#include "quantfinlib/pricing/triangular_arbitrage.hpp"

#include <algorithm>

namespace quantfinlib {
namespace triangular_arbitrage {

double arbitrageBps(const Quote& ab, const Quote& bc, const Quote& ac) {
    double syntheticAsk = ab.ask * bc.ask;  // cost in C to build one A via B
    double syntheticBid = ab.bid * bc.bid;  // proceeds in C unwinding one A via B
    double path1 = (ac.bid - syntheticAsk) / syntheticAsk;
    double path2 = (syntheticBid - ac.ask) / ac.ask;
    return std::max(path1, path2) * 1e4;
}

bool exists(const Quote& ab, const Quote& bc, const Quote& ac,
            double thresholdBps) {
    return arbitrageBps(ab, bc, ac) > thresholdBps;
}

double impliedCrossMid(const Quote& ab, const Quote& bc) {
    return ab.mid() * bc.mid();
}

}  // namespace triangular_arbitrage
}  // namespace quantfinlib
