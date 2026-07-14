#include "quantfinlib/data/tick_capture.hpp"

namespace quantfinlib {

std::unique_ptr<TickCapture> TickCapture::attach(HftMarketDataBus& bus, const std::string& file) {
    std::unique_ptr<TickCapture> capture(new TickCapture(bus, file));
    TickCapture* raw = capture.get();
    bus.subscribeAll([raw](int symbolId, double price, double size, std::int64_t timestampNanos) {
        raw->onTick(symbolId, price, size, timestampNanos);
    });
    return capture;
}

void TickCapture::onTick(int symbolId, double price, double size, std::int64_t timestampNanos) {
    writer_.defineSymbol(symbolId, bus_.symbol(symbolId));
    writer_.write(symbolId, price, size, timestampNanos);
}

}  // namespace quantfinlib
