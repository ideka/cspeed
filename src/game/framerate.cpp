// Frame rate.
//
// The game times its logic from the number of retraces since the previous frame (its graphics
// thread, func_8005434C, turns that count into seconds every frame), so it runs correctly at any
// rate. What held it at 30 FPS is its copy of the libultra scheduler, which only swaps buffers once
// at least 2 retraces have passed since the last swap: when a frame's task completes
// (__scTaskComplete), or else later from the retrace handler (__scHandleRetrace). Both checks are
// hooked (cspeed.us.toml) to use MIN_RETRACES_BETWEEN_SWAPS instead, so frames that are ready in
// time are shown at 60 FPS.

#include "recomp.h"

namespace {

constexpr int32_t MIN_RETRACES_BETWEEN_SWAPS = 1;
constexpr gpr SCHED_LAST_SWAP_FRAME = (gpr)(int32_t)0x800013FC;  // retrace count at the last swap
constexpr int32_t SCHED_FRAME_COUNT = 0x27C;                     // OSSched: retrace count

} // namespace

// Called where the scheduler checks whether a swap would come too soon ($s1 = the OSSched); the
// result replaces the game's own check ($v0 = retraces since the last swap < 2).
extern "C" int cspeed_swap_too_soon(uint8_t* rdram, recomp_context* ctx) {
    uint32_t retraces = (uint32_t)(MEM_W(SCHED_FRAME_COUNT, ctx->r17) - MEM_W(0, SCHED_LAST_SWAP_FRAME));
    return retraces < (uint32_t)MIN_RETRACES_BETWEEN_SWAPS;
}
