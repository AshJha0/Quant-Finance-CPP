// MoldUDP64 receiver, after MoldUdp64ReceiverTest.java, plus the ITCH wire
// rules from the 2026-11-06 review (H8, C11): 'C' messages, skip lengths for
// every 5.0 type, and length-checked decoding.
#include "quantfinlib/marketdata/moldudp64_receiver.hpp"

#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "qtest.hpp"
#include "quantfinlib/marketdata/itch_codec.hpp"
#include "quantfinlib/marketdata/l3_book_builder.hpp"

using quantfinlib::ItchCodec;
using quantfinlib::L3BookBuilder;
using quantfinlib::MoldUdp64Receiver;
using quantfinlib::Side;

namespace {

struct Sink : MoldUdp64Receiver::MessageHandler, MoldUdp64Receiver::Recovery {
    std::vector<std::int64_t> delivered;
    std::vector<std::pair<std::int64_t, int>> requests;
    std::vector<std::int64_t> snapshots;

    void onMessage(const unsigned char*, int, int, std::int64_t seq) override {
        delivered.push_back(seq);
    }
    void requestRetransmit(const std::string&, std::int64_t from, int count) override {
        requests.emplace_back(from, count);
    }
    void rebuildFromSnapshot(const std::string&, std::int64_t from) override {
        snapshots.push_back(from);
    }
};

std::vector<unsigned char> packet(std::int64_t first, int count, const char* session = "SESSION1") {
    std::vector<unsigned char> b(20 + static_cast<std::size_t>(count == 0xFFFF ? 0 : count) * 3);
    int p = MoldUdp64Receiver::encodeHeader(b.data(), 0, session, first, count);
    unsigned char payload = 'x';
    for (int i = 0; i < count && count != 0xFFFF; ++i) {
        p += MoldUdp64Receiver::encodeBlock(b.data(), p, &payload, 0, 1);
    }
    return b;
}

void feed(MoldUdp64Receiver& r, const std::vector<unsigned char>& p, std::int64_t now = 0) {
    r.onPacket(p.data(), 0, static_cast<int>(p.size()), now);
}

}  // namespace

TEST(deliversInSequenceAndNamesTheSession) {
    Sink s;
    MoldUdp64Receiver r(s, s, 100, 1000, 2);
    feed(r, packet(1, 2));
    feed(r, packet(3, 1));
    EXPECT_TRUE(s.delivered == (std::vector<std::int64_t>{1, 2, 3}));
    EXPECT_TRUE(r.session() == "SESSION1");
    EXPECT_TRUE(r.expectedSequence() == 4);
}

TEST(aGapRequestsTheMissingRangeAndDropsThePacket) {
    Sink s;
    MoldUdp64Receiver r(s, s, 100, 1000, 2);
    feed(r, packet(1, 1));
    feed(r, packet(5, 2));
    EXPECT_TRUE(r.isRecovering());
    EXPECT_TRUE(r.gapsDetected() == 1);
    EXPECT_TRUE(s.delivered.size() == 1);
    EXPECT_TRUE(s.requests.size() == 1);
    EXPECT_TRUE(s.requests[0].first == 2);
    EXPECT_TRUE(s.requests[0].second == 5);  // 2..6: the dropped packet too
    feed(r, packet(2, 5));
    EXPECT_TRUE(!r.isRecovering());
    EXPECT_TRUE(r.expectedSequence() == 7);
}

TEST(aHeartbeatAheadOfExpectedRevealsAGap) {
    Sink s;
    MoldUdp64Receiver r(s, s, 100, 1000, 2);
    feed(r, packet(4, 0));
    EXPECT_TRUE(r.isRecovering());
    EXPECT_TRUE(s.requests[0].second == 3);
}

TEST(duplicatesAreIgnoredAndStraddlersDeliverOnlyNewMessages) {
    Sink s;
    MoldUdp64Receiver r(s, s, 100, 1000, 2);
    feed(r, packet(1, 3));
    feed(r, packet(1, 3));
    EXPECT_TRUE(r.duplicatePackets() == 1);
    feed(r, packet(2, 4));
    EXPECT_TRUE(s.delivered == (std::vector<std::int64_t>{1, 2, 3, 4, 5}));
}

TEST(requestsAreCappedAtTheProtocolLimit) {
    Sink s;
    MoldUdp64Receiver r(s, s, 10, 1000, 2);
    feed(r, packet(1000, 1));
    EXPECT_TRUE(s.requests[0].second == 10);
}

TEST(timeoutReRequestsThenFallsBackToSnapshot) {
    Sink s;
    MoldUdp64Receiver r(s, s, 100, 1000, 2);
    feed(r, packet(3, 1), 0);
    r.onTimer(999);
    EXPECT_TRUE(s.requests.size() == 1);
    r.onTimer(1000);
    EXPECT_TRUE(s.requests.size() == 2);
    r.onTimer(2000);
    EXPECT_TRUE(s.snapshots == (std::vector<std::int64_t>{1}));
    EXPECT_TRUE(r.isAwaitingSnapshot());
    feed(r, packet(1, 1));  // nothing applies during the rebuild
    EXPECT_TRUE(s.delivered.empty());
    r.resumeAfterSnapshot(50);
    feed(r, packet(50, 1));
    EXPECT_TRUE(s.delivered == (std::vector<std::int64_t>{50}));
}

