// Port of com.quantfinlib.microstructure.HiddenLiquidityDetector: hidden-
// liquidity / iceberg detection from the lit tape. The sound per-print
// signature is a single execution larger than the size displayed at that
// moment -- the excess necessarily executed against hidden size. Per
// level, keeps an EWMA of the print/displayed ratio at those hidden
// events (this class covers both "iceberg" and general hidden-liquidity
// detection -- there is no separate IcebergDetector in the Java source).
#pragma once

#include <cstdint>
#include <vector>

namespace quantfinlib {

class HiddenLiquidityDetector {
public:
    /**
     * @param levels number of price levels tracked (dense tick indices)
     * @param alpha  EWMA weight on each refill observation, e.g. 0.2
     */
    explicit HiddenLiquidityDetector(int levels, double alpha = 0.2);

    /** The displayed size now standing at `level`. */
    void onDisplayed(int level, std::int64_t size);

    /** One trade print of qty at level, compared against the size displayed. */
    void onExecution(int level, std::int64_t qty);

    /** The level fully cleared (best moved away / all pulled). */
    void onLevelCleared(int level);

    /** Estimated ratio of true resting size to displayed size at a level. */
    double hiddenMultiplier(int level) const;

    /** Estimated total resting size at a level = displayed x hidden multiplier. */
    double estimatedTrueDepth(int level) const;

    /** True once a level has shown iceberg behavior at least once. */
    bool isIceberg(int level) const;

    std::int64_t refillObservations(int level) const { return refillObs_[static_cast<std::size_t>(level)]; }

    int levels() const { return levels_; }

private:
    int levels_;
    double alpha_;
    std::vector<std::int64_t> displayed_;
    std::vector<double> refillRatioEwma_;
    std::vector<std::int64_t> refillObs_;
};

}  // namespace quantfinlib
