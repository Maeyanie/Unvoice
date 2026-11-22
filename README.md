# Unvoice

An SKSE plugin that makes Skyrim treat the voice lines of chosen plugins as unvoiced, so that
[SkyrimNet](https://github.com/MinLL/SkyrimNet-GamePlugin) can speak them with TTS.

## Why

Some voice actors have opted out of voice cloning, and SkyrimNet respects that by using a default
voice for their characters' TTS. Such a character then has two voices: the recorded one for scripted
lines and the default one for everything else. With the mod's plugin listed in Unvoice, the recordings
aren't played and the character speaks with the default voice throughout.

Unvoice is **not** a way around the opt-out. It's built so that SkyrimNet's sample extraction sees
exactly what it would without Unvoice. It also needs nothing from the mods except their plugin
filenames: it doesn't read, change or include any of their files.

Unvoice isn't affiliated with SkyrimNet or with any of the mods in its default list.

## Requirements

- Skyrim SE/AE 1.5.x, 1.6.x or 1.7.104 (experimental) with [SKSE](https://skse.silverlock.org/). Not VR
- The current [Visual C++ x64 Redistributable](https://aka.ms/vs/17/release/vc_redist.x64.exe)
- [Address Library for SKSE Plugins](https://www.nexusmods.com/skyrimspecialedition/mods/32444)
- [Fuz Ro D-oh](https://www.nexusmods.com/skyrimspecialedition/mods/15109) **for your game version**:
  its builds can be for one version only, e.g. 2.5 for 1.6.1170 and 2.6 for 1.7.104
- SkyrimNet, with **Voice Silent NPCs** turned on, if you want TTS

## Use

1. Install a zip from [Releases](https://github.com/Maeyanie/Unvoice/releases) with your mod manager, or put
   `Unvoice.dll` and [`Unvoice.ini`](release/Unvoice.ini) in `Data/SKSE/Plugins`.
2. Edit the list under `[Plugins]` in the INI. It starts with `SeranaDialogAddon.esp` and
   `Ashe - Fire and Blood.esp`. Add or remove filenames, one per line:

   ```ini
   [Plugins]
   SomeFollower.esp
   AnotherMod.esp
   ```

3. Restart the game.

Installing a new version replaces the INI, so keep a copy if you've edited it.

`Documents/My Games/Skyrim Special Edition/SKSE/Unvoice.log` says whether each entry was found and how
many responses were muted or left real. It records entry numbers and counts only, never paths,
filenames or dialogue.

## Compatibility

Tested in game only on Skyrim 1.6.1170, with Fuz Ro D-oh 2.5 and SkyrimNet 1.2.0 and 1.3.1. Other 1.5.x
and 1.6.x versions may work but are untested.

1.7.104 support is experimental: it has been checked against that version's Address Library and a
simulated game, never the game itself. Other 1.7 versions and VR aren't supported: Unvoice declines to
load there, and SKSE reports it.

If SkyrimNet is installed and Unvoice can't recognise its sample extraction, for instance after an
update, Unvoice mutes nothing and says so in the log.

## Limits

- Every character in a listed plugin is muted.
- Lines are matched by the folder in their voice path, which names the plugin that created the line.
  A mod that revoices another plugin's lines isn't caught.
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

The call site and signature are also used by
[Dynamic Dialogue Replacer](https://github.com/KrisV-777/Dynamic-Dialogue-Replacer) and
[S.T.F.U](https://github.com/zevck/S.T.F.U). No code was copied from either.

## Build

Needs [xmake](https://xmake.io/) and Visual Studio 2022.

```
git clone --recurse-submodules https://github.com/Maeyanie/Unvoice.git
cd Unvoice
xmake f -m releasedbg --vs=2022 -y
xmake
```

The DLL ends up in `build/windows/x64/releasedbg`.

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
