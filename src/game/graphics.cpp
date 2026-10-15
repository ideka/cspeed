// Graphics extensions: RT64 extended GBI setup and widescreen support.

#include <algorithm>
#include <array>

#include "recomp.h"
#include "librecomp/helpers.hpp"
#include "ultramodern/config.hpp"
#include "hle/rt64_vi.h"

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

// Widescreen only applies during races. Menus combine a 4:3 background image with 3D objects that
// slide in from outside the 4:3 area, which a wider view would reveal. The game mode at 0x80141E00
// is 1 in races but also in the race setup menus, so a race is recognised by the speedometer needle
// having been drawn in the previous frame (see cspeed_widescreen_frame_submit).
constexpr gpr GAME_MODE = (gpr)(int32_t)0x80141E00;
constexpr int32_t GAME_MODE_RACE = 1;
bool needle_drawn_last_frame = false;

bool in_race(uint8_t* rdram) {
    return MEM_W(0, GAME_MODE) == GAME_MODE_RACE && needle_drawn_last_frame;
}

} // namespace

// Hook in the camera setup (func_800CF0B4) right after it writes G_SETSCISSOR into the display list
// ($t0 = display list position: scissor words at +0x10/+0x14; $s1 = camera index). When the camera's
// viewport spans the whole screen and the scissor is centred in it, open the scissor to the full
// screen so RT64 expands the scene. Split-screen viewports are left untouched.
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
    // Open it to the whole screen; the frame's black border strips are removed as well (see
    // remove_borders), so the scene also fills the 10-pixel bands at the top and bottom.
    MEM_W(0x10, dl) = (int32_t)(ul & 0xFF000000u);                                         // ulx = uly = 0
    MEM_W(0x14, dl) = (int32_t)(((uint32_t)width << 12) | (uint32_t)(MEM_H(10, vp) * 2));  // lrx = width, lry = height
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
constexpr gpr HUD_CLIP_LEFT = (gpr)(int32_t)0x801426D0;
constexpr gpr HUD_CLIP_RIGHT = (gpr)(int32_t)0x80134380;
constexpr int32_t SCREEN_WIDTH = 320;
constexpr int32_t SCREEN_HEIGHT = 240;

// Scissors for the HUD list. "Wide" spans the left to the right HUD origin (where RT64 puts the
// anchored HUD for the chosen HUD ratio); "plain" is the normal full screen, which RT64 stretches to
// the whole window and which full-width overlays need. Keep scissors within the screen: RT64 derives
// its 2D aspect ratio from the framebuffer's overall scissor, so anything larger distorts the HUD.
enum class HudScissor { Unset, Wide, Plain };
HudScissor hud_scissor = HudScissor::Unset;
uint64_t last_rect_align = ~0ull;
uint32_t hud_list_start = 0;

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

void set_hud_scissor(uint8_t* rdram, HudScissor scissor) {
    if (scissor == hud_scissor) {
        return;
    }
    hud_scissor = scissor;
    // gEXSetScissor(G_SC_NON_INTERLACE, lorigin, rorigin, 0, 0, lrx, 240): with the right origin,
    // lrx 0 is the right HUD edge.
    bool wide = (scissor == HudScissor::Wide);
    uint32_t lorigin = wide ? G_EX_ORIGIN_LEFT : G_EX_ORIGIN_NONE;
    uint32_t rorigin = wide ? G_EX_ORIGIN_RIGHT : G_EX_ORIGIN_NONE;
    uint32_t lrx = wide ? 0 : (uint32_t)(SCREEN_WIDTH * 4);
    emit_hud_command(rdram,
        (RT64_EXTENDED_OPCODE << 24) | G_EX_SETSCISSOR_V1,
        (lorigin << 2) | (rorigin << 14),
        0,
        (lrx << 16) | (uint32_t)(SCREEN_HEIGHT * 4));
}

} // namespace

// Hook at the end of func_8005A0D8 (begin the 2D list).
extern "C" void cspeed_hud_begin(uint8_t* rdram, recomp_context* ctx) {
    last_rect_align = ~0ull;
    hud_scissor = HudScissor::Unset;
    hud_list_start = 0;
    if (!hud_alignment_active(rdram)) {
        return;
    }
    hud_list_start = (uint32_t)MEM_W(0, HUD_DL_HEAD);
    set_hud_scissor(rdram, HudScissor::Wide);
}

