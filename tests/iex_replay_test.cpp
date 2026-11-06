// IEX capture reader, IEX-TP decoder, books and replay checks. Every byte here
// is built by hand from the public specifications (IEX Transport v1.25, DEEP
// v1.08, DEEP+ v1.06, pcap and pcapng layouts). No vendor data is in this file.
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <random>
#include <string>
#include <vector>

#include "quantfinlib/marketdata/iex_book.hpp"
#include "quantfinlib/marketdata/iex_capture_reader.hpp"
#include "quantfinlib/marketdata/iex_replay.hpp"
#include "quantfinlib/marketdata/iex_tp_decoder.hpp"
#include "qtest.hpp"

using namespace quantfinlib;

namespace {

using Bytes = std::vector<std::uint8_t>;

template <class T>
void put(Bytes& b, T v) {
    for (std::size_t i = 0; i < sizeof(T); ++i) {
        b.push_back(static_cast<std::uint8_t>(static_cast<std::uint64_t>(v) >> (8 * i)));
    }
}

void putSymbol(Bytes& b, const std::string& s) {
    for (std::size_t i = 0; i < 8; ++i) {
        b.push_back(i < s.size() ? static_cast<std::uint8_t>(s[i]) : ' ');
    }
}

Bytes head(char type, std::uint8_t flags, std::int64_t ts) {
    Bytes b{static_cast<std::uint8_t>(type), flags};
    put<std::int64_t>(b, ts);
    return b;
}

Bytes systemEvent(char code, std::int64_t ts) {
    return head('S', static_cast<std::uint8_t>(code), ts);
}

Bytes level(bool buy, bool complete, std::int64_t ts, const std::string& sym, std::uint32_t size,
            std::int64_t price) {
    Bytes b = head(buy ? '8' : '5', complete ? 1 : 0, ts);
    putSymbol(b, sym);
    put<std::uint32_t>(b, size);
    put<std::int64_t>(b, price);
    return b;
}

Bytes trade(std::int64_t ts, const std::string& sym, std::uint32_t size, std::int64_t price,
            std::int64_t id) {
    Bytes b = head('T', 0, ts);
    putSymbol(b, sym);
    put<std::uint32_t>(b, size);
    put<std::int64_t>(b, price);
    put<std::int64_t>(b, id);
    return b;
}

Bytes orderMsg(char type, std::uint8_t flags, std::int64_t ts, const std::string& sym,
               std::int64_t id, std::uint32_t size, std::int64_t price) {
    Bytes b = head(type, flags, ts);
    putSymbol(b, sym);
    put<std::int64_t>(b, id);
    put<std::uint32_t>(b, size);
    put<std::int64_t>(b, price);
    return b;
}

Bytes add(std::int64_t ts, const std::string& sym, std::int64_t id, bool buy, std::uint32_t size,
          std::int64_t price) {
    return orderMsg('a', buy ? '8' : '5', ts, sym, id, size, price);
}

Bytes executed(std::int64_t ts, const std::string& sym, std::int64_t id, std::uint32_t size,
               std::int64_t price, std::int64_t tradeId) {
    Bytes b = orderMsg('L', 0, ts, sym, id, size, price);
    put<std::int64_t>(b, tradeId);
    return b;
}

Bytes deleted(std::int64_t ts, const std::string& sym, std::int64_t id) {
    Bytes b = head('R', 0, ts);
    putSymbol(b, sym);
    put<std::int64_t>(b, id);
    return b;
}

Bytes segment(std::uint16_t protocol, std::int64_t seq, std::int64_t offset,
              const std::vector<Bytes>& messages) {
    Bytes body;
    for (const Bytes& m : messages) {
        put<std::uint16_t>(body, static_cast<std::uint16_t>(m.size()));
        body.insert(body.end(), m.begin(), m.end());
    }
    Bytes b{1, 0};
    put<std::uint16_t>(b, protocol);
    put<std::uint32_t>(b, 1);
    put<std::uint32_t>(b, 7);
    put<std::uint16_t>(b, static_cast<std::uint16_t>(body.size()));
    put<std::uint16_t>(b, static_cast<std::uint16_t>(messages.size()));
    put<std::int64_t>(b, offset);
    put<std::int64_t>(b, seq);
    put<std::int64_t>(b, 1000 + seq);
    b.insert(b.end(), body.begin(), body.end());
    return b;
}

constexpr std::uint16_t DEEP = IexTpDecoder::PROTOCOL_DEEP;
constexpr std::uint16_t PLUS = IexTpDecoder::PROTOCOL_DEEP_PLUS;

Bytes ethernetUdp(const Bytes& payload) {
    Bytes f(12, 0);
    f.push_back(0x81);  // one VLAN tag, to exercise the walk
    f.push_back(0x00);
    f.push_back(0);
    f.push_back(5);
    f.push_back(0x08);
    f.push_back(0x00);
    std::size_t udp = payload.size() + 8;
    std::size_t ip = udp + 20;
    Bytes h{0x45, 0, static_cast<std::uint8_t>(ip >> 8), static_cast<std::uint8_t>(ip), 0, 0, 0x40,
            0, 64, 17, 0, 0, 10, 0, 0, 1, 233, 215, 21, 4};
    f.insert(f.end(), h.begin(), h.end());
    Bytes u{0x28, 0x00, 0x28, 0x00, static_cast<std::uint8_t>(udp >> 8),
            static_cast<std::uint8_t>(udp), 0, 0};
    f.insert(f.end(), u.begin(), u.end());
    f.insert(f.end(), payload.begin(), payload.end());
    return f;
}

Bytes classicPcap(const std::vector<Bytes>& payloads) {
    Bytes b;
    put<std::uint32_t>(b, 0xA1B2C3D4);
    put<std::uint16_t>(b, 2);
    put<std::uint16_t>(b, 4);
    put<std::uint64_t>(b, 0);
    put<std::uint32_t>(b, 65535);
    put<std::uint32_t>(b, 1);
    for (const Bytes& p : payloads) {
        Bytes f = ethernetUdp(p);
        put<std::uint64_t>(b, 0);
        put<std::uint32_t>(b, static_cast<std::uint32_t>(f.size()));
        put<std::uint32_t>(b, static_cast<std::uint32_t>(f.size()));
        b.insert(b.end(), f.begin(), f.end());
    }
    return b;
}

Bytes pcapng(const std::vector<Bytes>& payloads) {
    Bytes b;
    put<std::uint32_t>(b, 0x0A0D0D0A);
    put<std::uint32_t>(b, 28);
    put<std::uint32_t>(b, 0x1A2B3C4D);
    put<std::uint16_t>(b, 1);
    put<std::uint16_t>(b, 0);
    put<std::int64_t>(b, -1);
    put<std::uint32_t>(b, 28);
    put<std::uint32_t>(b, 1);
    put<std::uint32_t>(b, 20);
    put<std::uint16_t>(b, 1);
    put<std::uint16_t>(b, 0);
    put<std::uint32_t>(b, 65535);
    put<std::uint32_t>(b, 20);
    for (const Bytes& p : payloads) {
        Bytes f = ethernetUdp(p);
        std::size_t padded = (f.size() + 3) / 4 * 4;
        std::uint32_t total = static_cast<std::uint32_t>(32 + padded);
        put<std::uint32_t>(b, 6);
        put<std::uint32_t>(b, total);
        put<std::uint32_t>(b, 0);
        put<std::uint64_t>(b, 0);
        put<std::uint32_t>(b, static_cast<std::uint32_t>(f.size()));
        put<std::uint32_t>(b, static_cast<std::uint32_t>(f.size()));
        b.insert(b.end(), f.begin(), f.end());
        b.resize(b.size() + padded - f.size());
        put<std::uint32_t>(b, total);
    }
    return b;
}

// Delivers input in small uneven pieces, so records straddle refills.
IexCaptureReader readerOf(const Bytes& input, std::size_t& at) {
    return IexCaptureReader([&input, &at](std::uint8_t* p, std::size_t n) {
        std::size_t take = std::min({n, input.size() - at, std::size_t{7}});
        std::memcpy(p, input.data() + at, take);
        at += take;
        return take;
    });
}

struct Collect : IexTpDecoder::Handler {
    std::vector<IexMessage> messages;
    std::int64_t lost = 0;
    void onMessage(const IexMessage& m) override { messages.push_back(m); }
    void onGap(std::int64_t n) override { lost += n; }
};

}  // namespace

