set_xmakever("2.8.2")

-- Pinned to an MIT-licensed commit. Upstream has since moved to GPL-3.0, so check before updating it.
includes("lib/commonlibsse-ng")

set_project("Unvoice")
set_license("0BSD")

set_languages("c++23")
set_warnings("allextra")

add_rules("mode.debug", "mode.releasedbg")

-- MIT. Hooks the start of a function whose first instructions differ by game version.
add_requires("microsoft-detours 2023.6.8")

-- Detours needs <windows.h>, which conflicts with CommonLib's own declarations, so it lives apart.
target("entryhook")
    set_kind("static")
    add_files("glue/*.cpp")
    add_headerfiles("glue/*.h")
    add_packages("microsoft-detours")

target("Unvoice")
    add_deps("commonlibsse-ng", "entryhook")

    -- not CommonLib's plugin rule: src/plugin.cpp and src/version.rc declare the plugin, see there
    set_kind("shared")
    set_arch("x64")
    add_rules("win.sdk.resource")

    add_files("src/**.cpp", "src/version.rc")
    add_headerfiles("src/**.h")
    add_includedirs("src", "glue")
    set_pcxxheader("src/pch.h")

-- game-independent logic: xmake build tests, then xmake run tests
target("tests")
    set_kind("binary")
    set_default(false)
    add_files("tests/test.cpp")
    add_includedirs("src")