// Hook at the entry of func_8005C718 (draw sprite) and func_8005CCC4 (fill rectangle); both take
// $a0..$a3 = ulx, uly, lrx, lry in screen pixels. Rectangles that span (nearly) the whole screen
// width, like the pause screen's darkening, are left unanchored under the plain scissor; their fill
// rectangles are widened to the full screen at frame submission so that RT64 stretches them over
// the whole window (see widen_full_width_fills).
extern "C" void cspeed_hud_sprite_align(uint8_t* rdram, recomp_context* ctx) {
    if (!hud_alignment_active(rdram)) {
        return;
    }
    // The drawers clip the rectangle to the 2D clip region after this hook runs; do the same here.
    int32_t ulx = std::max((int32_t)ctx->r4, (int32_t)MEM_W(0, HUD_CLIP_LEFT));
    int32_t lrx = std::min((int32_t)ctx->r6, (int32_t)MEM_W(0, HUD_CLIP_RIGHT));
    int32_t center_x = (ulx + lrx) / 2;
    uint32_t lorigin = G_EX_ORIGIN_NONE;
    uint32_t rorigin = G_EX_ORIGIN_NONE;
    int32_t ul_offset = 0;
    int32_t lr_offset = 0;
    bool full_width = (ulx <= 16 && lrx >= SCREEN_WIDTH - 16);
    set_hud_scissor(rdram, full_width ? HudScissor::Plain : HudScissor::Wide);
    if (full_width) {
        // Unanchored: RT64 stretches full-screen rectangles over the whole window by itself.
    } else if (center_x < SCREEN_WIDTH * 2 / 5) {
        lorigin = rorigin = G_EX_ORIGIN_LEFT;
    } else if (center_x > SCREEN_WIDTH * 3 / 5) {
        lorigin = rorigin = G_EX_ORIGIN_RIGHT;
        ul_offset = lr_offset = -SCREEN_WIDTH * 4;   // the right origin adds the screen width
    }
    // gEXSetRectAlign(lorigin, rorigin, ul_offset, 0, lr_offset, 0)
    uint32_t w1 = lorigin | (rorigin << 12);
    uint32_t w2 = ((uint32_t)ul_offset & 0xFFFF) << 16;
    uint32_t w3 = ((uint32_t)lr_offset & 0xFFFF) << 16;
    uint64_t key = ((uint64_t)w1 << 40) ^ ((uint64_t)w2 << 8) ^ w3;
    if (key == last_rect_align) {
        return;
    }
    last_rect_align = key;
    emit_hud_command(rdram, (RT64_EXTENDED_OPCODE << 24) | G_EX_SETRECTALIGN_V1, w1, w2, w3);
}

