#include "quantfinlib/fix/fix_parse.hpp"

#include <limits>
#include <stdexcept>

namespace quantfinlib {
namespace fix_parse {

const std::array<double, 19> NEG_POW10 = {
        1, 1e-1, 1e-2, 1e-3, 1e-4, 1e-5, 1e-6, 1e-7, 1e-8, 1e-9,
        1e-10, 1e-11, 1e-12, 1e-13, 1e-14, 1e-15, 1e-16, 1e-17, 1e-18};

namespace {

constexpr std::int64_t MAX_LONG = std::numeric_limits<std::int64_t>::max();

/** v * 10 + digit, or false on overflow. */
bool appendDigit(std::int64_t& v, int digit) {
    if (v > (MAX_LONG - digit) / 10) {
        return false;
    }
    v = v * 10 + digit;
    return true;
}

}  // namespace

std::int64_t parseLong(const std::uint8_t* buf, std::size_t from, std::size_t to) {
    if (from >= to) {
        throw std::invalid_argument("empty quantity field");
    }
    std::int64_t v = 0;
    for (std::size_t i = from; i < to; ++i) {
        std::uint8_t b = buf[i];
        if (b == '-') {
            throw std::invalid_argument("negative quantity field");
        }
        if (b == '.') {
            if (i == from) {
                throw std::invalid_argument("quantity field has no digits");
            }
            for (std::size_t j = i + 1; j < to; ++j) {
                if (buf[j] != '0') {
                    throw std::invalid_argument("fractional quantity not representable as long");
                }
            }
            return v;
        }
        if (b < '0' || b > '9') {
            throw std::invalid_argument("non-digit in quantity field");
        }
        if (!appendDigit(v, b - '0')) {
            throw std::invalid_argument("quantity field overflows");
        }
    }
    return v;
}

std::int64_t parseLongOrElse(const std::uint8_t* buf, std::size_t from, std::size_t to,
                              std::int64_t fallback) {
    std::int64_t v = 0;
    for (std::size_t i = from; i < to; ++i) {
        std::uint8_t b = buf[i];
        if (b < '0' || b > '9' || !appendDigit(v, b - '0')) {
            return fallback;
        }
    }
    return from == to ? fallback : v;
}

std::int64_t priceMantissa(const std::uint8_t* buf, std::size_t from, std::size_t to) {
    bool negative = from < to && buf[from] == '-';
    std::int64_t mantissa = 0;
    bool point = false;
    bool digit = false;
    for (std::size_t i = negative ? from + 1 : from; i < to; ++i) {
        std::uint8_t b = buf[i];
        if (b == '.' && !point) {
            point = true;
        } else if (b < '0' || b > '9') {
            throw std::invalid_argument("non-digit in price field");
        } else {
            digit = true;
            if (!appendDigit(mantissa, b - '0')) {
                throw std::invalid_argument("price field overflows");
            }
        }
    }
    if (!digit) {
        throw std::invalid_argument("price field has no digits");
    }
    return negative ? -mantissa : mantissa;
}

int priceDecimals(const std::uint8_t* buf, std::size_t from, std::size_t to) {
    for (std::size_t i = from; i < to; ++i) {
        if (buf[i] == '.') {
            if (to - i - 1 >= NEG_POW10.size()) {
                throw std::invalid_argument("price field has too many decimals");
            }
            return static_cast<int>(to - i - 1);
        }
    }
    return 0;
}

}  // namespace fix_parse
}  // namespace quantfinlib
