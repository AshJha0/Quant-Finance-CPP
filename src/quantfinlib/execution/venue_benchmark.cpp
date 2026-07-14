#include "quantfinlib/execution/venue_benchmark.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_map>

namespace quantfinlib {

VenueBenchmark& VenueBenchmark::add(const Sample& sample) {
    samples_.push_back(sample);
    return *this;
}

std::vector<VenueBenchmark::VenueStats> VenueBenchmark::rank() const {
    std::vector<std::string> order;
    std::unordered_map<std::string, std::vector<const Sample*>> byVenue;
    for (const Sample& s : samples_) {
        auto it = byVenue.find(s.venue);
        if (it == byVenue.end()) {
            order.push_back(s.venue);
            byVenue[s.venue].push_back(&s);
        } else {
            it->second.push_back(&s);
        }
    }
    std::vector<VenueStats> out;
    out.reserve(order.size());
    for (const std::string& venue : order) {
        const std::vector<const Sample*>& list = byVenue[venue];
        int attempts = static_cast<int>(list.size());
        int fills = 0;
        double effSpread = 0, markout = 0, latency = 0;
        for (const Sample* s : list) {
            if (!s->filled) {
                continue;
            }
            fills++;
            int sgn = sign(s->side);
            effSpread += 2.0 * sgn * (s->price - s->midAtExec) / s->midAtExec * 1e4;
            markout += sgn * (s->midAfterHorizon - s->price) / s->price * 1e4;
            latency += static_cast<double>(s->latencyToFillNanos) / 1e6;
        }
        out.push_back(VenueStats{venue, attempts, static_cast<double>(fills) / attempts,
                                 fills == 0 ? std::nan("") : effSpread / fills,
                                 fills == 0 ? std::nan("") : markout / fills,
                                 fills == 0 ? std::nan("") : latency / fills});
    }
    std::stable_sort(out.begin(), out.end(), [](const VenueStats& a, const VenueStats& b) {
        double ka = std::isnan(a.avgEffectiveSpreadBps) ? std::numeric_limits<double>::max()
                                                        : a.avgEffectiveSpreadBps - a.avgMarkoutBps;
        double kb = std::isnan(b.avgEffectiveSpreadBps) ? std::numeric_limits<double>::max()
                                                        : b.avgEffectiveSpreadBps - b.avgMarkoutBps;
        return ka < kb;
    });
    return out;
}

}  // namespace quantfinlib
