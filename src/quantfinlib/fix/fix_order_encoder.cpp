#include "quantfinlib/fix/fix_order_encoder.hpp"

#include <cstring>
#include <stdexcept>

namespace quantfinlib {

namespace {

std::int64_t floorDiv(std::int64_t a, std::int64_t b) {
    std::int64_t q = a / b;
    if ((a % b != 0) && ((a < 0) != (b < 0))) {
        --q;
    }
    return q;
}

/** Howard Hinnant's civil-from-days algorithm: days since 1970-01-01 -> (y, m, d). */
void civilFromDays(std::int64_t z, int& y, int& m, int& d) {
    z += 719468;
    std::int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    unsigned doe = static_cast<unsigned>(z - era * 146097);                 // [0, 146096]
    unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;   // [0, 399]
    std::int64_t yr = static_cast<std::int64_t>(yoe) + era * 400;
    unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);                 // [0, 365]
    unsigned mp = (5 * doy + 2) / 153;                                      // [0, 11]
    d = static_cast<int>(doy - (153 * mp + 2) / 5 + 1);                     // [1, 31]
    m = static_cast<int>(mp + (mp < 10 ? 3 : -9));                          // [1, 12]
    y = static_cast<int>(yr + (m <= 2 ? 1 : 0));
}

}  // namespace

FixOrderEncoder::FixOrderEncoder(const std::string& senderCompId, const std::string& targetCompId,
                                  int maxSymbols, int bufferSize)
        : buffer_(static_cast<std::size_t>(bufferSize > 0 ? bufferSize : 0)),
          symbols_(static_cast<std::size_t>(maxSymbols > 0 ? maxSymbols : 0)) {
    if (senderCompId.empty() || targetCompId.empty() || maxSymbols <= 0 || bufferSize < 256) {
        throw std::invalid_argument("need comp ids, maxSymbols > 0, bufferSize >= 256");
    }
    // The constant chunk of every message: 35=D|49=...|56=...|34=
    std::string head = "35=D" + std::string(1, static_cast<char>(SOH)) + "49=" + senderCompId
                        + std::string(1, static_cast<char>(SOH)) + "56=" + targetCompId
                        + std::string(1, static_cast<char>(SOH)) + "34=";
    header34_.assign(head.begin(), head.end());
}

FixOrderEncoder& FixOrderEncoder::registerSymbol(int symbolId, const std::string& symbol) {
    symbols_.at(static_cast<std::size_t>(symbolId)).assign(symbol.begin(), symbol.end());
    return *this;
}

int FixOrderEncoder::encodeLimit(std::int64_t msgSeqNum, std::int64_t clOrdId, int symbolId,
                                 Side side, std::int64_t quantity, std::int64_t priceMantissa,
                                 int priceDecimals, std::int64_t epochMillis) {
    return encode(msgSeqNum, clOrdId, symbolId, side, quantity, priceMantissa, priceDecimals,
                  false, epochMillis);
}

int FixOrderEncoder::encodeMarket(std::int64_t msgSeqNum, std::int64_t clOrdId, int symbolId,
                                  Side side, std::int64_t quantity, std::int64_t epochMillis) {
    return encode(msgSeqNum, clOrdId, symbolId, side, quantity, 0, 0, true, epochMillis);
}

