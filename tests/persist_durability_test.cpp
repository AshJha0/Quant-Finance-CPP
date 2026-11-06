#include <cstring>
// Regression tests for the 2026-11-06 review, findings H6 and H7: the FIX
// store survives torn and corrupt files, and a failed Checkpoint save can
// never damage the previous good file.
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

#ifndef _WIN32
#include <signal.h>
#include <sys/resource.h>
#endif

#include "qtest.hpp"
#include "quantfinlib/fix/file_session_store.hpp"
#include "quantfinlib/persist/checkpoint.hpp"
#include "quantfinlib/persist/durable_file.hpp"

using quantfinlib::BinReader;
using quantfinlib::BinWriter;
using quantfinlib::Checkpoint;
using quantfinlib::FileSessionStore;
using Stored = quantfinlib::FixSessionStore::StoredMessage;
namespace fs = std::filesystem;

namespace {

struct TempDir {
    fs::path path;
    TempDir() {
        static int counter = 0;
        path = fs::temp_directory_path()
               / ("qfl_durable_" + std::to_string(std::rand()) + "_" + std::to_string(++counter));
        fs::create_directories(path);
    }
    ~TempDir() {
        std::error_code ec;
        fs::remove_all(path, ec);
    }
    std::string str() const { return path.string(); }
    std::string file(const char* name) const { return (path / name).string(); }
};

std::vector<char> slurp(const std::string& p) {
    std::ifstream in(p, std::ios::binary);
    return std::vector<char>(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

void spit(const std::string& p, const std::vector<char>& data) {
    std::ofstream out(p, std::ios::binary | std::ios::trunc);
    out.write(data.data(), static_cast<std::streamsize>(data.size()));
}

void flipByte(const std::string& p, std::size_t at) {
    auto data = slurp(p);
    data[at] = static_cast<char>(data[at] ^ 0x40);
    spit(p, data);
}

Stored msg(const char* body) {
    return Stored{"D", body, "20261106-10:00:00.000"};
}

template <typename Fn>
bool throwsRuntime(Fn fn) {
    try {
        fn();
    } catch (const std::runtime_error&) {
        return true;
    }
    return false;
}

void writeLongs(const std::string& path, const char* name, int count) {
    Checkpoint::Writer w(path);
    w.section(name, [count](BinWriter& o) {
        for (int i = 0; i < count; ++i) {
            o.writeLong(i);
        }
    });
    w.close();
}

}  // namespace

TEST(crc32MatchesTheStandardCheckValue) {
    // The published CRC-32/ISO-HDLC check value for "123456789".
    EXPECT_TRUE(quantfinlib::crc32("123456789", 9) == 0xCBF43926U);
}

TEST(aTornTailIsCutSoLaterAppendsSurvive) {
    TempDir d;
    {
        FileSessionStore s(d.str());
        s.storeMessage(1, msg("a"));
        s.storeMessage(2, msg("b"));
    }
    std::string p = d.file("messages.dat");
    fs::resize_file(p, fs::file_size(p) - 3);
    {
        FileSessionStore s(d.str());
        EXPECT_TRUE(s.retrieve(1).has_value());
        EXPECT_TRUE(!s.retrieve(2).has_value());
        s.storeMessage(3, msg("c"));
        s.storeMessage(4, msg("d"));
    }
    FileSessionStore s(d.str());
    EXPECT_TRUE(s.retrieve(3).has_value() && s.retrieve(3)->body == "c");
    EXPECT_TRUE(s.retrieve(4).has_value());
}

TEST(aBadCrcOnTheLastRecordIsATornTail) {
    TempDir d;
    {
        FileSessionStore s(d.str());
        s.storeMessage(1, msg("a"));
        s.storeMessage(2, msg("b"));
    }
    std::string p = d.file("messages.dat");
    flipByte(p, fs::file_size(p) - 1);
    FileSessionStore s(d.str());
    EXPECT_TRUE(s.retrieve(1).has_value());
    EXPECT_TRUE(!s.retrieve(2).has_value());
}

TEST(aBadCrcWithGoodDataAfterItIsCorruptionNotATail) {
    TempDir d;
    {
        FileSessionStore s(d.str());
        s.storeMessage(1, msg("a"));
        s.storeMessage(2, msg("b"));
    }
    flipByte(d.file("messages.dat"), 8 + 8 + 9);  // inside record 1's payload
    EXPECT_TRUE(throwsRuntime([&] { FileSessionStore s(d.str()); }));
}

TEST(aForeignMessageFileIsRefused) {
    TempDir d;
    spit(d.file("messages.dat"), std::vector<char>(64, 'x'));
    EXPECT_TRUE(throwsRuntime([&] { FileSessionStore s(d.str()); }));
}

TEST(aTornSequenceWriteFallsBackToThePreviousValue) {
    TempDir d;
    {
        FileSessionStore s(d.str());
        s.saveOutgoingSeq(10);
        s.saveOutgoingSeq(11);
    }
    // Slots alternate, so the latest generation is in one of them: spoil it.
    auto data = slurp(d.file("seqnums.dat"));
    std::int64_t first = 0;
    std::memcpy(&first, data.data(), sizeof(first));
    flipByte(d.file("seqnums.dat"), first == 11 ? 2 : 34);
    FileSessionStore s(d.str());
    EXPECT_TRUE(s.nextOutgoingSeq() == 10);
}

TEST(sequenceFileWithNoValidSlotFailsClosed) {
    TempDir d;
    { FileSessionStore s(d.str()); s.saveOutgoingSeq(10); }
    spit(d.file("seqnums.dat"), std::vector<char>(64, 'x'));
    EXPECT_TRUE(throwsRuntime([&] { FileSessionStore s(d.str()); }));
}

TEST(resetDropsMessagesAndCountersDurably) {
    TempDir d;
    {
        FileSessionStore s(d.str());
        s.storeMessage(1, msg("old"));
        s.saveOutgoingSeq(9);
        s.saveIncomingSeq(8);
        s.reset();
        EXPECT_TRUE(!s.retrieve(1).has_value());
        s.storeMessage(1, msg("new"));
    }
    FileSessionStore s(d.str());
    EXPECT_TRUE(s.nextOutgoingSeq() == 1);
    EXPECT_TRUE(s.expectedIncomingSeq() == 1);
    EXPECT_TRUE(s.retrieve(1)->body == "new");
}

TEST(writesAfterCloseFailLoudly) {
    TempDir d;
    FileSessionStore s(d.str());
    s.close();
    s.close();
    EXPECT_TRUE(throwsRuntime([&] { s.saveOutgoingSeq(2); }));
    EXPECT_TRUE(throwsRuntime([&] { s.storeMessage(1, msg("a")); }));
}

#ifndef _WIN32
TEST(aFailedCheckpointWriteReportsAndKeepsTheGoodFile) {
    TempDir d;
    std::string p = d.file("state.ckpt");
    writeLongs(p, "good", 300);
    auto before = slurp(p);

    ::signal(SIGXFSZ, SIG_IGN);
    rlimit old{};
    ::getrlimit(RLIMIT_FSIZE, &old);
    rlimit tiny{1000, old.rlim_max};
    ::setrlimit(RLIMIT_FSIZE, &tiny);
    bool threw = throwsRuntime([&] { writeLongs(p, "good", 300); });
    ::setrlimit(RLIMIT_FSIZE, &old);

    EXPECT_TRUE(threw);
    EXPECT_TRUE(slurp(p) == before);
    EXPECT_TRUE(!fs::exists(p + ".tmp"));
}
#endif

TEST(anExceptionBetweenSectionsDoesNotCommitAPartialSnapshot) {
    TempDir d;
    std::string p = d.file("state.ckpt");
    writeLongs(p, "old", 1);
    try {
        Checkpoint::Writer w(p);
        w.section("first_of_two", [](BinWriter& o) { o.writeInt(1); });
        throw std::runtime_error("unrelated");
    } catch (const std::runtime_error&) {
    }
    Checkpoint::Reader r(p);
    EXPECT_TRUE(r.names().size() == 1 && r.names()[0] == "old");
}

TEST(theCrcTrailerCatchesBitRotAndTruncation) {
    TempDir d;
    std::string p = d.file("state.ckpt");
    {
        Checkpoint::Writer w(p);
        w.section("a", [](BinWriter& o) { o.writeInt(1); });
        w.section("b", [](BinWriter& o) { o.writeInt(2); });
        w.close();
    }
    Checkpoint::Reader good(p);
    EXPECT_TRUE(good.integrityVerified());
    EXPECT_TRUE(good.names().size() == 2);  // the trailer stays hidden

    flipByte(p, 16);
    EXPECT_TRUE(throwsRuntime([&] { Checkpoint::Reader r(p); }));
    flipByte(p, 16);

    // Cut the file inside the trailer: no longer a whole section.
    fs::resize_file(p, fs::file_size(p) - 2);
    EXPECT_TRUE(throwsRuntime([&] { Checkpoint::Reader r(p); }));
}

TEST(filesWithoutATrailerStayReadableButUnverified) {
    TempDir d;
    std::string p = d.file("state.ckpt");
    {
        Checkpoint::Writer w(p);
        w.section("a", [](BinWriter& o) { o.writeInt(1); });
        w.close();
    }
    // Strip the trailer: 2 (name length) + 7 (name) + 4 (length) + 4 (crc).
    fs::resize_file(p, fs::file_size(p) - 17);
    Checkpoint::Reader r(p);
    EXPECT_TRUE(!r.integrityVerified());
    EXPECT_TRUE(r.section("a", [](BinReader& in) { EXPECT_TRUE(in.readInt() == 1); }));
}

TEST(theTrailerNameIsReservedAndVersionZeroIsRefused) {
    TempDir d;
    std::string p = d.file("state.ckpt");
    bool threw = false;
    try {
        Checkpoint::Writer w(p);
        w.section(Checkpoint::CRC_SECTION, [](BinWriter&) {});
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    EXPECT_TRUE(threw);
    spit(p, {'Q', 'F', 'L', 'C', 0, 0, 0, 0});
    EXPECT_TRUE(throwsRuntime([&] { Checkpoint::Reader r(p); }));
}

QTEST_MAIN()
