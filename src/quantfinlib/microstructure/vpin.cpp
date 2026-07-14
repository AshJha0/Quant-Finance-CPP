#include "quantfinlib/microstructure/vpin.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace quantfinlib {

Vpin::Vpin(std::int64_t bucketVolume, int window) : bucketVolume_(bucketVolume) {
    if (bucketVolume <= 0) {
        throw std::invalid_argument("bucketVolume must be > 0");
    }
    if (window < 1) {
        throw std::invalid_argument("window must be >= 1");
    }
    imbalances_.assign(static_cast<std::size_t>(window), 0.0);
}

void Vpin::onTrade(std::int64_t quantity, bool buyAggressor) {
    if (quantity <= 0) {
        throw std::invalid_argument("quantity must be > 0");
    }
    std::int64_t remaining = quantity;
    std::int64_t capacity = bucketVolume_ - (bucketBuy_ + bucketSell_);
    std::int64_t take = std::min(remaining, capacity);
    if (buyAggressor) {
        bucketBuy_ += take;
    } else {
        bucketSell_ += take;
    }
    remaining -= take;
    if (bucketBuy_ + bucketSell_ == bucketVolume_) {
        completeBucket();
    }
    if (remaining == 0) {
        return;
    }
    std::int64_t full = remaining / bucketVolume_;
    auto windowLen = static_cast<std::int64_t>(imbalances_.size());
    std::int64_t toRecord = std::min(full, windowLen);
    for (std::int64_t i = 0; i < toRecord; i++) {
        imbalances_[static_cast<std::size_t>(head_)] = 1.0;
        head_ = static_cast<int>((head_ + 1) % imbalances_.size());
    }
    // Clamp full BEFORE adding: with bucketVolume == 1 a corrupt huge trade
    // would otherwise make filled + full overflow/wrap.
    filled_ = static_cast<int>(std::min<std::int64_t>(windowLen, filled_ + std::min(full, windowLen)));
    remaining -= full * bucketVolume_;
    if (remaining > 0) {
        if (buyAggressor) {
            bucketBuy_ = remaining;
        } else {
            bucketSell_ = remaining;
        }
    }
}

void Vpin::completeBucket() {
    imbalances_[static_cast<std::size_t>(head_)] =
            std::abs(static_cast<double>(bucketBuy_ - bucketSell_)) / static_cast<double>(bucketVolume_);
    head_ = static_cast<int>((head_ + 1) % imbalances_.size());
    if (filled_ < static_cast<int>(imbalances_.size())) {
        filled_++;
    }
    bucketBuy_ = 0;
    bucketSell_ = 0;
}

double Vpin::vpin() const {
    if (filled_ == 0) {
        return std::numeric_limits<double>::quiet_NaN();
    }
    double sum = 0;
    for (int i = 0; i < filled_; i++) {
        sum += imbalances_[static_cast<std::size_t>(i)];
    }
    return sum / filled_;
}

}  // namespace quantfinlib
