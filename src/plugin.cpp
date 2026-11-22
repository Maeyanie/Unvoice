#include "pch.h"

#include "version.h"

// Written out here rather than by CommonLib's xmake rule, which can't express SKSE's format 5 flag.

namespace
{
    // StructCompatibility is stored in SKSE's versionIndependenceEx, where bit 0 says no version-specific
    // structures are used and bit 1 that Address Library format 5 is understood. Older SKSEs ignore bit 1.
    constexpr auto kAddressLibraryV5 = 1u << 1;
    constexpr auto kCompatibility = static_cast<SKSE::StructCompatibility>(
        static_cast<std::uint32_t>(SKSE::StructCompatibility::Independent) | kAddressLibraryV5);
}

SKSEPluginInfo(
        .Version = { UNVOICE_VERSION_MAJOR, UNVOICE_VERSION_MINOR, UNVOICE_VERSION_PATCH, 0 },
        .Name = "Unvoice"sv,
        .Author = "Maeyanie"sv,
        .SupportEmail = ""sv,
        .StructCompatibility = kCompatibility,
        .RuntimeCompatibility = SKSE::VersionIndependence::AddressLibrary)
