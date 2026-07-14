// Port of com.quantfinlib.microstructure.TickSizeSchedule (Java):
// price-banded tick sizes -- the MiFID II / ESMA RTS 11 regime where the
// minimum price increment depends on the instrument's price (and liquidity
// band), rather than being one flat number. US equities are the degenerate
// single-band case ($0.01 above $1).
//
// Two ways to build one: builder() for explicit bands loaded from a
// venue's published table, or esmaStyle(band) for a generated 1-2-5
// progression per price decade (faithful in structure and magnitude, not a
// certified copy of RTS 11 Annex).
//
// Lookups are a binary search over primitive arrays -- allocation-free and
// cheap enough for a quoter to call per quote update. Rounding helpers come
// in directional flavors because order prices must round toward passivity
// (buys down, sells up) to stay exchange-valid.
#pragma once

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <vector>

namespace quantfinlib {

class TickSizeSchedule {
public:
    class Builder {
    public:
        /** The tick that applies from floorPrice up to the next band's floor. */
        Builder& addBand(double floorPrice, double tick) {
            if (floorPrice < 0 || tick <= 0) {
                throw std::invalid_argument("floor must be >= 0 and tick > 0");
            }
            bands_.push_back({floorPrice, tick});
            return *this;
        }

        TickSizeSchedule build() const {
            if (bands_.empty()) {
                throw std::runtime_error("at least one band required");
            }
            std::vector<std::pair<double, double>> sorted = bands_;
            std::sort(sorted.begin(), sorted.end(),
                      [](const auto& a, const auto& b) { return a.first < b.first; });
            std::vector<double> floors(sorted.size());
            std::vector<double> ticks(sorted.size());
            for (std::size_t i = 0; i < sorted.size(); i++) {
                floors[i] = sorted[i].first;
                ticks[i] = sorted[i].second;
                if (i > 0 && floors[i] == floors[i - 1]) {
                    throw std::invalid_argument("duplicate band floor " + std::to_string(floors[i]));
                }
            }
            return TickSizeSchedule(std::move(floors), std::move(ticks));
        }

    private:
        std::vector<std::pair<double, double>> bands_;
    };

    /** Single flat tick for every price (US-equity style). */
    static TickSizeSchedule flat(double tick) {
        if (tick <= 0) {
            throw std::invalid_argument("tick must be > 0");
        }
        return TickSizeSchedule({0.0}, {tick});
    }

    /**
     * ESMA-style generated schedule: within each price decade the bands
     * [1,2), [2,5), [5,10) carry ticks in a 1-2-5 progression, all scaled
     * up by one decade per liquidityBand step (0 = most liquid). Covers
     * prices from 0.0001 to 100,000.
     */
    static TickSizeSchedule esmaStyle(int liquidityBand) {
        if (liquidityBand < 0 || liquidityBand > 5) {
            throw std::invalid_argument("liquidityBand must be 0..5");
        }
        Builder b;
        for (int decade = -4; decade <= 4; decade++) {
            double base = std::pow(10.0, decade);
            double tick = std::pow(10.0, decade - 4 + liquidityBand);
            b.addBand(base, tick);
            b.addBand(2 * base, 2 * tick);
            b.addBand(5 * base, 5 * tick);
        }
        return b.build();
    }

    static Builder builder() { return Builder(); }

    // ------------------------------------------------------------------
    // Lookups (allocation-free)
    // ------------------------------------------------------------------

    /**
     * Like tickFor, but prices below the first band floor take the first
     * band's tick instead of throwing -- for engines (backtesters,
     * quoters) that must stay total over any positive price.
     */
    double tickForClamped(double price) const {
        return price < bandFloors_[0] ? ticks_[0] : tickFor(price);
    }

    /** The minimum increment in force at a price. */
    double tickFor(double price) const {
        if (price < bandFloors_[0]) {
            throw std::invalid_argument("price below the first band floor");
        }
        std::size_t lo = 0;
        std::size_t hi = bandFloors_.size() - 1;
        while (lo < hi) {
            std::size_t mid = (lo + hi + 1) / 2;
            if (bandFloors_[mid] <= price) {
                lo = mid;
            } else {
                hi = mid - 1;
            }
        }
        return ticks_[lo];
    }

    /** Rounds down to the grid -- the passive direction for a buy order. */
    double roundDown(double price) const {
        double tick = tickFor(price);
        return std::floor(price / tick + 1e-9) * tick;
    }

    /** Rounds up to the grid -- the passive direction for a sell order. */
    double roundUp(double price) const {
        double tick = tickFor(price);
        return std::ceil(price / tick - 1e-9) * tick;
    }

    /** roundDown with the clamped tick -- total over any price, for hot paths. */
    double roundDownClamped(double price) const {
        double tick = tickForClamped(price);
        return std::floor(price / tick + 1e-9) * tick;
    }

    /** roundUp with the clamped tick -- see roundDownClamped. */
    double roundUpClamped(double price) const {
        double tick = tickForClamped(price);
        return std::ceil(price / tick - 1e-9) * tick;
    }

    /** Rounds to the nearest grid point (marks, reference prices). */
    double roundNearest(double price) const {
        double tick = tickFor(price);
        return std::round(price / tick) * tick;
    }

    /** Whether a price already sits on the grid (within FP tolerance). */
    bool isOnTick(double price) const {
        double tick = tickFor(price);
        double ratio = price / tick;
        return std::fabs(ratio - std::round(ratio)) < 1e-6;
    }

    int bandCount() const { return static_cast<int>(bandFloors_.size()); }

private:
    TickSizeSchedule(std::vector<double> bandFloors, std::vector<double> ticks)
            : bandFloors_(std::move(bandFloors)), ticks_(std::move(ticks)) {}

    std::vector<double> bandFloors_;  // ascending; band i applies to [floor[i], floor[i+1])
    std::vector<double> ticks_;
};

}  // namespace quantfinlib
