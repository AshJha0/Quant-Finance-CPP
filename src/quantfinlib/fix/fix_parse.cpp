#include "quantfinlib/fix/fix_parse.hpp"

#include <stdexcept>

namespace quantfinlib {
namespace fix_parse {

const std::array<double, 19> NEG_POW10 = {
        1, 1e-1, 1e-2, 1e-3, 1e-4, 1e-5, 1e-6, 1e-7, 1e-8, 1e-9,
        1e-10, 1e-11, 1e-12, 1e-13, 1e-14, 1e-15, 1e-16, 1e-17, 1e-18};

std::int64_t parseLong(const std::uint8_t* buf, std::size_t from, std::size_t to) {
    std::int64_t v = 0;
    for (std::size_t i = from; i < to; ++i) {
        std::uint8_t b = buf[i];
        if (b == '-') {
            throw std::invalid_argument("negative quantity field");
        }
        if (b == '.') {
            for (std::size_t j = i + 1; j < to; ++j) {
                if (buf[j] != '0') {
                    throw std::invalid_argument("fractional quantity not representable as long");
                }
            }
            return v;
        }
        v = v * 10 + (b - '0');
    }
    return v;
}

std::int64_t parseLongOrElse(const std::uint8_t* buf, std::size_t from, std::size_t to,
                              std::int64_t fallback) {
    std::int64_t v = 0;
    for (std::size_t i = from; i < to; ++i) {
        std::uint8_t b = buf[i];
        if (b < '0' || b > '9') {
            return fallback;
        }
        v = v * 10 + (b - '0');
    }
    return from == to ? fallback : v;
}

std::int64_t priceMantissa(const std::uint8_t* buf, std::size_t from, std::size_t to) {
    bool negative = from < to && buf[from] == '-';
    std::int64_t mantissa = 0;
    for (std::size_t i = negative ? from + 1 : from; i < to; ++i) {
        std::uint8_t b = buf[i];
        if (b != '.') {
            mantissa = mantissa * 10 + (b - '0');
        }
    }
    return negative ? -mantissa : mantissa;
}

int priceDecimals(const std::uint8_t* buf, std::size_t from, std::size_t to) {
    for (std::size_t i = from; i < to; ++i) {
        if (buf[i] == '.') {
            return static_cast<int>(to - i - 1);
        }
    }
    return 0;
}

}  // namespace fix_parse
}  // namespace quantfinlib
