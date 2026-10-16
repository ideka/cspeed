// Debug tools (enabled with CSPEED_DEBUG=1), driven from the keyboard while the game window is focused:
//   F1  free camera: freezes the race and flies the camera around (camera 0). The mouse turns it (it is
//         captured while the free camera is on), W/S forward/back, A/D sideways, E/Q up/down, arrow keys
//         also turn, hold Shift to go faster.
//   F2  arcade textures on/off, without changing the option.
//   F3  culling off/on: draws what the game hides from the camera: scene nodes with the camera's hidden
//         bit (0x400 << camera, func_800CDE40) or beyond the draw distance (its distance check), and
//         chunks without its visible bit (func_800CE61C).
//   F5  fog off/on (src/game/graphics.cpp).
//   F6  far view: the camera's far plane (and the game's draw distance) from 2000 to 20000.
// CSPEED_TRACK=<n> goes from startup straight into a race on track n (0-15, locked ones too), skipping
// the title and the menus; it also replaces the track of races started from the menus.
// It also skips the startup screens (copyright, intro) to the title screen, and turns
// rumble off (src/main/main.cpp).
//
// The camera (one 0x30-byte block per camera at 0x801531BC) holds an up vector (+0x00), the direction
// the view looks along (+0x0C), and the position (+0x18, also used for level of detail and culling). The free camera writes
// them for camera 0 after the game updates them each frame. The race is frozen by skipping its update
// (func_80094234, the main loop's racing state) and stopping the frame time (0x80020EA8), so everything
// else keeps drawing.

#include <cmath>
#include <cstring>

#include <SDL.h>

#include "recomp.h"
#include "cspeed_arcade.h"
#include "cspeed_support.h"
#include "recompinput/input_state.h"

