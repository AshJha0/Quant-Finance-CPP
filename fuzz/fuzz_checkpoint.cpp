// Arbitrary bytes as a checkpoint file. The reader takes a path, so each
// input goes through a per-process scratch file.
#include <unistd.h>

#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <string>

#include "quantfinlib/persist/checkpoint.hpp"

using namespace quantfinlib;

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    static const std::string path =
            (std::filesystem::temp_directory_path() / ("qfl_fuzz_" + std::to_string(::getpid())))
                    .string();
    {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out.write(reinterpret_cast<const char*>(data), static_cast<std::streamsize>(size));
    }
    try {
        Checkpoint::Reader reader(path);
        for (const std::string& name : reader.names()) {
            try {
                reader.section(name, [](BinReader& in) {
                    while (in.remaining() >= 8) {
                        (void)in.readLong();
                    }
                    while (in.remaining() > 0) {
                        (void)in.readByte();
                    }
                });
            } catch (const std::exception&) {
            }
        }
    } catch (const std::runtime_error&) {
    }
    return 0;
}
