#pragma once

// Reads Address Library format 5, which Skyrim 1.7.x uses. Game-independent, for testing.
//
// Written from the layout of the files themselves, not from any other reader:
//   00  i32      5
//   04  i32[4]   game version
//   14  char[64] executable name
//   54  u32      pointer size (8)
//   58  u32      data format (0)
//   5C  u32      count
//   60  u32[]    offset for each ID, indexed by ID. 0 means the ID isn't in this version.

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <istream>
#include <span>
#include <vector>

namespace AddressLibrary5
{
    inline constexpr std::size_t   kHeaderSize = 0x60;
    inline constexpr std::uint32_t kMaxIDs = 0x400000;  // about seven times what 1.7.104 has

    struct Table
    {
        std::vector<std::uint32_t> offsets;
        const char*                error{ nullptr };
    };

    inline Table Read(std::istream& in, const std::array<std::uint32_t, 4>& version, std::span<const std::uint32_t> required)
    {
        char header[kHeaderSize];
        if (!in.read(header, sizeof(header))) {
            return { {}, "it's missing or too short" };
        }

        const auto field = [&](std::size_t at) {
            std::uint32_t value;
            std::memcpy(&value, header + at, sizeof(value));
            return value;
        };
        if (field(0x00) != 5 || field(0x54) != 8 || field(0x58) != 0) {
            return { {}, "it isn't format 5" };
        }
        for (std::size_t i = 0; i < version.size(); ++i) {
            if (field(0x04 + 4 * i) != version[i]) {
                return { {}, "it's for another game version" };
            }
        }
        const auto count = field(0x5C);
        if (count == 0 || count > kMaxIDs) {
            return { {}, "its size makes no sense" };
        }

        Table      table{ std::vector<std::uint32_t>(count) };
        const auto bytes = static_cast<std::streamsize>(count * sizeof(std::uint32_t));
        if (!in.read(reinterpret_cast<char*>(table.offsets.data()), bytes) || in.peek() != std::istream::traits_type::eof()) {
            return { {}, "its length doesn't match its header" };
        }
        for (const auto id : required) {
            if (id >= count || !table.offsets[id]) {
                return { {}, "it lacks an address Unvoice needs" };
            }
        }
        return table;
    }
}
