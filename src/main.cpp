#include "pch.h"

#include "callerscope.h"
#include "runtime.h"
#include "skyrimnet.h"
#include "voicepath.h"

extern "C" __declspec(dllimport) unsigned short __stdcall RtlCaptureStackBackTrace(
    unsigned long a_skip, unsigned long a_count, void** a_frames, unsigned long* a_hash);

// Privacy rule for this plugin's log: never write a voice path, a line of dialogue or a filename from a
// listed mod. Entries are referred to by their position in the INI.

namespace
{
    constexpr auto kConfigPath = R"(.\Data\SKSE\Plugins\Unvoice.ini)";

    // Filled during load and read-only afterwards, as dialogue can be built on more than one thread.
    std::vector<std::string>                g_plugins;
    std::vector<std::atomic<std::uint64_t>> g_muted;
    std::vector<std::atomic<std::uint64_t>> g_leftReal;

    enum class Mode
    {
        kPassThrough,   // mute nothing: not ready, or SkyrimNet is present and its extraction can't be recognised
        kMuteAll,       // no SkyrimNet
        kMutePlayback,  // mute, except where SkyrimNet is extracting
    };

    // g_extraction is written once, before g_mode is released as kMutePlayback.
    std::atomic<Mode> g_mode{ Mode::kPassThrough };
    CallerScope       g_extraction;

    void SetupLog()
    {
        auto logsFolder = SKSE::log::log_directory();
        if (!logsFolder) {
            SKSE::stl::report_and_fail("SKSE log_directory not provided, logs disabled.");
        }
        auto fileSink = std::make_shared<spdlog::sinks::basic_file_sink_mt>((*logsFolder / "Unvoice.log").string(), true);
        spdlog::set_default_logger(std::make_shared<spdlog::logger>("global", std::move(fileSink)));
        spdlog::set_level(spdlog::level::info);
        spdlog::flush_on(spdlog::level::info);
    }

    void LoadConfig()
    {
        std::ifstream file{ kConfigPath };
        if (!file) {
            logs::error("Unvoice.ini not found, nothing will be muted");
            return;
        }

        auto list = VoicePath::ReadList(file);
        for (const auto line : list.rejectedLines) {
            logs::warn("INI line {} ignored: expected a plugin filename ending in .esp, .esm or .esl", line);
        }
        g_plugins = std::move(list.plugins);
        g_muted = std::vector<std::atomic<std::uint64_t>>(g_plugins.size());
        g_leftReal = std::vector<std::atomic<std::uint64_t>>(g_plugins.size());
        logs::info("{} plugin(s) listed", g_plugins.size());
    }

    void Tally(std::vector<std::atomic<std::uint64_t>>& a_counts, std::size_t a_index, std::string_view a_what)
    {
        const auto count = ++a_counts[a_index];
        if (count == 1 || count % 100 == 0) {
            logs::info("Entry {}: {} response(s) {}", a_index + 1, count, a_what);
        }
    }

    void ChooseMode()
    {
        const auto skyrimNet = REX::W32::GetModuleHandleA("SkyrimNet.dll");
        if (!skyrimNet) {
            g_mode.store(Mode::kMuteAll, std::memory_order_release);
            logs::info("SkyrimNet isn't loaded, muting every path for listed plugins");
        } else if (g_extraction.Init(skyrimNet, SkyrimNet::kRealPathCallers)) {
            g_mode.store(Mode::kMutePlayback, std::memory_order_release);
            logs::info("Found SkyrimNet's dialogue extraction ({} functions), it will see real paths", g_extraction.Size());
        } else {
            logs::error("Can't recognise this SkyrimNet version's sample extraction. Muting nothing, so that its "
                        "voice-cloning block can't be affected. Unvoice needs an update.");
        }
    }

