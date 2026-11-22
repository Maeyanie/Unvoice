#define NOMINMAX
#include <Windows.h>

#include "callerscope.h"
#include "runtime.h"
#include "skyrimnet.h"
#include "voicepath.h"

#include <array>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <sstream>

namespace
{
    int failures = 0;
    int checks = 0;

    void Check(bool ok, const char* what)
    {
        ++checks;
        if (!ok) {
            ++failures;
            std::printf("FAIL: %s\n", what);
        }
    }

    bool Mask(char* path, const std::vector<std::string>& plugins)
    {
        const auto match = VoicePath::Find(path, plugins);
        if (match) {
            VoicePath::Apply(path, *match);
        }
        return match.has_value();
    }

    void Path(const char* in, const char* expected, const char* what)
    {
        const std::vector<std::string> plugins{ "Dragonborn.esm", "My Mod, Part 2=Final.esp" };
        char                           buffer[VoicePath::kBufferSize]{};
        std::memcpy(buffer, in, std::strlen(in) + 1);
        const bool masked = Mask(buffer, plugins);
        Check(std::strcmp(buffer, expected) == 0 && masked == (std::strcmp(in, expected) != 0), what);
    }

    // A real stack walk through a jmp [rip] stub, which is what SKSE's trampoline puts between caller and hook.
    const CallerScope* liveScope = nullptr;
    const char* volatile liveSink = nullptr;

    __declspec(noinline) int Probe()
    {
        void*      frames[64];
        const auto count = RtlCaptureStackBackTrace(0, 64, frames, nullptr);
        for (unsigned short i = 0; i < count; ++i) {
            if (liveScope->Contains(frames[i])) {
                return 1;
            }
        }
        return 0;
    }

    __declspec(noinline) int ExtractingCaller(int (*stub)())
    {
        liveSink = "UnvoiceTest::ExtractingCaller";
        return stub() + (liveSink ? 10 : 0);  // not a tail call
    }

    __declspec(noinline) int PlaybackCaller(int (*stub)())
    {
        liveSink = "UnvoiceTest::PlaybackCaller";
        return stub() + (liveSink ? 10 : 0);
    }

