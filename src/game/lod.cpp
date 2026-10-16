// Level of detail.
//
// Every model is drawn by func_800CD8E4. Its descriptor has a level count followed by 12-byte levels
// (+8 the level's max distance, +0xC its display list), level 0 being the most detailed. With the
// camera distance and the object's distance scale, the game hides the model beyond the last level's
// distance and otherwise picks the farthest level the object is within. Both are hooked
// (cspeed.us.toml) to always draw level 0 when the option is on. Objects that force a level
// themselves (object flag 4) keep it.

#include <cmath>

#include "recomp.h"
#include "cspeed_config.h"

namespace {

constexpr uint32_t OBJECT_FORCED_LEVEL = 4;  // object flags (+0): use the level in the low 2 bits

} // namespace

// Called before the draw-distance check ($f0 = the last level's distance * scale, compared with the
// camera distance in $f2).
extern "C" int cspeed_debug_no_terrain_culling();

extern "C" void cspeed_lod_cull(uint8_t* rdram, recomp_context* ctx) {
    if (cspeed::max_level_of_detail() || cspeed_debug_no_terrain_culling()) {
        ctx->f0.fl = INFINITY;
    }
}

// Called once the level is picked ($s1 = level, $v0 = level * 3, $s3 = descriptor, $s0 = object).
extern "C" void cspeed_lod_level(uint8_t* rdram, recomp_context* ctx) {
    if (!cspeed::max_level_of_detail() || (MEM_W(0, ctx->r16) & OBJECT_FORCED_LEVEL) != 0) {
        return;
    }
    if (MEM_W(0xC, ctx->r19) == 0) {  // level 0 has no display list
        return;
    }
    ctx->r17 = 0;
    ctx->r2 = 0;
}
