#pragma once

// Game-independent logic, so it can be tested without Skyrim.

#include <cstddef>
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

    struct Match
    {
        std::size_t index;  // in the plugin list
        std::size_t start;  // of the plugin's folder in the path
        std::size_t end;
    };

    // Is the path Data\Sound\Voice\<listed plugin>\...? Changes nothing, so the caller decides whether to mask.
    inline std::optional<Match> Find(const char* path, const std::vector<std::string>& plugins)
    {
        if (!path || plugins.empty()) {
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
        if (end == std::string_view::npos) {
            return std::nullopt;
        }

        const auto folder = full.substr(start, end - start);
        for (std::size_t i = 0; i < plugins.size(); ++i) {
            if (EqualsFolded(folder, plugins[i])) {
                return Match{ i, start, end };
            }
        }
        return std::nullopt;
    }

    // Overwrite the plugin segment with underscores. The result has the same length, so it always fits the
    // game's buffer, and it names a file that doesn't exist, which is how an unvoiced line looks to everything
    // after us.
    inline void Apply(char* path, const Match& match)
    {
        for (auto p = match.start; p < match.end; ++p) {
            path[p] = '_';
        }
    }

    struct List
    {
        std::vector<std::string> plugins;
        std::vector<std::size_t> rejectedLines;  // line numbers only, so the log never has to repeat them
    };

    // One plugin filename per line under [Plugins]. Not key=value, since filenames may contain '=', ',' etc.
    inline List ReadList(std::istream& in)
    {
        List        result;
        std::string line;
        bool        inPlugins = false;

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
                inPlugins = EqualsFolded(text, "[plugins]");
                continue;
            }
            if (!inPlugins) {
                continue;
            }

            const auto extension = text.size() > 4 ? text.substr(text.size() - 4) : std::string_view{};
            const bool isPlugin =
                EqualsFolded(extension, ".esp") || EqualsFolded(extension, ".esm") || EqualsFolded(extension, ".esl");

            if (isPlugin && text.find_first_of("\\/") == std::string_view::npos) {
                result.plugins.emplace_back(text);
            } else {
                result.rejectedLines.push_back(number);
            }
        }
        return result;
    }
}