// Hook in the graphics task setup (func_80055168) right before the finished task is sent to the
// scheduler ($s0 = OSScTask). Two fixes for widescreen races, applied to the finished display list:
//
// - The frame starts by drawing black 12-pixel borders around the 4:3 picture. The left and right
//   ones are narrow fill rectangles that RT64 keeps at the old 4:3 edges, where they show as two thin
//   bars wherever no 3D geometry covers them (the sky). They become no-ops.
// - The speedometer needle is a red quad drawn with the race camera (CPU-computed vertices) right
//   after the scene, so it stays at its 4:3 position while the dial sprites move to the right edge.
//   Its vertex load and triangles are moved into a small display list that re-applies the camera's
//   viewport aligned to the right edge (RT64 then draws it in a 4:3 viewport anchored to the right,
//   like the HUD) and restores the normal viewport afterwards.
namespace {

constexpr uint32_t G_EX_SETVIEWPORTALIGN_V1 = 0x000007;
constexpr uint32_t NEEDLE_COLOR = 0xC80000FF;
constexpr uint32_t NEEDLE_DL_BASE = PROLOGUE_BASE + 0x1000;   // one list per prologue slot
constexpr uint32_t NEEDLE_DL_SIZE = 128;
constexpr gpr VIEWPORTS = (gpr)(int32_t)0x80022FB0;            // pointer to this frame's Vp array

void remove_borders(uint8_t* rdram, uint32_t dl) {
    // The borders come within the first few dozen commands, before any geometry: narrow full-height
    // strips at the sides and short full-width strips at the top and bottom. They are removed so the
    // race view fills the whole screen.
    for (int i = 0; i < 64; i++, dl += 8) {
        gpr cmd = (gpr)(int32_t)dl;
        uint32_t w0 = (uint32_t)MEM_W(0, cmd);
        uint32_t w1 = (uint32_t)MEM_W(4, cmd);
        uint32_t op = w0 >> 24;
        if (op == 0xDA || op == 0x01 || op == 0xDF) {
            break;                      // matrices or geometry: past the frame setup
        }
        if (op != 0xF6) {
            continue;
        }
        int32_t lrx = (w0 >> 12) & 0xFFF, lry = w0 & 0xFFF;    // 10.2 fixed point
        int32_t ulx = (w1 >> 12) & 0xFFF, uly = w1 & 0xFFF;
        bool side = (lrx - ulx <= 16 * 4 && lry - uly >= 200 * 4);
        bool top_bottom = (lry - uly <= 12 * 4 && lrx - ulx >= 300 * 4 &&
                           (uly == 0 || lry >= (SCREEN_HEIGHT - 12) * 4));
        if (side || top_bottom) {
            MEM_W(0, cmd) = 0;          // G_NOOP
            MEM_W(4, cmd) = 0;
            continue;
        }
        // The sky and ground colour fills stop where the borders were; extend them to the edges.
        if (lrx - ulx >= 300 * 4) {
            if (uly == 10 * 4) {
                uly = 0;
            }
            if (lry == 229 * 4) {
                lry = (SCREEN_HEIGHT - 1) * 4;
            }
            MEM_W(0, cmd) = (int32_t)((w0 & ~0xFFFu) | (uint32_t)lry);
            MEM_W(4, cmd) = (int32_t)((w1 & ~0xFFFu) | (uint32_t)uly);
        }
    }
}

// Full-width fill rectangles in the HUD list (the pause screen darkening) start at x = 12 and are
// clipped to x = 319 by the game. RT64 only stretches rectangles that cover the whole screen over the
// whole window, so make them exactly cover it.
void widen_full_width_fills(uint8_t* rdram) {
    if (hud_list_start == 0) {
        return;
    }
    uint32_t end = HUD_DL_BASE + (uint32_t)MEM_W(0, HUD_DL_INDEX) * HUD_DL_SIZE + HUD_DL_SIZE;
    for (uint32_t dl = hud_list_start; dl + 8 <= end; dl += 8) {
        gpr cmd = (gpr)(int32_t)dl;
        uint32_t w0 = (uint32_t)MEM_W(0, cmd);
        uint32_t w1 = (uint32_t)MEM_W(4, cmd);
        uint32_t op = w0 >> 24;
        if (op == 0xDF) {
            break;
        }
        if (op != 0xF6) {
            continue;
        }
        int32_t lrx = (w0 >> 12) & 0xFFF, lry = w0 & 0xFFF;
        int32_t ulx = (w1 >> 12) & 0xFFF, uly = w1 & 0xFFF;
        if (ulx <= 16 * 4 && lrx >= (SCREEN_WIDTH - 16) * 4) {
            // Also cover the top and bottom bands, which the race view now fills (remove_borders).
            if (uly <= 16 * 4) {
                uly = 0;
            }
            if (lry >= (SCREEN_HEIGHT - 16) * 4) {
                lry = SCREEN_HEIGHT * 4;
            }
            MEM_W(0, cmd) = (int32_t)((w0 & 0xFF000000u) | ((uint32_t)(SCREEN_WIDTH * 4) << 12) | (uint32_t)lry);
            MEM_W(4, cmd) = (int32_t)((w1 & 0xFF000000u) | (uint32_t)uly);
        }
    }
}

// Finds the needle's vertex load in the frame's top-level display list: "4 vertices" after the needle
// colour, followed by its triangles. Returns the address of the vertex load (0 if not found) and
// the triangles' address in *tri.
uint32_t find_needle(uint8_t* rdram, uint32_t dl, uint32_t* tri) {
    bool red = false;
    for (int i = 0; i < 20000; i++, dl += 8) {
        uint32_t w0 = (uint32_t)MEM_W(0, (gpr)(int32_t)dl);
        uint32_t w1 = (uint32_t)MEM_W(4, (gpr)(int32_t)dl);
        uint32_t op = w0 >> 24;
        if (op == 0xDF) {
            return 0;
        }
        if (op == 0xFA) {
            red = (w1 == NEEDLE_COLOR);
            continue;
        }
        if (!red || w0 != 0x01004008) {
            continue;
        }
        for (uint32_t j = 1; j <= 3; j++) {
            if (((uint32_t)MEM_W(0, (gpr)(int32_t)(dl + j * 8)) >> 24) == 0x06) {
                *tri = dl + j * 8;
                return dl;
            }
        }
    }
    return 0;
}

void anchor_needle(uint8_t* rdram, uint32_t vtx_addr, uint32_t tri_addr, uint32_t slot) {
    gpr cmd = (gpr)(int32_t)vtx_addr;
    gpr tri = (gpr)(int32_t)tri_addr;
    uint32_t viewport = (uint32_t)MEM_W(0, VIEWPORTS);    // camera 0
    gpr sub = (gpr)(int32_t)(NEEDLE_DL_BASE + slot * NEEDLE_DL_SIZE);
    uint32_t align = (RT64_EXTENDED_OPCODE << 24) | G_EX_SETVIEWPORTALIGN_V1;
    uint32_t right_offset = ((uint32_t)(-SCREEN_WIDTH * 4) & 0xFFFF) << 16;
    int32_t words[] = {
        (int32_t)((RT64_EXTENDED_OPCODE << 24) | G_EX_SETSCISSOR_V1),   // gEXSetScissor(LEFT, RIGHT,
        (int32_t)((G_EX_ORIGIN_LEFT << 2) | (G_EX_ORIGIN_RIGHT << 14)),  //   0, 0, 0, 240): HUD edge to HUD
        0, SCREEN_HEIGHT * 4,                                           //   edge (the needle may pass the 4:3 edge)
        (int32_t)align, (int32_t)G_EX_ORIGIN_RIGHT,                     // gEXSetViewportAlign(RIGHT, -320*4, 0)
        (int32_t)right_offset, 0,
        (int32_t)0xDC080008, (int32_t)viewport,                        // gSPViewport (F3DEX2 movemem)
        MEM_W(0, cmd), MEM_W(4, cmd),                                  // the vertex load
        MEM_W(0, tri), MEM_W(4, tri),                                  // the triangles
        (int32_t)align, (int32_t)G_EX_ORIGIN_NONE,                      // gEXSetViewportAlign(NONE, 0, 0)
        0, 0,
        (int32_t)0xDC080008, (int32_t)viewport,                        // gSPViewport
        (int32_t)0xDF000000, 0,                                        // end (the game sets the HUD's
    };                                                                 //   scissor right after)
    for (size_t k = 0; k < sizeof(words) / sizeof(words[0]); k++) {
        MEM_W((int32_t)(k * 4), sub) = words[k];
    }
    MEM_W(0, cmd) = (int32_t)0xDE000000;                               // call it instead of the vertex load
    MEM_W(4, cmd) = (int32_t)(NEEDLE_DL_BASE + slot * NEEDLE_DL_SIZE);
    MEM_W(0, tri) = 0;                                                 // G_NOOP
    MEM_W(4, tri) = 0;
}

} // namespace

