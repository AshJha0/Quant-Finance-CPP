// Port of com.quantfinlib.data.PointInTimeUniverse: point-in-time universe
// membership -- the engine-side half of survivorship-bias-free
// backtesting. Per symbol records membership intervals (inclusive both
// ends; a symbol can leave and rejoin) and at most one terminal event
// (delisting or merger). Timestamps use the same epoch units as the
// BarSeries being backtested.
#pragma once

#include <cstdint>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace quantfinlib {

class PointInTimeUniverse {
public:
    /** Standard haircut for involuntary delistings with unknown proceeds (Shumway 1997). */
    static constexpr double DEFAULT_INVOLUNTARY_DELISTING_RETURN = -0.30;

    enum class EventType { DELISTING, MERGER };

    struct TerminalEvent {
        std::int64_t timestamp = 0;
        EventType type = EventType::DELISTING;
        double delistingReturn = 0;
        double cashPerShare = 0;
        double acquirerSharesPerShare = 0;
        std::string acquirer;  // empty = no acquirer (delistings, all-cash deals)
    };

    /** Adds a membership interval (inclusive of both endpoints). */
    PointInTimeUniverse& addMembership(const std::string& symbol, std::int64_t fromTimestamp,
                                        std::int64_t toTimestampInclusive);

    /** Membership from a date with no known end (a current constituent). */
    PointInTimeUniverse& addMembership(const std::string& symbol, std::int64_t fromTimestamp);

    /** Records a delisting; throws if the symbol already has a terminal event. */
    PointInTimeUniverse& recordDelisting(const std::string& symbol, std::int64_t timestamp,
                                          double delistingReturn);

    /** Records a merger/acquisition; throws if the symbol already has a terminal event. */
    PointInTimeUniverse& recordMerger(const std::string& symbol, std::int64_t timestamp,
                                       double cashPerShare, double acquirerSharesPerShare,
                                       const std::string& acquirer);

    /** Whether the symbol is a universe member at timestamp. */
    bool isMember(const std::string& symbol, std::int64_t timestamp) const;

    /** All members as of a timestamp, sorted for determinism. */
    std::set<std::string> membersAsOf(std::int64_t timestamp) const;

    /** The symbol's terminal event, or nullopt while it lives. */
    const TerminalEvent* terminalEvent(const std::string& symbol) const;

    /** Every symbol that ever appears in this universe (living and dead). */
    std::set<std::string> allSymbols() const;

private:
    struct Interval {
        std::int64_t from;
        std::int64_t to;
    };

    void putEvent(const std::string& symbol, TerminalEvent event);

    std::unordered_map<std::string, std::vector<Interval>> memberships_;
    std::unordered_map<std::string, TerminalEvent> events_;
};

}  // namespace quantfinlib
