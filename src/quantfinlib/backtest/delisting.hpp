// A terminal event for a held instrument. The position is closed at
// (last close before barIndex) * (1 + delistingReturn): the CRSP-style
// return from the last traded price to what holders actually received,
// which a price panel alone never shows. Unknown proceeds on an involuntary
// delisting: use PointInTimeUniverse::DEFAULT_INVOLUNTARY_DELISTING_RETURN.
#pragma once

#include <string>

namespace quantfinlib {

struct Delisting {
    std::string symbol;
    int barIndex = 0;  // first bar the instrument no longer trades
    double delistingReturn = 0;
};

}  // namespace quantfinlib
