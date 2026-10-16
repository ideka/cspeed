// Frame rate.
//
// The game times its logic from the number of retraces since the previous frame (its graphics
// thread, func_8005434C, turns that count into seconds every frame), so it runs correctly at any
// rate. What held it at 30 FPS is its copy of the libultra scheduler, which only swaps buffers once
// at least 2 retraces have passed since the last swap: when a frame's task completes
// (__scTaskComplete), or else later from the retrace handler (__scHandleRetrace). Both checks are
// hooked (cspeed.us.toml) to allow a swap every retrace during races, so frames that are ready in
// time are shown at 60 FPS. Menus keep 30: their animations advance per frame, not per retrace. So does
// the name entry for a new record on the results screen (its cursor moves per frame).

#include "recomp.h"

namespace {

constexpr gpr GAME_MODE = (gpr)(int32_t)0x80141E00;    // 1 = race and race setup menus
constexpr gpr GAME_STATE = (gpr)(int32_t)0x8016D928;   // 3..10 = countdown to results (see graphics.cpp)
constexpr gpr SCHED_LAST_SWAP_FRAME = (gpr)(int32_t)0x800013FC;  // retrace count at the last swap
constexpr int32_t SCHED_FRAME_COUNT = 0x27C;                     // OSSched: retrace count
constexpr int32_t STATE_RESULTS = 6;
// Set on the results screen while player 1/2 enters a name for a new record (func_80096C38).
constexpr gpr NAME_ENTRY_P1 = (gpr)(int32_t)0x800E47B8;
constexpr gpr NAME_ENTRY_P2 = (gpr)(int32_t)0x800E47BC;

int min_retraces_between_swaps(uint8_t* rdram) {
    int32_t state = MEM_W(0, GAME_STATE);
    bool race_view = MEM_W(0, GAME_MODE) == 1 && state >= 3 && state <= 10;
    bool name_entry = state == STATE_RESULTS && (MEM_W(0, NAME_ENTRY_P1) != 0 || MEM_W(0, NAME_ENTRY_P2) != 0);
    return race_view && !name_entry ? 1 : 2;
}

} // namespace

// Called where the scheduler checks whether a swap would come too soon ($s1 = the OSSched); the
// result replaces the game's own check ($v0 = retraces since the last swap < 2).
extern "C" int cspeed_swap_too_soon(uint8_t* rdram, recomp_context* ctx) {
    uint32_t retraces = (uint32_t)(MEM_W(SCHED_FRAME_COUNT, ctx->r17) - MEM_W(0, SCHED_LAST_SWAP_FRAME));
    return retraces < (uint32_t)min_retraces_between_swaps(rdram);
}
