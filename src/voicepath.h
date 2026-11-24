#pragma once

// Game-independent logic, so it can be tested without Skyrim.

#include <cstddef>
#include <cstdint>
#include <istream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace VoicePath
{
    // The game builds the path in a MAX_PATH buffer.
    inline constexpr std::size_t kBufferSize = 0x104;

    constexpr char Fold(char c)
    {
        if (c == '/') {
            return '\\';
        }
        return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
    }

    constexpr bool EqualsFolded(std::string_view a, std::string_view b)
    {
        if (a.size() != b.size()) {
            return false;
        }
        for (std::size_t i = 0; i < a.size(); ++i) {
            if (Fold(a[i]) != Fold(b[i])) {
                return false;
            }
        }
        return true;
    }

    constexpr bool StartsWithFolded(std::string_view text, std::string_view prefix)
    {
        return text.size() >= prefix.size() && EqualsFolded(text.substr(0, prefix.size()), prefix);
    }

    // Index of a case-insensitive match in a list, if any.
    inline std::optional<std::size_t> IndexOf(std::string_view text, const std::vector<std::string>& list)
    {
        for (std::size_t i = 0; i < list.size(); ++i) {
            if (EqualsFolded(text, list[i])) {
                return i;
            }
        }
        return std::nullopt;
    }

    // Where the plugin folder is in Data\Sound\Voice\<plugin>\..., which names the plugin that created the line.
    struct Segment
    {
        std::size_t start;
        std::size_t end;
    };

    inline std::optional<Segment> PluginFolder(const char* path)
    {
        if (!path) {
            return std::nullopt;
        }

        std::size_t length = 0;
        while (length < kBufferSize && path[length]) {
            ++length;
        }
        if (length == kBufferSize) {
            return std::nullopt;  // unterminated
        }

        const std::string_view full{ path, length };
        std::size_t            start = StartsWithFolded(full, "data\\") ? 5 : 0;

        constexpr std::string_view root{ "sound\\voice\\" };
        if (!StartsWithFolded(full.substr(start), root)) {
            return std::nullopt;
        }
        start += root.size();

        const auto end = full.find_first_of("\\/", start);
        if (end == std::string_view::npos || end == start) {
            return std::nullopt;
        }
        return Segment{ start, end };
    }

    struct Match : Segment
    {
        std::size_t index;  // in the plugin list
    };

    // Is the path Data\Sound\Voice\<listed plugin>\...? Changes nothing, so the caller decides whether to mask.
    inline std::optional<Match> Find(const char* path, const std::vector<std::string>& plugins)
    {
        if (plugins.empty()) {
            return std::nullopt;
        }
        const auto folder = PluginFolder(path);
        if (!folder) {
            return std::nullopt;
        }
        const auto index = IndexOf(std::string_view{ path + folder->start, folder->end - folder->start }, plugins);
        if (!index) {
            return std::nullopt;
        }
        return Match{ *folder, *index };
    }

    // Overwrite the plugin segment with underscores. The result has the same length, so it always fits the
    // game's buffer, and it names a file that doesn't exist, which is how an unvoiced line looks to everything
    // after us.
    inline void Apply(char* path, const Segment& segment)
    {
        for (auto p = segment.start; p < segment.end; ++p) {
            path[p] = '_';
        }
    }

    constexpr bool IsPluginName(std::string_view text)
    {
        const auto extension = text.size() > 4 ? text.substr(text.size() - 4) : std::string_view{};
        return (EqualsFolded(extension, ".esp") || EqualsFolded(extension, ".esm") || EqualsFolded(extension, ".esl")) &&
               text.find_first_of("\\/") == std::string_view::npos;
    }

    // An NPC given as SkyPatcher does: Plugin.esp|FormID, in hex, with or without the load-order digits.
    struct FormRef
    {
        std::string   plugin;
        std::uint32_t id;
    };

    inline std::optional<FormRef> ParseFormRef(std::string_view text)
    {
        const auto bar = text.find('|');
        if (bar == std::string_view::npos) {
            return std::nullopt;
        }
        const auto trim = [](std::string_view part) {
            const auto first = part.find_first_not_of(" \t");
            return first == std::string_view::npos ? std::string_view{} :
                                                     part.substr(first, part.find_last_not_of(" \t") - first + 1);
        };
        const auto plugin = trim(text.substr(0, bar));
        auto       digits = trim(text.substr(bar + 1));
        if (StartsWithFolded(digits, "0x")) {
            digits.remove_prefix(2);
        }
        if (!IsPluginName(plugin) || digits.empty() || digits.size() > 8) {
            return std::nullopt;
        }
        std::uint32_t id = 0;
        for (const char c : digits) {
            const int value = c >= '0' && c <= '9' ? c - '0' :
                              c >= 'a' && c <= 'f' ? c - 'a' + 10 :
                              c >= 'A' && c <= 'F' ? c - 'A' + 10 :
                                                     -1;
            if (value < 0) {
                return std::nullopt;
            }
            id = id << 4 | static_cast<std::uint32_t>(value);
        }
        return FormRef{ std::string{ plugin }, id };
    }

    // The full form ID of a plugin's record, from the ID as written (load-order digits or not), the plugin's light
    // flag and where it loaded. Built from the load slot rather than the flag: on VR without Skyrim VR ESL Support,
    // a light-flagged plugin loads in a full slot. On SE and AE the two always agree. 0 if it isn't loaded.
    constexpr std::uint32_t FormIDIn(std::uint32_t id, bool lightFlag, std::uint8_t slot, std::uint16_t lightSlot)
    {
        if (slot == 0xFF) {
            return 0;
        }
        const bool light = slot == 0xFE;
        const auto local = id & ((light || lightFlag) ? 0xFFFu : 0xFFFFFFu);
        return light ? 0xFE000000u | (std::uint32_t{ lightSlot } & 0xFFFu) << 12 | local : std::uint32_t{ slot } << 24 | local;
    }

    struct List
    {
        std::vector<std::string> plugins;
        std::vector<std::string> npcs;
        std::vector<std::size_t> rejectedLines;  // line numbers only, so the log never has to repeat them
    };

    // One entry per line: plugin filenames under [Plugins], NPC names or Plugin|FormID under [NPCs]. Not key=value, since
    // filenames and names may contain '=', ',' etc.
    inline List ReadList(std::istream& in)
    {
        enum class Section
        {
            kOther,
            kPlugins,
            kNPCs
        };

        List        result;
        std::string line;
        auto        section = Section::kOther;

        for (std::size_t number = 1; std::getline(in, line); ++number) {
            std::string_view text{ line };
            if (number == 1 && text.starts_with("\xEF\xBB\xBF")) {
                text.remove_prefix(3);
            }
            const auto first = text.find_first_not_of(" \t\r");
            if (first == std::string_view::npos) {
                continue;
            }
            text = text.substr(first, text.find_last_not_of(" \t\r") - first + 1);

            if (text.front() == ';') {
                continue;
            }
            if (text.front() == '[' && text.back() == ']') {
                section = EqualsFolded(text, "[plugins]") ? Section::kPlugins :
                          EqualsFolded(text, "[npcs]")    ? Section::kNPCs :
                                                            Section::kOther;
                continue;
            }

            if (section == Section::kNPCs) {
                result.npcs.emplace_back(text);
                continue;
            }
            if (section != Section::kPlugins) {
                continue;
            }

            if (IsPluginName(text)) {
                result.plugins.emplace_back(text);
            } else {
                result.rejectedLines.push_back(number);
            }
        }
        return result;
    }
}
