// Smooth radar (enhancement): the radar's car dots move in output pixels instead of whole 320x240 pixels.
//
// The radar (func_8009834C, one call per dot) turns a car's position relative to the player into a
// dot position on the 320x240 screen, x = 0.25 * right + 32 and y = -0.129 * forward + 192, and
// truncates both to whole pixels (the HUD object's s16 x at +0xE, y at +0x10), so the dots jump a
// whole low-resolution pixel at a time. The hooks keep the fraction the truncation drops, per HUD
// element (the object's element index is at +0x34), and when the HUD drawer (func_800CFB78) draws
// that element, move its rectangles by the fraction in quarter pixels, the finest step rectangles have
// (10.2 fixed point). RT64 samples unscaled 2D sprites on the original 320x240 grid (its "Upscale 2D"
// option), which would keep the dot's texels on that grid however its rectangle moves, so the dots
// are drawn with gEXForceUpscale2D, which our RT64 also draws at their exact, sub-pixel position.

#include <array>
#include <cmath>
#include <vector>

#include "recomp.h"
#include "cspeed_config.h"

namespace {

constexpr uint32_t HUD_ELEMENTS = 0x800F6110;            // the HUD's drawn elements, 0x20 bytes each
constexpr uint32_t HUD_ELEMENT_SIZE = 0x20;
constexpr size_t HUD_ELEMENT_COUNT = 180;
constexpr gpr HUD_DL_HEAD = (gpr)(int32_t)0x80141570;   // the 2D display list's write position
constexpr gpr HUD_DL_INDEX = (gpr)(int32_t)0x800F8818;  // which of the two 2D lists is being built
constexpr uint32_t HUD_DL_BASE = 0x80142A50;
constexpr uint32_t HUD_DL_SIZE = 0x3E80;
constexpr uint32_t HUD_DL_RESERVE = 0x400;              // room left for the game's own commands
constexpr uint32_t RT64_FORCE_UPSCALE_2D = (0x64u << 24) | 0x00000E;   // gEXForceUpscale2D
constexpr uint32_t RT64_PUSH_OTHER_MODE = (0x64u << 24) | 0x000019;    // gEXPushOtherMode
constexpr uint32_t RT64_POP_OTHER_MODE = (0x64u << 24) | 0x00001A;     // gEXPopOtherMode
constexpr uint32_t G_SETOTHERMODE_H_TEXTFILT = 0xE3001201;            // gDPSetTextureFilter (F3DEX2)
constexpr uint32_t G_TF_POINT = 0;

struct Fraction {
    bool valid;
    int16_t x, y;           // the whole-pixel position the fraction belongs to
    int8_t qx, qy;          // the fraction in quarter pixels
};

std::array<Fraction, HUD_ELEMENT_COUNT> fractions{};
int8_t dot_qx = 0, dot_qy = 0;
int32_t drawing_element = -1;
uint32_t element_dl_start = 0;
bool element_upscaled = false;

// The quarter pixels that truncating value to whole pixels dropped, if whole is that truncation.
int8_t quarters(float value, int16_t whole) {
    if (!(std::fabs(value) < 32768.0f) || (int32_t)value != whole) {
        return 0;
    }
    return (int8_t)((value - (float)whole) * 4.0f);
}

// Moves a rectangle command word's 10.2 coordinate pair (x in bits 12..23, y in bits 0..11).
uint32_t moved(uint32_t w, int dx, int dy) {
    uint32_t x = ((w >> 12) & 0xFFF) + dx;
    uint32_t y = (w & 0xFFF) + dy;
    return (w & 0xFF000000u) | ((x & 0xFFF) << 12) | (y & 0xFFF);
}

// The element's fraction, if it is a radar dot still at the position the fraction was computed for.
const Fraction* dot_fraction(uint8_t* rdram, int32_t index) {
    if (index < 0 || index >= (int32_t)HUD_ELEMENT_COUNT || !cspeed::smooth_radar()) {
        return nullptr;
    }
    const Fraction& f = fractions[index];
    gpr element = (gpr)(int32_t)(HUD_ELEMENTS + index * HUD_ELEMENT_SIZE);
    if (!f.valid || (int16_t)MEM_H(0xA, element) != f.x || (int16_t)MEM_H(0xC, element) != f.y) {
        return nullptr;
    }
    return &f;
}

// Appends gEXForceUpscale2D(force) to the 2D display list, if there is room.
bool emit_force_upscale(uint8_t* rdram, bool force) {
    uint32_t head = (uint32_t)MEM_W(0, HUD_DL_HEAD);
    uint32_t end = HUD_DL_BASE + (uint32_t)MEM_W(0, HUD_DL_INDEX) * HUD_DL_SIZE + HUD_DL_SIZE;
    if (head == 0 || head + 8 > end - HUD_DL_RESERVE) {
        return false;
    }
    MEM_W(0, (gpr)(int32_t)head) = (int32_t)RT64_FORCE_UPSCALE_2D;
    MEM_W(4, (gpr)(int32_t)head) = force ? 1 : 0;
    MEM_W(0, HUD_DL_HEAD) = (int32_t)(head + 8);
    return true;
}

} // namespace

// Start of func_8009834C: dots that are not computed (the player's own, fixed dot) have no fraction.
extern "C" void cspeed_radar_begin(uint8_t* rdram, recomp_context* ctx) {
    dot_qx = dot_qy = 0;
}

// After the x truncation ($f2 = x, $v1 = whole x).
extern "C" void cspeed_radar_x(uint8_t* rdram, recomp_context* ctx) {
    dot_qx = quarters(ctx->f2.fl, (int16_t)ctx->r3);
}

