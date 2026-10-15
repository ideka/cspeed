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

## UI

### Controls menu lag (could not reproduce)
- The recomp controls menu (not the in-game one) lagged badly when moving between options, once, while
  an attract-mode demo was playing in the background. Did not happen again.

## Infrastructure

### MIPS patch toolchain
- `patches/` needs a clang + lld with the Mips target. Neither the installed LLVM 21 nor VS's clang
  has it, Cygwin has no MIPS cross compiler, and WSL is not installed.
- Plan: `pip install ziglang` (bundles clang and lld with all targets) and point `patches/Makefile`
  at `python -m ziglang cc` / `python -m ziglang ld.lld`.

### Submodule patches
- `tools/runtime_patches/` holds fixes applied to submodules by hand (`git -C lib/<module> apply ...`):
  0001 (N64ModernRuntime: osStopThread on other threads), 0002 (RecompFrontend: F11/Alt+Enter fullscreen
  toggle is saved). They should become forks or upstream PRs.