TEST(decodesPublishedFieldLayouts) {
    IexTpDecoder d;
    Collect c;
    Bytes s = segment(DEEP, 1, 0,
                      {level(true, true, 5, "AAPL", 300, 1500100), trade(6, "AAPL", 50, 1500100, 77)});
    d.onDatagram(s.data(), s.size(), c);
    EXPECT_TRUE(c.messages.size() == 2);
    const IexMessage& l = c.messages[0];
    EXPECT_TRUE(l.type == '8' && l.flags == 1 && l.timestamp == 5);
    EXPECT_TRUE(l.size == 300 && l.price == 1500100);
    EXPECT_TRUE(l.symbol == IexReplay::symbolKey("AAPL"));
    EXPECT_TRUE(c.messages[1].tradeId == 77);

    IexTpDecoder p;
    Collect e;
    Bytes t = segment(PLUS, 1, 0, {executed(9, "MSFT", 42, 10, 3000000, 88)});
    p.onDatagram(t.data(), t.size(), e);
    EXPECT_TRUE(e.messages.size() == 1);
    EXPECT_TRUE(e.messages[0].orderId == 42 && e.messages[0].size == 10);
    EXPECT_TRUE(e.messages[0].price == 3000000 && e.messages[0].tradeId == 88);
}

TEST(sequenceGapIsReportedAndHeartbeatRevealsOne) {
    IexTpDecoder d;
    Collect c;
    Bytes a = segment(DEEP, 1, 0, {systemEvent('O', 1)});
    d.onDatagram(a.data(), a.size(), c);
    Bytes b = segment(DEEP, 5, 100, {systemEvent('S', 2)});
    d.onDatagram(b.data(), b.size(), c);
    EXPECT_TRUE(d.stats().seqGaps == 1 && d.stats().gapMessages == 3 && c.lost == 3);
    Bytes hb = segment(DEEP, 9, 200, {});
    d.onDatagram(hb.data(), hb.size(), c);
    EXPECT_TRUE(d.stats().heartbeats == 1);
    EXPECT_TRUE(d.stats().seqGaps == 2 && d.stats().gapMessages == 6);
}

