// Phase 5B persistence tests: Checkpoint (writer/reader) round trips, the
// safety properties from the Java reference (CheckpointTest) -- duplicate
// section rejection, config-mismatch/version/truncation errors, atomic
// replace, a failed section committing nothing -- and a CROSS-LANGUAGE
// test: the exact bytes of a checkpoint written by the Python port
// (quant-finance-py/src/quantfinlib/persist/checkpoint.py) are read here
// and must restore an EwmaCovariance identically to what Python computed.
//
// The Python fixture bytes below were generated once with:
//   PYTHONPATH=src python -c "
//   from quantfinlib.persist.checkpoint import Checkpoint
//   from quantfinlib.microstructure.ewma_covariance import EwmaCovariance
//   cov = EwmaCovariance(2, 0.94)
//   cov.on_returns([1e-4, 1e-4]); cov.on_returns([-1e-4, -2e-4])
//   with Checkpoint.writer(path) as w: w.section('basket', cov.write_state)
//   "
// which reported cov.covariance(0, 1) == 1.06e-08, cov.samples() == 2 --
// the values this test asserts after reading the embedded bytes back.
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "qtest.hpp"
#include "quantfinlib/execution/venue_scorecard.hpp"
#include "quantfinlib/microstructure/ewma_covariance.hpp"
#include "quantfinlib/microstructure/volume_curve.hpp"
#include "quantfinlib/persist/checkpoint.hpp"

using namespace quantfinlib;

namespace {

std::string scratchPath(const char* name) {
    return std::string("persist_checkpoint_scratch_") + name + ".qflc";
}

void writeFile(const std::string& path, const std::vector<std::uint8_t>& bytes) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

}  // namespace

// -------------------------------------------------------------- Round trips

TEST(volumeCurveAndEwmaCovarianceSurviveTheOvernight) {
    VolumeCurve volume(2, 0.5);
    volume.onVolume(0, 100);
    volume.onVolume(1, 300);
    volume.rollDay();
    // Leave intraday residue that must NOT survive the restore.
    volume.onVolume(0, 999);

    EwmaCovariance cov(2, 0.94);
    cov.onReturns({1e-4, 1e-4});
    cov.onReturns({-1e-4, -2e-4});

    std::string file = scratchPath("roundtrip");
    {
        Checkpoint::Writer w = Checkpoint::writer(file);
        w.section("volume", [&](BinWriter& out) { volume.writeState(out); });
        w.section("basket", [&](BinWriter& out) { cov.writeState(out); });
        w.close();
    }

    VolumeCurve volume2(2, 0.5);
    EwmaCovariance cov2(2, 0.94);
    Checkpoint::Reader r = Checkpoint::reader(file);
    EXPECT_TRUE(r.section("volume", [&](BinReader& in) { volume2.readState(in); }));
    EXPECT_TRUE(r.section("basket", [&](BinReader& in) { cov2.readState(in); }));

    EXPECT_NEAR(volume2.profileVolume(0), 100, 1e-9);
    EXPECT_NEAR(volume2.profileVolume(1), 300, 1e-9);
    EXPECT_TRUE(volume2.daysLearned() == 1);
    EXPECT_NEAR(volume2.realizedToday(), 0, 1e-9);  // intraday state is fresh

    EXPECT_NEAR(cov2.covariance(0, 1), cov.covariance(0, 1), 0.0);
    EXPECT_TRUE(cov2.samples() == cov.samples());
}

TEST(venueScorecardSurvivesTheOvernight) {
    VenueScorecard venues(2, 0.05, 0.95);
    for (int i = 0; i < 50; i++) {
        venues.onFill(0, 100000);
        venues.onMiss(1, 300000);
    }
    venues.onDarkProbe(1, 4000);

    std::string file = scratchPath("venues");
    {
        Checkpoint::Writer w = Checkpoint::writer(file);
        w.section("venues", [&](BinWriter& out) { venues.writeState(out); });
        w.close();
    }

    VenueScorecard venues2(2, 0.05, 0.95);
    Checkpoint::Reader r = Checkpoint::reader(file);
    EXPECT_TRUE(r.section("venues", [&](BinReader& in) { venues2.readState(in); }));

    EXPECT_NEAR(venues.fillRate(0), venues2.fillRate(0), 0.0);
    EXPECT_NEAR(venues.fillRate(1), venues2.fillRate(1), 0.0);
    EXPECT_NEAR(venues.measuredLatencyNanos(1), venues2.measuredLatencyNanos(1), 0.0);
    EXPECT_NEAR(venues.expectedHiddenShares(1), venues2.expectedHiddenShares(1), 0.0);
    EXPECT_TRUE(venues.sent(0) == venues2.sent(0));
}

