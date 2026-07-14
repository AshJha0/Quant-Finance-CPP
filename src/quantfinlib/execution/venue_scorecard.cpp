#include "quantfinlib/execution/venue_scorecard.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace quantfinlib {

VenueScorecard::VenueScorecard(int venueCount, double alpha, double fillRatePrior,
                               std::int64_t markoutHorizonNanos)
        : venueCount_(venueCount),
          alpha_(alpha),
          fillRatePrior_(fillRatePrior),
          markoutHorizonNanos_(markoutHorizonNanos) {
    if (venueCount < 1 || alpha <= 0 || alpha > 1 || fillRatePrior <= 0 || fillRatePrior > 1
            || markoutHorizonNanos <= 0) {
        throw std::invalid_argument(
                "need venueCount >= 1, alpha in (0,1], prior in (0,1], horizon > 0");
    }
    auto n = static_cast<std::size_t>(venueCount);
    sent_.assign(n, 0);
    filled_.assign(n, 0);
    fillRateEwma_.assign(n, 0.0);
    latencyNanosEwma_.assign(n, 0.0);
    hiddenFillEwma_.assign(n, 0.0);
    probes_.assign(n, 0);
    markoutEwma_.assign(n, 0.0);
    markoutCount_.assign(n, 0);
    pendingSide_.assign(n * PENDING_RING, 0);
    pendingMid_.assign(n * PENDING_RING, 0.0);
    pendingTs_.assign(n * PENDING_RING, 0);
    pendingCursor_.assign(n, 0);
}

VenueScorecard::VenueScorecard(int venueCount, double alpha, double fillRatePrior)
        : VenueScorecard(venueCount, alpha, fillRatePrior, 100'000'000LL) {}

VenueScorecard::VenueScorecard(int venueCount) : VenueScorecard(venueCount, 0.05, 0.95) {}

void VenueScorecard::seedOnFirstEvent(int venue, std::int64_t responseNanos) {
    auto v = static_cast<std::size_t>(venue);
    if (sent_[v] == 0) {
        fillRateEwma_[v] = fillRatePrior_;
        latencyNanosEwma_[v] = static_cast<double>(responseNanos);
    }
}

void VenueScorecard::onFill(int venue, std::int64_t responseNanos) {
    // EWMAs seed from the prior / first observation -- ramping from 0
    // would record a venue's FIRST successful fill as fillRate 0.05 and
    // get it vetoed by the router: success must never score below "never
    // tried".
    seedOnFirstEvent(venue, responseNanos);
    auto v = static_cast<std::size_t>(venue);
    sent_[v]++;
    filled_[v]++;
    fillRateEwma_[v] += alpha_ * (1 - fillRateEwma_[v]);
    latencyNanosEwma_[v] += alpha_ * (static_cast<double>(responseNanos) - latencyNanosEwma_[v]);
}

void VenueScorecard::onMiss(int venue, std::int64_t responseNanos) {
    seedOnFirstEvent(venue, responseNanos);
    auto v = static_cast<std::size_t>(venue);
    sent_[v]++;
    fillRateEwma_[v] += alpha_ * (0 - fillRateEwma_[v]);
    latencyNanosEwma_[v] += alpha_ * (static_cast<double>(responseNanos) - latencyNanosEwma_[v]);
}

void VenueScorecard::onFill(int venue, std::int64_t responseNanos, bool buy, double midAtFill,
                            std::int64_t timestampNanos) {
    onFill(venue, responseNanos);
    if (!std::isfinite(midAtFill)) {
        return;  // an Inf sentinel must not arm either
    }
    auto v = static_cast<std::size_t>(venue);
    std::size_t slot = v * PENDING_RING + pendingCursor_[v];
    if (pendingSide_[slot] == 0) {
        pendingCount_++;  // fresh slot; overwrite keeps count
    }
    pendingSide_[slot] = static_cast<std::int8_t>(buy ? 1 : -1);
    pendingMid_[slot] = midAtFill;
    pendingTs_[slot] = timestampNanos;
    pendingCursor_[v] = static_cast<std::uint8_t>((pendingCursor_[v] + 1) % PENDING_RING);
}

