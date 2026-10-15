# TODO

Known problems and wanted features that are not being worked on right now, with what is known so
far. Newest findings go into the item they belong to.

## Graphics

### Frame interpolation jitter (high framerate only)
- **Speedometer needle** shakes left-right while turning.
- **Tire tracks / skid marks** on the ground show the same shaking.
- Neither happens at the original framerate, so the game is fine; RT64's interpolation is pairing
  the wrong matrices or vertices between frames for these objects. The usual fix is a patch that
  tags their matrices (extended GBI `gEXMatrixGroup*`) or marks them as non-interpolated.
- Needs: MIPS patch toolchain (see below), then find where the needle and the skid marks are drawn.

### Midway logo flashes before animating in
- On the startup screen, the Midway word briefly appears off to the side for a moment before its
  animation starts. Cosmetic. Not yet checked whether it happens at the original framerate (if not,
  it is the same interpolation issue as above: the first frame of the animation is interpolated
  from a stale position).

### Widescreen ("Expand" aspect ratio) and HUD placement
- "Expand" only draws extra bars on the sides; the camera view is not widened.
- The HUD placement option has no effect.
- Both need game patches: widen the projection and the view/scissor for the 3D scene, and tag the
  HUD's 2D drawing with extended GBI viewport/alignment commands so RT64 knows what to move.

## Gameplay

### More precise steering
- Steering feels coarse with a modern analog stick. Things to look at: the runtime's stick conversion
  (`convert_to_n64_range` in ultramodern `input.cpp` maps to the N64's octagonal +-82 range), any
  deadzone/response curve the game applies on top, and whether the game quantizes the stick value
  before using it. A configurable response curve or deadzone in the recomp could help.

### Load the first Controller Pak profile on startup
- Right now the player has to pick LOAD on the 1 Player screen every session. Automatically load the
  first profile on the pak (player slot `0x8016D172` is -1 when none is loaded) once the startup pak
  probe has found it. Needs the game's load-profile routine identified (called from the LOAD menu).

## UI

### Controls menu lag (could not reproduce)
- The recomp controls menu (not the in-game one) lagged badly when moving between options, once, while
  an attract-mode demo was playing in the background. Did not happen again.

## Infrastructure

### MIPS patch toolchain
- A mips-elf GCC 16.2 + binutils 2.47 toolchain is installed in Cygwin at
  `%MIPS_TOOLCHAIN_BIN%` (needs `%MIPS_TOOLCHAIN_DLL_DIR%` on PATH for the Cygwin DLLs).
  Verified: `mips-elf-gcc -march=vr4300 -mabi=32 -mfix4300 -G0 -mno-abicalls -fno-pic -O2` builds
  correct VR4300 code.
- Still to do: point `patches/Makefile` at it (it currently expects clang/ld.lld and falls back to an
  empty `patches.elf`), then build a first real patch. This unblocks the graphics items above.

### Submodule patches
- `tools/runtime_patches/` holds fixes applied to submodules by hand (`git -C lib/<module> apply ...`):
  0001 (N64ModernRuntime: osStopThread on other threads), 0002 (RecompFrontend: F11/Alt+Enter fullscreen
  toggle is saved). They should become forks or upstream PRs.