// California Speed draws a black border around the picture (12 px left and right, 10 px top and
// bottom) that a CRT's overscan hid. RT64 is told to zoom the picture by 1/24 on each side so it is
// out of view, except in widescreen races: those remove the border instead (remove_borders), and the
// zoom would push the HUD anchored at the window edges off screen.
constexpr float OVERSCAN_CROP = 1.0f / 24.0f;

extern "C" void cspeed_widescreen_frame_submit(uint8_t* rdram, recomp_context* ctx) {
    needle_drawn_last_frame = false;
    struct CropForFrame {
        ~CropForFrame() {
            RT64::VI::setOverscanCrop((needle_drawn_last_frame && expand_enabled()) ? 0.0f : OVERSCAN_CROP);
        }
    } crop_for_frame;
    if (MEM_W(0, GAME_MODE) != GAME_MODE_RACE) {
        return;
    }
    // The task points at our prologue; the game's display list is the target of its branch.
    uint32_t prologue = (uint32_t)MEM_W(0x58, ctx->r16);
    if (prologue < PROLOGUE_BASE || prologue >= PROLOGUE_BASE + PROLOGUE_SLOTS * PROLOGUE_SIZE) {
        return;
    }
    uint32_t dl = (uint32_t)MEM_W(0xC, (gpr)(int32_t)prologue);
    if ((dl & 0xFF000000) != 0x80000000) {
        return;
    }
    uint32_t tri = 0;
    uint32_t needle = find_needle(rdram, dl, &tri);
    needle_drawn_last_frame = (needle != 0);
    if (!needle_drawn_last_frame || !expand_enabled()) {
        return;
    }
    remove_borders(rdram, dl);
    widen_full_width_fills(rdram);
    anchor_needle(rdram, needle, tri, (prologue - PROLOGUE_BASE) / PROLOGUE_SIZE);
}
