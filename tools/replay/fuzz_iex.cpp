// Walks arbitrary bytes as a capture, and as one bare IEX-TP segment, through
// the decoder and the replay checks.
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "quantfinlib/marketdata/iex_capture_reader.hpp"
#include "quantfinlib/marketdata/iex_replay.hpp"
#include "quantfinlib/marketdata/iex_tp_decoder.hpp"

using namespace quantfinlib;

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    if (size > (1U << 20)) {
        return 0;
    }
    IexTpDecoder decoder;
    IexReplay replay;
    decoder.onDatagram(data, size, replay);
    std::size_t at = 0;
    IexCaptureReader reader([&](std::uint8_t* p, std::size_t n) {
        std::size_t take = std::min(n, size - at);
        std::memcpy(p, data + at, take);
        at += take;
        return take;
    });
    IexCaptureReader::Datagram d;
    while (reader.next(d)) {
        decoder.onDatagram(d.data, d.size, replay);
    }
    replay.finish();
    return 0;
}
