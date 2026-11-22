set_xmakever("2.8.2")

-- Pinned to an MIT-licensed commit. Upstream has since moved to GPL-3.0, so check before updating it.
includes("lib/commonlibsse-ng")

set_project("Unvoice")
set_license("0BSD")

set_languages("c++23")
set_warnings("allextra")

add_rules("mode.debug", "mode.releasedbg")

target("Unvoice")
    add_deps("commonlibsse-ng")

    -- not CommonLib's plugin rule: src/plugin.cpp and src/version.rc declare the plugin, see there
    set_kind("shared")
    set_arch("x64")
    add_rules("win.sdk.resource")

    add_files("src/**.cpp", "src/version.rc")
    add_headerfiles("src/**.h")
    add_includedirs("src")
    set_pcxxheader("src/pch.h")

-- game-independent logic: xmake build tests, then xmake run tests
target("tests")
    set_kind("binary")
    set_default(false)
    add_files("tests/test.cpp")
    add_includedirs("src")
