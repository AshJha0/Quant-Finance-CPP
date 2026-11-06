// Port of com.quantfinlib.marketdata.ItchCodec (Java): ITCH 5.0-style
// binary market-data codec: the messages that drive a full-depth (L3) book
// -- add, add-with-attribution, execute, execute-with-price, cancel, delete,
// replace, and off-book trade -- with the exact field layout and big-endian
// encoding of the Nasdaq TotalView-ITCH 5.0 specification. length() also
// knows every other 5.0 type so a decoder can skip them. Styled after the
// spec for realism; not a certified implementation.
//
// Bounds: wire bytes must go through complete() or the three-argument
// View::wrap, so a short datagram is skipped instead of read past its end.
// The two-argument wrap is for buffers the caller has already validated.
//
// Decoding is a flyweight: View::wrap points at a message inside a
// caller-owned buffer and every getter reads the bytes directly -- no
// parsing step, no objects, zero allocation. Symbols travel as a 64-bit
// packed value of 8 ASCII bytes (packStock) so the hot path never touches
// std::string.
//
// Prices are unsigned 32-bit integers with four implied decimals -- i.e.
// the raw value IS the price in 0.0001 ticks, which plugs straight into
// tick-indexed books like L3BookBuilder. Domain limit: this library's
// tick-indexed pipeline is signed-int throughout, so prices above
// 2^31-1 ticks ($214,748.36) decode negative and are dropped by band
// checks downstream -- symbols above that price (BRK.A territory) need a
// coarser tick or a scaled feed, the same constraint as HftOrderBook's
// ladder.
//
// Encoders exist for simulators, replay tooling and tests; a production
// participant only decodes.
#pragma once

#include <cassert>
#include <cstdint>
#include <string>
#include <string_view>

namespace quantfinlib {

class ItchCodec {
public:
    static constexpr std::uint8_t ADD = 'A';
    static constexpr std::uint8_t ADD_MPID = 'F';
    static constexpr std::uint8_t EXECUTED = 'E';
    static constexpr std::uint8_t EXECUTED_WITH_PRICE = 'C';
    static constexpr std::uint8_t CANCEL = 'X';
    static constexpr std::uint8_t DELETE = 'D';
    static constexpr std::uint8_t REPLACE = 'U';
    static constexpr std::uint8_t TRADE = 'P';

    static constexpr std::uint8_t BUY = 'B';
    static constexpr std::uint8_t SELL = 'S';

    ItchCodec() = delete;

    /** Wire length of an ITCH 5.0 message type; -1 for an unknown type. */
    static int length(std::uint8_t type) {
        switch (type) {
            case 'S': return 12;            // system event
            case 'R': return 39;            // stock directory
            case 'H': return 25;            // trading action
            case 'Y': return 20;            // Reg SHO restriction
            case 'L': return 26;            // market participant position
            case 'V': return 35;            // MWCB decline level
            case 'W': return 12;            // MWCB status
            case 'K': return 28;            // IPO quoting period
            case 'J': return 35;            // LULD auction collar
            case 'h': return 21;            // operational halt
            case ADD: return 36;
            case ADD_MPID: return 40;
            case EXECUTED: return 31;
            case EXECUTED_WITH_PRICE: return 36;
            case CANCEL: return 23;
            case DELETE: return 19;
            case REPLACE: return 35;
            case TRADE: return 44;
            case 'Q': return 40;            // cross trade
            case 'B': return 19;            // broken trade
            case 'I': return 50;            // net order imbalance indicator
            case 'N': return 20;            // retail price improvement indicator
            case 'O': return 48;            // direct listing with capital raise
            default: return -1;
        }
    }

    /** True when `available` bytes at `offset` hold one whole message of a known type. */
    static bool complete(const unsigned char* buffer, int offset, int available) {
        if (buffer == nullptr || offset < 0 || available < 1) {
            return false;
        }
        int len = length(buffer[offset]);
        return len > 0 && len <= available;
    }

