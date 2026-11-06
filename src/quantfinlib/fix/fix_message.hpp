// Port of com.quantfinlib.fix.FixMessage (Java): FIX 4.4 wire-format
// message: tag=value fields delimited by SOH, framed by BeginString(8) /
// BodyLength(9) / CheckSum(10). Parsing validates the checksum and body
// length; building computes them. Field access by tag sees the last value of
// a tag; fields() keeps every field in wire order, duplicates included, and
// group() cuts a repeating group out of it. Nested groups are not parsed.
// Builder refuses a value containing SOH: it would forge extra fields.
// IllegalArgumentException maps to std::invalid_argument.
#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace quantfinlib {

class FixMessage {
public:
    static constexpr char SOH = static_cast<char>(1);
    static const std::string BEGIN_STRING;   // "FIX.4.4"

    // Common tags.
    static constexpr int AVG_PX = 6;
    static constexpr int BEGIN_SEQ_NO = 7;
    static constexpr int BEGIN_STRING_TAG = 8;
    static constexpr int BODY_LENGTH = 9;
    static constexpr int CHECK_SUM = 10;
    static constexpr int CL_ORD_ID = 11;
    static constexpr int CUM_QTY = 14;
    static constexpr int END_SEQ_NO = 16;
    static constexpr int EXEC_ID = 17;
    static constexpr int LAST_PX = 31;
    static constexpr int LAST_QTY = 32;
    static constexpr int MSG_SEQ_NUM = 34;
    static constexpr int MSG_TYPE = 35;
    static constexpr int NEW_SEQ_NO = 36;
    static constexpr int ORDER_ID = 37;
    static constexpr int ORDER_QTY = 38;
    static constexpr int ORD_STATUS = 39;
    static constexpr int ORD_TYPE = 40;
    static constexpr int ORIG_CL_ORD_ID = 41;
    static constexpr int POSS_DUP_FLAG = 43;
    static constexpr int PRICE = 44;
    static constexpr int REF_SEQ_NUM = 45;
    static constexpr int SENDER_COMP_ID = 49;
    static constexpr int SENDING_TIME = 52;
    static constexpr int SIDE = 54;
    static constexpr int SYMBOL = 55;
    static constexpr int TARGET_COMP_ID = 56;
    static constexpr int TEXT = 58;
    static constexpr int TIME_IN_FORCE = 59;
    static constexpr int TRANSACT_TIME = 60;
    static constexpr int ENCRYPT_METHOD = 98;
    static constexpr int HEART_BT_INT = 108;
    static constexpr int TEST_REQ_ID = 112;
    static constexpr int ORIG_SENDING_TIME = 122;
    static constexpr int GAP_FILL_FLAG = 123;
    static constexpr int RESET_SEQ_NUM_FLAG = 141;
    static constexpr int EXEC_TYPE = 150;
    static constexpr int LEAVES_QTY = 151;
    static constexpr int SESSION_REJECT_REASON = 373;
    static constexpr int CXL_REJ_REASON = 102;
    static constexpr int CXL_REJ_RESPONSE_TO = 434;
    static constexpr int USERNAME = 553;
    static constexpr int PASSWORD = 554;

    // Message types.
    static const std::string HEARTBEAT;
    static const std::string TEST_REQUEST;
    static const std::string RESEND_REQUEST;
    static const std::string REJECT;
    static const std::string SEQUENCE_RESET;
    static const std::string LOGON;
    static const std::string LOGOUT;
    static const std::string NEW_ORDER_SINGLE;
    static const std::string EXECUTION_REPORT;
    static const std::string ORDER_CANCEL_REQUEST;
    static const std::string ORDER_CANCEL_REPLACE_REQUEST;
    static const std::string ORDER_CANCEL_REJECT;
    static const std::string BUSINESS_MESSAGE_REJECT;

    /** One tag=value pair as it appeared on the wire. */
    struct Field {
        int tag;
        std::string value;
    };

    // ------------------------------------------------------------------
    // Parsing
    // ------------------------------------------------------------------

    /** Parses and validates one complete framed message. Throws std::invalid_argument. */
    static FixMessage parse(const std::vector<std::uint8_t>& bytes);
    static FixMessage parse(const std::uint8_t* bytes, std::size_t length);

    // ------------------------------------------------------------------
    // Accessors
    // ------------------------------------------------------------------

    const std::string& msgType() const;
    bool has(int tag) const;
    const std::string& getString(int tag) const;
    std::string getString(int tag, const std::string& defaultValue) const;
    std::int64_t getLong(int tag) const;
    double getDouble(int tag) const;
    double getDouble(int tag, double defaultValue) const;
    char getChar(int tag) const;
    std::string toString() const;

    /** Every field in wire order, header and trailer included, duplicates kept. */
    const std::vector<Field>& fields() const { return ordered_; }

    /** How many times a tag appears. */
    std::size_t count(int tag) const;

    /** Every value of a tag, in wire order. */
    std::vector<std::string> getAll(int tag) const;

    /**
     * Cuts out a repeating group. memberTags lists the tags of one entry,
     * delimiter first: each delimiter opens an entry, and the first tag not
     * listed ends the group. No count tag gives an empty result. Throws
     * std::invalid_argument if the count is malformed or disagrees with the
     * entries found, or if the fields after the count do not open with the
     * delimiter.
     */
    std::vector<std::vector<Field>> group(int countTag, const std::vector<int>& memberTags) const;

    // ------------------------------------------------------------------
    // Building
    // ------------------------------------------------------------------

    /** Body-field builder; the session supplies header fields at encode time. */
    class Builder {
    public:
        explicit Builder(std::string msgType);

        /** Rebuilds a builder from a stored message body (resend path). */
        static Builder restore(std::string msgType, std::string storedBody);

        /** Raw body fields (for the session's outbound message store). */
        const std::string& body() const { return body_; }

        /** Throws std::invalid_argument if the value contains SOH. */
        Builder& field(int tag, const std::string& value);
        Builder& field(int tag, std::int64_t value);
        Builder& field(int tag, char value);
        /** Plain-decimal rendering (FIX forbids scientific notation). */
        Builder& field(int tag, double value);

        const std::string& msgType() const { return msgType_; }

        /** Frames the message with header, body length and checksum. */
        std::vector<std::uint8_t> encode(const std::string& senderCompId,
                                          const std::string& targetCompId, std::int64_t seqNum,
                                          const std::string& sendingTimeUtc) const;

        /**
         * Framing with resend header fields: possDup sets PossDupFlag(43)=Y
         * and origSendingTime adds OrigSendingTime(122) -- required when
         * replaying stored messages.
         */
        std::vector<std::uint8_t> encode(const std::string& senderCompId,
                                          const std::string& targetCompId, std::int64_t seqNum,
                                          const std::string& sendingTimeUtc, bool possDup,
                                          const std::optional<std::string>& origSendingTime) const;

    private:
        std::string msgType_;
        std::string body_;
    };

    static Builder builder(const std::string& msgType) { return Builder(msgType); }

private:
    FixMessage(std::map<int, std::string> fields, std::vector<Field> ordered);

    std::map<int, std::string> fields_;
    std::vector<Field> ordered_;
};

}  // namespace quantfinlib
