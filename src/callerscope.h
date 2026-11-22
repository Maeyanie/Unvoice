#pragma once

// Recognises return addresses inside particular functions of a loaded x64 DLL, found by the __FUNCTION__
// strings they reference, so no addresses are tied to one build of that DLL. Game-independent, for testing.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

class CallerScope
{
public:
    // base is a module mapped as an image. Returns false, matching nothing, unless every marker was found in
    // use by at least one function.
    bool Init(const void* base, std::span<const std::string_view> markers)
    {
        _base = nullptr;
        _size = 0;
        _functions = {};
        _marked.clear();
        if (!base || markers.empty()) {
            return false;
        }

        const auto* image = static_cast<const std::uint8_t*>(base);
        if (Read<std::uint16_t>(image) != 0x5A4D) {
            return false;
        }
        const auto* nt = image + Read<std::uint32_t>(image + 0x3C);
        if (Read<std::uint32_t>(nt) != 0x4550) {
            return false;
        }
        const auto* optional = nt + 24;
        if (Read<std::uint16_t>(optional) != 0x20B) {
            return false;
        }
        _base = image;
        _size = Read<std::uint32_t>(optional + 56);

        Range        text{}, rdata{};
        const auto   count = Read<std::uint16_t>(nt + 6);
        const auto*  section = optional + Read<std::uint16_t>(nt + 20);
        for (std::uint16_t i = 0; i < count; ++i, section += 40) {
            const Range range{ Read<std::uint32_t>(section + 12), Read<std::uint32_t>(section + 8) };
            if (!Fits(range)) {
                continue;
            }
            if (std::memcmp(section, ".text\0\0\0", 8) == 0 && !text.size) {
                text = range;
            } else if (std::memcmp(section, ".rdata\0\0", 8) == 0 && !rdata.size) {
                rdata = range;
            }
        }

        // Exception directory: one entry per function fragment, sorted by address.
        const Range exceptions{ Read<std::uint32_t>(optional + 136), Read<std::uint32_t>(optional + 140) };
        if (!text.size || !rdata.size || !exceptions.size || !Fits(exceptions)) {
            return false;
        }
        _functions = { reinterpret_cast<const Function*>(_base + exceptions.rva), exceptions.size / sizeof(Function) };

        // Where each marker string starts.
        std::vector<std::pair<std::uint32_t, std::size_t>> strings;
        const auto*                                        first = reinterpret_cast<const char*>(_base + rdata.rva);
        const auto*                                        last = first + rdata.size;
        for (std::size_t m = 0; m < markers.size(); ++m) {
            const std::boyer_moore_horspool_searcher search{ markers[m].begin(), markers[m].end() };
            for (auto* at = first; (at = std::search(at, last, search)) != last; ++at) {
                if (at == first || at[-1] == '\0') {
                    strings.emplace_back(static_cast<std::uint32_t>(rdata.rva + (at - first)), m);
                }
            }
        }
        std::ranges::sort(strings);

        // Every `lea reg, [rip+disp32]` that loads one of those strings.
        std::vector<bool> found(markers.size());
        const auto*       code = _base + text.rva;
        for (std::uint32_t i = 0; i + 7 <= text.size; ++i) {
            if (code[i + 1] != 0x8D || (code[i] != 0x48 && code[i] != 0x4C) || (code[i + 2] & 0xC7) != 0x05) {
                continue;
            }
            const auto from = text.rva + i;
            const auto target = static_cast<std::uint32_t>(from + 7 + Read<std::int32_t>(code + i + 3));
            const auto hit = std::ranges::lower_bound(strings, target, {}, &std::pair<std::uint32_t, std::size_t>::first);
            if (hit == strings.end() || hit->first != target) {
                continue;
            }
            if (const auto owner = Owner(from)) {
                _marked.push_back(owner);
                found[hit->second] = true;
            }
        }

        std::ranges::sort(_marked);
        const auto duplicates = std::ranges::unique(_marked);
        _marked.erase(duplicates.begin(), duplicates.end());

        if (std::ranges::find(found, false) != found.end()) {
            _marked.clear();
            return false;
        }
        return true;
    }

    bool Contains(const void* returnAddress) const
    {
        const auto address = reinterpret_cast<std::uintptr_t>(returnAddress);
        const auto base = reinterpret_cast<std::uintptr_t>(_base);
        if (!_base || address <= base || address >= base + _size) {
            return false;
        }
        // -1: a return address can sit just past the end of the function that made the call.
        const auto owner = Owner(static_cast<std::uint32_t>(address - base - 1));
        return owner && std::ranges::binary_search(_marked, owner);
    }

    std::size_t Size() const { return _marked.size(); }

private:
    struct Range
    {
        std::uint32_t rva;
        std::uint32_t size;
    };

    struct Function
    {
        std::uint32_t begin;
        std::uint32_t end;
        std::uint32_t unwind;
    };

    template <class T>
    static T Read(const void* at)
    {
        T value;
        std::memcpy(&value, at, sizeof(T));
        return value;
    }

    bool Fits(Range range) const { return std::uint64_t{ range.rva } + range.size <= _size; }

    // Start of the function containing rva, following chained unwind info back from split-off fragments.
    std::uint32_t Owner(std::uint32_t rva) const
    {
        auto after = std::ranges::upper_bound(_functions, rva, {}, &Function::begin);
        if (after == _functions.begin()) {
            return 0;
        }
        auto function = *(after - 1);
        if (rva >= function.end) {
            return 0;
        }

        constexpr std::uint8_t kChained = 0x4;
        for (int depth = 0; depth < 16 && Fits({ function.unwind, 4 }); ++depth) {
            const auto* info = _base + function.unwind;
            if (!((info[0] >> 3) & kChained)) {
                break;
            }
            const std::uint32_t next = function.unwind + 4 + 2 * ((info[2] + 1u) & ~1u);
            if (!Fits({ next, sizeof(Function) })) {
                break;
            }
            function = Read<Function>(_base + next);
        }
        return function.begin;
    }

    const std::uint8_t*        _base{ nullptr };
    std::uint32_t              _size{ 0 };
    std::span<const Function>  _functions;
    std::vector<std::uint32_t> _marked;
};
