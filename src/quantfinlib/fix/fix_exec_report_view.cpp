#include "quantfinlib/fix/fix_exec_report_view.hpp"

#include "quantfinlib/fix/fix_message.hpp"
#include "quantfinlib/fix/fix_parse.hpp"

namespace quantfinlib {

namespace {
constexpr std::uint8_t SOH = 1;
}

bool FixExecReportView::wrap(const std::uint8_t* buffer, std::size_t offset, std::size_t length) {
    buf_ = buffer;
    execReport_ = false;
    clOrdId_ = -1;
    execType_ = 0;
    ordStatus_ = 0;
    side_ = 0;
    lastQty_ = 0;
    cumQty_ = 0;
    leavesQty_ = 0;
    lastPxMantissa_ = 0;
    lastPxDecimals_ = 0;
    symbolOffset_ = -1;
    symbolLength_ = 0;

    std::size_t end = offset + length;
    std::size_t p = offset;
    while (p < end) {
        // ---- tag: digits up to '=' ----
        int tag = 0;
        while (p < end && buffer[p] != '=') {
            tag = tag * 10 + (buffer[p] - '0');
            ++p;
        }
        ++p;  // skip '='
        std::size_t valueStart = p;
        while (p < end && buffer[p] != SOH) {
            ++p;
        }
        std::size_t valueEnd = p;
        ++p;  // skip SOH

        switch (tag) {
            case FixMessage::MSG_TYPE: {
                // 35=8 and nothing else; a one-byte value is required.
                execReport_ = (valueEnd - valueStart == 1) && buffer[valueStart] == '8';
                if (!execReport_) {
                    return false;  // not ours: stop scanning immediately
                }
                break;
            }
            // ClOrdID is free-format FIX: numeric when WE issued it
            // (FixOrderEncoder), counterparty-format on unsolicited
            // reports -- those map to the existing -1 "not ours" sentinel
            // instead of throwing mid-wrap.
            case FixMessage::CL_ORD_ID:
                clOrdId_ = fix_parse::parseLongOrElse(buf_, valueStart, valueEnd, -1);
                break;
            case FixMessage::EXEC_TYPE:
                execType_ = buffer[valueStart];
                break;
            case FixMessage::ORD_STATUS:
                ordStatus_ = buffer[valueStart];
                break;
            case FixMessage::SIDE:
                side_ = buffer[valueStart];
                break;
            case FixMessage::LAST_QTY:
                lastQty_ = fix_parse::parseLong(buf_, valueStart, valueEnd);
                break;
            case FixMessage::CUM_QTY:
                cumQty_ = fix_parse::parseLong(buf_, valueStart, valueEnd);
                break;
            case FixMessage::LEAVES_QTY:
                leavesQty_ = fix_parse::parseLong(buf_, valueStart, valueEnd);
                break;
            case FixMessage::LAST_PX:
                parsePrice(valueStart, valueEnd);
                break;
            case FixMessage::SYMBOL:
                symbolOffset_ = static_cast<std::ptrdiff_t>(valueStart);
                symbolLength_ = valueEnd - valueStart;
                break;
            default:
                // Every other tag is skipped without materializing it.
                break;
        }
    }
    return execReport_;
}

bool FixExecReportView::symbolEquals(const std::uint8_t* asciiSymbol,
                                     std::size_t asciiLength) const {
    if (symbolOffset_ < 0 || asciiLength != symbolLength_) {
        return false;
    }
    for (std::size_t i = 0; i < symbolLength_; ++i) {
        if (buf_[static_cast<std::size_t>(symbolOffset_) + i] != asciiSymbol[i]) {
            return false;
        }
    }
    return true;
}

void FixExecReportView::parsePrice(std::size_t from, std::size_t to) {
    lastPxMantissa_ = fix_parse::priceMantissa(buf_, from, to);
    lastPxDecimals_ = fix_parse::priceDecimals(buf_, from, to);
}

}  // namespace quantfinlib
