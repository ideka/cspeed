// Widescreen support.
//
// RT64 widens a 3D scene on its own when the "Expand" aspect ratio is selected, but only for draws
// whose viewport and scissor together cover the full framebuffer width. California Speed draws the
// 3D view with a full-screen viewport and a scissor inset by 12 pixels on each side (a black border),
// so the scene stayed 4:3 while the full-width background clear was stretched into grey side bars.

#include "recomp.h"
#include "librecomp/helpers.hpp"
#include "ultramodern/config.hpp"

namespace {

bool expand_enabled() {
    return ultramodern::renderer::get_graphics_config().ar_option == ultramodern::renderer::AspectRatio::Expand;
}

} // namespace

// Hook in the camera setup (func_800CF0B4) right after it writes G_SETSCISSOR into the display list
// ($t0 = display list position: scissor words at +0x10/+0x14; $s1 = camera index). When the camera's
// viewport spans the whole screen and the scissor is centred in it, open the scissor horizontally to
// the full width so RT64 expands the scene. Split-screen viewports are left untouched.
extern "C" void cspeed_widescreen_camera_scissor(uint8_t* rdram, recomp_context* ctx) {
    if (!expand_enabled()) {
        return;
    }
    gpr dl = ctx->r8;
    int camera = (int)ctx->r17;
    gpr vp = (gpr)(int32_t)MEM_W(0, (gpr)(int32_t)0x80022FB0) + camera * 16;
    int32_t scale_x = MEM_H(0, vp);    // quarter pixels
    int32_t trans_x = MEM_H(8, vp);
    if (scale_x != trans_x) {
        return;                         // not a full-width viewport
    }
    int32_t width = trans_x * 2;        // framebuffer width, 10.2 fixed point
    uint32_t ul = (uint32_t)MEM_W(0x10, dl);
    uint32_t lr = (uint32_t)MEM_W(0x14, dl);
    int32_t ulx = (ul >> 12) & 0xFFF;
    int32_t lrx = (lr >> 12) & 0xFFF;
    if (ulx + lrx != width || lrx - ulx < width / 2) {
        return;                         // not a centred, mostly full-width scissor
    }
    MEM_W(0x10, dl) = (int32_t)(ul & ~(0xFFFu << 12));                       // ulx = 0
    MEM_W(0x14, dl) = (int32_t)((lr & ~(0xFFFu << 12)) | ((uint32_t)width << 12)); // lrx = width
}
