# TODO

Known problems and wanted features that are not being worked on right now, with what is known so
far. Newest findings go into the item they belong to.

## Graphics

### Frame interpolation jitter (above 60 FPS only)
- The game now runs at 60 FPS itself (`src/game/framerate.cpp`), so RT64's frame interpolation is
  only used with a refresh rate above 60. The reports below were made at 30 FPS + interpolation;
  recheck them above 60.
- **Speedometer needle** shakes left-right while turning.
- **Tire tracks / skid marks** on the ground show the same shaking.
- Neither happens without interpolation, so the game is fine; RT64's interpolation is pairing
  the wrong matrices or vertices between frames for these objects. The usual fix is a patch that
  tags their matrices (extended GBI `gEXMatrixGroup*`) or marks them as non-interpolated.
- Next: find where the needle and the skid marks are drawn (the needle is a 3D object in the main
  scene, not part of the 2D HUD list) and tag them; `src/game/graphics.cpp` already enables the
  extended GBI for every frame.

### 60 FPS: things to check
- The game logic is timed by retraces, and a race clock checked against real time runs at the right
  speed at 60 FPS. Not yet checked: menus and attract-mode animations, AI and physics feel over a
  whole race, split-screen, and whether some effects are tied to frames rather than time.
- Frames that take longer than one retrace to produce simply fall back to 30 for that frame (the
  game measures the elapsed retraces), so slow moments should not change the game speed.
- Possibly make the frame rate an option (original 30 / 60).

### Midway logo flashes before animating in
- On the startup screen, the Midway word briefly appears off to the side for a moment before its
  animation starts. Does not happen at the original framerate, so it is the same interpolation issue
  as above (the first frame of the animation is interpolated from a stale position). Recheck above
  60 FPS now that the game itself runs at 60.

### Widescreen: remaining gaps
- The race view (countdown, race, race over, results) widens; during the race the 2D HUD, the
  speedometer needle and full-width overlays are anchored or stretched (`src/game/graphics.cpp`).
  The race view is recognised by the game state at `0x8016D928` (3..10, in game mode 1); the race
  HUD by the needle being drawn.
- Watch for interpolation side effects of the needle's extra viewport commands (one report of other
  racers blinking at high framerates, not reproduced since).
- Menus stay 4:3 on purpose (4:3 background art; 3D objects slide in from outside the 4:3 area).
- Objects may pop in/out at the widened screen edges if the game culls against a 4:3 frustum; not
  seen yet.
- Only Monterey has been checked closely; other tracks (sky dome fix, border removal) and
  split-screen (2 players) have not been checked in widescreen.

### Remove level of detail (LoD)
- Distant objects and scenery switch to lower-detail models or pop in at a set distance. At modern
  resolutions this is very visible; look into forcing the highest detail model and raising or removing
  the draw distance cutoff. Needs the game's LoD selection and culling code identified.

## Audio

### Improve music quality
- Explore whether the music can sound better than the original output: e.g. a higher output sample
  rate, better resampling/interpolation in the audio path, or the game's own audio settings (sample
  rate, voice count) set to higher values. Start by identifying the music player (see
  `musicLoaderThread` / `musicLoadRequest` in `symbols/functions.json`) and the audio format it uses.

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

## Rendering limits

### Huge render targets crash
- With high resolution settings plus 4x downsampling ("supersampling") quality, resizing the window to
  extreme sizes can request absurdly large render targets and crash. RT64 does not cap them; a sanity
  limit on the internal resolution would avoid it. The window now has a 320x240 minimum size, which
  covers the flat-window crash during races.

## UI

### Controls menu lag (could not reproduce)
- The recomp controls menu (not the in-game one) lagged badly when moving between options, once, while
  an attract-mode demo was playing in the background. Did not happen again.
