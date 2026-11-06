// Feeds arbitrary bytes through the stream framer, the message parser with
// every typed getter, and the zero-allocation exec report view.
#include <cstddef>
#include <cstdint>
#include <memory>
#include <cstring>
#include <stdexcept>

#include "quantfinlib/fix/fix_decoder.hpp"
#include "quantfinlib/fix/fix_exec_report_view.hpp"
#include "quantfinlib/fix/fix_message.hpp"

using namespace quantfinlib;

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    // Exact-size copy: libFuzzer's buffer is exact too, but make the bound explicit.
    auto copy = std::make_unique<std::uint8_t[]>(size == 0 ? 1 : size);
    if (size != 0) {
        std::memcpy(copy.get(), data, size);
    }

    FixDecoder decoder;
    try {
        // Split in two so fragmentation paths run as well.
        std::size_t half = size / 2;
        decoder.feed(copy.get(), 0, half);
        for (int pass = 0; pass < 2; ++pass) {
            for (;;) {
                try {
                    auto m = decoder.poll();
                    if (!m.has_value()) {
                        break;
                    }
                    try {
                        (void)m->msgType();
                        (void)m->getLong(FixMessage::MSG_SEQ_NUM);
                        (void)m->getDouble(FixMessage::PRICE);
                        (void)m->getChar(FixMessage::SIDE);
                    } catch (const std::exception&) {
                    }
                } catch (const std::invalid_argument&) {
                    // Garbled frame, consumed: keep going like the session does.
                }
            }
            if (pass == 0) {
                decoder.feed(copy.get(), half, size - half);
            }
        }
    } catch (const std::runtime_error&) {
        // Stream corrupt: the documented fatal outcome.
    }

    FixExecReportView view;
    try {
        view.wrap(copy.get(), 0, size);
        std::uint8_t symbol[4] = {'E', 'U', 'R', 0};
        (void)view.symbolEquals(symbol, 3);
    } catch (const std::invalid_argument&) {
    }
    return 0;
}