TEST(venueScorecardStillReadsItsPreMarkoutV1Format) {
    // A v1.8.0-era checkpoint has no markout fields; restoring it must
    // recover everything it holds and leave the markout state cold.
    std::string file = scratchPath("v1card");
    {
        Checkpoint::Writer w = Checkpoint::writer(file);
        w.section("venues", [&](BinWriter& out) {
            out.writeByte(1);  // the old format version
            Checkpoint::writeLongs(out, {10, 0});     // sent
            Checkpoint::writeLongs(out, {9, 0});      // filled
            Checkpoint::writeLongs(out, {3, 0});      // probes
            Checkpoint::writeDoubles(out, {0.9, 0});  // fillRate
            Checkpoint::writeDoubles(out, {50000, 0});  // latency
            Checkpoint::writeDoubles(out, {4000, 0});   // hidden
        });
        w.close();
    }
    VenueScorecard card(2, 0.05, 0.95);
    Checkpoint::Reader r = Checkpoint::reader(file);
    EXPECT_TRUE(r.section("venues", [&](BinReader& in) { card.readState(in); }));
    EXPECT_NEAR(card.fillRate(0), 0.9, 0.0);
    EXPECT_TRUE(card.sent(0) == 10);
    EXPECT_NEAR(card.postFillMarkout(0), 0, 0.0);
    EXPECT_TRUE(card.maturedFillMarkouts() == 0);
}

// -------------------------------------------------------------- Safety properties

TEST(configMismatchThrowsInsteadOfMisaligning) {
    VolumeCurve four(4, 0.5);
    four.rollDay();
    std::string file = scratchPath("mismatch");
    {
        Checkpoint::Writer w = Checkpoint::writer(file);
        w.section("volume", [&](BinWriter& out) { four.writeState(out); });
        w.close();
    }
    VolumeCurve five(5, 0.5);
    EXPECT_THROWS(Checkpoint::reader(file).section("volume", [&](BinReader& in) { five.readState(in); }));
}

TEST(unknownStateVersionThrows) {
    std::string file = scratchPath("future");
    {
        Checkpoint::Writer w = Checkpoint::writer(file);
        w.section("volume", [&](BinWriter& out) { out.writeByte(99); });
        w.close();
    }
    VolumeCurve curve(2, 0.5);
    EXPECT_THROWS(Checkpoint::reader(file).section("volume", [&](BinReader& in) { curve.readState(in); }));
}

TEST(missingSectionIsAColdStartNotACrash) {
    std::string file = scratchPath("partial");
    {
        Checkpoint::Writer w = Checkpoint::writer(file);
        VolumeCurve v(2, 0.5);
        w.section("volume", [&](BinWriter& out) { v.writeState(out); });
        w.close();
    }
    Checkpoint::Reader r = Checkpoint::reader(file);
    VolumeCurve untouched(2, 0.5);
    EXPECT_TRUE(!r.section("vol", [&](BinReader& in) { untouched.readState(in); }));
    EXPECT_TRUE(untouched.daysLearned() == 0);
    EXPECT_TRUE(r.names().size() == 1);
    EXPECT_TRUE(r.names()[0] == "volume");
}

TEST(unconsumedBytesAreAFormatDriftNotASilentSkip) {
    VolumeCurve curve(2, 0.5);
    curve.rollDay();
    std::string file = scratchPath("drift");
    {
        Checkpoint::Writer w = Checkpoint::writer(file);
        w.section("volume", [&](BinWriter& out) {
            curve.writeState(out);
            out.writeInt(42);  // a future field this build doesn't read
        });
        w.close();
    }
    VolumeCurve reader(2, 0.5);
    EXPECT_THROWS(Checkpoint::reader(file).section("volume", [&](BinReader& in) { reader.readState(in); }));
}

