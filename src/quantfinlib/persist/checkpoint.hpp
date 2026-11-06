// Port of com.quantfinlib.persist.Checkpoint: multi-day persistence of
// learned state as one binary file of named sections, written at end of
// day and restored at the next session start:
//
//   Checkpoint::Writer w = Checkpoint::writer(path);      // end of day
//   w.section("volume.AAPL", [&](BinWriter& o) { volumeCurve.writeState(o); });
//   w.close();                                            // commits atomically
//
//   Checkpoint::Reader r = Checkpoint::reader(path);       // next morning
//   r.section("volume.AAPL", [&](BinReader& i) { volumeCurve.readState(i); });
//
// Contract: each model persists its LEARNED (cross-day) state only;
// intraday state resets on read -- restore at session start, not
// mid-stream. The reading instance must be constructed with the same
// configuration (bucket count, venue count, ...): a mismatch throws
// std::runtime_error rather than silently misaligning arrays. Each
// section payload carries its own version byte so models can evolve
// their format independently of the file format.
//
// Wire format mirrors Java's DataOutputStream/DataInputStream byte
// layout (everything big-endian, matching Java's DataOutput):
//
//   header:  int32 MAGIC ("QFLC") + int32 FORMAT_VERSION
//   section: uint16 name-length + UTF-8 name bytes
//            + int32 payload-length + payload bytes
//
// One deviation from Java (matching the Python port at
// quant-finance-py/src/quantfinlib/persist/checkpoint.py, already
// verified byte-identical to Java in header/section framing against a
// Java golden file): section names are encoded as plain UTF-8, not
// Java's "modified UTF-8" (DataOutputStream.writeUTF) -- the two differ
// only for embedded NUL bytes or characters outside the Basic
// Multilingual Plane, neither of which this library's "model.symbol"
// section names ever contain.
//
// Durability: the writer buffers sections in memory and commits in
// close() -- a temp file beside the target, then a rename over the old
// checkpoint, so a crash mid-save leaves yesterday's file intact, never
// a torn one. If any section body throws, nothing is committed. The
// reader loads the whole file up front (these files are kilobytes) and
// rejects a section a model did not fully consume -- the loudest
// possible signal of a writer/reader format drift.
//
// Exception mapping: precondition violations (duplicate section name)
// map to std::invalid_argument; file-format / I/O errors (bad magic,
// truncated file, unread bytes) map to std::runtime_error, matching this
// port's data/tick_file_reader.cpp precedent for binary-file corruption.
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace quantfinlib {

/**
 * Growable big-endian byte sink -- the C++ counterpart of Java's
 * DataOutput, as passed to a model's writeState.
 */
class BinWriter {
public:
    void writeByte(std::int8_t v);
    void writeInt(std::int32_t v);
    void writeLong(std::int64_t v);
    void writeDouble(double v);

    const std::vector<std::uint8_t>& bytes() const { return buf_; }

private:
    std::vector<std::uint8_t> buf_;
};

/**
 * Fixed byte source with a cursor -- the C++ counterpart of Java's
 * DataInput, as passed to a model's readState.
 */
class BinReader {
public:
    explicit BinReader(std::vector<std::uint8_t> data) : buf_(std::move(data)) {}

    /** @throws std::runtime_error if fewer bytes remain than the field needs. */
    std::int8_t readByte();
    std::int32_t readInt();
    std::int64_t readLong();
    double readDouble();

    /** Bytes not yet consumed -- 0 after a section is read exactly. */
    std::size_t remaining() const { return buf_.size() - pos_; }

private:
    std::vector<std::uint8_t> buf_;
    std::size_t pos_ = 0;
};

using StateWriter = std::function<void(BinWriter&)>;
using StateReader = std::function<void(BinReader&)>;

/** Namespace-like class matching Java persist.Checkpoint; not instantiated. */
class Checkpoint {
public:
    static constexpr std::int32_t MAGIC = 0x51464C43;  // "QFLC"
    static constexpr std::int32_t FORMAT_VERSION = 1;
    /**
     * Reserved final section holding a big-endian CRC-32 of every preceding
     * byte. It keeps format 1: older files without it still load, and other
     * language readers ignore it as a section nobody asks for. The Reader
     * hides it from names().
     */
    static constexpr const char* CRC_SECTION = "__crc32";

    Checkpoint() = delete;

    /** Collects named sections and commits them atomically on close() (or destruction). */
    class Writer {
    public:
        explicit Writer(std::string target);
        ~Writer();

        Writer(const Writer&) = delete;
        Writer& operator=(const Writer&) = delete;
        Writer(Writer&&) = delete;
        Writer& operator=(Writer&&) = delete;

        /**
         * Serializes one model's state under `name`. A duplicate name (or a
         * throwing body) marks the writer failed: close() then commits
         * nothing, so a half-written model can never replace a good file.
         *
         * @throws std::invalid_argument on a duplicate section name
         */
        Writer& section(const std::string& name, const StateWriter& body);

        /**
         * Commits: temp file beside the target, written with checked I/O and
         * fsynced, then a rename over it and a directory fsync. No-op if a
         * section body threw, or if already closed. The destructor commits
         * too, except during stack unwinding.
         *
         * @throws std::runtime_error on any write failure; the existing
         *         checkpoint is left untouched
         */
        void close();

    private:
        std::string target_;
        std::vector<std::pair<std::string, std::vector<std::uint8_t>>> sections_;
        bool failed_ = false;
        bool closed_ = false;
        int uncaughtAtStart_ = 0;
    };

    /** Random access to a loaded checkpoint's sections by name. */
    class Reader {
    public:
        /** @throws std::runtime_error if the file is missing or not a quantfinlib checkpoint. */
        explicit Reader(const std::string& path);

        /**
         * Restores one model from the named section. Returns false when the
         * section is absent (the caller decides whether that is a cold
         * start or an error).
         *
         * @throws std::runtime_error if the model does not consume the
         *         payload exactly (writer/reader format drift)
         */
        bool section(const std::string& name, const StateReader& body);

        /** The section names present, in file order. */
        const std::vector<std::string>& names() const { return order_; }

        /**
         * True when the file carried the CRC trailer and it matched. False
         * for files from older writers, where truncation at a section
         * boundary cannot be detected.
         */
        bool integrityVerified() const { return verified_; }

    private:
        std::unordered_map<std::string, std::vector<std::uint8_t>> sections_;
        std::vector<std::string> order_;
        bool verified_ = false;
    };

    /** Opens a writer; nothing touches `path` until close(). */
    static Writer writer(const std::string& path) { return Writer(path); }

    /** Loads a checkpoint fully into memory and validates the header. */
    static Reader reader(const std::string& path) { return Reader(path); }

    /**
     * Reads and checks a model's leading state-version byte -- the shared
     * first line of every readState.
     *
     * @throws std::runtime_error if the version does not match `expected`
     */
    static void requireVersion(BinReader& in, int expected, const std::string& model);

    /** Length-prefixed double array. */
    static void writeDoubles(BinWriter& out, const std::vector<double>& a);

    /**
     * Reads a length-prefixed double array INTO `a` -- a length mismatch
     * means the checkpoint was written by a differently-configured
     * instance and throws before touching it.
     */
    static void readDoublesInto(BinReader& in, std::vector<double>& a);

    /** Length-prefixed long array. */
    static void writeLongs(BinWriter& out, const std::vector<std::int64_t>& a);

    /** Long-array counterpart of readDoublesInto. */
    static void readLongsInto(BinReader& in, std::vector<std::int64_t>& a);
};

}  // namespace quantfinlib