namespace {

constexpr gpr CAMERAS = (gpr)(int32_t)0x801531BC;
constexpr int32_t CAMERA_SIZE = 0x30;
constexpr int32_t CAMERA_UP = 0x00, CAMERA_FORWARD = 0x0C, CAMERA_POS = 0x18;
constexpr gpr GAME_MODE = (gpr)(int32_t)0x80141E00;
constexpr int32_t MODE_MAIN_MENU = 0, MODE_RACE = 1, MODE_COPYRIGHT = 15, MODE_TITLE = 17, MODE_INTRO = 18;
constexpr gpr GAME_STATE = (gpr)(int32_t)0x8016D928;
constexpr int32_t STATE_LOAD_RACE = 7;
constexpr gpr LOADED_SCREEN = (gpr)(int32_t)0x800E2BB8;  // s16: the mode whose screen is loaded

const bool enabled = [] { const char *v = getenv("CSPEED_DEBUG"); return v != nullptr && v[0] == '1'; }();

bool free_camera = false;
float pos[3], yaw, pitch;
bool keys_before[SDL_NUM_SCANCODES];

float get_f32(uint8_t *rdram, gpr base, int32_t offset) {
    uint32_t bits = MEM_W(offset, base);
    float f;
    memcpy(&f, &bits, sizeof(f));
    return f;
}

void set_f32(uint8_t *rdram, gpr base, int32_t offset, float f) {
    uint32_t bits;
    memcpy(&bits, &f, sizeof(bits));
    MEM_W(offset, base) = bits;
}

bool pressed(const Uint8 *keys, SDL_Scancode key) {
    bool now = keys[key] != 0, before = keys_before[key];
    keys_before[key] = now;
    return now && !before;
}

void start_free_camera(uint8_t *rdram) {
    for (int i = 0; i < 3; i++) pos[i] = get_f32(rdram, CAMERAS, CAMERA_POS + i * 4);
    float fx = get_f32(rdram, CAMERAS, CAMERA_FORWARD), fy = get_f32(rdram, CAMERAS, CAMERA_FORWARD + 4), fz = get_f32(rdram, CAMERAS, CAMERA_FORWARD + 8);
    float len = std::sqrt(fx * fx + fy * fy + fz * fz);
    if (len > 0) { fx /= len; fy /= len; fz /= len; }
    yaw = std::atan2(fx, fz);
    pitch = std::asin(std::fmax(-1.0f, std::fmin(1.0f, fy)));
    printf("Free camera at %.1f %.1f %.1f\n", pos[0], pos[1], pos[2]);
}

void fly(uint8_t *rdram, const Uint8 *keys) {
    float speed = (keys[SDL_SCANCODE_LSHIFT] || keys[SDL_SCANCODE_RSHIFT]) ? 12.0f : 2.0f;
    float turn = 0.03f, mouse = 0.003f;
    float dx = 0, dy = 0;
    recompinput::get_raw_mouse_deltas(&dx, &dy);
    yaw += dx * mouse;
    pitch -= dy * mouse;
    if (keys[SDL_SCANCODE_LEFT]) yaw -= turn;
    if (keys[SDL_SCANCODE_RIGHT]) yaw += turn;
    if (keys[SDL_SCANCODE_UP]) pitch += turn;
    if (keys[SDL_SCANCODE_DOWN]) pitch -= turn;
    pitch = std::fmax(-1.5f, std::fmin(1.5f, pitch));

    float f[3] = { std::sin(yaw) * std::cos(pitch), std::sin(pitch), std::cos(yaw) * std::cos(pitch) };
    float r[3] = { std::cos(yaw), 0, -std::sin(yaw) };
    auto move = [&](const float *dir, float amount) { for (int i = 0; i < 3; i++) pos[i] += dir[i] * amount; };
    if (keys[SDL_SCANCODE_W]) move(f, speed);
    if (keys[SDL_SCANCODE_S]) move(f, -speed);
    if (keys[SDL_SCANCODE_D]) move(r, speed);
    if (keys[SDL_SCANCODE_A]) move(r, -speed);
    if (keys[SDL_SCANCODE_E]) pos[1] += speed;
    if (keys[SDL_SCANCODE_Q]) pos[1] -= speed;

    for (int i = 0; i < 3; i++) {
        set_f32(rdram, CAMERAS, CAMERA_POS + i * 4, pos[i]);
        set_f32(rdram, CAMERAS, CAMERA_FORWARD + i * 4, f[i]);
    }
    set_f32(rdram, CAMERAS, CAMERA_UP, 0.0f);
    set_f32(rdram, CAMERAS, CAMERA_UP + 4, 1.0f);
    set_f32(rdram, CAMERAS, CAMERA_UP + 8, 0.0f);
}

bool arcade_textures_hidden = false;
bool no_terrain_culling = false;
bool no_fog = false;
bool far_view = false;
float saved_far = 0, saved_draw_distance = 0;
constexpr gpr CAMERA_FAR = (gpr)(int32_t)(0x800F93F0 + 0x38);  // camera 0's projection far plane
constexpr gpr DRAW_DISTANCE = (gpr)(int32_t)0x80142764;       // nodes beyond it are not drawn
constexpr float FAR_VIEW = 20000.0f;

} // namespace

