// Terminal corporate actions, resolved the same way for every engine. The
// portfolio and alpha backtesters both call schedule(), so a delisting or a
// merger means one thing whichever engine runs it.
//
// A merger converts a holder at the deal terms on the effective bar: each
// share becomes cashPerShare in cash plus acquirerSharesPerShare shares of the
// acquirer. Cash, stock and mixed deals are the same formula. Shorts convert
// too: they pay the cash and end up short the acquirer. Nothing about a deal
// touches a position, a price or a weight before the effective bar, and the
// conversion itself pays no trading cost.
//
// Sources, for both engines: Config::delistings, Config::mergers, and the
// PointInTimeUniverse's terminal events. A universe event lands on the first
// bar at or after its timestamp, never on bar 0. An event in Config replaces
// the universe's for that symbol; two Config events for one symbol throw.
//
// A stock deal needs the acquirer in the panel and still trading on the
// effective bar, otherwise schedule() throws: guessing would invent value.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "quantfinlib/backtest/delisting.hpp"
#include "quantfinlib/data/point_in_time_universe.hpp"

namespace quantfinlib {

struct Merger {
    std::string symbol;
    int barIndex = 0;  // effective bar: the first bar the target no longer trades
    double cashPerShare = 0;
    double acquirerSharesPerShare = 0;
    std::string acquirer;  // empty for an all-cash deal
};

namespace corporate_actions {

/** One symbol's terminal event, by panel index. bar < 0 means none. */
struct TerminalAction {
    int bar = -1;
    bool merger = false;
    double delistingReturn = 0;
    double cashPerShare = 0;
    double acquirerSharesPerShare = 0;
    int acquirerIndex = -1;

    /** True from the effective bar on. */
    bool goneBy(int index) const { return bar >= 0 && index >= bar; }
};

/**
 * @param symbols    the panel, in index order
 * @param timestamps the panel's bar timestamps
 * @param universe   may be nullptr
 * @throws std::invalid_argument on any event that cannot be applied
 */
std::vector<TerminalAction> schedule(const std::vector<std::string>& symbols,
                                     const std::vector<std::int64_t>& timestamps,
                                     const PointInTimeUniverse* universe,
                                     const std::vector<Delisting>& delistings,
                                     const std::vector<Merger>& mergers);

}  // namespace corporate_actions
}  // namespace quantfinlib