    // When in doubt this says no: a recording playing is a smaller harm than meddling with the cloning block.
    bool ShouldMute()
    {
        switch (g_mode.load(std::memory_order_acquire)) {
        case Mode::kMuteAll:
            return true;
        case Mode::kMutePlayback:
            {
                void*      frames[64];
                const auto count = RtlCaptureStackBackTrace(0, 64, frames, nullptr);
                if (count < 4) {
                    return false;  // couldn't see who's asking
                }
                for (unsigned short i = 0; i < count; ++i) {
                    if (g_extraction.Contains(frames[i])) {
                        return false;
                    }
                }
                return true;
            }
        default:
            return false;
        }
    }

    // The response setup function (34429/35249) calls a function at +0xDE that writes the voice path into a
    // stack buffer, then stores that buffer at +0xEC, where Fuz Ro D-oh checks whether the file exists.
    // Editing the buffer in between makes Fuz Ro D-oh treat the line as unvoiced and supply timed silence,
    // which is what SkyrimNet's silent-line TTS looks for.
    //
    // DialogueItem::Ctor looks like the obvious site, but dialogue-menu answers are built without it.
    //
    // Not every call is playback: SkyrimNet finds voice samples through here too, see ShouldMute.
    struct BuildVoicePathHook
    {
        static bool thunk(RE::TESTopicInfo::ResponseData* a_response, char* a_path, RE::BGSVoiceType* a_voiceType,
            RE::TESTopic* a_topic, RE::TESTopicInfo* a_info)
        {
            const bool built = func(a_response, a_path, a_voiceType, a_topic, a_info);
            if (!built) {
                return built;
            }

            if (const auto match = VoicePath::Find(a_path, g_plugins)) {
                if (!ShouldMute()) {
                    Tally(g_leftReal, match->index, "left real"sv);
                    return built;
                }
                VoicePath::Apply(a_path, *match);

                // A response can name a sound record to play in place of its file. ResponseData is loaded for
                // each use, so clearing this doesn't alter the form.
                if (a_response) {
                    a_response->sound = nullptr;
                }
                Tally(g_muted, match->index, "muted"sv);
            }
            return built;
        }

        static inline REL::Relocation<decltype(thunk)> func;

        static bool Install()
        {
            const REL::Relocation<std::uintptr_t> function{ RELOCATION_ID(34429, 35249) };
            const auto                            site = function.address() + 0xDE;

            if (*reinterpret_cast<const std::uint8_t*>(site) != 0xE8) {
                logs::error("No call at the expected hook site on this game version, nothing will be muted");
                return false;
            }

            SKSE::AllocTrampoline(14);
            func = SKSE::GetTrampoline().write_call<5>(site, thunk);
            logs::info("Hook installed at RVA {:X}", site - REL::Module::get().base());
            return true;
        }
    };

    void OnMessage(SKSE::MessagingInterface::Message* a_message)
    {
        switch (a_message->type) {
        case SKSE::MessagingInterface::kPostLoad:
            ChooseMode();
            if (!REX::W32::GetModuleHandleA("Fuz Ro D'oh.dll")) {
                logs::error("Fuz Ro D-oh isn't loaded: muted lines will flash past without timing or TTS");
            }
            break;
        case SKSE::MessagingInterface::kDataLoaded:
            if (auto* data = RE::TESDataHandler::GetSingleton()) {
                for (std::size_t i = 0; i < g_plugins.size(); ++i) {
                    if (data->LookupModByName(g_plugins[i])) {
                        logs::info("Entry {}: plugin found", i + 1);
                    } else {
                        logs::warn("Entry {}: no plugin by that name, check the spelling", i + 1);
                    }
                }
            }
            break;
        default:
            break;
        }
    }
}

SKSEPluginLoad(const SKSE::LoadInterface* a_skse)
{
    // Nothing that touches REL can come before this, the log included. SKSE reports the refusal itself.
    if (!Runtime::IsSupported(a_skse->RuntimeVersion().pack())) {
        return false;
    }

    SKSE::Init(a_skse, false);
    SetupLog();
    LoadConfig();

    if (!g_plugins.empty() && BuildVoicePathHook::Install()) {
        SKSE::GetMessagingInterface()->RegisterListener(OnMessage);
    }
    return true;
}
