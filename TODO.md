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
- Next: find where the needle and the skid marks are drawn (the needle is a 3D object in the main
  scene, not part of the 2D HUD list) and tag them; `src/game/graphics.cpp` already enables the
  extended GBI for every frame.

### Midway logo flashes before animating in
- On the startup screen, the Midway word briefly appears off to the side for a moment before its
  animation starts. Does not happen at the original framerate, so it is the same interpolation issue
  as above (the first frame of the animation is interpolated from a stale position).

### Widescreen: remaining gaps
- Races widen and the 2D HUD is anchored to the screen edges (`src/game/graphics.cpp`). The
  speedometer **needle** is a red quad (4 CPU-computed vertices, prim colour `C80000FF`) drawn with
  the race camera right after the scene, so it stays at its 4:3 position while the dial moves to the
  right edge. Re-applying the camera viewport with a right origin around it works visually
  (`anchor_needle`, disabled by `ANCHOR_NEEDLE`), but the extra mid-frame viewport changes add RT64
  projections and break frame interpolation (other racers and wheels blink). Options: move the
  needle's vertices instead (needs its coordinate space: probably world space with an identity
  model matrix and a combined projection*lookat), or find a way to tag it that RT64 interpolation
  tolerates (e.g. a matrix group for that projection).
- Menus stay 4:3 on purpose (4:3 background art; 3D objects slide in from outside the 4:3 area).
- Objects may pop in/out at the widened screen edges if the game culls against a 4:3 frustum; not
  seen yet.
- Split-screen (2 players) has not been checked in widescreen.

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

### Submodule patches
- `lib/N64ModernRuntime` tracks the `cspeed` branch of the
  [ideka/N64ModernRuntime](https://github.com/ideka/N64ModernRuntime) fork (osStopThread on other
  threads). Upstream does not accept AI-assisted contributions, so it stays a fork.
- `tools/runtime_patches/0002-*.patch` (RecompFrontend: F11/Alt+Enter fullscreen toggle is saved) is
  still applied by hand (`git -C lib/RecompFrontend apply ../../tools/runtime_patches/0002-*.patch`);
  move it to a RecompFrontend fork (upstream does not accept AI-assisted contributions).
