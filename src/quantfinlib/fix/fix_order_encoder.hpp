// Port of com.quantfinlib.fix.FixOrderEncoder (Java): garbage-free FIX 4.4
// NewOrderSingle encoder -- the hot-lane counterpart of the string-based
// FixMessage codec. One reusable byte buffer (body written at a fixed
// offset, header written backwards in front of it, checksum appended),
// ASCII digit writers, prices as scaled longs, a cached yyyyMMdd- timestamp
// prefix, and pre-registered symbols. Single-threaded: one encoder per
// session, like the session itself. IllegalArgumentException maps to
// std::invalid_argument, IllegalStateException maps to std::runtime_error.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "quantfinlib/orderbook/side.hpp"

namespace quantfinlib {

class FixOrderEncoder {
public:
    /**
     * @param senderCompId session sender (tag 49)
     * @param targetCompId session target (tag 56)
     * @param maxSymbols   dense symbol-id capacity
     * @param bufferSize   message buffer (512 is ample for a NewOrderSingle)
     */
    FixOrderEncoder(const std::string& senderCompId, const std::string& targetCompId,
                    int maxSymbols, int bufferSize);

    /** Registers a tradeable symbol (cold path, before trading). */
    FixOrderEncoder& registerSymbol(int symbolId, const std::string& symbol);

    /**
     * Encodes a limit NewOrderSingle into the reusable buffer. Zero
     * allocation. The encoded message occupies [offset(), offset()+length())
     * of buffer() until the next encode.
     */
    int encodeLimit(std::int64_t msgSeqNum, std::int64_t clOrdId, int symbolId, Side side,
                     std::int64_t quantity, std::int64_t priceMantissa, int priceDecimals,
                     std::int64_t epochMillis);

    /** Market NewOrderSingle (40=1, no price tag). Zero allocation. */
    int encodeMarket(std::int64_t msgSeqNum, std::int64_t clOrdId, int symbolId, Side side,
                      std::int64_t quantity, std::int64_t epochMillis);

    /** The reusable buffer holding the last encoded message. */
    const std::uint8_t* buffer() const {
        return buffer_.data();
    }

    /** Start offset of the last message within buffer(). */
    int offset() const {
        return start_;
    }

    /** Length of the last message. */
    int length() const {
        return end_ - start_;
    }

private:
    int encode(std::int64_t msgSeqNum, std::int64_t clOrdId, int symbolId, Side side,
               std::int64_t quantity, std::int64_t priceMantissa, int priceDecimals, bool market,
               std::int64_t epochMillis);

    int tag(int p, char a, char b);
    int raw(int p, const std::vector<std::uint8_t>& bytes);
    int digits(int p, std::int64_t value);
    int price(int p, std::int64_t mantissa, int decimals);
    int timestamp(int p, std::int64_t epochMillis);
    void refreshDate(std::int64_t epochMillis);

    static constexpr std::uint8_t SOH = 1;
    static constexpr int PREFIX_RESERVE = 32;

    std::vector<std::uint8_t> buffer_;
    std::vector<std::uint8_t> header34_;
    std::vector<std::vector<std::uint8_t>> symbols_;
    int start_ = 0;
    int end_ = 0;

    std::uint8_t datePrefix_[9] = {};
    std::int64_t cachedEpochDay_ = INT64_MIN;
};

}  // namespace quantfinlib
