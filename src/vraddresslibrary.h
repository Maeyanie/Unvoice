#pragma once

// Game-independent, so it can be tested without Skyrim.
//
// Skyrim VR's addresses come from VR Address Library for SKSEVR, a CSV that CommonLib reads itself. It ends the
// game if that file is missing or malformed, or lacks an ID it's asked for, so Unvoice reads it first and declines
// to load instead.

#include <cstddef>
#include <cstdint>
#include <istream>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace VRAddressLibrary
{
    inline constexpr auto kPath = R"(.\Data\SKSE\Plugins\version-1-4-15-0.csv)";

    namespace Detail
    {
        constexpr bool Digits(std::string_view text, bool hex)
        {
            if (text.empty() || text.size() > 20) {
                return false;
            }
            for (const char c : text) {
                const bool digit = c >= '0' && c <= '9';
                if (!digit && !(hex && ((c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F')))) {
                    return false;
                }
            }
            return true;
        }

        constexpr std::uint64_t Value(std::string_view text, bool hex)
        {
            std::uint64_t value = 0;
            for (const char c : text) {
                const auto digit = static_cast<std::uint64_t>(c <= '9' ? c - '0' : (c | 0x20) - 'a' + 10);
                value = value * (hex ? 16 : 10) + digit;
            }
            return value;
        }
    }

    // nullptr if CommonLib can read the file and will find every required ID in it, otherwise what's wrong.
    // The layout CommonLib expects: a header line, then "count,version", then exactly count lines of
    // "decimal id,hex offset".
    inline const char* Check(std::istream& in, std::span<const std::uint32_t> required)
    {
        if (!in) {
            return "Data\\SKSE\\Plugins\\version-1-4-15-0.csv isn't there";
        }

        std::vector<bool> found(required.size());
        std::uint64_t     expected = 0;
        std::uint64_t     entries = 0;
        bool              blank = false;
        std::string       line;
        for (std::size_t number = 1; std::getline(in, line); ++number) {
            std::string_view text{ line };
            if (!text.empty() && text.back() == '\r') {
                text.remove_suffix(1);
            }
            if (number == 1) {
                continue;  // column names
            }
            if (text.empty()) {
                blank = true;  // only allowed at the end
                continue;
            }
            if (blank) {
                return "it has a blank line partway through";
            }

            const auto comma = text.find(',');
            if (comma == std::string_view::npos) {
                return "it has a line that isn't an ID and an address";
            }
            const auto first = text.substr(0, comma);
            const auto second = text.substr(comma + 1);
            if (number == 2) {
                if (!Detail::Digits(first, false)) {
                    return "its first line doesn't give the number of addresses";
                }
                expected = Detail::Value(first, false);
                continue;
            }
            if (!Detail::Digits(first, false) || !Detail::Digits(second, true)) {
                return "it has a line that isn't an ID and an address";
            }
            ++entries;
            const auto id = Detail::Value(first, false);
            for (std::size_t i = 0; i < required.size(); ++i) {
                found[i] = found[i] || (id == required[i] && Detail::Value(second, true) != 0);
            }
        }

        if (expected == 0) {
            return "it's empty";
        }
        if (entries != expected) {
            return "it doesn't have as many addresses as it says, so it may be damaged: download it again";
        }
        for (const bool f : found) {
            if (!f) {
                return "it lacks an address Unvoice needs, so it may be out of date";
            }
        }
        return nullptr;
    }
}
