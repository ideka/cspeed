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
- `N64RecompGameTemplate/` — SrBananaMan's template for a new game recomp. Submodules:
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
