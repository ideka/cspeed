// World scale (draw distance experiment).
//
// The game draws everything relative to the camera in N64 units of 1/16 world unit, in 16-bit fixed
// point (+-32768 units): model matrices (func_800CA90C, func_800CABEC) hold rotation x 65536 and the
// object's offset from the camera (or, for child nodes, from their parent) x 16 x 65536, and leave out anything more than 2048 world units from
// the camera on an axis; terrain pieces (func_800CE61C) are written as camera-relative vertices the same
// way. Drawing at a smaller scale (all of those, and the projection's near and far planes) looks the
// same and reaches further: at 1/4, 8192 world units. Hooked in cspeed.us.toml.

#include <cstring>

#include "recomp.h"

namespace {

float world_scale = 1.0f;
float matrix_scale = 1.0f;  // for the model matrix being built

float as_float(gpr reg) {
    uint32_t bits = (uint32_t)reg;
    float f;
    memcpy(&f, &bits, sizeof(f));
    return f;
}

gpr as_gpr(float f) {
    uint32_t bits;
    memcpy(&bits, &f, sizeof(bits));
    return (gpr)(int32_t)bits;
}

} // namespace

extern "C" float cspeed_world_scale() {
    return world_scale;
}

// At the start of a model matrix build: matrices relative to a parent node (wheels, some billboards)
// are not scaled, since they are multiplied onto the parent's, which already is.
extern "C" void cspeed_matrix_begin(int relative_to_parent) {
    matrix_scale = relative_to_parent ? 1.0f : world_scale;
}

extern "C" float cspeed_matrix_scale() {
    return matrix_scale;
}

extern "C" void cspeed_set_world_scale(float scale) {
    world_scale = scale;
}

// Before guOrtho in func_800CF0B4 (orthographic cameras): scale the view's left, right, bottom
// ($a1-$a3) and top (stack) like the rest. Near and far are scaled where they are computed.
extern "C" void cspeed_world_scale_ortho(uint8_t *rdram, recomp_context *ctx) {
    if (world_scale == 1.0f) {
        return;
    }
    ctx->r5 = as_gpr(as_float(ctx->r5) * world_scale);
    ctx->r6 = as_gpr(as_float(ctx->r6) * world_scale);
    ctx->r7 = as_gpr(as_float(ctx->r7) * world_scale);
    MEM_W(0x10, ctx->r29) = (int32_t)as_gpr(as_float(MEM_W(0x10, ctx->r29)) * world_scale);
}
