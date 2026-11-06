// Every SBE flyweight through its checked wrap, reading all fields on success.
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>

#include "quantfinlib/sbe/order_flyweight.hpp"
#include "quantfinlib/sbe/quote_flyweight.hpp"
#include "quantfinlib/sbe/trade_flyweight.hpp"

using namespace quantfinlib;

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    if (size == 0 || size > (1U << 16)) {
        return 0;
    }
    auto copy = std::make_unique<unsigned char[]>(size);  // flyweights take a mutable buffer
    std::memcpy(copy.get(), data, size);
    int length = static_cast<int>(size);
    OrderFlyweight order;
    if (order.wrap(copy.get(), 0, length)) {
        (void)order.orderId();
        (void)order.side();
        (void)order.price();
        (void)order.timestampNanos();
    }
    TradeFlyweight trade;
    if (trade.wrap(copy.get(), 0, length)) {
        (void)trade.price();
        (void)trade.size();
    }
    QuoteFlyweight quote;
    if (quote.wrap(copy.get(), 0, length)) {
        (void)quote.bidPrice();
        (void)quote.askSize();
    }
    return 0;
}
