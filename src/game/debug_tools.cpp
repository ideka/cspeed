// Debug tools (enabled with CSPEED_DEBUG=1), driven from the keyboard while the game window is focused:
//   F1  free camera: freezes the race and flies the camera around (camera 0).
//         W/S forward/back, A/D sideways, R/F up/down, arrow keys turn, hold Shift to go faster.
//   F2  arcade textures on/off, without changing the option.
//
// The camera (one 0x30-byte block per camera at 0x801531BC) holds an up vector (+0x00), the vector the
// view looks along, negated (+0x0C: func_800CF0B4 builds the view with guLookAt from it towards the
// origin), and the position (+0x18, also used for level of detail and culling). The free camera writes
// them for camera 0 after the game updates them each frame. The race is frozen by skipping its update
// (func_80094234, the main loop's racing state) and stopping the frame time (0x80020EA8), so everything
// else keeps drawing.

#include <cmath>
#include <cstring>

#include <SDL.h>

#include "recomp.h"
#include "cspeed_arcade.h"

namespace {

constexpr gpr CAMERAS = (gpr)(int32_t)0x801531BC;
constexpr int32_t CAMERA_SIZE = 0x30;
constexpr int32_t CAMERA_UP = 0x00, CAMERA_BACK = 0x0C, CAMERA_POS = 0x18;

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
    float fx = -get_f32(rdram, CAMERAS, CAMERA_BACK), fy = -get_f32(rdram, CAMERAS, CAMERA_BACK + 4), fz = -get_f32(rdram, CAMERAS, CAMERA_BACK + 8);
    float len = std::sqrt(fx * fx + fy * fy + fz * fz);
    if (len > 0) { fx /= len; fy /= len; fz /= len; }
    yaw = std::atan2(fx, fz);
    pitch = std::asin(std::fmax(-1.0f, std::fmin(1.0f, fy)));
    printf("Free camera at %.1f %.1f %.1f\n", pos[0], pos[1], pos[2]);
}

void fly(uint8_t *rdram, const Uint8 *keys) {
    float speed = (keys[SDL_SCANCODE_LSHIFT] || keys[SDL_SCANCODE_RSHIFT]) ? 12.0f : 2.0f;
    float turn = 0.03f;
    if (keys[SDL_SCANCODE_LEFT]) yaw += turn;
    if (keys[SDL_SCANCODE_RIGHT]) yaw -= turn;
    if (keys[SDL_SCANCODE_UP]) pitch += turn;
    if (keys[SDL_SCANCODE_DOWN]) pitch -= turn;
    pitch = std::fmax(-1.5f, std::fmin(1.5f, pitch));

    float f[3] = { std::sin(yaw) * std::cos(pitch), std::sin(pitch), std::cos(yaw) * std::cos(pitch) };
    float r[3] = { std::cos(yaw), 0, -std::sin(yaw) };
    auto move = [&](const float *dir, float amount) { for (int i = 0; i < 3; i++) pos[i] += dir[i] * amount; };
    if (keys[SDL_SCANCODE_W]) move(f, speed);
    if (keys[SDL_SCANCODE_S]) move(f, -speed);
    if (keys[SDL_SCANCODE_D]) move(r, -speed);
    if (keys[SDL_SCANCODE_A]) move(r, speed);
    if (keys[SDL_SCANCODE_R]) pos[1] += speed;
    if (keys[SDL_SCANCODE_F]) pos[1] -= speed;

    for (int i = 0; i < 3; i++) {
        set_f32(rdram, CAMERAS, CAMERA_POS + i * 4, pos[i]);
        set_f32(rdram, CAMERAS, CAMERA_BACK + i * 4, -f[i]);
    }
    set_f32(rdram, CAMERAS, CAMERA_UP, 0.0f);
    set_f32(rdram, CAMERAS, CAMERA_UP + 4, 1.0f);
    set_f32(rdram, CAMERAS, CAMERA_UP + 8, 0.0f);
}

bool arcade_textures_hidden = false;

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
    }
    if (pressed(keys, SDL_SCANCODE_F2)) {
        arcade_textures_hidden = !arcade_textures_hidden;
        cspeed::arcade::set_hidden(arcade_textures_hidden);
    }
    if (free_camera) {
        fly(rdram, keys);
    }
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

// Called at the start of the race update (func_80094234): returns true to skip it.
extern "C" int cspeed_debug_freeze() {
    return enabled && free_camera;
}
