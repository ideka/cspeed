# California Speed: Recompiled — developer guide

How the port is put together, how to build it, and what has been learned about the game. For known
issues and planned work see [TODO.md](TODO.md).

## What a "recomp" is
[N64Recomp](https://github.com/N64Recomp/N64Recomp) statically translates every MIPS function in the
ROM into C, which is linked against a modern runtime
([N64ModernRuntime](https://github.com/N64Recomp/N64ModernRuntime): `ultramodern` + `librecomp`), the
[RT64](https://github.com/rt64/rt64) renderer and the
[RecompFrontend](https://github.com/N64Recomp/RecompFrontend) launcher/UI. The result is a native
executable that still reads the original ROM at runtime for its assets. No emulator is involved.
The project started from [N64RecompGameTemplate](https://github.com/SrBananaMan/N64RecompGameTemplate).

## The ROM
| Item | Value |
|---|---|
| File | `California Speed (USA).z64` (big-endian, 16 MiB); convert `.n64`/`.v64` dumps first |
| Internal name / game code | `CAL SPEED` / `NCLE`, version 0 |
| CIC | 6102 |
| Entry point | `0x80000400` (ROM offset `0x1000`) |
| Graphics microcode | F3DEX2-style display lists (`RSP Gfx ucode F3DEX.NoN fifo 2.04`), handled by RT64 |
| Audio | libultra audio library with the stock `aspMain` RSP microcode |
| Compiler | SGI IDO (judging from libultra assert strings) |
| Copyright | 1998 Atari Games / Midway Home Entertainment |
| .z64 CRC32 / MD5 | `6f6262cb` / `965ad2fa317f0644e49a89a3219719cb` |

There is no public decompilation of this game, so function boundaries and names were produced from
scratch (see "Symbols" below).

## Memory layout
| Segment | ROM range | RAM range | Notes |
|---|---|---|---|
| boot | `0x1000`..`0x1CD60` | `0x80000400`..`0x8001C160` | text+data; libultra + early game code |
| boot BSS | – | `0x8001C160`..`0x80053710` | cleared by the entry stub |
| main | `0x1CD60`..`0xA6C70` | `0x80053710`..`0x800DD620` | loaded right after the boot BSS; code ends near `0x800D20A0`, then data |
| main BSS | – | `0x800DD620`..~`0x80170000` | estimated from address references |
| cart | `0x0`..`0x1000000` | `0xB0000000`.. | PI domain 1 view of the ROM |

For the main segment `vram − rom = 0x800369B0`. The layout is also described in
`California Speed (USA).z64.segments.json`, which the Binary Ninja loader reads.

## Requirements (Windows)
- Visual Studio 2022 with the "Desktop development with C++" workload and "C++ CMake tools for
  Windows" (for MSVC headers/libraries, CMake and Ninja).
- LLVM (a recent release): `clang-cl` builds the game, `clang` builds N64Recomp.
- Python 3 (standard library only).
- GNU make (e.g. GnuWin32 Make) for the patch build.
- Optional, for `patches/`: a MIPS toolchain, either a `mips-elf` GCC + binutils or a clang/lld built
  with the Mips target. Without one the build still works; `patches/Makefile` then writes an empty
  `patches.elf` and no patches are applied.
- Optional: Binary Ninja, for reverse engineering (see below).

The build scripts find Visual Studio with `vswhere` and LLVM in its default install location. Anything
else (LLVM elsewhere, make or the MIPS toolchain not on PATH) goes in a git-ignored
`build_env.local.bat` at the repository root; `tools/build_env.bat` lists the variables it reads
(`LLVM_BIN`, `MAKE_BIN`, `MIPS_TOOLCHAIN_BIN`, `MIPS_TOOLCHAIN_DLL_DIR`).

## Building
```
git clone --recurse-submodules <this repository>
build_n64recomp.bat                       # builds N64Recomp/build/N64Recomp.exe and RSPRecomp.exe
copy N64Recomp\build\N64Recomp.exe .      # CMake runs N64Recomp from the repo root for the patch step
copy N64Recomp\build\RSPRecomp.exe .
rem put "California Speed (USA).z64" in the repo root, then generate the code from it:
python tools\gen_syms.py                  # symbols -> cspeed.us.syms.toml / cspeed.us.datasyms.toml
N64Recomp.exe cspeed.us.toml              # -> RecompiledFuncs/*.c
RSPRecomp.exe aspMain.us.toml             # -> rsp/aspMain.cpp
build.bat                                 # Release build into build/ (build_dbg.bat: RelWithDebInfo into build-dbg/)
```
Everything derived from the ROM (`RecompiledFuncs/`, `rsp/aspMain.cpp`, the ROM itself) is git-ignored
and must never be committed. Rerun the generation steps after changing symbols or `cspeed.us.toml`.

Notes:
- The template expects the `clang_cl_x64` toolset; plain `clang` fails on MSVC-style flags.
- `__PRFCHWINTRIN_H` is pre-defined for clang-cl because RT64's bundled SDL2 headers define
  `_m_prefetch`, which newer clang treats as a builtin.
- `rsp/aspMain.cpp` is compiled with `/clang:-msse4.1 /clang:-mssse3` (librecomp's RSP vector unit
  needs them).
- `lib/N64ModernRuntime` and `lib/RecompFrontend` track the `cspeed` branches of forks
  (github.com/ideka/N64ModernRuntime: stopping other threads, thread queue removal;
  github.com/ideka/RecompFrontend: the fullscreen toggle is saved). The upstream projects do not take
  AI-assisted contributions, so these stay forks.

## Running and debugging
Start `CaliforniaSpeedRecompiled.exe`; the launcher asks for the ROM the first time (it is checked by
hash). Settings and saves live in `%LOCALAPPDATA%\CaliforniaSpeedRecompiled`.

Environment variables for test runs:
- `CSPEED_AUTOSTART=1`: skip the launcher and start the game.
- `CSPEED_INPUT_SCRIPT="12:S,15:A"`: press N64 buttons on controller 1 at the given times (seconds),
  for driving menus without a controller.
- `CSPEED_UNBUFFERED=1`: unbuffered stdout/stderr, so logs survive a crash.

The usual loop when something fails: find the recompiled function in `RecompiledFuncs/` (names come
from `symbols/functions.json`), study it (Binary Ninja helps), then fix the symbols, add a hook in
`cspeed.us.toml` or native code in `src/game/`. Windows crash dumps (`%LOCALAPPDATA%\CrashDumps`)
open in WinDbg/cdb with the matching `.pdb` from `build-dbg/`.

## Symbols
- `symbols/function_seeds.json`: function start addresses from Binary Ninja's analysis (seeds only).
- `tools/funcbounds.py`: a MIPS control-flow walker that turns the seeds into exact function extents,
  resolves jump tables, merges fragments that were split off and classifies data misread as code.
  Its docstring explains the heuristics.
- `symbols/functions.json`: hand-curated function names (source of truth), including the identified
  libultra functions. The runtime replaces libultra functions by name, so these names matter.
- `symbols/recomp_names.json`: names given to N64Recomp instead of the real ones, to keep the game's
  own copy of a libultra function (`game_*`) or to route a call to native code (`cspeed_*`).
- `tools/gen_syms.py` writes the symbol files from all of the above; `tools/check_syms.py` validates them
  the way N64Recomp would (branches, `jal` targets, unsupported instructions).

## Binary Ninja (optional)
- `tools/binaryninja/n64_rom_loader.py` is a loader plugin: it maps the segments from the
  `.segments.json`, names hardware registers and sets the entry point. Install it by copying it to
  Binary Ninja's user plugin folder as `n64_rom_loader/__init__.py`, then open the `.z64` with the
  "N64 ROM" view type.
- [binary_ninja_mcp](https://github.com/fosdickio/binary_ninja_mcp) exposes the open database over MCP
  and an HTTP API (port 9009), which the tools here use: `tools/bn_apply_names.py` pushes
  `symbols/functions.json` into the database. Refresh the seeds after re-analysis with:
  ```
  curl -s "http://127.0.0.1:9009/methods?limit=100000" | python -c "import sys,json; f=json.load(sys.stdin)['functions']; d=json.load(open('symbols/function_seeds.json')); d['seeds']=sorted({'0x%08x'%int(x['address'],16) for x in f}); json.dump(d, open('symbols/function_seeds.json','w',newline='\n'), indent=1)"
  ```
- Binary Ninja's MIPS decompiler does not lift FPU instructions; read float code in `RecompiledFuncs/`.

## Project layout
| Path | What |
|---|---|
| `src/`, `include/`, `assets/`, `icons/`, `CMakeLists.txt` | the game project (from the template) |
| `src/game/controller_pak.cpp` | Controller Pak emulation (also a Rumble Pak) |
| `src/game/game_fixes.cpp` | native replacements for timing-dependent game code |
| `src/game/graphics.cpp` | RT64 extended GBI setup, widescreen and HUD placement |
| `cspeed.us.toml` | N64Recomp config, including hooks into game functions |
| `aspMain.us.toml` | RSPRecomp config for the audio microcode |
| `patches/` | game code rewritten in C and compiled for MIPS |
| `symbols/`, `tools/` | symbol data and the scripts that produce the symbol files |
| `lib/` | runtime, renderer and UI submodules; `N64Recomp/` is the recompiler submodule |

## What has been learned about the game
- **Tasks.** The graphics task is set up at `0x80055168` (rspboot `0x80015330`, microcode text
  `0x80016220`, data `0x8001B400`); the audio task at `0x800577C0` (aspMain text `0x80015400`, ROM
  `0x16000`, `0xE20` bytes, data `0x8001B140`).
- **libultra names that were easy to get wrong.** `osViSetMode`/`osViSwapBuffer` are `0x80009B80` /
  `0x80009D40` (swapped, nothing is ever displayed); `osMotorStop`/`osMotorStart` are `0x8000C7A0` /
  `0x8000C960`.
- **Scheduling.** The game stops and restarts its scheduler thread every frame (hence the runtime
  fork), and polls `0x80001400` in busy loops; `cspeed.us.toml` inserts yields there.
- **Saves.** Controller Pak only (no EEPROM/SRAM/Flash), so the game entry uses `SaveType::None`. The
  runtime has no Controller Pak support, so `src/game/controller_pak.cpp` emulates it below libultra:
  the game's own `osPfs*` code is recompiled and only `__osContRamRead/Write`, `__osPfsGetStatus` and
  `osPfsIsPlug` are native. Paks are `.mpk` images next to the runtime's save file, each with its own
  pak ID (the game treats equal IDs on two controllers as one pak that was moved).
- **Rumble.** Every emulated pak is also a Rumble Pak: a hook after the pak probe (`func_8005D1C8`)
  sets the rumble bitmask (`0x800E1B81`) and the motor OSPfs (`0x800E1D38 + n*0x68`), and requests a
  controller re-query (`0x800200B0`), without which no controller is assigned to a player slot. The
  pre-race "insert Rumble Pak" check (`func_800A9B54`) is told the pak is present.
- **ROM loading.** Compressed ROM loads take a lock with a non-blocking try (`romLockTryAcquire`,
  `0x80053A64`); when the music loader held it, an asset bank was registered empty and the menu
  crashed. It now waits for the lock (`src/game/game_fixes.cpp`).
- **Rendering.** The display list head is at `0x80022DC8`; camera setup is `func_800CF0B4` (per camera:
  0x54-byte struct at `0x800F93F0 + n*0x54`, viewport at `*0x80022FB0 + n*16`). The 3D scissor is
  inset 12 px per side, which kept RT64 from widening the scene. 2D elements go into a separate,
  double-buffered list (`func_8005A0D8` begins it, head at `0x80141570`); sprites are drawn by
  `func_8005C718`, fill rectangles by `func_8005CCC4`. The game mode at `0x80141E00` is 1 during races.
