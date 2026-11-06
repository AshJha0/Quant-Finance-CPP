// Walks arbitrary bytes as a MoldUDP64 packet carrying ITCH into an L3 book,
// and as a bare ITCH stream with the checked decoder.
#include <cstddef>
#include <cstdint>

#include "quantfinlib/marketdata/itch_codec.hpp"
#include "quantfinlib/marketdata/l3_book_builder.hpp"
#include "quantfinlib/marketdata/moldudp64_receiver.hpp"

using namespace quantfinlib;

namespace {

struct Feed : MoldUdp64Receiver::MessageHandler, MoldUdp64Receiver::Recovery {
    L3BookBuilder book{1, 0, 2000, 64};

    void onMessage(const unsigned char* buf, int offset, int length, std::int64_t) override {
        book.onMessage(buf, offset, length);
    }
    void requestRetransmit(const std::string&, std::int64_t, int) override {}
    void rebuildFromSnapshot(const std::string&, std::int64_t) override {}
};

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    if (size > (1U << 20)) {
        return 0;
    }
    int length = static_cast<int>(size);
    Feed feed;
    MoldUdp64Receiver receiver(feed, feed, 100, 1000, 2);
    receiver.onPacket(data, 0, length, 0);
    receiver.onTimer(5000);

    int offset = 0;
    ItchCodec::View view;
    while (offset < length && view.wrap(data, offset, length - offset)) {
        feed.book.onMessage(data, offset, length - offset);
        (void)view.timestampNanos();
        offset += ItchCodec::length(view.type());
    }
    return 0;
}
