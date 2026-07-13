#include "quantfinlib/data/bar.hpp"

#include <stdexcept>
#include <string>

namespace quantfinlib {

Bar::Bar(std::int64_t timestamp, double open, double high, double low,
         double close, double volume)
    : timestamp_(timestamp),
      open_(open),
      high_(high),
      low_(low),
      close_(close),
      volume_(volume) {
    if (high < low) {
        throw std::invalid_argument("high (" + std::to_string(high)
                                    + ") < low (" + std::to_string(low) + ")");
    }
}

double Bar::typicalPrice() const {
    return (high_ + low_ + close_) / 3.0;
}

double Bar::range() const {
    return high_ - low_;
}

bool Bar::isBullish() const {
    return close_ > open_;
}

}  // namespace quantfinlib
