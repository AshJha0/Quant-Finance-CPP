#include "quantfinlib/fix/execution_report.hpp"

namespace quantfinlib {

ExecutionReport ExecutionReport::accepted(const NewOrderSingle& order, const std::string& orderId,
                                           const std::string& execId) {
    return ExecutionReport{orderId, execId, EXEC_TYPE_NEW, ORD_STATUS_NEW,
                            order.clOrdId, order.symbol, order.side,
                            0, 0, 0, static_cast<double>(order.quantity), 0};
}

ExecutionReport ExecutionReport::filled(const NewOrderSingle& order, const std::string& orderId,
                                        const std::string& execId, double fillPrice) {
    return ExecutionReport{orderId, execId, EXEC_TYPE_TRADE, ORD_STATUS_FILLED,
                            order.clOrdId, order.symbol, order.side,
                            static_cast<double>(order.quantity), fillPrice,
                            static_cast<double>(order.quantity), 0, fillPrice};
}

ExecutionReport ExecutionReport::canceled(const OrderCancelRequest& request,
                                          const std::string& orderId, const std::string& execId,
                                          double cumQty) {
    return ExecutionReport{orderId, execId, EXEC_TYPE_CANCELED, ORD_STATUS_CANCELED,
                            request.clOrdId, request.symbol, request.side,
                            0, 0, cumQty, 0, 0};
}

ExecutionReport ExecutionReport::replaced(const OrderCancelReplaceRequest& request,
                                          const std::string& orderId, const std::string& execId,
                                          double cumQty) {
    return ExecutionReport{orderId, execId, EXEC_TYPE_REPLACED, ORD_STATUS_REPLACED,
                            request.clOrdId, request.symbol, request.side,
                            0, 0, cumQty, static_cast<double>(request.quantity) - cumQty, 0};
}

ExecutionReport ExecutionReport::fromMessage(const FixMessage& m) {
    return ExecutionReport{
            m.getString(FixMessage::ORDER_ID),
            m.getString(FixMessage::EXEC_ID),
            m.getChar(FixMessage::EXEC_TYPE),
            m.getChar(FixMessage::ORD_STATUS),
            m.getString(FixMessage::CL_ORD_ID, ""),
            m.getString(FixMessage::SYMBOL),
            m.getChar(FixMessage::SIDE) == '1' ? Side::BUY : Side::SELL,
            m.getDouble(FixMessage::LAST_QTY, 0),
            m.getDouble(FixMessage::LAST_PX, 0),
            m.getDouble(FixMessage::CUM_QTY, 0),
            m.getDouble(FixMessage::LEAVES_QTY, 0),
            m.getDouble(FixMessage::AVG_PX, 0)};
}

FixMessage::Builder ExecutionReport::toBuilder() const {
    return FixMessage::builder(FixMessage::EXECUTION_REPORT)
            .field(FixMessage::ORDER_ID, orderId)
            .field(FixMessage::EXEC_ID, execId)
            .field(FixMessage::EXEC_TYPE, execType)
            .field(FixMessage::ORD_STATUS, ordStatus)
            .field(FixMessage::CL_ORD_ID, clOrdId)
            .field(FixMessage::SYMBOL, symbol)
            .field(FixMessage::SIDE, side == Side::BUY ? '1' : '2')
            .field(FixMessage::LAST_QTY, lastQty)
            .field(FixMessage::LAST_PX, lastPrice)
            .field(FixMessage::CUM_QTY, cumQty)
            .field(FixMessage::LEAVES_QTY, leavesQty)
            .field(FixMessage::AVG_PX, avgPrice);
}

}  // namespace quantfinlib
