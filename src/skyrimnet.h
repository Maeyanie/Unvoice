#pragma once

#include <array>
#include <string_view>

namespace SkyrimNet
{
    // SkyrimNet builds DialogueItems for more than playback. These two functions walk an NPC's dialogue to
    // pick voice samples (where the cloning block applies) and to catalogue it. They have to see exactly
    // what they would without Unvoice. SilentDialogueManager and TopicInfoEventHandler build them too, and
    // those have to see the muted path, or there's no TTS.
    //
    // Deliberately not whole classes: ActorDialogueExtractor can also run dialogue fragments, and speech
    // started from there must still be muted.
    inline constexpr std::array<std::string_view, 2> kRealPathCallers{
        "SkyrimNet::VoiceSampleExtractor::GetDialogueResponses",
        "SkyrimNet::Skyrim::Utils::ActorDialogueExtractor::BuildDialogueEntry",
    };
}
