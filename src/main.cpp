#include "pch.h"

#include "callerscope.h"
#include "entryhook.h"
#include "format5.h"
#include "runtime.h"
#include "skyrimnet.h"
#include "version.h"
#include "voicepath.h"
#include "vraddresslibrary.h"

extern "C" __declspec(dllimport) unsigned short __stdcall RtlCaptureStackBackTrace(
    unsigned long a_skip, unsigned long a_count, void** a_frames, unsigned long* a_hash);

// Privacy rule for this plugin's log: never write a voice path, a line of dialogue or a filename from a
// listed mod, or an NPC's name. Entries are referred to by their position in their section.

namespace
{
    constexpr auto kConfigPath = R"(.\Data\SKSE\Plugins\Unvoice.ini)";

    // Filled during load and read-only afterwards, as dialogue can be built on more than one thread.
    std::vector<std::string>                g_plugins;
    std::vector<std::atomic<std::uint64_t>> g_muted;
    std::vector<std::atomic<std::uint64_t>> g_leftReal;
    std::vector<std::string>                g_npcs;
    std::vector<std::atomic<std::uint64_t>> g_npcMuted;
    std::vector<std::atomic<std::uint64_t>> g_npcLeftReal;
    std::vector<RE::FormID>                 g_npcIDs;  // for Plugin|FormID entries, 0 for names
    std::atomic_bool                        g_npcIDsReady{ false };  // set once g_npcIDs is filled, at DataLoaded