TEST(lineDuplicatesAreDroppedAndOverlapDeliversOnlyNewMessages) {
    IexTpDecoder d;
    Collect c;
    Bytes a = segment(DEEP, 1, 0, {systemEvent('O', 1), systemEvent('S', 2)});
    d.onDatagram(a.data(), a.size(), c);
    d.onDatagram(a.data(), a.size(), c);
    EXPECT_TRUE(d.stats().duplicateSegments == 1 && c.messages.size() == 2);
    Bytes b = segment(DEEP, 2, 12, {systemEvent('S', 2), systemEvent('R', 3)});
    d.onDatagram(b.data(), b.size(), c);
    EXPECT_TRUE(d.stats().duplicateMessages == 1 && c.messages.size() == 3);
    EXPECT_TRUE(c.messages[2].flags == 'R');
}

TEST(offsetMismatchIsCounted) {
    IexTpDecoder d;
    Collect c;
    Bytes a = segment(DEEP, 1, 0, {systemEvent('O', 1)});
    Bytes b = segment(DEEP, 2, 999, {systemEvent('S', 2)});
    d.onDatagram(a.data(), a.size(), c);
    d.onDatagram(b.data(), b.size(), c);
    EXPECT_TRUE(d.stats().offsetMismatches == 1);
}

TEST(malformedSegmentDeliversNothingAndBecomesAGap) {
    IexTpDecoder d;
    Collect c;
    Bytes good = segment(DEEP, 1, 0, {systemEvent('O', 1)});
    d.onDatagram(good.data(), good.size(), c);
    // Second block's length runs past the payload: the sound first block must not
    // be delivered either.
    Bytes bad = segment(DEEP, 2, 12, {systemEvent('S', 2), systemEvent('R', 3)});
    bad[40 + 12] = 0xFF;
    d.onDatagram(bad.data(), bad.size(), c);
    EXPECT_TRUE(d.stats().badSegments == 1 && c.messages.size() == 1);
    Bytes next = segment(DEEP, 4, 36, {systemEvent('M', 4)});
    d.onDatagram(next.data(), next.size(), c);
    EXPECT_TRUE(d.stats().seqGaps == 1 && d.stats().gapMessages == 2);
}

