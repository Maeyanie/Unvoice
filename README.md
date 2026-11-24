# Unvoice

An SKSE plugin that makes Skyrim treat the voice lines of chosen plugins as unvoiced, so that
[SkyrimNet](https://github.com/MinLL/SkyrimNet-GamePlugin) can speak them with TTS.

## Why

Some voice actors have opted out of voice cloning, and SkyrimNet respects that by using a default
voice for their characters' TTS. Such a character then has two voices: the recorded one for scripted
lines and the default one for everything else. With the mod's plugin listed in Unvoice, the recordings
aren't played and the character speaks with the default voice throughout.

It can also mute particular NPCs wherever their lines come from, the base game included, so that a
character can be given a new voice entirely, such as one made with SkyrimNet's voice designer.

Unvoice is **not** a way around the opt-out. It's built so that SkyrimNet's sample extraction sees
exactly what it would without Unvoice. It also needs nothing from the mods except their plugin
filenames: it doesn't read, change or include any of their files.

Unvoice isn't affiliated with SkyrimNet or with any of the mods in its default list.

## Requirements

- Skyrim SE/AE 1.5.x, 1.6.x or 1.7.104 with [SKSE](https://skse.silverlock.org/), or Skyrim VR 1.4.15
  with SKSEVR. 1.7.104 and VR are experimental
- The current [Visual C++ x64 Redistributable](https://aka.ms/vs/17/release/vc_redist.x64.exe)
- [Address Library for SKSE Plugins](https://www.nexusmods.com/skyrimspecialedition/mods/32444), or on VR
  [VR Address Library for SKSEVR](https://www.nexusmods.com/skyrimspecialedition/mods/58101)
- Fuz Ro D-oh **for your game version**, as its builds can be for one version only:
  [Fuz Ro D-oh](https://www.nexusmods.com/skyrimspecialedition/mods/15109) 2.5 for 1.6.1170 and 2.6 for
  1.7.104, or on VR [Fuz Ro D'oh VR](https://www.nexusmods.com/skyrimspecialedition/mods/91644), which
  has subtitles always on
- SkyrimNet, with **Voice Silent NPCs** turned on, if you want TTS

## Use

1. Install a zip from [Releases](https://github.com/Maeyanie/Unvoice/releases) with your mod manager, or put
   `Unvoice.dll` and [`Unvoice.ini`](release/Unvoice.ini) in `Data/SKSE/Plugins`.
2. Edit the lists in the INI, one entry per line:

   ```ini
   [Plugins]
   SomeFollower.esp
   AnotherMod.esp

   [NPCs]
   Lydia
   ```

   - `[Plugins]` mutes every line a plugin created, whoever says it. It starts with
     `SeranaDialogAddon.esp` and `Ashe - Fire and Blood.esp`.
   - `[NPCs]` mutes everything an NPC says, from any plugin. Give the name you see in game, which
     mutes every NPC with that name, or `Plugin|FormID` as SkyPatcher takes it (`Skyrim.esm|A2C94`,
     `Dawnguard.esm|02000801`, `MyMod.esl|800`), which mutes that one record. Case doesn't matter.

     To find an NPC's form ID, type `help "<name>" 4` in the console. It lists the NPC records by
     that name, with their IDs. Clicking an NPC in the console shows the ID of that placed copy instead,
     which won't match. Some appearance replacers add a second record with the same name, which a
     name entry also mutes.

3. Restart the game.

Installing a new version replaces the INI, so keep a copy if you've edited it.

`Documents/My Games/Skyrim Special Edition/SKSE/Unvoice.log` (`Skyrim VR` on VR) says whether each entry
was found and how many responses were muted or left real. It records entry numbers and counts only,
never paths, filenames, names or dialogue.

## Compatibility

Tested in game only on Skyrim 1.6.1170, with Fuz Ro D-oh 2.5 and SkyrimNet 1.2.0 and 1.3.1. Other 1.5.x
and 1.6.x versions may work but are untested.

1.7.104 and VR support are experimental. They've been checked against those versions' Address Library
and Fuz Ro D-oh builds and in a simulated game, never in the game itself. Other 1.7 versions aren't
supported: Unvoice declines to load there, and SKSE reports it. On VR, Unvoice also declines to load if
VR Address Library is missing, damaged or too old for it, and says so in the log.

If SkyrimNet is installed and Unvoice can't recognise its sample extraction, for instance after an
update, Unvoice mutes nothing and says so in the log.

## Limits

- Every character in a listed plugin is muted.
- Plugins are matched by the folder in their voice path, which names the plugin that created the line.
  A mod that revoices another plugin's lines isn't caught by listing that mod, but listing the NPC works.
- NPCs are matched by their NPC record's name or form ID. A name given in game, for instance by
  renaming a follower, isn't seen. NPCs made from a template at runtime, such as most bandits, don't
  have the template's form ID. Speakers that aren't NPCs, like talking objects, can't be listed.
- Speech played as a plain sound effect, outside dialogue, isn't caught.
- Muted lines lose their recorded lip sync.
- Fuz Ro D-oh caps its silence at 10 seconds per response, which may be shorter than the TTS.
- Plugin names with non-ASCII characters need the INI saved in the system's ANSI code page.

## How it works

The call at +0xDE of Address Library 34429 (SE) / 35249 (AE) writes a response's voice path into a
buffer. When it returns, Unvoice overwrites the plugin's folder in that buffer with underscores.
Fuz Ro D-oh checks the path at +0xEC, finds no file, and supplies timed silence as it does for any
unvoiced line.

SkyrimNet also builds dialogue through that function to find voice samples, and muting there would
hide the real paths from its cloning code. So on a match Unvoice checks the call stack, and leaves the
path real if either of these is on it:

- `VoiceSampleExtractor::GetDialogueResponses`
- `ActorDialogueExtractor::BuildDialogueEntry`

They're found at startup by their name strings. An update that adds another extraction route under a
new name would need adding to [`src/skyrimnet.h`](src/skyrimnet.h).

The voice path doesn't say who is speaking, and many NPCs share each voice type and its folder. For
`[NPCs]`, Unvoice also hooks the start of that function with
[Microsoft Detours](https://github.com/microsoft/Detours), as its fourth argument is the speaker, and
checks that speaker's NPC record, by name or form ID, when the path is built. If that hook can't be
installed, NPC entries do nothing and the log says so.

The call site and signature are also used by
[Dynamic Dialogue Replacer](https://github.com/KrisV-777/Dynamic-Dialogue-Replacer) and
[S.T.F.U](https://github.com/zevck/S.T.F.U), and Dynamic Dialogue Replacer also hooks the function's
start, which is where its arguments are known from. No code was copied from either.

## Build

Needs [xmake](https://xmake.io/) and Visual Studio 2022.

```
git clone --recurse-submodules https://github.com/Maeyanie/Unvoice.git
cd Unvoice
xmake f -m releasedbg --vs=2022 -y
xmake
```

The DLL ends up in `build/windows/x64/releasedbg`. On the first configure, xmake downloads Microsoft
Detours at the commit pinned in [`xmake.lua`](xmake.lua).

To run the tests:

```
xmake build tests
xmake run tests
```

`xmake run tests <absolute path to SkyrimNet.dll>` adds checks against SkyrimNet 1.2.0. These map the
DLL without running any of it, and their addresses apply to that version only.

## License

Unvoice's own code is under the [BSD Zero Clause License](LICENSE).

The CommonLibSSE-NG submodule is pinned to an MIT-licensed commit and used unmodified. Unvoice has its
own reader for 1.7's Address Library format, as that commit predates it. Later upstream commits are
GPL-3.0. Anyone distributing a built DLL needs to include the notices of the libraries linked into it:
see [THIRD-PARTY-NOTICES.txt](release/THIRD-PARTY-NOTICES.txt).
