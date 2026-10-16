// Full analog controls.
//
// The runtime hands the game the stick as the N64 does, a whole number from -82 to 82, and gas and
// brake are buttons. The game copies the stick into a per-player input block (func_800821E4) and
// already works in float from there:
// - Steering (func_80081C7C): clamp(x / (0.95 * max), -1, 1), where max is the largest stick value
//   seen so far (it starts at 70). Hooked to use the same formula on the unrounded stick value, with the
//   full range (82) from the start.
// - Gas and brake (func_80081AC0): 1.0 while their button is held. Hooked to use how far the buttons
//   are pressed instead, which is analog when they are bound to e.g. the triggers.
// Only live controller input is changed: the hooks check the game's value against the controller.

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>

#include "recomp.h"
#include "cspeed_config.h"
#include "cspeed_support.h"
#include "recompinput/profiles.h"

namespace {

// Per-player input block: +1 controller (4 or more = none), +4 buttons, +8/+9 stick X/Y,
// +0xA/+0xB largest stick X/Y seen (the game's calibration), +0xC/+0xE gas/brake button masks.
constexpr int32_t BLOCK_CONTROLLER = 0x1;
constexpr int32_t BLOCK_BUTTONS = 0x4;
constexpr int32_t BLOCK_STICK_X = 0x8;
constexpr int32_t BLOCK_GAS_BUTTONS = 0xC;
constexpr int32_t BLOCK_BRAKE_BUTTONS = 0xE;
// Car: gas and brake amounts (0 to 1), from the masks at +0xC and +0xE.
constexpr int32_t CAR_GAS = 0x71C;
constexpr int32_t CAR_BRAKE = 0x718;

constexpr int MAX_CONTROLLERS = 4;
constexpr float N64_STICK_RANGE = 82.0f;  // the runtime's stick range on an axis
std::atomic<float> host_stick_x[MAX_CONTROLLERS];
std::atomic<float> host_stick_y[MAX_CONTROLLERS];

// The runtime's mapping of the stick into the N64's octagon (ultramodern convert_to_n64_range),
// without rounding to whole numbers.
float n64_stick_x(float x, float y) {
    constexpr float PI = 3.14159265358979323846f;
    float magnitude = std::min(sqrtf(x * x + y * y), 1.0f);
    float angle = atan2f(y, x);
    float angle_modulo = fmodf(angle + 2.0f * PI, PI / 2.0f);
    if (angle_modulo > PI / 4.0f) {
        angle_modulo = PI / 2.0f - angle_modulo;
    }
    constexpr float r0 = 82.0f;
    constexpr float alpha = 1.39414574f;
    float n64_radius = r0 * sin(alpha) / (sin(PI - angle_modulo - alpha));
    return cosf(angle) * magnitude * n64_radius;
}

int block_controller(uint8_t* rdram, gpr block) {
    int controller = MEM_BU(BLOCK_CONTROLLER, block);
    return controller < MAX_CONTROLLERS ? controller : -1;
}

void set_float(uint8_t* rdram, gpr base, int32_t offset, float value) {
    uint32_t bits;
    memcpy(&bits, &value, sizeof(bits));
    MEM_W(offset, base) = bits;
}

void apply_pedal(uint8_t* rdram, gpr block, gpr car, int controller, int32_t mask_offset, int32_t value_offset) {
    uint16_t mask = MEM_HU(mask_offset, block);
    if (mask == 0 || (uint16_t)(mask + 3) < 2) {  // not set up yet, or gas/brake on the stick
        return;
    }
    float amount = recompinput::profiles::get_n64_button_analog(controller, mask);
    bool held_in_game = (MEM_HU(BLOCK_BUTTONS, block) & mask) != 0;
    if (held_in_game && amount == 0.0f) {  // not from the controller
        return;
    }
    set_float(rdram, car, value_offset, amount);
}

} // namespace

void cspeed::set_host_stick(int controller, float x, float y) {
    if (controller >= 0 && controller < MAX_CONTROLLERS) {
        host_stick_x[controller] = x;
        host_stick_y[controller] = y;
    }
}

// Called after the steering amount is computed ($f0), $s0 = input block.
extern "C" void cspeed_analog_steering(uint8_t* rdram, recomp_context* ctx) {
    if (!cspeed::full_analog_controls()) {
        return;
    }
    gpr block = ctx->r16;
    int controller = block_controller(rdram, block);
    if (controller < 0) {
        return;
    }
    float x = n64_stick_x(host_stick_x[controller], host_stick_y[controller]);
    int8_t game_x = MEM_B(BLOCK_STICK_X, block);
    if (std::abs((int)x - game_x) > 1) {  // not from the controller
        return;
    }
    // The game calibrates the stick's range as it goes: it assumes 70 at the start of each race and
    // raises it while the stick goes further, so full lock first comes at 81% of the stick's travel and
    // only later at 95%. Use the full range (the runtime's 82) from the start instead.
    ctx->f0.fl = std::clamp(x / (0.95f * N64_STICK_RANGE), -1.0f, 1.0f);
}

// Called after gas and brake are set, $s0 = car, $s1 = input block.
extern "C" void cspeed_analog_pedals(uint8_t* rdram, recomp_context* ctx) {
    if (!cspeed::full_analog_controls()) {
        return;
    }
    gpr car = ctx->r16;
    gpr block = ctx->r17;
    int controller = block_controller(rdram, block);
    if (controller < 0) {
        return;
    }
    apply_pedal(rdram, block, car, controller, BLOCK_GAS_BUTTONS, CAR_GAS);
    apply_pedal(rdram, block, car, controller, BLOCK_BRAKE_BUTTONS, CAR_BRAKE);
}