    void TestLiveStack()
    {
        auto* code = static_cast<unsigned char*>(VirtualAlloc(nullptr, 64, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
        Check(code != nullptr, "allocate stub");
        if (!code) {
            return;
        }
        const unsigned char jump[]{ 0xFF, 0x25, 0, 0, 0, 0 };
        const auto          destination = &Probe;
        std::memcpy(code, jump, sizeof(jump));
        std::memcpy(code + sizeof(jump), &destination, sizeof(destination));
        DWORD old;
        VirtualProtect(code, 64, PAGE_EXECUTE_READ, &old);
        const auto stub = reinterpret_cast<int (*)()>(code);

        // Joined at runtime, so that this function doesn't itself reference the marker string.
        const std::string                     name = std::string{ "UnvoiceTe" } + "st::ExtractingCaller";
        const std::array<std::string_view, 1> markers{ name };
        CallerScope                           scope;
        Check(scope.Init(GetModuleHandleA(nullptr), markers), "marker found in own image");
        liveScope = &scope;

        Check(ExtractingCaller(stub) == 11, "live stack: extraction recognised through a trampoline");
        Check(PlaybackCaller(stub) == 10, "live stack: playback not recognised as extraction");

        liveScope = nullptr;
        VirtualFree(code, 0, MEM_RELEASE);
    }

    // Maps the DLL as a non-executable image, so none of its code runs.
    void TestAgainstSkyrimNet(const char* file)
    {
        const auto handle =
            CreateFileA(file, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0, nullptr);
        Check(handle != INVALID_HANDLE_VALUE, "open SkyrimNet.dll");
        if (handle == INVALID_HANDLE_VALUE) {
            return;
        }
        const auto  mapping = CreateFileMappingA(handle, nullptr, PAGE_READONLY | SEC_IMAGE_NO_EXECUTE, 0, 0, nullptr);
        const auto* base = mapping ? static_cast<const char*>(MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, 0)) : nullptr;
        Check(base != nullptr, "map SkyrimNet.dll");
        if (!base) {
            return;
        }

        const auto  began = GetTickCount64();
        CallerScope scope;
        Check(scope.Init(base, SkyrimNet::kRealPathCallers), "markers found");
        std::printf("scan: %zu functions in %llu ms\n", scope.Size(), GetTickCount64() - began);

        // Return addresses on each route into DialogueItem::Ctor in SkyrimNet 1.2.0.
        const auto real = [&](std::initializer_list<std::uint32_t> route) {
            for (const auto rva : route) {
                if (scope.Contains(base + rva)) {
                    return true;
                }
            }
            return false;
        };
        Check(real({ 0x10AD1CD, 0x42BD6F, 0x42F29D, 0x42E96A }), "voice samples, first route: real path");
        Check(real({ 0x10AD1CD, 0x42BD6F, 0x42F29D, 0x42EA6F }), "voice samples, second route: real path");
        Check(real({ 0x10AD1CD, 0x3B0036, 0x3B102A, 0x3B7FE7, 0x3B2B3F }), "catalogue, first route: real path");
        Check(real({ 0x10AD1CD, 0x3B0036, 0x3B119F, 0x3B8735, 0x3B297B }), "catalogue, second route: real path");
        Check(!real({ 0x10AD1CD, 0x4131BC, 0x417E06, 0x419CF5, 0x741832 }), "PrepareReplyTo: muted");
        Check(!real({ 0x10AD1CD, 0x41336C, 0x418EC1, 0x419E20, 0x9330AC }), "PrepareNPCDialogue: muted");
        Check(!real({ 0x10AD1CD, 0x41A0ED, 0x419C08 }), "reply helper, first route: muted");
        Check(!real({ 0x10AD1CD, 0x41A0ED, 0x419D01 }), "reply helper, second route: muted");
        Check(!real({ 0x10AD1CD, 0xDFED0B }), "TopicInfo event: muted");
        Check(!real({ 0xDD12D7 }), "game playback under SkyrimNet's ctor thunk: muted");
        Check(!scope.Contains(base - 16) && !scope.Contains(nullptr), "outside the module");

        const std::array<std::string_view, 2> stale{ SkyrimNet::kRealPathCallers[0], "SkyrimNet::Gone::Renamed" };
        CallerScope                           closed;
        Check(!closed.Init(base, stale) && closed.Size() == 0, "missing marker fails closed");

        UnmapViewOfFile(base);
        CloseHandle(mapping);
        CloseHandle(handle);
    }
}

int main(int argc, char** argv)
{
    Path(R"(Data\Sound\Voice\Dragonborn.esm\MaleNord\Line_00012345_1.wav)",
        R"(Data\Sound\Voice\______________\MaleNord\Line_00012345_1.wav)", "listed plugin");
    Path(R"(DATA\SOUND\VOICE\dragonBORN.ESM\v\l.wav)", R"(DATA\SOUND\VOICE\______________\v\l.wav)", "case");
    Path("data/sound/voice/Dragonborn.esm/v/l.wav", "data/sound/voice/______________/v/l.wav", "forward slashes");
    Path(R"(Sound\Voice\Dragonborn.esm\v\l.wav)", R"(Sound\Voice\______________\v\l.wav)", "no Data prefix");
    Path(R"(Data\Sound\Voice\My Mod, Part 2=Final.esp\v\l.wav)",
        R"(Data\Sound\Voice\________________________\v\l.wav)", "punctuation in name");

    for (const char* same : {
             R"(Data\Sound\Voice\Skyrim.esm\MaleNord\l.wav)",
             R"(Data\Sound\Voice\Dragonborn.esm.esp\v\l.wav)",
             R"(Data\Sound\Voice\Dragonborn.es\v\l.wav)",
             R"(Data\Sound\Voice\XDragonborn.esm\v\l.wav)",
             R"(Data\Sound\Voice\Other.esp\Dragonborn.esm\l.wav)",
             R"(Data\Sound\Voice\Fuz Ro Doh\Stock_3.xwm)",
             R"(Data\Sound\FX\Dragonborn.esm\l.wav)",
             R"(Data\Sound\Voice\Dragonborn.esm)",
             R"(Data\Sound\Voice\)",
             "Data\\",
             "" }) {
        Path(same, same, same);
    }

    char unterminated[VoicePath::kBufferSize];
    std::memset(unterminated, 'a', sizeof(unterminated));
    Check(!Mask(unterminated, { "a.esp" }), "unterminated buffer");
    Check(!Mask(nullptr, { "a.esp" }), "null path");

    char       twice[VoicePath::kBufferSize] = R"(Data\Sound\Voice\b.esp\v\l.wav)";
    const auto found = VoicePath::Find(twice, { "a.esp", "b.esp" });
    Check(found && found->index == 1, "index of match");
    Check(std::strcmp(twice, R"(Data\Sound\Voice\b.esp\v\l.wav)") == 0, "Find leaves the path alone");
    Mask(twice, { "a.esp", "b.esp" });
    Check(!Mask(twice, { "a.esp", "b.esp" }), "already masked");

    std::istringstream ini{ "\xEF\xBB\xBF; comment\r\n[Other]\r\nIgnored.esp\r\n\r\n[plugins]\r\n"
                            "  One Mod.esp \t\r\n;Off.esp\r\nTwo=2, x.ESL\r\nNoExtension\r\nBad\\Path.esp\r\nThree.esm" };
    const auto         list = VoicePath::ReadList(ini);
    Check(list.plugins == std::vector<std::string>{ "One Mod.esp", "Two=2, x.ESL", "Three.esm" }, "list entries");
    Check(list.rejectedLines == std::vector<std::size_t>{ 9, 10 }, "list rejects");

    std::istringstream bom{ "\xEF\xBB\xBF[Plugins]\nA.esp\n" };
    Check(VoicePath::ReadList(bom).plugins == std::vector<std::string>{ "A.esp" }, "BOM before header");

    Check(Runtime::IsSupported(0x01050610), "runtime 1.5.97");
    Check(Runtime::IsSupported(0x010613D0), "runtime 1.6.317");
    Check(Runtime::IsSupported(0x01064920), "runtime 1.6.1170");
    Check(!Runtime::IsSupported(0x010400F0), "runtime VR 1.4.15 refused");
    Check(!Runtime::IsSupported(0x01070630), "runtime 1.7.99 refused");
    Check(!Runtime::IsSupported(0x01070680), "runtime 1.7.104 refused");

    TestLiveStack();

    if (argc > 1) {
        TestAgainstSkyrimNet(argv[1]);
    }

    std::printf("%d checks, %d failed\n", checks, failures);
    return failures ? 1 : 0;
}
