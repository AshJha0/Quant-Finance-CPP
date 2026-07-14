#include "quantfinlib/fix/fix_message.hpp"

#include <charconv>
#include <cstdio>
#include <stdexcept>

namespace quantfinlib {

const std::string FixMessage::BEGIN_STRING = "FIX.4.4";

const std::string FixMessage::HEARTBEAT = "0";
const std::string FixMessage::TEST_REQUEST = "1";
const std::string FixMessage::RESEND_REQUEST = "2";
const std::string FixMessage::REJECT = "3";
const std::string FixMessage::SEQUENCE_RESET = "4";
const std::string FixMessage::LOGON = "A";
const std::string FixMessage::LOGOUT = "5";
const std::string FixMessage::NEW_ORDER_SINGLE = "D";
const std::string FixMessage::EXECUTION_REPORT = "8";
const std::string FixMessage::ORDER_CANCEL_REQUEST = "F";
const std::string FixMessage::ORDER_CANCEL_REPLACE_REQUEST = "G";

namespace {

std::string printable(const std::string& raw) {
    std::string out = raw;
    for (char& c : out) {
        if (c == FixMessage::SOH) {
            c = '|';
        }
    }
    return out;
}

/** Renders a FIX-legal plain decimal, mirroring BigDecimal.valueOf(...).stripTrailingZeros(). */
std::string formatDecimal(double value) {
    if (value == 0.0) {
        return "0.0";
    }
    char buf[64];
    auto res = std::to_chars(buf, buf + sizeof(buf), value, std::chars_format::fixed);
    return std::string(buf, res.ptr);
}

std::string threeDigits(unsigned v) {
    char buf[4];
    std::snprintf(buf, sizeof(buf), "%03u", v % 256);
    return std::string(buf);
}

}  // namespace

FixMessage::FixMessage(std::map<int, std::string> fields) : fields_(std::move(fields)) {
}

FixMessage FixMessage::parse(const std::uint8_t* bytes, std::size_t length) {
    std::string raw(reinterpret_cast<const char*>(bytes), length);
    std::string beginPrefix = "8=" + BEGIN_STRING + std::string(1, SOH);
    if (raw.rfind(beginPrefix, 0) != 0) {
        throw std::invalid_argument("missing BeginString: " + printable(raw));
    }
    std::string checksumMarker = std::string(1, SOH) + "10=";
    std::size_t checksumField = raw.rfind(checksumMarker);
    if (checksumField == std::string::npos || raw.empty() || raw.back() != SOH) {
        throw std::invalid_argument("missing CheckSum: " + printable(raw));
    }
    unsigned sum = 0;
    for (std::size_t i = 0; i <= checksumField; ++i) {
        sum += static_cast<unsigned char>(bytes[i]);
    }
    std::string declared = raw.substr(checksumField + 4, raw.size() - 1 - (checksumField + 4));
    if (threeDigits(sum) != declared) {
        throw std::invalid_argument("checksum mismatch: declared " + declared + " computed "
                                     + threeDigits(sum));
    }

    std::map<int, std::string> fields;
    std::string body = raw.substr(0, raw.size() - 1);
    std::size_t pos = 0;
    while (pos <= body.size()) {
        std::size_t next = body.find(SOH, pos);
        std::string pair = (next == std::string::npos) ? body.substr(pos)
                                                         : body.substr(pos, next - pos);
        std::size_t eq = pair.find('=');
        if (eq == std::string::npos || eq == 0) {
            throw std::invalid_argument("malformed field: " + printable(pair));
        }
        int tag = std::atoi(pair.substr(0, eq).c_str());
        fields[tag] = pair.substr(eq + 1);
        if (next == std::string::npos) {
            break;
        }
        pos = next + 1;
    }

    // Validate body length: bytes after the BodyLength field's SOH, up to CheckSum.
    std::size_t nineEq = raw.find("9=");
    std::size_t bodyStart = raw.find(SOH, nineEq) + 1;
    auto bl = fields.find(BODY_LENGTH);
    if (bl == fields.end()) {
        throw std::invalid_argument("missing tag " + std::to_string(BODY_LENGTH));
    }
    long declaredLen = std::atol(bl->second.c_str());
    long actualLen = static_cast<long>(checksumField) + 1 - static_cast<long>(bodyStart);
    if (declaredLen != actualLen) {
        throw std::invalid_argument("body length mismatch: declared "
                                     + std::to_string(declaredLen) + " actual "
                                     + std::to_string(actualLen));
    }
    return FixMessage(std::move(fields));
}

FixMessage FixMessage::parse(const std::vector<std::uint8_t>& bytes) {
    return parse(bytes.data(), bytes.size());
}

const std::string& FixMessage::msgType() const {
    return getString(MSG_TYPE);
}

bool FixMessage::has(int tag) const {
    return fields_.find(tag) != fields_.end();
}

const std::string& FixMessage::getString(int tag) const {
    auto it = fields_.find(tag);
    if (it == fields_.end()) {
        throw std::invalid_argument("missing tag " + std::to_string(tag) + " in " + toString());
    }
    return it->second;
}

std::string FixMessage::getString(int tag, const std::string& defaultValue) const {
    auto it = fields_.find(tag);
    return it == fields_.end() ? defaultValue : it->second;
}

std::int64_t FixMessage::getLong(int tag) const {
    return std::atoll(getString(tag).c_str());
}

double FixMessage::getDouble(int tag) const {
    return std::atof(getString(tag).c_str());
}

double FixMessage::getDouble(int tag, double defaultValue) const {
    auto it = fields_.find(tag);
    return it == fields_.end() ? defaultValue : std::atof(it->second.c_str());
}

char FixMessage::getChar(int tag) const {
    return getString(tag).at(0);
}

std::string FixMessage::toString() const {
    std::string sb;
    for (const auto& [tag, value] : fields_) {
        sb += std::to_string(tag);
        sb += '=';
        sb += value;
        sb += '|';
    }
    return sb;
}

// ------------------------------------------------------------------
// Builder
// ------------------------------------------------------------------

FixMessage::Builder::Builder(std::string msgType) : msgType_(std::move(msgType)) {
}

FixMessage::Builder FixMessage::Builder::restore(std::string msgType, std::string storedBody) {
    Builder b(std::move(msgType));
    b.body_ = std::move(storedBody);
    return b;
}

FixMessage::Builder& FixMessage::Builder::field(int tag, const std::string& value) {
    body_ += std::to_string(tag);
    body_ += '=';
    body_ += value;
    body_ += SOH;
    return *this;
}

FixMessage::Builder& FixMessage::Builder::field(int tag, std::int64_t value) {
    return field(tag, std::to_string(value));
}

FixMessage::Builder& FixMessage::Builder::field(int tag, char value) {
    return field(tag, std::string(1, value));
}

FixMessage::Builder& FixMessage::Builder::field(int tag, double value) {
    return field(tag, formatDecimal(value));
}

std::vector<std::uint8_t> FixMessage::Builder::encode(const std::string& senderCompId,
                                                       const std::string& targetCompId,
                                                       std::int64_t seqNum,
                                                       const std::string& sendingTimeUtc) const {
    return encode(senderCompId, targetCompId, seqNum, sendingTimeUtc, false, std::nullopt);
}

std::vector<std::uint8_t> FixMessage::Builder::encode(
        const std::string& senderCompId, const std::string& targetCompId, std::int64_t seqNum,
        const std::string& sendingTimeUtc, bool possDup,
        const std::optional<std::string>& origSendingTime) const {
    std::string afterLength;
    afterLength += "35=" + msgType_ + SOH;
    afterLength += "49=" + senderCompId + SOH;
    afterLength += "56=" + targetCompId + SOH;
    afterLength += "34=" + std::to_string(seqNum) + SOH;
    if (possDup) {
        afterLength += std::string("43=Y") + SOH;
    }
    afterLength += "52=" + sendingTimeUtc + SOH;
    if (origSendingTime.has_value()) {
        afterLength += "122=" + *origSendingTime + SOH;
    }
    afterLength += body_;

    std::string head = "8=" + BEGIN_STRING + SOH + "9=" + std::to_string(afterLength.size()) + SOH;
    std::string payload = head + afterLength;
    unsigned sum = 0;
    for (unsigned char c : payload) {
        sum += c;
    }
    std::string full = payload + "10=" + threeDigits(sum) + SOH;
    return std::vector<std::uint8_t>(full.begin(), full.end());
}

}  // namespace quantfinlib
