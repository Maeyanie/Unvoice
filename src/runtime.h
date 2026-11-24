#pragma once

#include <array>
#include <cstdint>

namespace Runtime
{
    // Packed the way SKSE passes it.
    inline constexpr std::uint32_t k1_4_15 = 0x010400F0;  // Skyrim VR, its only version
    inline constexpr std::uint32_t k1_7_104 = 0x01070680;

    // Told by version alone: no SE release was 1.4.15, and SE 1.4.2 is refused below.
    constexpr bool IsVR(std::uint32_t packed)
    {
        return packed == k1_4_15;
    }

    // 1.7.104 is the only 1.7 supported, as it's the only one Fuz Ro D-oh has a build for.
    constexpr bool UsesFormat5(std::uint32_t packed)
    {
        return packed == k1_7_104;
    }

    // Has to be checked before SKSE::Init: that loads the Address Library, and the pinned CommonLib ends the
    // game on a version it can't handle.
    constexpr bool IsSupported(std::uint32_t packed)
    {
        const auto major = packed >> 24;
        const auto minor = (packed >> 16) & 0xFF;
        return (major == 1 && (minor == 5 || minor == 6)) || UsesFormat5(packed) || IsVR(packed);
    }

    // VR uses the SE IDs, mapped to its own addresses by VR Address Library.
    inline constexpr std::uint32_t kResponseSetupSE = 34429;
    inline constexpr std::uint32_t kResponseSetupAE = 35249;
    inline constexpr std::uint32_t kDataHandlerSE = 514141;  // looked up by TESDataHandler::GetSingleton
    inline constexpr std::uint32_t kDataHandlerAE = 400269;

    // Every ID Unvoice ends up resolving. Checked up front where a missing one would be fatal: in the 1.7.104
    // table and in VR Address Library.
    inline constexpr std::array kRequiredSE{ kResponseSetupSE, kDataHandlerSE };
    inline constexpr std::array kRequiredAE{ kResponseSetupAE, kDataHandlerAE };
}
