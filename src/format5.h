#pragma once

#include <cstdint>
#include <span>

namespace Format5
{
    // Gives CommonLib the 1.7.104 Address Library. Call before SKSE::Init or any ID lookup.
    // Returns nullptr on success, otherwise what's wrong with the file, having changed nothing.
    [[nodiscard]] const char* Install(std::span<const std::uint32_t> a_required);
}