// After the y truncation ($f2 = y, $v1 = whole y).
extern "C" void cspeed_radar_y(uint8_t* rdram, recomp_context* ctx) {
    dot_qy = quarters(ctx->f2.fl, (int16_t)ctx->r3);
}

// Before the dot's HUD object is copied to its element ($s1 = object).
extern "C" void cspeed_radar_store(uint8_t* rdram, recomp_context* ctx) {
    gpr obj = ctx->r17;
    uint32_t index = (uint16_t)MEM_H(0x34, obj);
    if (index < HUD_ELEMENT_COUNT) {
        fractions[index] = { true, (int16_t)MEM_H(0xE, obj), (int16_t)MEM_H(0x10, obj), dot_qx, dot_qy };
    }
}

// Start of the HUD element drawer (func_800CFB78, $a0 = element).
extern "C" void cspeed_radar_draw_begin(uint8_t* rdram, recomp_context* ctx) {
    drawing_element = (int32_t)(((uint32_t)ctx->r4 - HUD_ELEMENTS) / HUD_ELEMENT_SIZE);
    element_upscaled = dot_fraction(rdram, drawing_element) != nullptr && emit_force_upscale(rdram, true);
    element_dl_start = (uint32_t)MEM_W(0, HUD_DL_HEAD);
}

// End of the HUD element drawer, for a radar dot: rewrites the commands it drew. Its rectangles
// (sprites as texture rectangles, plain ones as fill rectangles) move by the dot's fraction, its
// sprites are drawn with point filtering (bilinear filtering at the output resolution would blend in
// the neighbouring texels around the dot, which the original's low-resolution sampling never shows),
// and the forced upscaling ends.
extern "C" void cspeed_radar_draw_end(uint8_t* rdram, recomp_context* ctx) {
    int32_t index = drawing_element;
    drawing_element = -1;
    if (!element_upscaled) {
        return;
    }
    element_upscaled = false;
    const Fraction& f = fractions[index];
    uint32_t start = element_dl_start;
    uint32_t end = (uint32_t)MEM_W(0, HUD_DL_HEAD);
    uint32_t limit = HUD_DL_BASE + (uint32_t)MEM_W(0, HUD_DL_INDEX) * HUD_DL_SIZE + HUD_DL_SIZE - HUD_DL_RESERVE;
    std::vector<uint32_t> out;
    if (end >= start && end - start <= 0x1000) {
        for (uint32_t dl = start; dl + 8 <= end; dl += 8) {
            uint32_t w0 = (uint32_t)MEM_W(0, (gpr)(int32_t)dl);
            uint32_t w1 = (uint32_t)MEM_W(4, (gpr)(int32_t)dl);
            uint32_t op = w0 >> 24;
            if (op == 0xF6) {                                   // G_FILLRECT
                out.insert(out.end(), { moved(w0, f.qx, f.qy), moved(w1, f.qx, f.qy) });
                continue;
            }
            // A texture rectangle (G_TEXRECT, G_TEXRECTFLIP) followed by G_RDPHALF_1 (s, t in 10.5) and
            // G_RDPHALF_2 (dsdx, dtdy in 5.10).
            bool texrect = (op == 0xE4 || op == 0xE5) && dl + 24 <= end &&
                ((uint32_t)MEM_W(8, (gpr)(int32_t)dl) >> 24) == 0xE1 && ((uint32_t)MEM_W(16, (gpr)(int32_t)dl) >> 24) == 0xF1;
            if (!texrect) {
                out.insert(out.end(), { w0, w1 });
                continue;
            }
            uint32_t st = (uint32_t)MEM_W(12, (gpr)(int32_t)dl);
            uint32_t d = (uint32_t)MEM_W(20, (gpr)(int32_t)dl);
            int16_t dsdx = (int16_t)(d >> 16), dtdy = (int16_t)(d & 0xFFFF);
            int16_t s = (int16_t)(st >> 16), t = (int16_t)(st & 0xFFFF);
            if (f.qy != 0) {
                // Starting on a fractional line, a texture rectangle samples from one row further along
                // (as the RDP does, and RT64 with it), which would cut the sprite's first row: start a
                // row earlier.
                t -= dtdy >> 5;
            }
            // The RDP samples at whole pixel steps, the upscaled rectangle at the centres of the output
            // pixels: going backwards (the radar's sprites are drawn upside down), those land just
            // before each whole texel, one texel off. Start just under a texel further along.
            if (dsdx < 0) {
                s += (-dsdx >> 5) - 1;
            }
            if (dtdy < 0) {
                t += (-dtdy >> 5) - 1;
            }
            st = ((uint32_t)(uint16_t)s << 16) | (uint16_t)t;
            out.insert(out.end(), {
                RT64_PUSH_OTHER_MODE, 0,
                G_SETOTHERMODE_H_TEXTFILT, G_TF_POINT,
                moved(w0, f.qx, f.qy), moved(w1, f.qx, f.qy),
                (uint32_t)MEM_W(8, (gpr)(int32_t)dl), st,
                (uint32_t)MEM_W(16, (gpr)(int32_t)dl), d,
                RT64_POP_OTHER_MODE, 0,
            });
            dl += 16;
        }
    }
    out.insert(out.end(), { RT64_FORCE_UPSCALE_2D, 0 });
    if (start + out.size() * 4 > limit) {
        // No room: leave the element as the game drew it and drop the force-on before it.
        MEM_W(0, (gpr)(int32_t)(start - 8)) = 0;               // G_NOOP
        MEM_W(4, (gpr)(int32_t)(start - 8)) = 0;
        return;
    }
    for (size_t k = 0; k < out.size(); k++) {
        MEM_W(0, (gpr)(int32_t)(start + k * 4)) = (int32_t)out[k];
    }
    MEM_W(0, HUD_DL_HEAD) = (int32_t)(start + out.size() * 4);
}
