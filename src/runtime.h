#pragma once

#include <cstdint>

namespace Runtime
{
    // Takes SKSE's packed version. Has to be checked before SKSE::Init: that loads the Address Library, and
    // the pinned CommonLib ends the game on a format it can't read (1.7.x). VR isn't supported either.
    constexpr bool IsSupported(std::uint32_t packed)
    {
        const auto major = packed >> 24;
        const auto minor = (packed >> 16) & 0xFF;
        return major == 1 && (minor == 5 || minor == 6);
    }
}
