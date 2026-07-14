// Port of com.quantfinlib.fix.ExecutionReport (Java): typed view of a FIX
// ExecutionReport (35=8) -- the venue's answer to an order: acknowledgement,
// fill, or rejection.
#pragma once

#include <string>

#include "quantfinlib/fix/fix_message.hpp"
#include "quantfinlib/fix/new_order_single.hpp"
#include "quantfinlib/fix/order_cancel_replace_request.hpp"
#include "quantfinlib/fix/order_cancel_request.hpp"
#include "quantfinlib/orderbook/side.hpp"

namespace quantfinlib {

struct ExecutionReport {
    std::string orderId;
    std::string execId;
    char execType;
    char ordStatus;
    std::string clOrdId;
    std::string symbol;
    Side side;
    double lastQty;
    double lastPrice;
    double cumQty;
    double leavesQty;
    double avgPrice;

    static constexpr char EXEC_TYPE_NEW = '0';
    static constexpr char EXEC_TYPE_CANCELED = '4';
    static constexpr char EXEC_TYPE_REPLACED = '5';
    static constexpr char EXEC_TYPE_TRADE = 'F';
    static constexpr char EXEC_TYPE_REJECTED = '8';
    static constexpr char ORD_STATUS_NEW = '0';
    static constexpr char ORD_STATUS_PARTIALLY_FILLED = '1';
    static constexpr char ORD_STATUS_FILLED = '2';
    static constexpr char ORD_STATUS_CANCELED = '4';
    static constexpr char ORD_STATUS_REPLACED = '5';
    static constexpr char ORD_STATUS_REJECTED = '8';

    bool isFill() const {
        return execType == EXEC_TYPE_TRADE;
    }

    /** Venue-side convenience: acknowledge a new order. */
    static ExecutionReport accepted(const NewOrderSingle& order, const std::string& orderId,
                                     const std::string& execId);

    /** Venue-side convenience: full fill at one price. */
    static ExecutionReport filled(const NewOrderSingle& order, const std::string& orderId,
                                  const std::string& execId, double fillPrice);

    /** Venue-side convenience: confirm a cancel (cumQty already executed). */
    static ExecutionReport canceled(const OrderCancelRequest& request, const std::string& orderId,
                                    const std::string& execId, double cumQty);

    /** Venue-side convenience: confirm a cancel/replace with the new working quantity. */
    static ExecutionReport replaced(const OrderCancelReplaceRequest& request,
                                     const std::string& orderId, const std::string& execId,
                                     double cumQty);

    static ExecutionReport fromMessage(const FixMessage& m);

    FixMessage::Builder toBuilder() const;
};

}  // namespace quantfinlib
