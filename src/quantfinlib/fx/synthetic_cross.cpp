#include "quantfinlib/fx/synthetic_cross.hpp"

#include <limits>

namespace quantfinlib {
namespace synthetic_cross {

namespace {

/** A dealable price: finite and strictly positive. */
bool priced(double p) {
    return p > 0 && p < std::numeric_limits<double>::infinity();
}

}  // namespace

double syntheticAsk(CrossOp op, double /*bidA*/, double askA, double bidB, double askB) {
    return op == CrossOp::MULTIPLY ? askA * askB : askA / bidB;
}

double syntheticBid(CrossOp op, double bidA, double /*askA*/, double bidB, double askB) {
    return op == CrossOp::MULTIPLY ? bidA * bidB : bidA / askB;
}

double buySavings(double directAsk, CrossOp op, double bidA, double askA,
                  double bidB, double askB) {
    if (!priced(directAsk) || !priced(askA)
            || !priced(op == CrossOp::MULTIPLY ? askB : bidB)) {
        return std::numeric_limits<double>::quiet_NaN();
    }
    return directAsk - syntheticAsk(op, bidA, askA, bidB, askB);
}

double sellSavings(double directBid, CrossOp op, double bidA, double askA,
                   double bidB, double askB) {
    if (!priced(directBid) || !priced(bidA)
            || !priced(op == CrossOp::MULTIPLY ? bidB : askB)) {
        return std::numeric_limits<double>::quiet_NaN();
    }
    return syntheticBid(op, bidA, askA, bidB, askB) - directBid;
}

bool buySyntheticWins(double directAsk, CrossOp op, double bidA, double askA,
                      double bidB, double askB) {
    return buySavings(directAsk, op, bidA, askA, bidB, askB) > 0;
}

bool sellSyntheticWins(double directBid, CrossOp op, double bidA, double askA,
                       double bidB, double askB) {
    return sellSavings(directBid, op, bidA, askA, bidB, askB) > 0;
}

}  // namespace synthetic_cross
}  // namespace quantfinlib