TEST(headerLiesAreRejected) {
    Collect c;
    Bytes good = segment(DEEP, 1, 0, {systemEvent('O', 1)});
    auto rejected = [&](Bytes s) {
        IexTpDecoder d;
        d.onDatagram(s.data(), s.size(), c);
        return d.stats().badSegments == 1 && c.messages.empty();
    };
    Bytes wrongCount = good;
    wrongCount[14] = 2;
    EXPECT_TRUE(rejected(wrongCount));
    Bytes longPayload = good;
    longPayload[12] = 200;
    EXPECT_TRUE(rejected(longPayload));
    Bytes version = good;
    version[0] = 2;
    EXPECT_TRUE(rejected(version));
    Bytes negativeSeq = good;
    negativeSeq[31] = 0x80;
    EXPECT_TRUE(rejected(negativeSeq));
    Bytes nearMaxSeq = good;
    std::memset(nearMaxSeq.data() + 24, 0xFF, 7);
    nearMaxSeq[31] = 0x7F;
    EXPECT_TRUE(rejected(nearMaxSeq));
    EXPECT_TRUE(rejected(Bytes(good.begin(), good.begin() + 39)));
}

TEST(shortAndInvalidMessagesAreDroppedGrownOnesKept) {
    IexTpDecoder d;
    Collect c;
    Bytes shortLevel = level(true, true, 1, "AAPL", 1, 100);
    shortLevel.pop_back();
    Bytes badPrice = level(true, true, 1, "AAPL", 1, 0);
    Bytes grown = level(true, true, 1, "AAPL", 1, 100);
    grown.push_back(0);
    Bytes unknown = head('?', 0, 1);
    Bytes wrongFeed = add(1, "AAPL", 1, true, 1, 100);  // DEEP+ type on DEEP
    Bytes s = segment(DEEP, 1, 0, {shortLevel, badPrice, grown, unknown, wrongFeed});
    d.onDatagram(s.data(), s.size(), c);
    EXPECT_TRUE(d.stats().badMessages == 2);
    EXPECT_TRUE(d.stats().grownMessages == 1);
    EXPECT_TRUE(d.stats().unknownType == 2);
    EXPECT_TRUE(c.messages.size() == 1);
}

TEST(captureReaderReadsBothContainers) {
    std::vector<Bytes> payloads{segment(DEEP, 1, 0, {systemEvent('O', 1)}),
                                segment(DEEP, 2, 12, {systemEvent('S', 2)})};
    for (const Bytes& capture : {classicPcap(payloads), pcapng(payloads)}) {
        std::size_t at = 0;
        IexCaptureReader r = readerOf(capture, at);
        IexCaptureReader::Datagram d;
        int n = 0;
        while (r.next(d)) {
            EXPECT_TRUE(d.size == payloads[static_cast<std::size_t>(n)].size());
            EXPECT_TRUE(std::memcmp(d.data, payloads[static_cast<std::size_t>(n)].data(), d.size) == 0);
            ++n;
        }
        EXPECT_TRUE(n == 2);
        EXPECT_TRUE(!r.failed());
    }
}

TEST(cutCaptureFailsAtEveryLength) {
    Bytes whole = pcapng({segment(DEEP, 1, 0, {systemEvent('O', 1)})});
    for (std::size_t n = 1; n < whole.size(); ++n) {
        Bytes cut(whole.begin(), whole.begin() + static_cast<std::ptrdiff_t>(n));
        std::size_t at = 0;
        IexCaptureReader r = readerOf(cut, at);
        IexCaptureReader::Datagram d;
        while (r.next(d)) {
        }
        // A cut exactly between blocks is indistinguishable from a shorter capture.
        bool boundary = n == 28 || n == 48;
        EXPECT_TRUE(r.failed() != boundary);
    }
}

