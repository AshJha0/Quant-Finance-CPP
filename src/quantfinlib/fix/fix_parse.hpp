// Port of com.quantfinlib.fix.FixParse (Java, package-private): shared
// primitive parsers for the garbage-free FIX flyweights (FixExecReportView)
// -- the fraction-rejection and scaled-long price rules are load-bearing
// for the "feed-to-order never touches a double" invariant, so they exist
// exactly once. Free functions, static, zero allocation.
// IllegalArgumentException maps to std::invalid_argument.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace quantfinlib {
namespace fix_parse {

/**
 * Non-negative integer field (quantities, counts). Tolerates a zero
 * fraction ("100.0"); a real fraction or a '-' sign fails loudly --
 * negative quantities are protocol violations that must never parse into
 * silent garbage. Throws std::invalid_argument.
 */
std::int64_t parseLong(const std::uint8_t* buf, std::size_t from, std::size_t to);

/**
 * Numeric field that may legitimately be non-numeric on the wire -- e.g.
 * ClOrdID (tag 11), which is only numeric by OUR encoder's convention;
 * unsolicited venue messages carry counterparty formats. Returns
 * `fallback` on any non-digit instead of throwing: a foreign id must
 * never kill the message pump.
 */
std::int64_t parseLongOrElse(const std::uint8_t* buf, std::size_t from, std::size_t to,
                              std::int64_t fallback);

/**
 * Price mantissa as a signed scaled long: "1.08505" -> 108505, "-0.5" -> -5
 * (with priceDecimals = 1). Negative prices are real in FX (forward
 * points, negative rates) and must round-trip.
 */
std::int64_t priceMantissa(const std::uint8_t* buf, std::size_t from, std::size_t to);

/** Digits after the decimal point: "1.08505" -> 5; "99" -> 0. */
int priceDecimals(const std::uint8_t* buf, std::size_t from, std::size_t to);

/** 10^-n for n in 0..18: scaled-long -> double conversion without pow(). */
extern const std::array<double, 19> NEG_POW10;

}  // namespace fix_parse
}  // namespace quantfinlib