// Called in func_800CF0B4 (builds a camera's view; $s1 = camera index) once the game has updated it.
extern "C" void cspeed_debug_camera(uint8_t *rdram, recomp_context *ctx) {
    if (!enabled || ctx->r17 != 0) {
        return;
    }
    const Uint8 *keys = SDL_GetKeyboardState(nullptr);
    if (pressed(keys, SDL_SCANCODE_F1)) {
        free_camera = !free_camera;
        if (free_camera) start_free_camera(rdram);
        // Capture the mouse (the input layer's event loop owns the cursor; it applies this).
        recompinput::set_cursor_lock_override(free_camera);
    }
    if (pressed(keys, SDL_SCANCODE_F2)) {
        arcade_textures_hidden = !arcade_textures_hidden;
        cspeed::arcade::set_hidden(arcade_textures_hidden);
    }
    if (pressed(keys, SDL_SCANCODE_F3)) {
        no_terrain_culling = !no_terrain_culling;
        printf("Terrain culling %s\n", no_terrain_culling ? "off" : "on");
    }
    if (pressed(keys, SDL_SCANCODE_F5)) {
        no_fog = !no_fog;
    }
    if (pressed(keys, SDL_SCANCODE_F6)) {
        far_view = !far_view;
        if (far_view) {
            saved_far = get_f32(rdram, CAMERA_FAR, 0);
            saved_draw_distance = get_f32(rdram, DRAW_DISTANCE, 0);
        }
        else {
            set_f32(rdram, CAMERA_FAR, 0, saved_far);
            set_f32(rdram, DRAW_DISTANCE, 0, saved_draw_distance);
        }
    }
    if (far_view) {
        set_f32(rdram, CAMERA_FAR, 0, FAR_VIEW);
        set_f32(rdram, DRAW_DISTANCE, 0, FAR_VIEW);
    }
    if (free_camera) {
        fly(rdram, keys);
    }
}

// Called at the start of the front end's update (func_8006F240): moves through the startup screens
// (copyright, intro) as soon as each one has loaded, in the game's own order, to the title screen. With
// CSPEED_TRACK, it goes on through the title and from the main menu straight to loading the race (game
// mode 1, state 7) with the default car, skipping the menus. (Going on from the title by itself leaves the
// main menu ignoring input, so without CSPEED_TRACK the title still needs Start.)
extern "C" void cspeed_debug_skip_intro(uint8_t *rdram, recomp_context *ctx) {
    if (!enabled) {
        return;
    }
    static const bool quick_race = getenv("CSPEED_TRACK") != nullptr;
    static bool raced = false;
    int32_t mode = MEM_W(0, GAME_MODE);
    if (quick_race && !raced && mode == MODE_MAIN_MENU) {
        raced = true;
        MEM_W(0, GAME_MODE) = MODE_RACE;
        MEM_W(0, GAME_STATE) = STATE_LOAD_RACE;
        return;
    }
    if (mode != MEM_H(0, LOADED_SCREEN)) {
        return;
    }
    if (mode == MODE_COPYRIGHT) MEM_W(0, GAME_MODE) = MODE_INTRO;
    else if (mode == MODE_INTRO) MEM_W(0, GAME_MODE) = MODE_TITLE;
    else if (mode == MODE_TITLE && quick_race && !raced) MEM_W(0, GAME_MODE) = MODE_MAIN_MENU;
}

bool cspeed::debug_no_fog() {
    return enabled && no_fog;
}

bool cspeed::debug_tools_enabled() {
    return enabled;
}

// Called after the graphics thread stores the frame time (func_8005434C): stops the clock while frozen,
// so the race timer and animations stop too.
extern "C" void cspeed_debug_frame_time(uint8_t *rdram, recomp_context *ctx) {
    if (enabled && free_camera) {
        MEM_W(0, (gpr)(int32_t)0x80020EA8) = 0;
    }
}

// Called when a race starts loading (main loop, game state 7). CSPEED_TRACK=<n> (with CSPEED_DEBUG=1)
// replaces the chosen track with track n, so a scripted race can go to any track.
extern "C" void cspeed_debug_track(uint8_t *rdram, recomp_context *ctx) {
    static const char *track = getenv("CSPEED_TRACK");
    if (enabled && track != nullptr && track[0] != '\0') {
        MEM_B(0, (gpr)(int32_t)0x800F779C) = (int8_t)atoi(track);
    }
}

// Called in the terrain drawing (func_800CE61C) where a chunk's visibility bit for the camera is
// tested: returns true to draw it anyway.
extern "C" int cspeed_debug_no_terrain_culling() {
    return enabled && no_terrain_culling;
}

// Called at the start of the race update (func_80094234): returns true to skip it.
extern "C" int cspeed_debug_freeze() {
    return enabled && free_camera;
}