int FixOrderEncoder::encode(std::int64_t msgSeqNum, std::int64_t clOrdId, int symbolId, Side side,
                            std::int64_t quantity, std::int64_t priceMantissa, int priceDecimals,
                            bool market, std::int64_t epochMillis) {
    if (symbolId < 0 || static_cast<std::size_t>(symbolId) >= symbols_.size()
        || symbols_[static_cast<std::size_t>(symbolId)].empty()) {
        throw std::runtime_error("symbol id " + std::to_string(symbolId) + " not registered");
    }
    const std::vector<std::uint8_t>& symbol = symbols_[static_cast<std::size_t>(symbolId)];
    refreshDate(epochMillis);

    // ---- body: 35=D|49|56|34=seq|52=ts|11|55|54|38|40[|44]|60=ts| ----
    int p = PREFIX_RESERVE;
    p = raw(p, header34_);
    p = digits(p, msgSeqNum);
    buffer_[static_cast<std::size_t>(p++)] = SOH;
    p = tag(p, '5', '2');
    p = timestamp(p, epochMillis);
    buffer_[static_cast<std::size_t>(p++)] = SOH;
    p = tag(p, '1', '1');
    p = digits(p, clOrdId);
    buffer_[static_cast<std::size_t>(p++)] = SOH;
    p = tag(p, '5', '5');
    p = raw(p, symbol);
    buffer_[static_cast<std::size_t>(p++)] = SOH;
    p = tag(p, '5', '4');
    buffer_[static_cast<std::size_t>(p++)] = side == Side::BUY ? std::uint8_t('1') : std::uint8_t('2');
    buffer_[static_cast<std::size_t>(p++)] = SOH;
    p = tag(p, '3', '8');
    p = digits(p, quantity);
    buffer_[static_cast<std::size_t>(p++)] = SOH;
    p = tag(p, '4', '0');
    buffer_[static_cast<std::size_t>(p++)] = market ? std::uint8_t('1') : std::uint8_t('2');
    buffer_[static_cast<std::size_t>(p++)] = SOH;
    if (!market) {
        p = tag(p, '4', '4');
        p = price(p, priceMantissa, priceDecimals);
        buffer_[static_cast<std::size_t>(p++)] = SOH;
    }
    p = tag(p, '6', '0');
    p = timestamp(p, epochMillis);
    buffer_[static_cast<std::size_t>(p++)] = SOH;
    int bodyEnd = p;

    // ---- prefix, written BACKWARDS in front of the body: 8=..|9=len| ----
    int bodyLen = bodyEnd - PREFIX_RESERVE;
    int q = PREFIX_RESERVE;
    buffer_[static_cast<std::size_t>(--q)] = SOH;
    std::int64_t len = bodyLen;
    do {
        buffer_[static_cast<std::size_t>(--q)] = static_cast<std::uint8_t>('0' + (len % 10));
        len /= 10;
    } while (len > 0);
    buffer_[static_cast<std::size_t>(--q)] = '=';
    buffer_[static_cast<std::size_t>(--q)] = '9';
    buffer_[static_cast<std::size_t>(--q)] = SOH;
    buffer_[static_cast<std::size_t>(--q)] = '4';
    buffer_[static_cast<std::size_t>(--q)] = '.';
    buffer_[static_cast<std::size_t>(--q)] = '4';
    buffer_[static_cast<std::size_t>(--q)] = '.';
    buffer_[static_cast<std::size_t>(--q)] = 'X';
    buffer_[static_cast<std::size_t>(--q)] = 'I';
    buffer_[static_cast<std::size_t>(--q)] = 'F';
    buffer_[static_cast<std::size_t>(--q)] = '=';
    buffer_[static_cast<std::size_t>(--q)] = '8';
    start_ = q;

    // ---- trailer: 10=NNN| over everything from 8= through the body ----
    int sum = 0;
    for (int i = start_; i < bodyEnd; ++i) {
        sum += buffer_[static_cast<std::size_t>(i)];
    }
    sum &= 0xFF;
    p = bodyEnd;
    p = tag(p, '1', '0');
    buffer_[static_cast<std::size_t>(p++)] = static_cast<std::uint8_t>('0' + sum / 100);
    buffer_[static_cast<std::size_t>(p++)] = static_cast<std::uint8_t>('0' + (sum / 10) % 10);
    buffer_[static_cast<std::size_t>(p++)] = static_cast<std::uint8_t>('0' + sum % 10);
    buffer_[static_cast<std::size_t>(p++)] = SOH;
    end_ = p;
    return end_ - start_;
}

int FixOrderEncoder::tag(int p, char a, char b) {
    buffer_[static_cast<std::size_t>(p++)] = static_cast<std::uint8_t>(a);
    buffer_[static_cast<std::size_t>(p++)] = static_cast<std::uint8_t>(b);
    buffer_[static_cast<std::size_t>(p++)] = '=';
    return p;
}

