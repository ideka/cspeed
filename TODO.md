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
- Races widen; the 2D HUD, the speedometer needle and full-width overlays are anchored or stretched
  (`src/game/graphics.cpp`). A race is recognised by the needle being drawn (game mode 1 also covers
  the race setup menus).
- Watch for interpolation side effects of the needle's extra viewport commands (one report of other
  racers blinking at high framerates, not reproduced since).
- Menus stay 4:3 on purpose (4:3 background art; 3D objects slide in from outside the 4:3 area).
- Objects may pop in/out at the widened screen edges if the game culls against a 4:3 frustum; not
  seen yet.
- Split-screen (2 players) has not been checked in widescreen.

## Stability

### Hang or crash after backing out of track select (RT64)
- Race -> Practice -> track select (Monterey) -> Back: the menu slides out and never returns; quitting
  then hangs. Timing dependent (reproduced most runs with CSPEED_INPUT_SCRIPT="10:S,12:S,16:A,20:A,24:D,26:A,31:B").
- The game is fine; frames stop because RT64 never finishes the frame: the runtime's gfx thread spins
  in `PresentQueue::advanceToNextPresent` (lib/rt64/src/hle/rt64_present_queue.cpp) waiting for
  the present barrier, while RT64's present thread waits for new writes. Some runs instead crash with
  heap corruption (c0000374) while RT64 frees its interpolation map (`GameFrameMap::clear` in
  `GameFrame::set`), so something corrupts RT64's state first.
- Ruled out: widescreen code, the per-frame extended GBI prologue, frame interpolation (refresh rate
  Original), presentation mode (SkipBuffering), and the ROM lock change (original try-lock crashes
  here too).
- Next: run RT64 with debug checks (heap verification / ASan) to find the corruption; compare with
  upstream RT64 (24 commits newer, none touch the present queue).

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