    /** Packs up to 8 ASCII chars into a big-endian value, space-padded (ITCH alpha style). */
    static std::int64_t packStock(std::string_view symbol) {
        std::uint64_t v = 0;
        for (int i = 0; i < 8; i++) {
            char c = static_cast<std::size_t>(i) < symbol.size() ? symbol[i] : ' ';
            v = (v << 8) | (static_cast<unsigned char>(c) & 0xFFu);
        }
        return static_cast<std::int64_t>(v);
    }

    /** Inverse of packStock: trailing spaces stripped. Test/logging use only. */
    static std::string unpackStock(std::int64_t packed) {
        std::uint64_t u = static_cast<std::uint64_t>(packed);
        std::string s(8, ' ');
        for (int i = 7; i >= 0; i--) {
            s[7 - i] = static_cast<char>((u >> (i * 8)) & 0xFFu);
        }
        std::size_t end = s.size();
        while (end > 0 && s[end - 1] == ' ') {
            end--;
        }
        return s.substr(0, end);
    }

    // ------------------------------------------------------------------
    // Flyweight decoder
    // ------------------------------------------------------------------

    /**
     * Mutable flyweight over one message in a caller-owned buffer. Reuse a
     * single instance per decoding thread; every getter is a direct
     * big-endian read. Getters are only meaningful for the message types
     * that carry the field (per the ITCH layout) -- reading a field the
     * type does not carry returns garbage, exactly like pointing a C
     * struct at the wrong bytes. Type-specific getters assert() the type,
     * so a misdispatched read fails loudly in debug builds and costs
     * nothing in Release (NDEBUG strips assert).
     */
    class View {
    public:
        /** Points this view at a message already known to be complete. */
        View& wrap(const unsigned char* buffer, int offset) {
            buf_ = buffer;
            off_ = offset;
            return *this;
        }

        /**
         * Checked wrap for wire bytes: `available` is how many bytes follow
         * `offset`. Returns false, leaving the view untouched, unless a whole
         * message of a known type is there.
         */
        bool wrap(const unsigned char* buffer, int offset, int available) {
            if (!complete(buffer, offset, available)) {
                return false;
            }
            buf_ = buffer;
            off_ = offset;
            return true;
        }

        std::uint8_t type() const { return buf_[off_]; }

        /** Per-symbol locate code -- the feed's symbol id for the day. */
        int stockLocate() const { return u16(off_ + 1); }

        int trackingNumber() const { return u16(off_ + 3); }

        /** Nanoseconds since midnight (48-bit wire field). */
        std::int64_t timestampNanos() const { return u48(off_ + 5); }

        /** Order reference (A/F/E/X/D/P, and the original ref of U via origRef). */
        std::int64_t orderRef() const {
            assert(hasAny("AFECXDUP") && "no orderRef on this type");
            return u64(off_ + 11);
        }

        /** U only: the replaced (original) order reference. */
        std::int64_t origRef() const {
            assert(buf_[off_] == REPLACE && "origRef only on U");
            return u64(off_ + 11);
        }

        /** U only: the new order reference. */
        std::int64_t newRef() const {
            assert(buf_[off_] == REPLACE && "newRef only on U");
            return u64(off_ + 19);
        }

        /** A/F/P: BUY or SELL. */
        std::uint8_t side() const {
            assert(hasAny("AFP") && "no side on this type");
            return buf_[off_ + 19];
        }

        /** A/F/P: displayed shares. U: the new total shares. */
        std::int64_t shares() const {
            assert(hasAny("AFPU") && "no shares on this type");
            return buf_[off_] == REPLACE ? u32(off_ + 27) : u32(off_ + 20);
        }

        /** E/C: executed shares. X: cancelled shares. */
        std::int64_t deltaShares() const {
            assert(hasAny("ECX")
                   && "no deltaShares on this type");
            return u32(off_ + 19);
        }