TEST(foreignSessionsAndMalformedPacketsAreCounted) {
    Sink s;
    MoldUdp64Receiver r(s, s, 100, 1000, 2);
    feed(r, packet(1, 1));
    feed(r, packet(2, 1, "OTHER"));
    EXPECT_TRUE(r.foreignSessionPackets() == 1);
    auto shortPacket = packet(2, 1);
    r.onPacket(shortPacket.data(), 0, 19, 0);
    EXPECT_TRUE(r.malformedPackets() == 1);
    feed(r, packet(0, 1));  // sequence numbers start at 1
    EXPECT_TRUE(r.malformedPackets() == 2);
    EXPECT_TRUE(r.expectedSequence() == 2);
}

TEST(aBlockLengthPastThePacketEndStopsWithoutOverread) {
    Sink s;
    MoldUdp64Receiver r(s, s, 100, 1000, 2);
    auto p = packet(1, 2);
    auto heap = std::make_unique<unsigned char[]>(p.size());
    std::memcpy(heap.get(), p.data(), p.size());
    heap[23] = 0xFF;  // second block claims 65 KB
    heap[24] = 0xFF;
    r.onPacket(heap.get(), 0, static_cast<int>(p.size()), 0);
    EXPECT_TRUE(s.delivered.size() == 1);
    EXPECT_TRUE(r.malformedPackets() == 1);
}

TEST(aSequenceNearTheTopOfTheRangeIsMalformedNotAnOverflow) {
    Sink s;
    MoldUdp64Receiver r(s, s, 100, 1000, 2);
    auto p = packet(1, 1);
    std::memset(p.data() + 10, 0xFF, 8);
    feed(r, p);
    EXPECT_TRUE(r.malformedPackets() == 1);
    EXPECT_TRUE(!r.isRecovering());
}

TEST(endOfSessionIsReported) {
    Sink s;
    MoldUdp64Receiver r(s, s, 100, 1000, 2);
    feed(r, packet(1, 0xFFFF));
    EXPECT_TRUE(r.isEndOfSession());
}

TEST(constructorAndEncoderRefuseBadArguments) {
    Sink s;
    bool threw = false;
    try {
        MoldUdp64Receiver r(s, s, 0xFFFF, 1000, 2);
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    EXPECT_TRUE(threw);
    unsigned char b[20];
    threw = false;
    try {
        MoldUdp64Receiver::encodeRequest(b, 0, "ELEVENCHARS", 1, 1);
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    EXPECT_TRUE(threw);
}

// ---------------------------------------------------------------- ITCH

TEST(everyItch50TypeHasItsSpecifiedSkipLength) {
    const std::pair<char, int> table[] = {
            {'S', 12}, {'R', 39}, {'H', 25}, {'Y', 20}, {'L', 26}, {'V', 35}, {'W', 12}, {'K', 28},
            {'J', 35}, {'h', 21}, {'A', 36}, {'F', 40}, {'E', 31}, {'C', 36}, {'X', 23}, {'D', 19},
            {'U', 35}, {'P', 44}, {'Q', 40}, {'B', 19}, {'I', 50}, {'N', 20}, {'O', 48}};
    for (const auto& [type, length] : table) {
        EXPECT_TRUE(ItchCodec::length(static_cast<std::uint8_t>(type)) == length);
    }
    EXPECT_TRUE(ItchCodec::length('z') == -1);
}

TEST(executedWithPriceReducesTheOrderAndPrintsOnlyIfPrintable) {
    unsigned char m[64];
    L3BookBuilder b(1, 0, 100000, 16);
    ItchCodec::encodeAdd(m, 0, 1, 1, 7, ItchCodec::BUY, 100, ItchCodec::packStock("X"), 5000);
    b.onMessage(m, 0);
    ItchCodec::encodeExecutedWithPrice(m, 0, 1, 2, 7, 30, 99, false, 5100);
    EXPECT_TRUE(b.onMessage(m, 0, 36) == 36);
    EXPECT_TRUE(b.openQuantity(7) == 70);
    ItchCodec::View v;
    EXPECT_TRUE(v.wrap(m, 0, 36));
    EXPECT_TRUE(v.deltaShares() == 30);
    EXPECT_TRUE(v.matchNumber() == 99);
    EXPECT_TRUE(v.printable() == 'N');
    EXPECT_TRUE(v.executionPriceTick() == 5100);
    ItchCodec::encodeExecutedWithPrice(m, 0, 1, 3, 7, 70, 100, true, 5200);
    b.onMessage(m, 0, 36);
    EXPECT_TRUE(b.openQuantity(7) == 0);
    EXPECT_TRUE(b.lastTradeTick() == 5200);
}

TEST(checkedWrapRefusesTruncatedAndUnknownMessages) {
    unsigned char m[64];
    ItchCodec::encodeDelete(m, 0, 1, 1, 7);
    ItchCodec::View v;
    EXPECT_TRUE(v.wrap(m, 0, 19));
    EXPECT_TRUE(!v.wrap(m, 0, 18));
    EXPECT_TRUE(!v.wrap(m, 0, 0));
    EXPECT_TRUE(!v.wrap(m, -1, 19));
    EXPECT_TRUE(!v.wrap(nullptr, 0, 19));
    m[0] = 'z';
    EXPECT_TRUE(!v.wrap(m, 0, 19));
}

TEST(aTruncatedDatagramNeverReachesTheBook) {
    auto p = std::make_unique<unsigned char[]>(3);
    p[0] = 'A';
    p[1] = 0;
    p[2] = 1;
    L3BookBuilder b(1, 0, 1000, 16);
    EXPECT_TRUE(b.onMessage(p.get(), 0, 3) == 0);
}

QTEST_MAIN()
