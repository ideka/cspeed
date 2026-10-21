// Fog zones.
//
// Tunnels and the like change the fog colour, which is also the sky and ground colour behind the
// scenery (func_800D1278 sets all three): the end of Mojave Desert, inside the alien spaceship, turns
// it black. The game does that per player (func_800C4F34, every frame) with a small list of zones per
// track (count at 0x800C573C + track * 8, list at +4; 0x14 bytes each: x, z, half size, type, colour),
// squares 90 to 220 units wide. While a player's car is inside one, its colour (or for type 0 the
// track's own) is faded in over 30 frames. The squares sit on the AI's racing line: a car that passes
// beside one, which the road is wide enough for, never triggers it, and the sky stays the colour
// before (white or lavender at Mojave's tunnel, or a grey halfway through a fade).
//
// Every car also keeps its place on the track's path: the nearest of its points (+0x10E) on the
// main road or one of the branches (+0x10C, -1 for the main road), found by func_8009BF2C. The main
// road's points (x, y, z as s16) are at 0x80142704 (count at 0x80142700), the branches' in a table at
// 0x8014270C (count in the byte at 0x80142708; 16 bytes each: point count at +0xA, points at +0xC).
// A zone also counts as reached when the car's path point is at either end of a stretch of the path
// that runs through the zone's square, wherever the car is across the road. Hooked into the zone
// test (cspeed.us.toml).

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

#include "recomp.h"

namespace {

constexpr gpr TRACK = (gpr)(int32_t)0x800F779C;           // s8
constexpr uint32_t ZONE_COUNTS = 0x800C573C;              // u8, then the list pointer, 8 bytes per track
constexpr uint32_t ZONE_SIZE = 0x14;
constexpr gpr MAIN_COUNT = (gpr)(int32_t)0x80142700;      // u16
constexpr gpr MAIN_POINTS = (gpr)(int32_t)0x80142704;
constexpr gpr BRANCH_COUNT = (gpr)(int32_t)0x80142708;    // u8
constexpr gpr BRANCHES = (gpr)(int32_t)0x8014270C;
constexpr uint32_t BRANCH_SIZE = 0x10;
constexpr uint32_t POINT_SIZE = 6;
constexpr uint32_t CARS = 0x80151B00;
constexpr uint32_t CAR_SIZE = 0x2C8;
constexpr int MAIN_ROAD = -1;

struct Stretch {
    int road;
    int point;  // the path from this point to the next runs through the zone
};

// The stretches of path through each zone of the current track, and what they were found for.
std::vector<std::vector<Stretch>> zone_stretches;
uint32_t cached_zones = 0, cached_main = 0, cached_branches = 0;
int cached_track = -100;

float read_float(uint8_t* rdram, gpr addr) {
    uint32_t bits = (uint32_t)MEM_W(0, addr);
    float value;
    memcpy(&value, &bits, sizeof(value));
    return value;
}

// Whether the segment from (x0, z0) to (x1, z1) passes through the square (clipped against each side).
bool segment_crosses_square(float x0, float z0, float x1, float z1, float cx, float cz, float half) {
    float t0 = 0.0f, t1 = 1.0f;
    float d[2] = { x1 - x0, z1 - z0 };
    float lo[2] = { cx - half - x0, cz - half - z0 };
    float hi[2] = { cx + half - x0, cz + half - z0 };
    for (int axis = 0; axis < 2; axis++) {
        if (d[axis] == 0.0f) {
            if (lo[axis] > 0.0f || hi[axis] < 0.0f) {
                return false;
            }
            continue;
        }
        float a = lo[axis] / d[axis], b = hi[axis] / d[axis];
        t0 = std::max(t0, std::min(a, b));
        t1 = std::min(t1, std::max(a, b));
        if (t0 > t1) {
            return false;
        }
    }
    return true;
}

void add_stretches(uint8_t* rdram, std::vector<Stretch> &out, int road, uint32_t points, uint32_t count,
                   float cx, float cz, float half) {
    for (uint32_t k = 0; k + 1 < count; k++) {
        gpr a = (gpr)(int32_t)(points + k * POINT_SIZE);
        gpr b = (gpr)(int32_t)(points + (k + 1) * POINT_SIZE);
        if (segment_crosses_square((float)MEM_H(0, a), (float)MEM_H(4, a), (float)MEM_H(0, b), (float)MEM_H(4, b), cx, cz, half)) {
            out.push_back({ road, (int)k });
        }
    }
}

void find_stretches(uint8_t* rdram, int track, uint32_t zones) {
    uint32_t main_points = (uint32_t)MEM_W(0, MAIN_POINTS), branches = (uint32_t)MEM_W(0, BRANCHES);
    if (track == cached_track && zones == cached_zones && main_points == cached_main && branches == cached_branches) {
        return;
    }
    cached_track = track;
    cached_zones = zones;
    cached_main = main_points;
    cached_branches = branches;
    int count = MEM_BU(0, (gpr)(int32_t)(ZONE_COUNTS + track * 8));
    zone_stretches.assign(count, {});
    for (int i = 0; i < count; i++) {
        gpr zone = (gpr)(int32_t)(zones + i * ZONE_SIZE);
        float cx = read_float(rdram, zone), cz = read_float(rdram, zone + 4), half = read_float(rdram, zone + 8);
        add_stretches(rdram, zone_stretches[i], MAIN_ROAD, main_points, (uint16_t)MEM_HU(0, MAIN_COUNT), cx, cz, half);
        for (int b = 0; b < MEM_BU(0, BRANCH_COUNT); b++) {
            gpr branch = (gpr)(int32_t)(branches + b * BRANCH_SIZE);
            add_stretches(rdram, zone_stretches[i], b, (uint32_t)MEM_W(0xC, branch), (uint16_t)MEM_HU(0xA, branch), cx, cz, half);
        }
    }
}

// In func_800C4F34's zone loop ($s1 = zone index, $s4 = zone, player in the byte at $sp+0x1F).
bool zone_reached_on_path(uint8_t* rdram, recomp_context* ctx) {
    int track = MEM_B(0, TRACK);
    uint32_t zones = (uint32_t)MEM_W(0, (gpr)(int32_t)(ZONE_COUNTS + track * 8 + 4));
    find_stretches(rdram, track, zones);
    int index = (int)ctx->r17;
    if (index < 0 || index >= (int)zone_stretches.size()) {
        return false;
    }
    gpr car = (gpr)(int32_t)(CARS + MEM_BU(0x1F, ctx->r29) * CAR_SIZE);
    int road = MEM_B(0x10C, car);
    int point = (uint16_t)MEM_HU(0x10E, car);
    for (const Stretch &s : zone_stretches[index]) {
        if (s.road == road && (point == s.point || point == s.point + 1)) {
            return true;
        }
    }
    return false;
}

} // namespace

// Before the zone test's comparisons of the car's distance across x ($f2) and z ($f0) with the zone's
// half size: no distance when the car has reached the zone along the path.
extern "C" void cspeed_fog_zone_x(uint8_t* rdram, recomp_context* ctx) {
    if (zone_reached_on_path(rdram, ctx)) {
        ctx->f2.fl = 0.0f;
    }
}

extern "C" void cspeed_fog_zone_z(uint8_t* rdram, recomp_context* ctx) {
    if (zone_reached_on_path(rdram, ctx)) {
        ctx->f0.fl = 0.0f;
    }
}
