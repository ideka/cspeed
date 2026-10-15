# California Speed: Recompiled

A native PC port of **California Speed** (Nintendo 64, 1999), built by statically recompiling the
original game code with [N64Recomp](https://github.com/N64Recomp/N64Recomp). The game runs as native
code on [N64ModernRuntime](https://github.com/N64Recomp/N64ModernRuntime), renders through
[RT64](https://github.com/rt64/rt64) and uses the [RecompFrontend](https://github.com/N64Recomp/RecompFrontend)
launcher and menus.

**This repository contains no game data.** You need your own copy of the game: the launcher asks for
the ROM on first start, and the build generates the recompiled code from it.

## Status

Early, but playable from boot to finish line.

- Title screen, menus, races and audio work.
- **Controller Pak saves** are emulated. Each controller gets its own pak, stored as a standard
  `.mpk` image (the format emulators use), so saves can be moved to and from emulators.
- **Rumble** works. The emulated pak is a Controller Pak and a Rumble Pak at the same time, so you
  never have to swap paks the way the original hardware required.
- Higher resolutions, MSAA and high framerates through RT64; keyboard and modern controller support
  with remappable controls (defaults follow the N64 layout: A on the right face button, B on the
  bottom one).

Known issues and planned work are tracked in [TODO.md](TODO.md). The main ones:

- At framerates above the original, a few things jitter (the speedometer needle, tire tracks).
- Widescreen ("Expand" aspect ratio) and HUD placement are not hooked up yet; Expand only adds
  borders.

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

Settings and saves live in `%LOCALAPPDATA%\CaliforniaSpeedRecompiled`.

[GETTING_STARTED.md](GETTING_STARTED.md) has the full details: required tools, how the ROM is laid out,
how function boundaries and names are produced, and the debugging workflow.

## How it works

- `tools/` finds the game's functions in the ROM (`funcbounds.py`) and writes the symbol file
  N64Recomp consumes (`gen_syms.py`). Names come from `symbols/functions.json`.
- `cspeed.us.toml` configures the recompilation, including small hooks where the game's timing
  assumptions don't hold in a multithreaded runtime.
- `src/game/` holds the game-specific native code: Controller Pak emulation (`controller_pak.cpp`)
  and fixes for timing-dependent game behavior (`game_fixes.cpp`).
- `patches/` is for game code rewritten in C and compiled for MIPS (needs a mips-elf toolchain).

## Credits

- [N64Recomp](https://github.com/N64Recomp/N64Recomp), [N64ModernRuntime](https://github.com/N64Recomp/N64ModernRuntime)
  and [RecompFrontend](https://github.com/N64Recomp/RecompFrontend) by Wiseguy and contributors.
- [RT64](https://github.com/rt64/rt64) by Darío and contributors.
- California Speed was developed by Atari Games and published by Midway. This project is not
  affiliated with or endorsed by them or by Nintendo.