TEST(hostileBlockLengthsStopTheReader) {
    Bytes whole = pcapng({segment(DEEP, 1, 0, {systemEvent('O', 1)})});
    for (std::uint32_t lie : {0U, 3U, 13U, 0xFFFFFFFFU, 0x7FFFFFFCU}) {
        Bytes bad = whole;
        std::memcpy(bad.data() + 48 + 4, &lie, 4);
        std::size_t at = 0;
        IexCaptureReader r = readerOf(bad, at);
        IexCaptureReader::Datagram d;
        EXPECT_TRUE(!r.next(d));
        EXPECT_TRUE(r.failed());
    }
    Bytes capturedLie = whole;
    std::uint32_t big = 100000;
    std::memcpy(capturedLie.data() + 48 + 20, &big, 4);
    std::size_t at = 0;
    IexCaptureReader r = readerOf(capturedLie, at);
    IexCaptureReader::Datagram d;
    EXPECT_TRUE(!r.next(d));
    EXPECT_TRUE(r.failed());
}

TEST(levelBookHoldsBboThroughAnEvent) {
    IexReplay replay;
    IexTpDecoder d;
    Bytes s = segment(DEEP, 1, 0,
                      {level(true, true, 1, "X", 100, 1000), level(false, true, 2, "X", 100, 1100),
                       // Mid-event the book crosses; only the finished state counts.
                       level(true, false, 3, "X", 100, 1200), level(false, false, 3, "X", 100, 1300),
                       level(false, false, 3, "X", 0, 1100), level(true, true, 3, "X", 0, 1000)});
    d.onDatagram(s.data(), s.size(), replay);
    replay.finish();
    EXPECT_TRUE(replay.checks().events == 3);
    EXPECT_TRUE(replay.checks().crossedEvents == 0);
    EXPECT_TRUE(replay.checks().removeMissingLevel == 0);
}

TEST(crossedLockedAndMissingRemovalAreCounted) {
    IexReplay replay;
    IexTpDecoder d;
    Bytes s = segment(DEEP, 1, 0,
                      {level(true, true, 1, "X", 100, 1000), level(false, true, 2, "X", 100, 1000),
                       level(true, true, 3, "X", 100, 1050), level(true, true, 4, "X", 0, 7777),
                       level(true, false, 5, "X", 1, 900)});
    d.onDatagram(s.data(), s.size(), replay);
    replay.finish();
    EXPECT_TRUE(replay.checks().lockedEvents == 1);
    EXPECT_TRUE(replay.checks().crossedEvents == 2);
    EXPECT_TRUE(replay.checks().removeMissingLevel == 1);
    EXPECT_TRUE(replay.checks().incompleteAtEnd == 1);
    EXPECT_TRUE(replay.checks().restingAtEnd == 1);
}

TEST(printsAreCheckedAgainstTheBbo) {
    IexReplay replay;
    IexTpDecoder d;
    Bytes s = segment(DEEP, 1, 0,
                      {systemEvent('R', 1), trade(1, "X", 5, 1000, 1),
                       level(true, true, 2, "X", 100, 1000), level(false, true, 3, "X", 100, 1100),
                       trade(4, "X", 5, 1050, 2), trade(5, "X", 5, 1200, 3), systemEvent('M', 6),
                       trade(7, "X", 5, 1200, 4)});
    d.onDatagram(s.data(), s.size(), replay);
    const auto& k = replay.checks();
    EXPECT_TRUE(k.prints == 4 && k.printShares == 20 && k.printIdSum == 10);
    EXPECT_TRUE(k.printsOneSided == 1 && k.printsInside == 1);
    EXPECT_TRUE(k.printsOutside == 1 && k.printsOutsideOther == 1);
}

TEST(systemEventsOutOfOrderAreCounted) {
    IexReplay replay;
    IexTpDecoder d;
    Bytes s = segment(DEEP, 1, 0, {systemEvent('O', 1), systemEvent('R', 2), systemEvent('S', 3)});
    d.onDatagram(s.data(), s.size(), replay);
    EXPECT_TRUE(replay.checks().systemEvents == 3 && replay.checks().systemOutOfOrder == 1);
}

TEST(orderBookKeepsLevelsNonNegativeAndRefusesOversize) {
    IexOrderBook b;
    EXPECT_TRUE(b.add(1, true, 1000, 100));
    EXPECT_TRUE(b.add(2, true, 1000, 50));
    EXPECT_TRUE(!b.add(2, true, 1000, 50));
    EXPECT_TRUE(b.levels().size(true, 1000) == 150);
    EXPECT_TRUE(!b.execute(1, 101));
    EXPECT_TRUE(b.execute(1, 40));
    EXPECT_TRUE(b.modify(1, 1010, 60));
    EXPECT_TRUE(b.levels().size(true, 1000) == 50 && b.levels().size(true, 1010) == 60);
    EXPECT_TRUE(b.remove(2));
    EXPECT_TRUE(!b.remove(2));
    EXPECT_TRUE(b.execute(1, 60));
    EXPECT_TRUE(b.levels().empty() && b.orders().empty());
    EXPECT_TRUE(b.stats().oversizeExecutions == 1 && b.stats().unknownOrders == 1);
    EXPECT_TRUE(b.stats().duplicateOrders == 1 && b.stats().negativeLevels == 0);
}

