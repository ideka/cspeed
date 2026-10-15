// Graphics extensions: RT64 extended GBI setup and widescreen support.

#include <array>

#include "recomp.h"
#include "librecomp/helpers.hpp"
#include "ultramodern/config.hpp"

namespace {

// RT64 extended GBI (lib/rt64/include/rt64_extended_gbi.h), F3DEX2 encoding.
constexpr uint32_t RT64_HOOK_WORD0 = (0xE0u << 24) | 0x525464;      // G_NOOP carrying the RT64 magic
constexpr uint32_t RT64_EXTENDED_OPCODE = 0x64;
constexpr uint32_t RT64_HOOK_OP_ENABLE = 0x1;
constexpr uint32_t G_DL_BRANCH_WORD0 = 0xDE010000;                    // gSPBranchList

// Per-frame prologue display lists live in the last 64 KiB of the extra RAM recomp provides
// (patches/patches.ld stops patch code before it). One slot per distinct game display list
// address, so a prologue is never rewritten while RT64 may still be reading an earlier frame.
constexpr uint32_t PROLOGUE_BASE = 0x80FF0000;
constexpr size_t PROLOGUE_SLOTS = 16;
constexpr uint32_t PROLOGUE_SIZE = 16;

} // namespace

// Hook in the graphics task setup (func_80055168) after it stores OSTask.data_ptr ($s0 = OSScTask,
// data_ptr at +0x58). Redirects the task to a prologue that enables RT64's extended GBI (which RT64
// resets at the start of every task) and then branches to the game's display list.
extern "C" void cspeed_gfx_task_prologue(uint8_t* rdram, recomp_context* ctx) {
    static std::array<uint32_t, PROLOGUE_SLOTS> slot_dl{};
    static size_t next_slot = 0;
    gpr task = ctx->r16;
    uint32_t dl = (uint32_t)MEM_W(0x58, task);
    size_t slot = PROLOGUE_SLOTS;
    for (size_t i = 0; i < PROLOGUE_SLOTS; i++) {
        if (slot_dl[i] == dl) {
            slot = i;
            break;
        }
    }
    if (slot == PROLOGUE_SLOTS) {
        slot = next_slot;
        next_slot = (next_slot + 1) % PROLOGUE_SLOTS;
        slot_dl[slot] = dl;
    }
    gpr prologue = (gpr)(int32_t)(PROLOGUE_BASE + slot * PROLOGUE_SIZE);
    MEM_W(0x0, prologue) = (int32_t)RT64_HOOK_WORD0;
    MEM_W(0x4, prologue) = (int32_t)((RT64_HOOK_OP_ENABLE << 28) | RT64_EXTENDED_OPCODE);
    MEM_W(0x8, prologue) = (int32_t)G_DL_BRANCH_WORD0;
    MEM_W(0xC, prologue) = (int32_t)dl;
    MEM_W(0x58, task) = (int32_t)(PROLOGUE_BASE + slot * PROLOGUE_SIZE);
}

// Widescreen support.
//
// RT64 widens a 3D scene on its own when the "Expand" aspect ratio is selected, but only for draws
// whose viewport and scissor together cover the full framebuffer width. California Speed draws the
// 3D view with a full-screen viewport and a scissor inset by 12 pixels on each side (a black border),
// so the scene stayed 4:3 while the full-width background clear was stretched into grey side bars.

namespace {

bool expand_enabled() {
    return ultramodern::renderer::get_graphics_config().ar_option == ultramodern::renderer::AspectRatio::Expand;
}

// Widescreen only applies during races (game mode 0x80141E00 == 1). Menus combine a 4:3 background
// image with 3D objects that slide in from outside the 4:3 area, which a wider view would reveal.
constexpr gpr GAME_MODE = (gpr)(int32_t)0x80141E00;
constexpr int32_t GAME_MODE_RACE = 1;

bool in_race(uint8_t* rdram) {
    return MEM_W(0, GAME_MODE) == GAME_MODE_RACE;
}

} // namespace