int FixOrderEncoder::raw(int p, const std::vector<std::uint8_t>& bytes) {
    std::memcpy(buffer_.data() + p, bytes.data(), bytes.size());
    return p + static_cast<int>(bytes.size());
}

int FixOrderEncoder::digits(int p, std::int64_t value) {
    if (value < 0) {
        throw std::invalid_argument("negative value: " + std::to_string(value));
    }
    int n = 1;
    for (std::int64_t v = value; v >= 10; v /= 10) {
        ++n;
    }
    int endPos = p + n;
    std::int64_t v = value;
    for (int i = endPos - 1; i >= p; --i) {
        buffer_[static_cast<std::size_t>(i)] = static_cast<std::uint8_t>('0' + (v % 10));
        v /= 10;
    }
    return endPos;
}

int FixOrderEncoder::price(int p, std::int64_t mantissa, int decimals) {
    if (mantissa < 0 || decimals < 0) {
        throw std::invalid_argument("mantissa and decimals must be >= 0");
    }
    if (decimals == 0) {
        return digits(p, mantissa);
    }
    std::int64_t scale = 1;
    for (int i = 0; i < decimals; ++i) {
        scale *= 10;
    }
    p = digits(p, mantissa / scale);
    buffer_[static_cast<std::size_t>(p++)] = '.';
    std::int64_t frac = mantissa % scale;
    for (std::int64_t s = scale / 10; s > 0; s /= 10) {
        buffer_[static_cast<std::size_t>(p++)] = static_cast<std::uint8_t>('0' + (frac / s));
        frac %= s;
    }
    return p;
}

int FixOrderEncoder::timestamp(int p, std::int64_t epochMillis) {
    std::memcpy(buffer_.data() + p, datePrefix_, 9);
    p += 9;
    int millisOfDay = static_cast<int>(epochMillis - cachedEpochDay_ * 86'400'000LL);
    int hh = millisOfDay / 3'600'000;
    int mm = (millisOfDay / 60'000) % 60;
    int ss = (millisOfDay / 1'000) % 60;
    int ms = millisOfDay % 1'000;
    buffer_[static_cast<std::size_t>(p++)] = static_cast<std::uint8_t>('0' + hh / 10);
    buffer_[static_cast<std::size_t>(p++)] = static_cast<std::uint8_t>('0' + hh % 10);
    buffer_[static_cast<std::size_t>(p++)] = ':';
    buffer_[static_cast<std::size_t>(p++)] = static_cast<std::uint8_t>('0' + mm / 10);
    buffer_[static_cast<std::size_t>(p++)] = static_cast<std::uint8_t>('0' + mm % 10);
    buffer_[static_cast<std::size_t>(p++)] = ':';
    buffer_[static_cast<std::size_t>(p++)] = static_cast<std::uint8_t>('0' + ss / 10);
    buffer_[static_cast<std::size_t>(p++)] = static_cast<std::uint8_t>('0' + ss % 10);
    buffer_[static_cast<std::size_t>(p++)] = '.';
    buffer_[static_cast<std::size_t>(p++)] = static_cast<std::uint8_t>('0' + ms / 100);
    buffer_[static_cast<std::size_t>(p++)] = static_cast<std::uint8_t>('0' + (ms / 10) % 10);
    buffer_[static_cast<std::size_t>(p++)] = static_cast<std::uint8_t>('0' + ms % 10);
    return p;
}

void FixOrderEncoder::refreshDate(std::int64_t epochMillis) {
    std::int64_t epochDay = floorDiv(epochMillis, 86'400'000LL);
    if (epochDay != cachedEpochDay_) {
        int y, m, d;
        civilFromDays(epochDay, y, m, d);
        char date[9];
        std::snprintf(date, sizeof(date), "%04d%02d%02d", y, m, d);
        for (int i = 0; i < 8; ++i) {
            datePrefix_[i] = static_cast<std::uint8_t>(date[i]);
        }
        datePrefix_[8] = '-';
        cachedEpochDay_ = epochDay;
    }
}

}  // namespace quantfinlib
