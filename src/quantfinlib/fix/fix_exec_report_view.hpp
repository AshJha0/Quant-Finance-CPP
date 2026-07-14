// Port of com.quantfinlib.fix.FixExecReportView (Java): garbage-free FIX 4.4
// ExecutionReport reader -- the inbound half of the FIX hot path, completing
// the round trip FixOrderEncoder started: order out garbage-free, fill in
// garbage-free. wrap() performs one pass over the framed bytes recording
// primitive values and field positions; every getter returns a primitive.
// Prices come back as scaled longs (lastPxMantissa() * 10^-lastPxDecimals()).
// Assumes an already-framed message (FixDecoder owns framing/checksum).
// Single-threaded, one view per session thread -- wrap, read, repeat.
#pragma once

#include <cstddef>
#include <cstdint>

namespace quantfinlib {

class FixExecReportView {
public:
    /**
     * Parses one framed message in place. Returns true when it is an
     * ExecutionReport (35=8); other message types return false and leave
     * the getters undefined. Zero allocation.
     */
    bool wrap(const std::uint8_t* buffer, std::size_t offset, std::size_t length);

    /** Numeric ClOrdID as issued by FixOrderEncoder; -1 when absent. */
    std::int64_t clOrdId() const {
        return clOrdId_;
    }

    /** Tag 150 as its ASCII byte ('0' new, 'F' trade, '4' canceled, ...). */
    std::uint8_t execType() const {
        return execType_;
    }

    /** Tag 39 as its ASCII byte. */
    std::uint8_t ordStatus() const {
        return ordStatus_;
    }

    /** Tag 54: '1' buy, '2' sell. */
    std::uint8_t side() const {
        return side_;
    }

    std::int64_t lastQty() const {
        return lastQty_;
    }

    std::int64_t cumQty() const {
        return cumQty_;
    }

    std::int64_t leavesQty() const {
        return leavesQty_;
    }

    /** LastPx as a scaled long: mantissa * 10^-decimals. */
    std::int64_t lastPxMantissa() const {
        return lastPxMantissa_;
    }

    int lastPxDecimals() const {
        return lastPxDecimals_;
    }

    /**
     * Compares the in-place symbol bytes against a registered ASCII symbol
     * -- the getter that replaces a string: resolve the dense id by probing
     * your table.
     */
    bool symbolEquals(const std::uint8_t* asciiSymbol, std::size_t asciiLength) const;

private:
    void parsePrice(std::size_t from, std::size_t to);

    const std::uint8_t* buf_ = nullptr;
    bool execReport_ = false;
    std::int64_t clOrdId_ = -1;
    std::uint8_t execType_ = 0;
    std::uint8_t ordStatus_ = 0;
    std::uint8_t side_ = 0;
    std::int64_t lastQty_ = 0;
    std::int64_t cumQty_ = 0;
    std::int64_t leavesQty_ = 0;
    std::int64_t lastPxMantissa_ = 0;
    int lastPxDecimals_ = 0;
    std::ptrdiff_t symbolOffset_ = -1;
    std::size_t symbolLength_ = 0;
};

}  // namespace quantfinlib
