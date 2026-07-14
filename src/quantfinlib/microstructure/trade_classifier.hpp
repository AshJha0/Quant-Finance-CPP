// Port of com.quantfinlib.microstructure.TradeClassifier: Lee-Ready (1991)
// trade aggressor classification -- quote rule (at/through the quotes,
// else lean on the mid) falling back to the tick test (uptick/downtick/
// zero-tick repeats the last side). One instance per symbol.
#pragma once

#include <limits>

namespace quantfinlib {

class TradeClassifier {
public:
    static constexpr int BUY = 1;
    static constexpr int SELL = -1;
    static constexpr int UNKNOWN = 0;

    /** The current inside quote (NaN sides are treated as absent). */
    void onQuote(double bid, double ask);

    /** Classifies a trade print and remembers it for the tick test. */
    int classify(double tradePrice);

    /** Convenience: UNKNOWN maps to the last known side. */
    bool isBuyAggressor(double tradePrice);

private:
    int quoteRule(double price) const;
    int tickTest(double price) const;

    double bid_ = std::numeric_limits<double>::quiet_NaN();
    double ask_ = std::numeric_limits<double>::quiet_NaN();
    double lastTradePrice_ = std::numeric_limits<double>::quiet_NaN();
    int lastClassification_ = UNKNOWN;
};

}  // namespace quantfinlib