// Hook in the camera setup (func_800CF0B4) right after it writes G_SETSCISSOR into the display list
// ($t0 = display list position: scissor words at +0x10/+0x14; $s1 = camera index). When the camera's
// viewport spans the whole screen and the scissor is centred in it, open the scissor horizontally to
// the full width so RT64 expands the scene. Split-screen viewports are left untouched.
extern "C" void cspeed_widescreen_camera_scissor(uint8_t* rdram, recomp_context* ctx) {
    if (!expand_enabled() || !in_race(rdram)) {
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

// HUD placement.
//
// 2D elements are drawn into a separate display list (begun by func_8005A0D8, double-buffered at
// 0x80142A50 + n * 0x3E80, current position in 0x80141570) which the main list calls. Sprites
// (texture rectangles) all go through func_8005C718. During a race (game mode 0x80141E00 == 1) each
// sprite is anchored to the screen side it sits on with RT64's gEXSetRectAlign: left part to the
// left edge, right part to the right edge, the middle stays centred. RT64 then places them according
// to the "HUD ratio" option (4:3 edges, 16:9 edges or the window edges). The 2D list has no scissor
// of its own and inherits the 4:3 one, so a full-width extended scissor is set at its start.
// Menus are left alone: their layouts are designed around the 4:3 screen.
namespace {

constexpr uint32_t G_EX_SETSCISSOR_V1 = 0x000005;
constexpr uint32_t G_EX_SETRECTALIGN_V1 = 0x000006;
constexpr uint32_t G_EX_ORIGIN_NONE = 0x800;
constexpr uint32_t G_EX_ORIGIN_LEFT = 0x0;
constexpr uint32_t G_EX_ORIGIN_RIGHT = 0x400;
constexpr gpr HUD_DL_HEAD = (gpr)(int32_t)0x80141570;
constexpr gpr HUD_DL_INDEX = (gpr)(int32_t)0x800F8818;
constexpr uint32_t HUD_DL_BASE = 0x80142A50;
constexpr uint32_t HUD_DL_SIZE = 0x3E80;
constexpr uint32_t HUD_DL_RESERVE = 0x400;   // leave room for the game's own remaining commands
constexpr int32_t SCREEN_WIDTH = 320;
constexpr int32_t SCREEN_HEIGHT = 240;

uint32_t last_rect_origin = 0xFFFFFFFF;

bool hud_alignment_active(uint8_t* rdram) {
    return expand_enabled() && in_race(rdram);
}

// Appends a 2-command (16 byte) extended GBI command to the HUD list, if there is room.
void emit_hud_command(uint8_t* rdram, uint32_t w0, uint32_t w1, uint32_t w2, uint32_t w3) {
    uint32_t head = (uint32_t)MEM_W(0, HUD_DL_HEAD);
    uint32_t end = HUD_DL_BASE + (uint32_t)MEM_W(0, HUD_DL_INDEX) * HUD_DL_SIZE + HUD_DL_SIZE;
    if (head == 0 || head + 16 > end - HUD_DL_RESERVE) {
        return;
    }
    gpr dl = (gpr)(int32_t)head;
    MEM_W(0x0, dl) = (int32_t)w0;
    MEM_W(0x4, dl) = (int32_t)w1;
    MEM_W(0x8, dl) = (int32_t)w2;
    MEM_W(0xC, dl) = (int32_t)w3;
    MEM_W(0, HUD_DL_HEAD) = (int32_t)(head + 16);
}

} // namespace

// Hook at the end of func_8005A0D8 (begin the 2D list).
extern "C" void cspeed_hud_begin(uint8_t* rdram, recomp_context* ctx) {
    last_rect_origin = 0xFFFFFFFF;
    if (!hud_alignment_active(rdram)) {
        return;
    }
    // gEXSetScissor(G_SC_NON_INTERLACE, LEFT, RIGHT, 0, 0, 0, 240): left edge to right edge.
    emit_hud_command(rdram,
        (RT64_EXTENDED_OPCODE << 24) | G_EX_SETSCISSOR_V1,
        (G_EX_ORIGIN_LEFT << 2) | (G_EX_ORIGIN_RIGHT << 14),
        0,
        (uint32_t)(SCREEN_HEIGHT * 4));
}

// Hook at the entry of func_8005C718 (draw sprite) and func_8005CCC4 (fill rectangle); both take
// $a0..$a3 = ulx, uly, lrx, lry in screen pixels.
extern "C" void cspeed_hud_sprite_align(uint8_t* rdram, recomp_context* ctx) {
    if (!hud_alignment_active(rdram)) {
        return;
    }
    int32_t center_x = ((int32_t)ctx->r4 + (int32_t)ctx->r6) / 2;
    uint32_t origin = G_EX_ORIGIN_NONE;
    int32_t x_offset = 0;
    if (center_x < SCREEN_WIDTH * 2 / 5) {
        origin = G_EX_ORIGIN_LEFT;
    } else if (center_x > SCREEN_WIDTH * 3 / 5) {
        origin = G_EX_ORIGIN_RIGHT;
        x_offset = -SCREEN_WIDTH * 4;   // the right origin adds the screen width (10.2 fixed point)
    }
    if (origin == last_rect_origin) {
        return;
    }
    last_rect_origin = origin;
    uint32_t x = (uint32_t)x_offset & 0xFFFF;
    // gEXSetRectAlign(origin, origin, x_offset, 0, x_offset, 0)
    emit_hud_command(rdram,
        (RT64_EXTENDED_OPCODE << 24) | G_EX_SETRECTALIGN_V1,
        origin | (origin << 12),
        x << 16,
        x << 16);
}
