#include "quantfinlib/data/point_in_time_universe.hpp"

#include <limits>
#include <stdexcept>

namespace quantfinlib {

PointInTimeUniverse& PointInTimeUniverse::addMembership(const std::string& symbol,
                                                         std::int64_t fromTimestamp,
                                                         std::int64_t toTimestampInclusive) {
    if (toTimestampInclusive < fromTimestamp) {
        throw std::invalid_argument("membership ends before it starts: " + symbol);
    }
    memberships_[symbol].push_back(Interval{fromTimestamp, toTimestampInclusive});
    return *this;
}

PointInTimeUniverse& PointInTimeUniverse::addMembership(const std::string& symbol,
                                                         std::int64_t fromTimestamp) {
    return addMembership(symbol, fromTimestamp, std::numeric_limits<std::int64_t>::max());
}

PointInTimeUniverse& PointInTimeUniverse::recordDelisting(const std::string& symbol,
                                                           std::int64_t timestamp,
                                                           double delistingReturn) {
    if (delistingReturn < -1) {
        throw std::invalid_argument("delisting return cannot be below -100%");
    }
    TerminalEvent e;
    e.timestamp = timestamp;
    e.type = EventType::DELISTING;
    e.delistingReturn = delistingReturn;
    putEvent(symbol, e);
    return *this;
}

PointInTimeUniverse& PointInTimeUniverse::recordMerger(const std::string& symbol,
                                                        std::int64_t timestamp,
                                                        double cashPerShare,
                                                        double acquirerSharesPerShare,
                                                        const std::string& acquirer) {
    if (cashPerShare < 0 || acquirerSharesPerShare < 0) {
        throw std::invalid_argument("deal terms cannot be negative");
    }
    if (acquirerSharesPerShare > 0 && acquirer.empty()) {
        throw std::invalid_argument("stock component needs an acquirer symbol");
    }
    TerminalEvent e;
    e.timestamp = timestamp;
    e.type = EventType::MERGER;
    e.cashPerShare = cashPerShare;
    e.acquirerSharesPerShare = acquirerSharesPerShare;
    e.acquirer = acquirer;
    putEvent(symbol, e);
    return *this;
}

void PointInTimeUniverse::putEvent(const std::string& symbol, TerminalEvent event) {
    if (events_.find(symbol) != events_.end()) {
        throw std::invalid_argument(symbol + " already has a terminal event");
    }
    events_[symbol] = event;
}

bool PointInTimeUniverse::isMember(const std::string& symbol, std::int64_t timestamp) const {
    auto eventIt = events_.find(symbol);
    if (eventIt != events_.end() && timestamp >= eventIt->second.timestamp) {
        return false;
    }
    auto it = memberships_.find(symbol);
    if (it == memberships_.end()) {
        return false;
    }
    for (const auto& iv : it->second) {
        if (timestamp >= iv.from && timestamp <= iv.to) {
            return true;
        }
    }
    return false;
}

std::set<std::string> PointInTimeUniverse::membersAsOf(std::int64_t timestamp) const {
    std::set<std::string> members;
    for (const auto& kv : memberships_) {
        if (isMember(kv.first, timestamp)) {
            members.insert(kv.first);
        }
    }
    return members;
}

const PointInTimeUniverse::TerminalEvent* PointInTimeUniverse::terminalEvent(
        const std::string& symbol) const {
    auto it = events_.find(symbol);
    return it == events_.end() ? nullptr : &it->second;
}

std::set<std::string> PointInTimeUniverse::allSymbols() const {
    std::set<std::string> all;
    for (const auto& kv : memberships_) {
        all.insert(kv.first);
    }
    for (const auto& kv : events_) {
        all.insert(kv.first);
    }
    return all;
}

}  // namespace quantfinlib