        /** A/F/P: symbol as 8 packed ASCII bytes (compare against packStock). */
        std::int64_t stock() const {
            assert(hasAny("AFP") && "no stock on this type");
            return u64(off_ + 24);
        }

        /**
         * Price in 0.0001 ticks (A/F/P; U: the new price). Signed-int
         * domain: wire prices above 2^31-1 ticks decode negative (see the
         * class doc's domain-limit note).
         */
        int priceTick() const {
            assert(hasAny("AFPU") && "no price on this type");
            return buf_[off_] == REPLACE ? static_cast<int>(u32(off_ + 31))
                                        : static_cast<int>(u32(off_ + 32));
        }

        /** E/C/P: the venue's match (execution) number. */
        std::int64_t matchNumber() const {
            assert(hasAny("ECP") && "no matchNumber on this type");
            return buf_[off_] == TRADE ? u64(off_ + 36) : u64(off_ + 23);
        }

        /** C: 'Y' when the execution should print to the tape. */
        std::uint8_t printable() const {
            assert(buf_[off_] == EXECUTED_WITH_PRICE && "printable only on C");
            return buf_[off_ + 31];
        }

        /** C: execution price in 0.0001 ticks. */
        int executionPriceTick() const {
            assert(buf_[off_] == EXECUTED_WITH_PRICE && "execution price only on C");
            return static_cast<int>(u32(off_ + 32));
        }

    private:
        bool hasAny(const char* types) const {
            for (const char* p = types; *p; p++) {
                if (static_cast<unsigned char>(*p) == buf_[off_]) {
                    return true;
                }
            }
            return false;
        }

        int u16(int i) const {
            return ((buf_[i] & 0xFF) << 8) | (buf_[i + 1] & 0xFF);
        }

        std::int64_t u32(int i) const {
            return (static_cast<std::int64_t>(buf_[i] & 0xFF) << 24)
                    | ((buf_[i + 1] & 0xFF) << 16) | ((buf_[i + 2] & 0xFF) << 8)
                    | (buf_[i + 3] & 0xFF);
        }

        std::int64_t u48(int i) const {
            return (static_cast<std::int64_t>(u16(i)) << 32) | u32(i + 2);
        }

        std::int64_t u64(int i) const {
            return (u32(i) << 32) | u32(i + 4);
        }

        const unsigned char* buf_ = nullptr;
        int off_ = 0;
    };

    // ------------------------------------------------------------------
    // Encoders (simulator / replay / test side)
    // ------------------------------------------------------------------

    /** Encodes an Add Order (A); returns bytes written. */
    static int encodeAdd(unsigned char* buf, int off, int stockLocate,
                         std::int64_t timestampNanos, std::int64_t orderRef, std::uint8_t side,
                         std::int64_t shares, std::int64_t packedStock, int priceTick) {
        header(buf, off, ADD, stockLocate, timestampNanos);
        p64(buf, off + 11, orderRef);
        buf[off + 19] = side;
        p32(buf, off + 20, shares);
        p64(buf, off + 24, packedStock);
        p32(buf, off + 32, priceTick);
        return 36;
    }

    /** Encodes an Order Executed (E); returns bytes written. */
    static int encodeExecuted(unsigned char* buf, int off, int stockLocate,
                              std::int64_t timestampNanos, std::int64_t orderRef,
                              std::int64_t executedShares, std::int64_t matchNumber) {
        header(buf, off, EXECUTED, stockLocate, timestampNanos);
        p64(buf, off + 11, orderRef);
        p32(buf, off + 19, executedShares);
        p64(buf, off + 23, matchNumber);
        return 31;
    }