TEST(levelBookIsBounded) {
    IexLevelBook b;
    for (std::size_t i = 1; i <= IexLevelBook::MAX_LEVELS + 5; ++i) {
        b.set(true, static_cast<std::int64_t>(i), 1);
    }
    EXPECT_TRUE(b.refused() == 5);
    EXPECT_TRUE(!b.add(true, 1, -2));
}

TEST(shadowHftBookTracksOrdersAndSpotsNonFrontFills) {
    IexReplay replay({"X"});
    IexTpDecoder d;
    Bytes s = segment(PLUS, 1, 0,
                      {add(1, "X", 1, true, 100, 10000), add(2, "X", 2, true, 50, 10000),
                       add(3, "X", 3, false, 70, 10100),
                       executed(4, "X", 1, 100, 10000, 9),   // at the front
                       add(5, "X", 4, true, 30, 10000),
                       executed(6, "X", 4, 30, 10000, 10),   // not at the front
                       deleted(7, "X", 2), deleted(8, "X", 3)});
    d.onDatagram(s.data(), s.size(), replay);
    replay.finish();
    const auto& k = replay.checks();
    EXPECT_TRUE(k.hftExecAtFront == 1 && k.hftExecNotAtFront == 1);
    EXPECT_TRUE(k.execAtTouch == 2 && k.execBehindTouch == 0);
    EXPECT_TRUE(k.hftBboCompared > 0 && k.hftBboMismatches == 0);
    EXPECT_TRUE(k.hftRejects == 0);
    EXPECT_TRUE(k.restingAtEnd == 0);
}

TEST(queueWorkloadDrainsOnPrintsAndClosesWhenLevelGoes) {
    IexReplay replay({"X"});
    IexTpDecoder d;
    Bytes s = segment(DEEP, 1, 0,
                      {level(true, true, 1, "X", 300, 1000),   // join behind 300
                       trade(2, "X", 300, 1000, 1), level(true, true, 2, "X", 1, 1000),
                       level(true, true, 3, "X", 0, 1000)});
    d.onDatagram(s.data(), s.size(), replay);
    const auto& k = replay.checks();
    EXPECT_TRUE(k.queueJoins == 2 && k.queueDrained == 1);
    EXPECT_TRUE(k.queueLevelGone == 1 && k.queueInvariantFailures == 0);
}

// Deterministic stand-in for the libFuzzer target, so plain CTest under ASan and
// UBSan also walks damaged input. Mutates valid captures byte by byte.
TEST(randomDamageNeverReadsOutOfBounds) {
    std::vector<Bytes> payloads{
        segment(DEEP, 1, 0, {level(true, true, 1, "X", 1, 100), trade(2, "X", 1, 100, 1)}),
        segment(PLUS, 1, 0, {add(1, "X", 1, true, 5, 100), executed(2, "X", 1, 5, 100, 1)})};
    std::mt19937 rng(20251212);
    for (const Bytes& seed : {pcapng(payloads), classicPcap(payloads)}) {
        for (int round = 0; round < 4000; ++round) {
            Bytes b = seed;
            int edits = 1 + static_cast<int>(rng() % 8);
            for (int e = 0; e < edits; ++e) {
                b[rng() % b.size()] = static_cast<std::uint8_t>(rng());
            }
            b.resize(b.size() - rng() % 16);
            std::size_t at = 0;
            IexCaptureReader r = readerOf(b, at);
            IexTpDecoder d;
            IexReplay replay({"X"});
            IexCaptureReader::Datagram g;
            while (r.next(g)) {
                d.onDatagram(g.data, g.size, replay);
            }
            replay.finish();
        }
    }
    EXPECT_TRUE(true);
}

QTEST_MAIN()