void VenueScorecard::onMid(double mid, std::int64_t timestampNanos) {
    if (pendingCount_ == 0 || !std::isfinite(mid)) {
        return;
    }
    std::size_t n = static_cast<std::size_t>(venueCount_) * PENDING_RING;
    for (std::size_t slot = 0; slot < n; slot++) {
        if (pendingSide_[slot] != 0 && timestampNanos - pendingTs_[slot] >= markoutHorizonNanos_) {
            std::size_t venue = slot / PENDING_RING;
            double move = pendingSide_[slot] * (mid - pendingMid_[slot]);
            // Seed from the first matured markout -- ramping from 0 would
            // read a venue's first adverse fills at ~5% strength.
            markoutEwma_[venue] = markoutCount_[venue] == 0
                    ? move
                    : markoutEwma_[venue] + alpha_ * (move - markoutEwma_[venue]);
            markoutCount_[venue]++;
            pendingSide_[slot] = 0;
            pendingCount_--;
            maturedFillMarkouts_++;
        }
    }
}

void VenueScorecard::onDarkProbe(int venue, std::int64_t sharesFilled) {
    auto v = static_cast<std::size_t>(venue);
    hiddenFillEwma_[v] = probes_[v] == 0
            ? static_cast<double>(sharesFilled)
            : hiddenFillEwma_[v] + alpha_ * (static_cast<double>(sharesFilled) - hiddenFillEwma_[v]);
    probes_[v]++;
}

double VenueScorecard::fillRate(int venue) const {
    auto v = static_cast<std::size_t>(venue);
    return sent_[v] == 0 ? fillRatePrior_ : fillRateEwma_[v];
}

double VenueScorecard::measuredLatencyNanos(int venue) const {
    return latencyNanosEwma_[static_cast<std::size_t>(venue)];
}

double VenueScorecard::expectedHiddenShares(int venue) const {
    return hiddenFillEwma_[static_cast<std::size_t>(venue)];
}

double VenueScorecard::postFillMarkout(int venue) const {
    return markoutEwma_[static_cast<std::size_t>(venue)];
}

std::int64_t VenueScorecard::sent(int venue) const {
    return sent_[static_cast<std::size_t>(venue)];
}

std::int64_t VenueScorecard::filled(int venue) const {
    return filled_[static_cast<std::size_t>(venue)];
}

std::int64_t VenueScorecard::probes(int venue) const {
    return probes_[static_cast<std::size_t>(venue)];
}

void VenueScorecard::writeState(BinWriter& out) const {
    out.writeByte(2);
    Checkpoint::writeLongs(out, sent_);
    Checkpoint::writeLongs(out, filled_);
    Checkpoint::writeLongs(out, probes_);
    Checkpoint::writeDoubles(out, fillRateEwma_);
    Checkpoint::writeDoubles(out, latencyNanosEwma_);
    Checkpoint::writeDoubles(out, hiddenFillEwma_);
    Checkpoint::writeDoubles(out, markoutEwma_);
    Checkpoint::writeLongs(out, markoutCount_);
    out.writeLong(maturedFillMarkouts_);
}

void VenueScorecard::readState(BinReader& in) {
    int v = in.readByte();
    if (v != 1 && v != 2) {
        throw std::runtime_error("VenueScorecard state version " + std::to_string(v)
                                  + " not supported (this build reads versions 1-2)");
    }
    Checkpoint::readLongsInto(in, sent_);
    Checkpoint::readLongsInto(in, filled_);
    Checkpoint::readLongsInto(in, probes_);
    Checkpoint::readDoublesInto(in, fillRateEwma_);
    Checkpoint::readDoublesInto(in, latencyNanosEwma_);
    Checkpoint::readDoublesInto(in, hiddenFillEwma_);
    if (v >= 2) {
        Checkpoint::readDoublesInto(in, markoutEwma_);
        Checkpoint::readLongsInto(in, markoutCount_);
        maturedFillMarkouts_ = in.readLong();
    } else {
        std::fill(markoutEwma_.begin(), markoutEwma_.end(), 0.0);
        std::fill(markoutCount_.begin(), markoutCount_.end(), 0);
        maturedFillMarkouts_ = 0;
    }
    std::fill(pendingSide_.begin(), pendingSide_.end(), static_cast<std::int8_t>(0));
    std::fill(pendingCursor_.begin(), pendingCursor_.end(), static_cast<std::uint8_t>(0));
    pendingCount_ = 0;
}

}  // namespace quantfinlib
