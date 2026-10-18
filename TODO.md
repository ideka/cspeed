# TODO

Known problems and wanted features that are not being worked on right now, with what is known so
far. Newest findings go into the item they belong to.

## Graphics

### Frame interpolation glitches (refresh rates above 60 only)
- Races run at 60 FPS natively and menus at 30 (`src/game/framerate.cpp`), with RT64's frame
  interpolation off at the original refresh rate. These glitches were seen with interpolation (30 FPS
  game) and are gone at native 60; they may still show with a refresh rate above 60:
  - **Speedometer needle** and **tire tracks / skid marks** shaking while turning.
  - The **Midway word** flashing for about a frame at startup, in the intro after the copyright
    screen (game modes 18 then 17): after the red logo assembles and before the word's horizontal
    reveal, part of the word shows in pale yellow far to the right (outside the 4:3 area, so only in
    widescreen). Captured by slowing the VI rate to 12 Hz (a local change to the runtime's VI
    thread) with a 16:9 window. The game's display lists draw nothing there (no rectangles past
    x=320, only the usual two framebuffers, normal scissors), so RT64 is presenting something else
    for that frame. Declaring a 1000 Hz refresh rate for those frames (no in-between frames) made it
    worse: the finished logo + word then blinked in during the assembly, i.e. RT64 sometimes
    presents a framebuffer the game is not showing. Look at how the present queue picks the image
    (`PresentQueue::threadPresent`, present-early / interpolation target selection).
- Cause: RT64's interpolation pairs the wrong matrices or vertices between frames for these objects.
  The usual fix is tagging their matrices (extended GBI `gEXMatrixGroup*`) or marking them as
  non-interpolated; `src/game/graphics.cpp` already enables the extended GBI for every frame.

### Clouds missing on the eyecatch screen
- Reported: the clouds do not show on the eyecatch (attract) screen; in races they are fine. Not
  investigated yet. The sky dome fix in `src/game/graphics.cpp` (`show_sky_dome` lowers the dome's
  depth) only runs in the race view, and the eyecatch is not race view, so it likely has the same
  problem the races had before that fix: extend the fix to the eyecatch, or check the 60 FPS / LoD
  changes by comparing against a build from before them.

### Black bars in the credits scene
- The credits scene shows black bars (presumably the game's overscan border or a 4:3 frame in
  widescreen); remove them like the race view does (`remove_borders` in `src/game/graphics.cpp`).
  Not investigated yet.

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

### Extended draw distance (enhancement)
- The game draws relative to the camera in 1/16 world units in 16-bit fixed point, so models more than
  2048 units from the camera on an axis are not drawn. The debug tools' F7 draws at 1/4 scale
  (`src/game/draw_distance.cpp`: model matrices and their range checks, terrain pieces, projection near
  and far), which reaches 8192 units and looks the same. With F3 (culling off), F5 (fog off) and F6 (far
  plane 20000) the whole track is visible.
- Child nodes (wheels, some billboards) get matrices relative to their parent, which are not scaled
  (the parent's already is): that was the vanishing wheels and the small, misplaced bush billboards.
  Every other x16 site found draws under a scaled matrix or as terrain (bounding boxes for G_CULLDL,
  particles and skid marks), or is not a position (timers, setup, fog and sky colours).
- As an option ("Draw Distance: Extended"): 1/4 scale, far plane and draw distance pushed out, the
  distance culls off, and the fog moved further out (scaled with the far plane) instead of removed.

### Arcade textures: remaining work
- The mapping (`assets/arcade_textures.json`, 782 textures) is built offline from the arcade data with
  the scripts in the (uncommitted) `California-Speed_Arcade_EN/tools`: dump N64 textures
  (`RT64_DUMP_TEXTURES`, `dump_textures.ps1`), decode (`n64tex.py`), match (`match.py`, `match2.py`),
  review (`review_page.py`), build (`build_mapping.py`, which also refines crops and picks key colours,
  mattes and opacities), and check transparency against the N64 textures (`alpha_check.py`).
  `combined.py prepare` / `build` runs it all over every dump so far (the all-races dump and the 16
  per-track ones, 4054 textures), keeping every earlier review verdict, so only new textures are
  reviewed (one page with both passes' candidates). Transparency: 759 of 782 match the N64 textures
  (the rest are close: the N64 transparency is coarser).
- Car paint: 79 palettized images (1857 hashes) differ only by palette (the car colour), and the RT64
  hash includes the palette, so they are not replaced. Options: one recoloured replacement per dumped
  colour (colours never dumped stay N64), or palette-independent matching with tinting in RT64.
- HUD and menu art is split into thin strips (4 KB of TMEM): stack the strips of the same source image
  (by RDRAM address) before matching, then replace each strip with its slice.
- Not every track was played when dumping (some are locked): textures only seen there are missing.
- Wrong matches show up in game: add their hashes to `rejected.json`.
- Cutout edges (not fully fixed): trees still show a thin transparent gap along their bottom edge
  (floating above the ground), and roofs had a line along their top. Both come from filtering across the
  edge of a wrapping texture, which blends in its opposite side. Cutouts are packed without RT64's half
  texel shift and discarded under half opacity (`RasterPS.hlsl`, `ReplacementTexture::cutout`), which
  narrowed it but did not remove it. Next idea: decide the cutout from the nearest texel (point-sampled
  alpha) while the colour stays filtered, so the opposite edge never counts.

## Gameplay

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
