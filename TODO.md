# TODO

Known problems and wanted features that are not being worked on right now, with what is known so
far. Newest findings go into the item they belong to.

## Graphics

### Frame interpolation glitches (refresh rates above 60 only)
- Races run at 60 FPS natively and menus at 30 (`src/game/framerate.cpp`), with RT64's frame
  interpolation off at the original refresh rate. These glitches were seen with interpolation (30 FPS
  game) and are gone at native 60; they may still show with a refresh rate above 60:
  - **Speedometer needle** and **tire tracks / skid marks** shaking while turning.
  - The **Midway logo** briefly appearing off to the side before its startup animation. The logo
    screen (game mode 15) now declares a 1000 Hz refresh rate so RT64 skips interpolating it
    (`src/game/graphics.cpp`); not yet
    confirmed. Other 30 FPS menus still interpolate above 30.
- Cause: RT64's interpolation pairs the wrong matrices or vertices between frames for these objects.
  The usual fix is tagging their matrices (extended GBI `gEXMatrixGroup*`) or marking them as
  non-interpolated; `src/game/graphics.cpp` already enables the extended GBI for every frame.

### 60 FPS: things to check
- Races only (game state `0x8016D928` 3..10): the race logic is timed by retraces, and the race
  clock matches real time at 60 FPS. Menu animations advance per frame, so menus stay at 30.
- Not yet checked: AI and physics feel over a whole race, split-screen, attract-mode demo races (they
  are not in game mode 1, so they run at 30), and effects that may be tied to frames.
- Possibly make the frame rate an option (original 30 / 60).

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
