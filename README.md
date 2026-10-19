# California Speed: Recompiled

> [!NOTE]
> This project is vibe coded: nearly all of its code was written by an AI coding assistant (Claude),
> directed and play-tested by a human. Expect rough edges.

A native PC port of **California Speed** (Nintendo 64, 1999), built by statically recompiling the
original game code with [N64Recomp](https://github.com/N64Recomp/N64Recomp). The game runs as native
code on [N64ModernRuntime](https://github.com/N64Recomp/N64ModernRuntime), renders through
[RT64](https://github.com/rt64/rt64) and uses the [RecompFrontend](https://github.com/N64Recomp/RecompFrontend)
launcher and menus. All three are used through forks with this port's changes.

**This repository contains no game data.** You need your own copy of the game: the launcher asks for
the ROM on first start, and the build generates the recompiled code from it.

## Status

Playable from boot to the credits. Split screen (2 players) has not been tested yet.

- Title screen, menus, races and audio work.
- **60 FPS races.** The game times its logic in real time, so races run at 60 FPS; menus stay at the
  original 30, since their animations advance per frame.
- **Widescreen.** With the "Expand" aspect ratio, races and the credits scene fill the window and the
  race HUD moves to the screen edges (RT64's HUD ratio option). Menus stay 4:3 on purpose: their
  background art is 4:3.
- Higher resolutions, MSAA and anisotropic filtering through RT64.
- **Controller Pak saves** are emulated. Each controller gets its own pak, stored as a standard
  `.mpk` image (the format emulators use), so saves can be moved to and from emulators.
- **Rumble** works. The emulated pak is a Controller Pak and a Rumble Pak at the same time, so you
  never have to swap paks the way the original hardware required.
- Keyboard and modern controller support with remappable controls (defaults follow the N64 layout:
  A on the right face button, B on the bottom one).

Known issues and planned work are tracked in [TODO.md](TODO.md). The main ones:

- The eyecatch (attract) screen still has bars at the sides in widescreen, and its clouds are missing.
- The Midway word flashes briefly during the startup intro in widescreen.

## Enhancements

All optional, in the **Enhancements** tab of the settings:

| Option | Default | What it does |
| --- | --- | --- |
| Full Analog Controls | On | Steers with the stick's full precision instead of the N64 stick's steps; gas and brake are analog when bound to analog inputs such as the triggers. |
| Maximum Level of Detail | On | Always draws models at their highest detail and does not hide distant objects early. |
| Smooth Radar | On | Radar dots move smoothly at the display resolution, and slide out under the radar's frame at its edges instead of vanishing. |
| Resume Music After Pause | On | Pausing a race freezes the music, and continuing picks it up exactly where it was, instead of starting the song over. |
| Sample Rate | 48000 Hz | The rate the game mixes its sound at (the original is 22050 Hz). Takes effect after a restart. |
| Skip Copyright Screen | Off | Goes straight to the intro at startup. |
| Arcade Textures (Experimental) | Off | See below. |

The **Graphics** tab adds **Anisotropic Filtering** (default 8x), which keeps the road and walls sharp
at a glancing angle.

### Arcade textures (experimental)

California Speed started as an arcade game (Atari Games, 1998), whose textures have a much higher
resolution than the N64's. This option replaces the N64 textures with the matching arcade ones, made
from your own copy of the arcade version's hard drive image (`calspeed.chd`, as used by MAME). No arcade
data is included or downloaded.

To use them, pick the image with **Arcade Data** in the launcher. The textures are then extracted
into a texture pack (in `%LOCALAPPDATA%\CaliforniaSpeedRecompiled\arcade_textures`, which takes a
minute), and **Arcade Textures** turns them on. In game, **F2** hides and shows them for comparison.

It is experimental and off by default:

- 782 N64 textures are matched to arcade ones (`assets/arcade_textures.json`); the rest stay as they
  are. Car paint, the HUD and the menus are not replaced yet.
- The arcade's own transparency is used (its colour keys and alpha). A few transparent edges still show
  thin lines, and a wrong match can slip through.
- When an update changes the matching, the pack is rebuilt from the arcade image chosen in the
  launcher; if the image has moved, pick it again.

## Supported version

| Game | Region | Format | MD5 |
| --- | --- | --- | --- |
| California Speed | USA (NCLE) | `.z64` (big-endian) | `965ad2fa317f0644e49a89a3219719cb` |

`.n64`/`.v64` dumps of the same ROM need converting to `.z64` first.

## Building

Windows is the only platform built so far. In short:

1. Build N64Recomp and RSPRecomp (`build_n64recomp.bat`) and copy both executables to the repo root.
2. Generate the recompiled code from your ROM:
   ```
   python tools/gen_syms.py
   N64Recomp.exe cspeed.us.toml
   RSPRecomp.exe aspMain.us.toml
   ```
3. Build with `build.bat` (Release, into `build/`) or `build_dbg.bat` (RelWithDebInfo, into `build-dbg/`).
4. Run `CaliforniaSpeedRecompiled.exe` and select your ROM in the launcher.

Settings and saves live in `%LOCALAPPDATA%\CaliforniaSpeedRecompiled` (the Controller Pak images in
`saves\`).

[GETTING_STARTED.md](GETTING_STARTED.md) has the full details: required tools, how the ROM is laid out,
how function boundaries and names are produced, and the debugging workflow.

### Debug tools

`./debug.sh [track]` starts the game with the debug tools on (`CSPEED_DEBUG=1`), and with a track
number (0-15) goes straight into a race on it. Keys: F1 free camera, F3 culling, F5 fog, F6 far view,
F7 world at 1/4 scale (a draw distance experiment), F8/F9 the game's built-in debug menus, F10 to win
a race and reach the credits. Details are at the top of `src/game/debug_tools.cpp`.

## How it works

- `tools/` finds the game's functions in the ROM (`funcbounds.py`) and writes the symbol file
  N64Recomp consumes (`gen_syms.py`). Names come from `symbols/functions.json`.
- `cspeed.us.toml` configures the recompilation, including the hooks that call into the native code
  below.
- `src/game/` holds the game-specific native code, each file explained at its top:
  - `controller_pak.cpp`: Controller Pak and Rumble Pak emulation.
  - `game_fixes.cpp`: fixes for game behaviour that depends on N64 timing.
  - `framerate.cpp`: 60 FPS races.
  - `graphics.cpp`: widescreen, HUD placement and the sky.
  - `analog.cpp`, `lod.cpp`, `radar.cpp`, `audio.cpp`: the enhancements (and the sample rate).
  - `arcade_textures.cpp`: reads the arcade drive image and builds the arcade texture pack.
  - `debug_tools.cpp`, `draw_distance.cpp`: the debug tools.
- `patches/` is for game code rewritten in C and compiled for MIPS (needs a mips-elf toolchain).

## License

This project's code is licensed under the GNU General Public License v3.0; see [COPYING](COPYING).
The libraries in `lib/` keep their own licenses. California Speed itself is not covered by this
license and is not included.

## Credits

- [N64Recomp](https://github.com/N64Recomp/N64Recomp), [N64ModernRuntime](https://github.com/N64Recomp/N64ModernRuntime)
  and [RecompFrontend](https://github.com/N64Recomp/RecompFrontend) by Wiseguy and contributors.
- [RT64](https://github.com/rt64/rt64) by Darío and contributors.
- California Speed was developed by Atari Games and published by Midway. This project is not
  affiliated with or endorsed by them or by Nintendo.
