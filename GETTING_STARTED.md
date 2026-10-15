# California Speed (N64) Recomp — Getting Started

## What a "recomp" is, in one paragraph
N64: Recompiled (Mr-Wiseguy's N64Recomp) statically translates every MIPS function in the ROM
into C, then links that C against a modern runtime (N64ModernRuntime = `ultramodern` + `librecomp`)
and the RT64 renderer. The output is a native Windows/Linux executable that still loads the
original ROM at runtime for assets. No emulator is involved.

## The ROM
| Item | Value |
|---|---|
| Converted file | `California Speed (USA).z64` (big-endian, 16 MiB) — use this one |
| Internal name / game code | `CAL SPEED` / `NCLE`, version 0 |
| CIC | 6102 |
| Entry point | `0x80000400` (ROM offset `0x1000`) |
| BSS clear in boot | `0x8001C160`, length `0x375B0` → text+data = ROM `0x1000`..`0x1CD60` |
| Graphics microcode | `RSP Gfx ucode F3DEX.NoN fifo 2.04` (stock Nintendo — RT64 supports it) |
| Audio | libultra audio lib (`seqplayer.c`, `sndplayer.c`, `synthesizer.c` asserts) → stock `aspMain` RSP ucode |
| Compiler (likely) | SGI IDO, judging from libultra assert strings — N64Recomp's best-tested input |
| Copyright | 1998 Atari Games / Midway Home Entertainment |
| .z64 CRC32 / MD5 | `6f6262cb` / `965ad2fa317f0644e49a89a3219719cb` |

There is **no public decompilation or disassembly** of this game (GitHub search returned nothing).
That means the symbol list has to be produced from scratch — see "The hard part" below.

## Memory layout (worked out 2026-10-14)
| Segment | ROM range | RAM range | Notes |
|---|---|---|---|
| boot | `0x1000`..`0x1CD60` | `0x80000400`..`0x8001C160` | text+data; libultra + early game code |
| boot BSS | – | `0x8001C160`..`0x80053710` | cleared by the entry stub |
| main | `0x1CD60`..`0xA6C70` | `0x80053710`..`0x800DD620` | loaded contiguously after boot BSS; last code ≈ `0x800D20A0`, then data |
| main BSS | – | `0x800DD620`..~`0x80170000` | estimate from address references |
| cart | `0x0`..`0x1000000` | `0xB0000000`.. | PI domain 1 view of the ROM |

Derivation: `vram − rom = 0x800369B0` for every 128 KiB block of the second region, found by matching
`jal` targets against `addiu sp,sp,-N` prologues (`scratch/` has the spimdisasm listing of the boot segment).
The game-specific part lives in `California Speed (USA).z64.segments.json`; the loader reads it.

## Opening the ROM in Binary Ninja
A custom loader plugin is installed at `%APPDATA%\Binary Ninja\plugins\n64_rom_loader\__init__.py`.
1. Restart Binary Ninja so the plugin loads (check the Log pane for "N64 ROM loader").
2. File → Open → `California Speed (USA).z64`. The "N64 ROM" view type is picked automatically
   (if BN asks, choose **N64 ROM**, not Raw/Mapped). Keep the `.segments.json` next to the ROM.
3. Let analysis finish, then save as `California Speed (USA).bndb`.
4. Plugins → MCP Server → Start (so Claude can query the database via the `binary_ninja_mcp` server).
5. Good first checks: `entrypoint` at `0x80000400`; the function at `0x8000D024` touches `PI_STATUS_REG`
   (that is the PI DMA path, i.e. `osPiRawStartDma`/`__osPiGetAccess` territory).

The plugin maps segments, hardware-register symbols, the cartridge at `0xB0000000`, and the entry
point. It does **not** name libultra functions; that is the next job (signature matching).

## Repos cloned into this folder
- `N64Recomp/` — the recompiler itself (`N64Recomp.exe`) plus `RSPRecomp.exe` for RSP microcode.
- (moved to the repo root, see Project layout below) SrBananaMan's template for a new game recomp. Submodules:
  `lib/N64ModernRuntime`, `lib/rt64`, `lib/RecompFrontend`. Run
  `python configure_game.py "California Speed" --preview` then without `--preview`,
  and grep for `User Task:` to see what must be filled in.

## Build N64Recomp — DONE (toolchain verified 2026-10-14)
`build_n64recomp.bat` (repo root) loads the VS 2022 environment, puts LLVM and VS's Ninja on PATH, and runs
CMake + Ninja with clang/clang++. Re-run it after pulling updates:
```
cmd /c N64Recompuild.bat
```
Built outputs in `N64Recomp/build/`: `N64Recomp.exe`, `RSPRecomp.exe` (plus mod tools you won't need yet).
Copy the same recipe (vcvars64 + LLVM + Ninja on PATH) to build the game template later.

## The pipeline
1. **Symbols** — N64Recomp needs to know every function's name, VRAM address and size.
   Two ways to feed it:
   - `elf_path`: an ELF with symbols (what decomp-based recomps use). Requires a full
     disassembly that reassembles to an ELF — a lot of work on Windows.
   - `symbols_file_path` + `rom_file_path`: a TOML listing sections and functions. **Use this.**
     Format (from N64Recomp `from_symbol_file`):
     ```toml
     [[section]]
     name = "main"
     rom = 0x1000
     vram = 0x80000400
     size = 0x2BD60
     functions = [
       { name = "entrypoint", vram = 0x80000400, size = 0x50 },
       # ...
     ]
     ```
     Generate this file with a script from Binary Ninja's / Ghidra's / splat's function list.
2. **Run the recompiler**: `N64Recomp cspeed.us.toml` → `RecompiledFuncs/*.c`.
3. **Recompile the RSP audio microcode**: find the `aspMain` ucode in the ROM (text+data
   addresses), write an `aspMain.us.toml`, run `RSPRecomp`.
4. **Runtime glue**: the template's `src/` has the `User Task:` spots — ROM validation
   (use the MD5 above), save type (EEPROM/SRAM — check what the game uses), game-specific
   patches in `patches/`.
5. **Build** the template with CMake/Ninja, run, fix crashes one function at a time.

## The hard part (honest estimate)
Everything above is mechanical except step 1. Without a decomp you need:
- A reliable **function boundary list** for the main segment and any **overlays** the game
  DMA's in later (search the boot code / `osPiStartDma` callers for extra segments).
- **libultra function identification** so you can map `osRecvMesg`, `osViSwapBuffer`, etc.
  to ultramodern's implementations (the runtime matches them by *name*). Signature matching
  against a libultra 2.0I-era build (Ghidra FID / BinDiff / byte signatures) does most of this.
- Handling any **self-modifying or jump-table-heavy code**: N64Recomp needs jump tables to be
  resolvable; otherwise you add `manual_funcs` or instruction patches.

Realistic ordering: get `N64Recomp` to run cleanly over the main segment with auto-detected
functions first, then chase runtime crashes.

## Project layout (after the 2026-10-15 restructure)
The repo root is now the recomp project itself (SrBananaMan's N64RecompGameTemplate, configured for this game):

| Path | What |
|---|---|
| `src/`, `include/`, `patches/`, `assets/`, `icons/`, `CMakeLists.txt` | the game project (from the template) |
| `lib/N64ModernRuntime`, `lib/rt64`, `lib/RecompFrontend` | runtime, renderer, UI (submodules pinned to the template's commits) |
| `N64Recomp/` | the recompiler (submodule); `build_n64recomp.bat` builds it |
| `cspeed.us.toml` | N64Recomp config (symbol-file mode) |
| `cspeed.us.syms.toml`, `cspeed.us.datasyms.toml` | generated symbol files (do not edit; run `tools/gen_syms.py`) |
| `aspMain.us.toml` | RSPRecomp config for the audio microcode, output `rsp/aspMain.cpp` |
| `RecompiledFuncs/` | N64Recomp output (generated, ignored by git) |
| `symbols/functions.json` | hand-curated function names (source of truth) |
| `tools/` | analysis and generation scripts (see below) |
| `build.bat` | configure/build the game executable (VS env + LLVM clang-cl + Ninja) |

## The pipeline, as it exists now
```
python tools/gen_syms.py          # ROM + Binary Ninja seeds + names -> cspeed.us.syms.toml / datasyms
python tools/check_syms.py        # validate like N64Recomp would (branches, jal targets, unsupported insns)
N64Recomp/build/N64Recomp.exe cspeed.us.toml      # -> RecompiledFuncs/*.c   (1522 functions, exit 0)
N64Recomp/build/RSPRecomp.exe aspMain.us.toml     # -> rsp/aspMain.cpp
build.bat                          # cmake configure + build -> build/CaliforniaSpeedRecompiled.exe
```
`tools/funcbounds.py` is the heart of it: a MIPS control-flow walker that turns Binary Ninja's
function starts (dumped to `scratch/bn_funcs.json` via the MCP HTTP API) into exact function
extents, resolves jump tables, merges fragments BN split off, and classifies data-misread-as-code
as junk. Its docstring explains the heuristics. `tools/n64feat.py` extracts per-function features
(used for libultra identification). `tools/bn_apply_names.py` pushes `symbols/functions.json` into
the open Binary Ninja database (HTTP API on port 9009; renames are POST).

Refreshing BN seeds after re-analysis in Binary Ninja:
```
curl -s "http://127.0.0.1:9009/methods?limit=100000" | python -c "import sys,json; json.dump(json.load(sys.stdin)['functions'], open('scratch/bn_funcs.json','w'))"
```

## Known facts about the game's RSP tasks and saves
- Graphics task (set up at `0x80055168`): rspboot `0x80015330`, F3DEX.NoN fifo 2.04 text `0x80016220`, data `0x8001B400`. Handled by RT64.
- Audio task (set up at `0x800577C0`): aspMain text `0x80015400` (ROM `0x16000`, `0xE20` bytes), data `0x8001B140`; dispatch table at data+0x10 (16 commands). See `aspMain.us.toml`.
- Saves: Controller Pak (osPfs*) only; no EEPROM/SRAM/Flash commands exist in the ROM, so the game entry uses `SaveType::None`.
  The runtime has no Controller Pak support, so `src/game/controller_pak.cpp` emulates it below libultra: the game's own
  osPfs* code is recompiled (renamed `game_*` through `symbols/recomp_names.json`) and only `__osContRamRead/Write`,
  `__osPfsGetStatus` and `osPfsIsPlug` are native. Paks are stored as `.mpk` images next to the runtime save file.
- Every emulated pak is also a Rumble Pak: a hook after the game's pak probe (`func_8005D1C8`) sets the rumble bitmask
  (`0x800E1B81`) and the motor OSPfs (`0x800E1D38 + n*0x68`), so no Controller Pak/Rumble Pak swapping is needed.

## Status and known issues (2026-10-15)
- The whole pipeline runs: symbols -> N64Recomp -> RSPRecomp -> CMake build of `CaliforniaSpeedRecompiled.exe`
  (see the build notes below for what was needed to get there).
- **No MIPS compiler on this machine.** The LLVM 21 in `%LLVM_DIR%` was built without the Mips
  target, so `patches/` cannot be compiled yet. `patches/Makefile` detects this and writes an empty
  `patches.elf` (via `tools/make_empty_patches_elf.py`), which keeps the build working with zero patches.
  Neither the winget LLVM nor VS's clang has it. A mips-elf GCC toolchain is installed in Cygwin
  (`%MIPS_TOOLCHAIN_BIN%`); wiring it into `patches/Makefile` is tracked in `TODO.md`.
- `osMotorStop`/`osMotorStart` are 0x8000C7A0 / 0x8000C960 (`osMotorInit` fills the buffer the latter sends with 1s).
- `osViSetMode`/`osViSwapBuffer` are 0x80009B80 / 0x80009D40; having them swapped meant nothing was ever displayed.
- Open items and ideas live in `TODO.md`.
- `__osExceptionPreamble` (0x8000E170) is split by two bogus seeds; harmless (N64Recomp ignores it).
- Binary Ninja's function list contains ~1859 seeds; after cleanup 1409 functions remain
  (1226 reachable from the entry point). Unreachable-but-kept functions are dead code or pointer-called.

## Build notes (Windows)
- Use `build.bat` (VS 2022 env + `clang-cl` from `%LLVM_DIR%` + VS Ninja + GnuWin32 make).
  The template expects the `clang_cl_x64` toolset; plain `clang` fails on MSVC-style flags.
- `__PRFCHWINTRIN_H` is pre-defined for clang-cl because RT64's bundled SDL2 2.26 headers define
  `_m_prefetch`, which newer clang treats as a builtin.
- `rsp/aspMain.cpp` is compiled with `/clang:-msse4.1 /clang:-mssse3` (librecomp's RSP vector unit uses them).
- CMake runs `N64Recomp` from the repo root for the patch step: copy `N64Recomp/build/N64Recomp.exe`
  and `RSPRecomp.exe` to the root (both are git-ignored).
- `src/game/recomp_api.cpp` provides `osPiRawReadIo_recomp`, which the game's `boot()` calls directly
  and the runtime does not implement.

## Running it
```
build.bat                         # builds build/CaliforniaSpeedRecompiled.exe (and copies assets/ next to it)
build\CaliforniaSpeedRecompiled.exe
```
The launcher (RecompFrontend) opens; choose "Select ROM" and pick `California Speed (USA).z64`
(the runtime checks its XXH3 hash against `src/main/main.cpp`), then start the game.
Settings and saves live under `%LOCALAPPDATA%/CaliforniaSpeedRecompiled`.
Expect crashes or glitches on first runs: nothing game-specific has been debugged yet. When it
fails, the usual loop is: find the recompiled function in `RecompiledFuncs/` (names come from
`symbols/functions.json`), inspect it in Binary Ninja, and either fix the symbol list or add a patch.

## References
- https://github.com/N64Recomp/N64Recomp
- https://github.com/N64Recomp/N64ModernRuntime
- https://github.com/Zelda64Recomp/Zelda64Recomp (reference project + BUILDING.md)
- https://github.com/SrBananaMan/N64RecompGameTemplate
- https://github.com/ethteck/splat

## Tracked copy of the Binary Ninja loader
`tools/binaryninja/n64_rom_loader.py` is the source of truth for the loader plugin. The installed copy is
`%APPDATA%\Binary Ninja\plugins\n64_rom_loader\__init__.py`; after editing the tracked file, copy it over
(or make the plugin folder a junction to `tools/binaryninja`) and reload Python plugins in Binary Ninja.
