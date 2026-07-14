// Port of com.quantfinlib.microstructure.Vpin: Volume-synchronized
// Probability of INformed trading (Easley, Lopez de Prado & O'Hara).
// Trades fill fixed-VOLUME buckets (volume time); each full bucket scores
// its absolute buy/sell imbalance; VPIN is the average over the last
// `window` completed buckets. A trade larger than the bucket's remaining
// capacity splits across buckets. The full-bucket count is clamped BEFORE
// adding to `filled` so a corrupt huge trade cannot wrap it negative
// (Java's long-overflow-avoidance clamp, preserved here).
#pragma once

#include <cstdint>
#include <vector>

namespace quantfinlib {

class Vpin {
public:
    /**
     * @param bucketVolume shares/contracts per volume bucket, > 0
     * @param window       completed buckets averaged, >= 1 (classic: 50)
     */
    Vpin(std::int64_t bucketVolume, int window);

    /** One classified trade; splits across bucket boundaries as needed. */
    void onTrade(std::int64_t quantity, bool buyAggressor);

    /** The toxicity estimate over completed buckets; NaN until the first bucket completes. */
    double vpin() const;

    /** True once the full window of buckets has completed. */
    bool ready() const { return filled_ == static_cast<int>(imbalances_.size()); }

    int bucketsCompleted() const { return filled_; }

private:
    void completeBucket();

    std::int64_t bucketVolume_;
    std::vector<double> imbalances_;
    int filled_ = 0;
    int head_ = 0;
    std::int64_t bucketBuy_ = 0;
    std::int64_t bucketSell_ = 0;
};

}  // namespace quantfinlib
