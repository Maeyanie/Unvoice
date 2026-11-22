set_xmakever("2.8.2")

-- Pinned to an MIT-licensed commit. Upstream has since moved to GPL-3.0, so check before updating it.
includes("lib/commonlibsse-ng")

set_project("Unvoice")
set_version("1.0.0")
set_license("0BSD")

set_languages("c++23")
set_warnings("allextra")

add_rules("mode.debug", "mode.releasedbg")

target("Unvoice")
    add_deps("commonlibsse-ng")

    add_rules("commonlibsse-ng.plugin", {
        name = "Unvoice",
        author = "Maeyanie",
        description = "Treats the voice lines of listed plugins as unvoiced"
    })

    add_files("src/**.cpp")
    add_headerfiles("src/**.h")
    add_includedirs("src")
    set_pcxxheader("src/pch.h")

-- game-independent logic: xmake build tests, then xmake run tests
target("tests")
    set_kind("binary")
    set_default(false)
    add_files("tests/test.cpp")
    add_includedirs("src")