TEST(duplicateSectionNamesAreRejected) {
    Checkpoint::Writer w = Checkpoint::writer(scratchPath("dup"));
    w.section("x", [](BinWriter& out) { out.writeByte(1); });
    EXPECT_THROWS(w.section("x", [](BinWriter& out) { out.writeByte(1); }));
}

TEST(commitIsAtomicReplace) {
    std::string file = scratchPath("eod");
    for (int day = 0; day < 2; day++) {
        VolumeCurve curve(2, 0.5);
        curve.onVolume(0, 100 * (day + 1));
        curve.rollDay();
        Checkpoint::Writer w = Checkpoint::writer(file);
        w.section("volume", [&](BinWriter& out) { curve.writeState(out); });
        w.close();
    }
    VolumeCurve restored(2, 0.5);
    Checkpoint::reader(file).section("volume", [&](BinReader& in) { restored.readState(in); });
    EXPECT_NEAR(restored.profileVolume(0), 200, 1e-9);  // second save replaced the first
}

TEST(aFailedSectionCommitsNothing) {
    std::string file = scratchPath("failed");
    {
        VolumeCurve v(2, 0.5);
        Checkpoint::Writer w = Checkpoint::writer(file);
        w.section("volume", [&](BinWriter& out) { v.writeState(out); });
        w.close();
    }
    std::ifstream before(file, std::ios::binary);
    std::vector<char> original((std::istreambuf_iterator<char>(before)), std::istreambuf_iterator<char>());
    before.close();

    EXPECT_THROWS(([&] {
        Checkpoint::Writer w = Checkpoint::writer(file);
        w.section("good", [](BinWriter& out) { out.writeByte(1); });
        w.section("bad", [](BinWriter&) { throw std::runtime_error("model blew up mid-save"); });
        w.close();
    })());

    std::ifstream after(file, std::ios::binary);
    std::vector<char> current((std::istreambuf_iterator<char>(after)), std::istreambuf_iterator<char>());
    EXPECT_TRUE(original == current);
}

TEST(garbageFilesAreRejectedLoudly) {
    std::string file = scratchPath("garbage");
    writeFile(file, {1, 2, 3, 4, 5, 6, 7, 8, 9});
    EXPECT_THROWS(Checkpoint::reader(file));
}

// -------------------------------------------------- Cross-language (Python)

TEST(readsAChekpointFileWrittenByThePythonPort) {
    // Byte-identical to what CheckpointWriter (Python) produced for a
    // single "basket" section holding EwmaCovariance(2, 0.94).write_state
    // after two on_returns() calls -- see the file header for the exact
    // generating script.
    const std::vector<std::uint8_t> pythonBytes = {
            0x51, 0x46, 0x4c, 0x43, 0x00, 0x00, 0x00, 0x01, 0x00, 0x06, 0x62, 0x61, 0x73, 0x6b,
            0x65, 0x74, 0x00, 0x00, 0x00, 0x25, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x02, 0x00, 0x00, 0x00, 0x03, 0x3e, 0x45, 0x79, 0x8e, 0xe2, 0x30, 0x8c, 0x3a, 0x3e,
            0x46, 0xc3, 0x69, 0x60, 0x66, 0xa9, 0x1f, 0x3e, 0x49, 0x57, 0x1e, 0x5c, 0xd2, 0xe2,
            0xe9};
    std::string file = scratchPath("from_python");
    writeFile(file, pythonBytes);

    EwmaCovariance restored(2, 0.94);
    Checkpoint::Reader r = Checkpoint::reader(file);
    EXPECT_TRUE(r.names().size() == 1);
    EXPECT_TRUE(r.names()[0] == "basket");
    EXPECT_TRUE(r.section("basket", [&](BinReader& in) { restored.readState(in); }));

    EXPECT_TRUE(restored.samples() == 2);
    EXPECT_NEAR(restored.covariance(0, 1), 1.06e-08, 1e-15);
}

QTEST_MAIN()