    /** Encodes an Order Executed With Price (C); returns bytes written. */
    static int encodeExecutedWithPrice(unsigned char* buf, int off, int stockLocate,
                                       std::int64_t timestampNanos, std::int64_t orderRef,
                                       std::int64_t executedShares, std::int64_t matchNumber,
                                       bool printable, int priceTick) {
        header(buf, off, EXECUTED_WITH_PRICE, stockLocate, timestampNanos);
        p64(buf, off + 11, orderRef);
        p32(buf, off + 19, executedShares);
        p64(buf, off + 23, matchNumber);
        buf[off + 31] = printable ? 'Y' : 'N';
        p32(buf, off + 32, priceTick);
        return 36;
    }

    /** Encodes an Order Cancel (X, partial cancel); returns bytes written. */
    static int encodeCancel(unsigned char* buf, int off, int stockLocate,
                            std::int64_t timestampNanos, std::int64_t orderRef,
                            std::int64_t cancelledShares) {
        header(buf, off, CANCEL, stockLocate, timestampNanos);
        p64(buf, off + 11, orderRef);
        p32(buf, off + 19, cancelledShares);
        return 23;
    }

    /** Encodes an Order Delete (D); returns bytes written. */
    static int encodeDelete(unsigned char* buf, int off, int stockLocate,
                            std::int64_t timestampNanos, std::int64_t orderRef) {
        header(buf, off, DELETE, stockLocate, timestampNanos);
        p64(buf, off + 11, orderRef);
        return 19;
    }

    /** Encodes an Order Replace (U); returns bytes written. */
    static int encodeReplace(unsigned char* buf, int off, int stockLocate,
                             std::int64_t timestampNanos, std::int64_t origRef,
                             std::int64_t newRef, std::int64_t shares, int priceTick) {
        header(buf, off, REPLACE, stockLocate, timestampNanos);
        p64(buf, off + 11, origRef);
        p64(buf, off + 19, newRef);
        p32(buf, off + 27, shares);
        p32(buf, off + 31, priceTick);
        return 35;
    }

    /** Encodes a non-cross Trade (P); returns bytes written. */
    static int encodeTrade(unsigned char* buf, int off, int stockLocate,
                           std::int64_t timestampNanos, std::int64_t orderRef, std::uint8_t side,
                           std::int64_t shares, std::int64_t packedStock, int priceTick,
                           std::int64_t matchNumber) {
        header(buf, off, TRADE, stockLocate, timestampNanos);
        p64(buf, off + 11, orderRef);
        buf[off + 19] = side;
        p32(buf, off + 20, shares);
        p64(buf, off + 24, packedStock);
        p32(buf, off + 32, priceTick);
        p64(buf, off + 36, matchNumber);
        return 44;
    }

private:
    static void header(unsigned char* buf, int off, std::uint8_t type, int locate,
                       std::int64_t tsNanos) {
        buf[off] = type;
        buf[off + 1] = static_cast<unsigned char>(locate >> 8);
        buf[off + 2] = static_cast<unsigned char>(locate);
        buf[off + 3] = 0;                        // tracking number (unused here)
        buf[off + 4] = 0;
        buf[off + 5] = static_cast<unsigned char>(static_cast<std::uint64_t>(tsNanos) >> 40);
        buf[off + 6] = static_cast<unsigned char>(static_cast<std::uint64_t>(tsNanos) >> 32);
        p32(buf, off + 7, tsNanos & 0xFFFFFFFFLL);
    }

    static void p32(unsigned char* buf, int off, std::int64_t v) {
        std::uint32_t u = static_cast<std::uint32_t>(v);
        buf[off] = static_cast<unsigned char>(u >> 24);
        buf[off + 1] = static_cast<unsigned char>(u >> 16);
        buf[off + 2] = static_cast<unsigned char>(u >> 8);
        buf[off + 3] = static_cast<unsigned char>(u);
    }

    static void p64(unsigned char* buf, int off, std::int64_t v) {
        p32(buf, off, static_cast<std::int64_t>(static_cast<std::uint64_t>(v) >> 32));
        p32(buf, off + 4, v & 0xFFFFFFFFLL);
    }
};

}  // namespace quantfinlib
