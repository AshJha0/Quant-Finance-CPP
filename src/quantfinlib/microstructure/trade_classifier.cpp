#include "quantfinlib/microstructure/trade_classifier.hpp"

#include <cmath>
#include <limits>

namespace quantfinlib {

void TradeClassifier::onQuote(double bid, double ask) {
    bid_ = bid;
    ask_ = ask;
}

int TradeClassifier::classify(double tradePrice) {
    double inf = std::numeric_limits<double>::infinity();
    if (!(tradePrice > 0) || tradePrice == inf) {
        return UNKNOWN;
    }
    int result = quoteRule(tradePrice);
    if (result == UNKNOWN) {
        result = tickTest(tradePrice);
    }
    lastTradePrice_ = tradePrice;
    if (result != UNKNOWN) {
        lastClassification_ = result;
    }
    return result;
}

bool TradeClassifier::isBuyAggressor(double tradePrice) {
    int c = classify(tradePrice);
    return (c == UNKNOWN ? lastClassification_ : c) == BUY;
}

int TradeClassifier::quoteRule(double price) const {
    bool hasBid = !std::isnan(bid_);
    bool hasAsk = !std::isnan(ask_);
    if (hasAsk && price >= ask_) {
        return BUY;
    }
    if (hasBid && price <= bid_) {
        return SELL;
    }
    if (hasBid && hasAsk) {
        double mid = 0.5 * (bid_ + ask_);
        if (price > mid) {
            return BUY;
        }
        if (price < mid) {
            return SELL;
        }
    }
    return UNKNOWN;
}

int TradeClassifier::tickTest(double price) const {
    if (std::isnan(lastTradePrice_)) {
        return UNKNOWN;
    }
    if (price > lastTradePrice_) {
        return BUY;
    }
    if (price < lastTradePrice_) {
        return SELL;
    }
    return lastClassification_;
}

}  // namespace quantfinlib
