#pragma once

#include <array>
#include <cstdint>

namespace Runtime
{
    // Packed the way SKSE passes it.
    inline constexpr std::uint32_t k1_7_104 = 0x01070680;

    // 1.7.104 is the only 1.7 supported, as it's the only one Fuz Ro D-oh has a build for.
    constexpr bool UsesFormat5(std::uint32_t packed)
    {
        return packed == k1_7_104;
    }

    // Has to be checked before SKSE::Init: that loads the Address Library, and the pinned CommonLib ends the
    // game on a version it can't handle. VR isn't supported either.
    constexpr bool IsSupported(std::uint32_t packed)
    {
        const auto major = packed >> 24;
        const auto minor = (packed >> 16) & 0xFF;
        return (major == 1 && (minor == 5 || minor == 6)) || UsesFormat5(packed);
    }

    inline constexpr std::uint32_t kResponseSetupSE = 34429;
    inline constexpr std::uint32_t kResponseSetupAE = 35249;
    inline constexpr std::uint32_t kDataHandlerAE = 400269;  // looked up by TESDataHandler::GetSingleton

    // Every AE ID Unvoice ends up resolving. Checked up front on format 5, so a bad table can't be fatal.
    inline constexpr std::array kRequiredAE{ kResponseSetupAE, kDataHandlerAE };
}
