#include "quantfinlib/backtest/corporate_actions.hpp"

#include <algorithm>
#include <cmath>
#include <set>
#include <stdexcept>

namespace quantfinlib {
namespace corporate_actions {

namespace {

int indexOf(const std::vector<std::string>& symbols, const std::string& symbol) {
    auto it = std::find(symbols.begin(), symbols.end(), symbol);
    return it == symbols.end() ? -1 : static_cast<int>(it - symbols.begin());
}

/** Terms of a deal; bar is set by the caller. */
TerminalAction mergerAction(const std::vector<std::string>& symbols, const std::string& target,
                            double cash, double ratio, const std::string& acquirer) {
    if (!std::isfinite(cash) || !std::isfinite(ratio) || cash < 0 || ratio < 0
        || (cash == 0 && ratio == 0)) {
        throw std::invalid_argument("invalid merger terms for " + target);
    }
    TerminalAction a;
    a.merger = true;
    a.cashPerShare = cash;
    a.acquirerSharesPerShare = ratio;
    if (ratio > 0) {
        a.acquirerIndex = indexOf(symbols, acquirer);
        if (a.acquirerIndex < 0 || acquirer == target) {
            throw std::invalid_argument("stock merger of " + target + " needs its acquirer in the panel");
        }
    }
    return a;
}

}  // namespace

std::vector<TerminalAction> schedule(const std::vector<std::string>& symbols,
                                     const std::vector<std::int64_t>& timestamps,
                                     const PointInTimeUniverse* universe,
                                     const std::vector<Delisting>& delistings,
                                     const std::vector<Merger>& mergers) {
    std::vector<TerminalAction> out(symbols.size());
    int bars = static_cast<int>(timestamps.size());

    std::set<std::string> seen;
    auto claim = [&](const std::string& symbol, int bar) {
        int index = indexOf(symbols, symbol);
        if (index < 0 || bar < 1 || bar >= bars || !seen.insert(symbol).second) {
            throw std::invalid_argument("invalid corporate action for " + symbol);
        }
        return static_cast<std::size_t>(index);
    };
    for (const Delisting& d : delistings) {
        std::size_t index = claim(d.symbol, d.barIndex);
        if (!std::isfinite(d.delistingReturn) || d.delistingReturn < -1) {
            throw std::invalid_argument("invalid delisting for " + d.symbol);
        }
        out[index].bar = d.barIndex;
        out[index].delistingReturn = d.delistingReturn;
    }
    for (const Merger& m : mergers) {
        std::size_t index = claim(m.symbol, m.barIndex);
        out[index] = mergerAction(symbols, m.symbol, m.cashPerShare, m.acquirerSharesPerShare, m.acquirer);
        out[index].bar = m.barIndex;
    }

    for (std::size_t i = 0; universe != nullptr && i < symbols.size(); i++) {
        const auto* event = universe->terminalEvent(symbols[i]);
        if (event == nullptr || seen.count(symbols[i]) != 0) {
            continue;
        }
        for (int t = 1; t < bars; t++) {
            if (timestamps[static_cast<std::size_t>(t)] < event->timestamp) {
                continue;
            }
            if (event->type == PointInTimeUniverse::EventType::MERGER) {
                out[i] = mergerAction(symbols, symbols[i], event->cashPerShare,
                                      event->acquirerSharesPerShare, event->acquirer);
            } else {
                out[i].delistingReturn = event->delistingReturn;
            }
            out[i].bar = t;
            break;
        }
    }

    for (std::size_t i = 0; i < out.size(); i++) {
        const TerminalAction& a = out[i];
        if (a.acquirerIndex >= 0 && out[static_cast<std::size_t>(a.acquirerIndex)].goneBy(a.bar)) {
            throw std::invalid_argument("acquirer of " + symbols[i] + " is gone by the effective bar");
        }
    }
    return out;
}

}  // namespace corporate_actions
}  // namespace quantfinlib
