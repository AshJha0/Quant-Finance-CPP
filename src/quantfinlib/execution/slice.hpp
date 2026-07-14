// Port of com.quantfinlib.execution.Slice: one child slice of an execution
// schedule.
#pragma once

#include <cstdint>

namespace quantfinlib {

struct Slice {
    std::int64_t offsetMillis;
    std::int64_t quantity;
};

}  // namespace quantfinlib
