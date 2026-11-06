// Feeds a capture through the IEX decoder and prints aggregate checks.
//
//   gzip -dc DAY.pcap.gz | iex_replay [--track AAPL,MSFT] [--bbo-log FILE] -
//   iex_replay --compare DEEP.log DEEPPLUS.log
//
// Exit status is 0 only when the capture was read to its end with no framing
// failure. Correctness only: nothing here is an edge or profit study.
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <tuple>
#include <vector>

#include "quantfinlib/marketdata/iex_capture_reader.hpp"
#include "quantfinlib/marketdata/iex_replay.hpp"
#include "quantfinlib/marketdata/iex_tp_decoder.hpp"

using namespace quantfinlib;

namespace {

using Key = std::tuple<std::int64_t, std::int64_t, std::int64_t, std::int64_t, std::int64_t>;

std::map<std::uint64_t, std::vector<Key>> load(const char* path) {
    std::map<std::uint64_t, std::vector<Key>> out;
    std::FILE* f = std::fopen(path, "rb");
    if (f == nullptr) {
        std::perror(path);
        return out;
    }
    IexReplay::BboRecord r;
    while (std::fread(&r, sizeof r, 1, f) == 1) {
        out[r.symbol].emplace_back(r.timestamp, r.bbo.bid, r.bbo.bidSize, r.bbo.ask, r.bbo.askSize);
    }
    std::fclose(f);
    return out;
}

int compare(const char* a, const char* b) {
    auto left = load(a);
    auto right = load(b);
    bool same = !left.empty() && left.size() == right.size();
    for (auto& [symbol, l] : left) {
        std::vector<Key>& r = right[symbol];
        std::sort(l.begin(), l.end());
        std::sort(r.begin(), r.end());
        std::vector<Key> both;
        std::set_intersection(l.begin(), l.end(), r.begin(), r.end(), std::back_inserter(both));
        char name[9] = {};
        std::memcpy(name, &symbol, 8);
        std::printf("%s changes_a=%zu changes_b=%zu identical=%zu\n", name, l.size(), r.size(),
                    both.size());
        same = same && both.size() == l.size() && both.size() == r.size();
    }
    std::printf("bbo_logs_identical=%d\n", same ? 1 : 0);
    return same ? 0 : 1;
}

#define SHOW(owner, field) \
    std::printf(#field "=%llu\n", static_cast<unsigned long long>((owner).field))

}  // namespace

int main(int argc, char** argv) {
    std::vector<std::string> tracked;
    std::FILE* log = nullptr;
    const char* input = nullptr;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--compare" && i + 2 < argc) {
            return compare(argv[i + 1], argv[i + 2]);
        }
        if (arg == "--track" && i + 1 < argc) {
            std::string list = argv[++i];
            for (std::size_t at = 0; at <= list.size();) {
                std::size_t comma = std::min(list.find(',', at), list.size());
                tracked.push_back(list.substr(at, comma - at));
                at = comma + 1;
            }
        } else if (arg == "--bbo-log" && i + 1 < argc) {
            log = std::fopen(argv[++i], "wb");
            if (log == nullptr) {
                std::perror("bbo log");
                return 2;
            }
        } else {
            input = argv[i];
        }
    }
    if (input == nullptr) {
        std::fprintf(stderr, "usage: iex_replay [--track A,B] [--bbo-log F] FILE|-\n"
                             "       iex_replay --compare LOG_A LOG_B\n");
        return 2;
    }
    std::FILE* in = std::strcmp(input, "-") == 0 ? stdin : std::fopen(input, "rb");
    if (in == nullptr) {
        std::perror(input);
        return 2;
    }
    IexCaptureReader reader([in](std::uint8_t* p, std::size_t n) {
        return std::fread(p, 1, n, in);
    });
    IexTpDecoder decoder;
    IexReplay replay(tracked, log);
    auto start = std::chrono::steady_clock::now();
    IexCaptureReader::Datagram d;
    while (reader.next(d)) {
        decoder.onDatagram(d.data, d.size, replay);
    }
    replay.finish();
    double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    if (log != nullptr) {
        std::fclose(log);
    }

    const auto& c = reader.stats();
    const auto& t = decoder.stats();
    const auto& k = replay.checks();
    const auto o = replay.orderBookStats();
    std::printf("container=%s\ncapture_failed=%d\n", reader.pcapng() ? "pcapng" : "pcap",
                reader.failed() ? 1 : 0);
    SHOW(c, packets); SHOW(c, datagrams); SHOW(c, notUdp); SHOW(c, fragments); SHOW(c, snapped);
    SHOW(c, unknownLinkType); SHOW(c, otherBlocks); SHOW(c, truncated); SHOW(c, badFormat);
    SHOW(t, segments); SHOW(t, heartbeats); SHOW(t, messages); SHOW(t, badSegments);
    SHOW(t, badMessages); SHOW(t, unknownProtocol); SHOW(t, unknownType); SHOW(t, grownMessages);
    SHOW(t, ignored); SHOW(t, seqGaps); SHOW(t, gapMessages); SHOW(t, offsetMismatches);
    SHOW(t, duplicateSegments); SHOW(t, duplicateMessages); SHOW(t, lateStartMessages);
    SHOW(t, sendTimeRegressions); SHOW(t, streams);
    for (int i = 0; i < 256; ++i) {
        if (t.byType[i] != 0) {
            std::printf("type_%c=%llu\n", i, static_cast<unsigned long long>(t.byType[i]));
        }
    }
    std::printf("symbols=%llu\n", static_cast<unsigned long long>(replay.symbols()));
    SHOW(k, events); SHOW(k, crossedEvents); SHOW(k, lockedEvents); SHOW(k, removeMissingLevel);
    SHOW(k, timestampRegressions); SHOW(k, incompleteAtEnd); SHOW(k, restingAtEnd);
    SHOW(k, prints); SHOW(k, printShares); SHOW(k, printIdSum); SHOW(k, printsInside);
    SHOW(k, printsOutside); SHOW(k, printsOutsideOther); SHOW(k, printsOneSided);
    SHOW(k, tradeBreaks); SHOW(k, execAtTouch); SHOW(k, execBehindTouch);
    SHOW(k, execPriceDiffers); SHOW(k, modifyKeptPriority); SHOW(k, clearBook);
    SHOW(k, systemEvents); SHOW(k, systemOutOfOrder); SHOW(k, tradingStatus);
    SHOW(o, unknownOrders); SHOW(o, duplicateOrders); SHOW(o, oversizeExecutions);
    SHOW(o, negativeLevels); SHOW(o, refused);
    SHOW(k, hftOrders); SHOW(k, hftNonCentSkipped); SHOW(k, hftRejects); SHOW(k, hftExecAtFront);
    SHOW(k, hftExecNotAtFront); SHOW(k, hftPriorityLossModifies); SHOW(k, hftBboCompared);
    SHOW(k, hftBboMismatches);
    SHOW(k, queueJoins); SHOW(k, queueUpdates); SHOW(k, queueDrained); SHOW(k, queueLevelGone);
    SHOW(k, queueInvariantFailures);
    std::printf("indicative_seconds=%.1f\nindicative_messages_per_second=%.0f\n", seconds,
                seconds > 0 ? static_cast<double>(t.messages) / seconds : 0.0);
    return reader.failed() ? 1 : 0;
}
