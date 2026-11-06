// FIX 4.4 OrderCancelReject (35=9): the venue refusing a cancel or a replace.
// CxlRejResponseTo(434) says which, so it is required.
#pragma once

#include <stdexcept>
#include <string>

#include "quantfinlib/fix/fix_message.hpp"

namespace quantfinlib {

struct OrderCancelReject {
    std::string orderId;
    std::string clOrdId;       // of the refused request
    std::string origClOrdId;
    char ordStatus;
    char responseTo;           // RESPONSE_TO_*
    int reason;                // CxlRejReason(102); -1 when absent
    std::string text;

    static constexpr char RESPONSE_TO_CANCEL = '1';
    static constexpr char RESPONSE_TO_REPLACE = '2';
    static constexpr int REASON_TOO_LATE = 0;
    static constexpr int REASON_UNKNOWN_ORDER = 1;

    static OrderCancelReject fromMessage(const FixMessage& m) {
        char responseTo = m.getChar(FixMessage::CXL_REJ_RESPONSE_TO);
        if (responseTo != RESPONSE_TO_CANCEL && responseTo != RESPONSE_TO_REPLACE) {
            throw std::invalid_argument("CxlRejResponseTo is neither cancel nor replace");
        }
        return OrderCancelReject{
                m.getString(FixMessage::ORDER_ID),
                m.getString(FixMessage::CL_ORD_ID),
                m.getString(FixMessage::ORIG_CL_ORD_ID, ""),
                m.getChar(FixMessage::ORD_STATUS),
                responseTo,
                m.has(FixMessage::CXL_REJ_REASON)
                        ? static_cast<int>(m.getLong(FixMessage::CXL_REJ_REASON))
                        : -1,
                m.getString(FixMessage::TEXT, "")};
    }

    FixMessage::Builder toBuilder() const {
        FixMessage::Builder b = FixMessage::builder(FixMessage::ORDER_CANCEL_REJECT);
        b.field(FixMessage::ORDER_ID, orderId)
                .field(FixMessage::CL_ORD_ID, clOrdId)
                .field(FixMessage::ORIG_CL_ORD_ID, origClOrdId)
                .field(FixMessage::ORD_STATUS, ordStatus)
                .field(FixMessage::CXL_REJ_RESPONSE_TO, responseTo);
        if (reason >= 0) {
            b.field(FixMessage::CXL_REJ_REASON, static_cast<std::int64_t>(reason));
        }
        if (!text.empty()) {
            b.field(FixMessage::TEXT, text);
        }
        return b;
    }
};

}  // namespace quantfinlib