    // Who the response being built is for, set by the setup function's entry hook. Per thread, as
    // dialogue can be built on more than one.
    thread_local const RE::TESObjectREFR* t_speaker = nullptr;

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
        g_npcs = std::move(list.npcs);
        g_npcMuted = std::vector<std::atomic<std::uint64_t>>(g_npcs.size());
        g_npcLeftReal = std::vector<std::atomic<std::uint64_t>>(g_npcs.size());
        g_npcIDs = std::vector<RE::FormID>(g_npcs.size());
        logs::info("{} plugin(s) and {} NPC(s) listed", g_plugins.size(), g_npcs.size());
    }

    void Tally(std::vector<std::atomic<std::uint64_t>>& a_counts, std::string_view a_kind, std::size_t a_index,
        std::string_view a_what)
    {
        const auto count = ++a_counts[a_index];
        if (count == 1 || count % 100 == 0) {
            logs::info("{} {}: {} response(s) {}", a_kind, a_index + 1, count, a_what);
        }
    }

    // Which [NPCs] entry the speaker's NPC record matches, by name or by form ID.
    std::optional<std::size_t> MatchSpeaker(const RE::TESObjectREFR* a_speaker)
    {
        const auto* base = a_speaker ? a_speaker->GetObjectReference() : nullptr;
        if (!base || base->GetFormType() != RE::FormType::NPC) {
            return std::nullopt;
        }
        if (g_npcIDsReady.load(std::memory_order_acquire)) {
            const auto id = base->GetFormID();
            for (std::size_t i = 0; i < g_npcIDs.size(); ++i) {
                if (g_npcIDs[i] && g_npcIDs[i] == id) {
                    return i;
                }
            }
        }
        const char* name = static_cast<const RE::TESNPC*>(base)->fullName.c_str();
        return name && *name ? VoicePath::IndexOf(name, g_npcs) : std::nullopt;
    }

    void ChooseMode()
    {
        const auto skyrimNet = REX::W32::GetModuleHandleA("SkyrimNet.dll");
        if (!skyrimNet) {
            g_mode.store(Mode::kMuteAll, std::memory_order_release);
            logs::info("SkyrimNet isn't loaded, so every listed line is muted");
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
    static_assert(offsetof(RE::TESTopicInfo::ResponseData, sound) == 0x18);  // the same on VR, 1.5, 1.6 and 1.7

    struct BuildVoicePathHook
    {
        static bool thunk(RE::TESTopicInfo::ResponseData* a_response, char* a_path, RE::BGSVoiceType* a_voiceType,
            RE::TESTopic* a_topic, RE::TESTopicInfo* a_info)
        {
            const bool built = func(a_response, a_path, a_voiceType, a_topic, a_info);
            if (!built) {
                return built;
            }

            const auto folder = VoicePath::PluginFolder(a_path);
            if (!folder) {
                return built;
            }
            const auto plugin = VoicePath::Find(a_path, g_plugins);
            const auto npc = (plugin || g_npcs.empty()) ? std::nullopt : MatchSpeaker(t_speaker);
            if (!plugin && !npc) {
                return built;
            }
            const auto tally = [&](auto& a_plugins, auto& a_npcs, std::string_view a_what) {
                plugin ? Tally(a_plugins, "Entry"sv, plugin->index, a_what) : Tally(a_npcs, "NPC entry"sv, *npc, a_what);
            };

            if (!ShouldMute()) {
                tally(g_leftReal, g_npcLeftReal, "left real"sv);
                return built;
            }
            VoicePath::Apply(a_path, *folder);

            // A response can name a sound record to play in place of its file. ResponseData is loaded for
            // each use, so clearing this doesn't alter the form.
            if (a_response) {
                a_response->sound = nullptr;
            }
            tally(g_muted, g_npcMuted, "muted"sv);
            return built;
        }

        static inline REL::Relocation<decltype(thunk)> func;

        static bool Install()
        {
            const REL::Relocation<std::uintptr_t> function{ RELOCATION_ID(Runtime::kResponseSetupSE, Runtime::kResponseSetupAE) };
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

    // The setup function's fourth argument is the speaker (Dynamic Dialogue Replacer, which hooks this entry
    // too, shows the signature). Noted on the way in, so the hook at +0xDE can check it against [NPCs].
    struct ResponseSetupEntryHook
    {
        static std::int64_t thunk(std::int64_t a_1, RE::TESTopic* a_topic, RE::TESTopicInfo* a_info,
            RE::TESObjectREFR* a_speaker, void* a_response)
        {
            const auto* outer = t_speaker;
            t_speaker = a_speaker;
            const auto result = original(a_1, a_topic, a_info, a_speaker, a_response);
            t_speaker = outer;
            return result;
        }

        static inline decltype(&thunk) original = nullptr;

        static void Install()
        {
            const REL::Relocation<std::uintptr_t> function{ RELOCATION_ID(Runtime::kResponseSetupSE, Runtime::kResponseSetupAE) };
            original = reinterpret_cast<decltype(&thunk)>(function.address());
            if (const auto error = EntryHook::Attach(reinterpret_cast<void**>(&original), reinterpret_cast<void*>(&thunk))) {
                logs::error("Can't hook the start of the setup function (error {}), so [NPCs] entries won't work", error);
                return;
            }
            logs::info("Speaker hook installed");
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
                if (!g_npcs.empty()) {
                    std::vector<bool> byID(g_npcs.size()), unloaded(g_npcs.size());
                    for (std::size_t i = 0; i < g_npcs.size(); ++i) {
                        if (const auto ref = VoicePath::ParseFormRef(g_npcs[i])) {
                            byID[i] = true;
                            const auto* file = data->LookupModByName(ref->plugin);
                            if (!file || file->GetCompileIndex() == 0xFF) {
                                unloaded[i] = true;
                                continue;
                            }
                            // Not CommonLib's LookupFormID, which on VR depends on how Skyrim VR ESL Support is set up.
                            g_npcIDs[i] = VoicePath::FormIDIn(ref->id, file->IsLight(), file->GetCompileIndex(),
                                file->GetSmallFileCompileIndex());
                        }
                    }
                    g_npcIDsReady.store(true, std::memory_order_release);

                    std::vector<std::size_t> records(g_npcs.size());
                    for (const auto* form : data->GetFormArray<RE::TESNPC>()) {
                        if (!form) {
                            continue;
                        }
                        for (std::size_t i = 0; i < g_npcs.size(); ++i) {
                            const bool match = byID[i] ? (g_npcIDs[i] && form->GetFormID() == g_npcIDs[i]) :
                                                         VoicePath::EqualsFolded(form->fullName.c_str(), g_npcs[i]);
                            records[i] += match;
                        }
                    }
                    for (std::size_t i = 0; i < g_npcs.size(); ++i) {
                        if (unloaded[i]) {
                            logs::warn("NPC entry {}: that plugin isn't loaded", i + 1);
                        } else if (byID[i]) {
                            if (records[i]) {
                                logs::info("NPC entry {}: NPC record found", i + 1);
                            } else {
                                logs::warn("NPC entry {}: no NPC record with that ID", i + 1);
                            }
                        } else if (records[i]) {
                            logs::info("NPC entry {}: {} NPC record(s) by that name", i + 1, records[i]);
                        } else {
                            logs::warn("NPC entry {}: no NPC by that name, check the spelling", i + 1);
                        }
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
    const auto version = a_skse->RuntimeVersion();
    if (!Runtime::IsSupported(version.pack())) {
        return false;
    }
    if (Runtime::UsesFormat5(version.pack())) {
        if (const auto* error = Format5::Install(Runtime::kRequiredAE)) {
            SetupLog();
            logs::error("Can't use the Address Library for 1.7.104 ({}), so not loading", error);
            return false;
        }
    }
    const bool vr = Runtime::IsVR(version.pack());
    if (vr) {
        std::ifstream csv{ VRAddressLibrary::kPath };
        if (const auto* error = VRAddressLibrary::Check(csv, Runtime::kRequiredSE)) {
            SetupLog();
            logs::error("Can't use VR Address Library ({}), so not loading", error);
            return false;
        }
    }

    SKSE::Init(a_skse, false);
    SetupLog();
    logs::info("Unvoice {} on Skyrim {}{}", UNVOICE_VERSION_STRING, vr ? "VR "sv : ""sv, version.string("."sv));
    LoadConfig();

    if ((!g_plugins.empty() || !g_npcs.empty()) && BuildVoicePathHook::Install()) {
        if (!g_npcs.empty()) {
            ResponseSetupEntryHook::Install();
        }
        SKSE::GetMessagingInterface()->RegisterListener(OnMessage);
    }
    return true;
}
