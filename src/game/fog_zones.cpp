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
#include <cstdlib>
#include <cstring>
#include <unordered_map>
#include <utility>
#include <vector>

#include "recomp.h"
#include "cspeed_support.h"

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
    int height; // the point's y
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
            out.push_back({ road, (int)k, MEM_H(2, a) });
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

// The current track's zone list (and the stretches through each zone, found if needed).
uint32_t current_zones(uint8_t* rdram, int &count) {
    int track = MEM_B(0, TRACK);
    uint32_t zones = (uint32_t)MEM_W(0, (gpr)(int32_t)(ZONE_COUNTS + track * 8 + 4));
    find_stretches(rdram, track, zones);
    count = (int)zone_stretches.size();
    return zones;
}

bool car_on_stretch(uint8_t* rdram, int index, gpr car) {
    int road = MEM_B(0x10C, car);
    int point = (uint16_t)MEM_HU(0x10E, car);
    for (const Stretch &s : zone_stretches[index]) {
        if (s.road == road && (point == s.point || point == s.point + 1)) {
            return true;
        }
    }
    return false;
}

// In func_800C4F34's zone loop ($s1 = zone index, $s4 = zone, player in the byte at $sp+0x1F).
bool zone_reached_on_path(uint8_t* rdram, recomp_context* ctx) {
    int count;
    current_zones(rdram, count);
    int index = (int)ctx->r17;
    if (index < 0 || index >= count) {
        return false;
    }
    return car_on_stretch(rdram, index, (gpr)(int32_t)(CARS + MEM_BU(0x1F, ctx->r29) * CAR_SIZE));
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

// Debug view (CSPEED_FOG_ZONES=1): each zone's square drawn as translucent walls in the race view,
// coloured by type (green: back to the track's colour, magenta: a colour, cyan: colour cycle,
// yellow: pulse, white: type 4) and brighter while player 1 has reached it (in the square or along
// the path). The walls rise from the path's height where it crosses the zone (or the car's), which
// can be below the road, and show through the scenery (no depth test). Drawn with the race camera's
// projection, which includes its rotation; like the game's model matrices (multiplied onto the
// current one), the walls' matrix places them relative to the camera (0x801531D4, x y z) in 1/16
// world units.
namespace {

constexpr gpr CAMERA_POSITION = (gpr)(int32_t)0x801531D4;
constexpr float WALL_HEIGHT = 40.0f;                    // world units (a car is about 5 long)
constexpr float DRAW_RANGE = 2000.0f;                   // the game draws up to 2048 units away on an axis
constexpr uint32_t COMMANDS_SIZE = 0x200;               // then the matrices, then the vertices
constexpr uint32_t MATRIX_SIZE = 0x40;
constexpr uint32_t VERTICES_SIZE = 8 * 0x10;
constexpr int MAX_DRAWN = 8;

void write_matrix(uint8_t* rdram, gpr mtx, float scale, float tx, float ty, float tz) {
    float m[16] = { scale, 0, 0, 0,  0, scale, 0, 0,  0, 0, scale, 0,  tx, ty, tz, 1 };
    for (int i = 0; i < 16; i++) {
        int32_t fixed = (int32_t)std::lround(m[i] * 65536.0f);
        MEM_H(i * 2, mtx) = (int16_t)(fixed >> 16);
        MEM_H(32 + i * 2, mtx) = (int16_t)(fixed & 0xFFFF);
    }
}

uint32_t zone_color(int type, bool reached) {
    uint32_t rgb;
    switch (type) {
        case 0: rgb = 0x40FF40; break;
        case 1: rgb = 0xFF40FF; break;
        case 2: rgb = 0x40FFFF; break;
        case 3: rgb = 0xFFFF40; break;
        default: rgb = 0xFFFFFF; break;
    }
    return (rgb << 8) | (reached ? 0xB0 : 0x50);
}

} // namespace

extern "C" float cspeed_world_scale();

bool cspeed::fog_zones_visible() {
    static const bool visible = getenv("CSPEED_FOG_ZONES") != nullptr;
    return visible;
}

void cspeed::draw_fog_zones(uint8_t* rdram, uint32_t slot_addr, uint32_t list_addr, bool replaced_first) {
    int count;
    uint32_t zones = current_zones(rdram, count);
    gpr slot = (gpr)(int32_t)slot_addr;
    gpr cmd = (gpr)(int32_t)list_addr;
    auto emit = [&](uint32_t w0, uint32_t w1) {
        MEM_W(0, cmd) = (int32_t)w0;
        MEM_W(4, cmd) = (int32_t)w1;
        cmd += 8;
    };
    // The game sends the same display list again while the race is frozen (the free camera): the slot
    // then already calls this list, and the command it replaced is the one saved the first time.
    static std::unordered_map<uint32_t, std::pair<uint32_t, uint32_t>> replaced;
    uint32_t replaced_w0 = (uint32_t)MEM_W(0, slot), replaced_w1 = (uint32_t)MEM_W(4, slot);
    if (replaced_w0 == 0xDE000000 && replaced_w1 == list_addr) {
        auto it = replaced.find(slot_addr);
        if (it == replaced.end()) {
            return;
        }
        replaced_w0 = it->second.first;
        replaced_w1 = it->second.second;
    }
    else {
        replaced[slot_addr] = { replaced_w0, replaced_w1 };
    }
    if (replaced_first) {
        emit(replaced_w0, replaced_w1);
    }
    emit(0xE7000000, 0);                    // gDPPipeSync
    emit(0xD9000000, 0x00200004);           // gSPGeometryMode: only shade, smooth shading
    emit(0xD7000000, 0);                    // gSPTexture off
    emit(0xE3000A01, 0);                    // gDPSetCycleType(G_CYC_1CYCLE)
    emit(0xE200001C, 0x00504A40);           // gDPSetRenderMode(G_RM_XLU_SURF, G_RM_XLU_SURF2): no depth test
    emit(0xFCFFFFFF, 0xFFFE793C);           // gDPSetCombineMode(G_CC_SHADE, G_CC_SHADE)

    float scale = cspeed_world_scale();
    float cam[3] = { read_float(rdram, CAMERA_POSITION), read_float(rdram, CAMERA_POSITION + 4), read_float(rdram, CAMERA_POSITION + 8) };
    gpr car = (gpr)(int32_t)CARS;
    float car_x = read_float(rdram, car), car_y = read_float(rdram, car + 4), car_z = read_float(rdram, car + 8);
    int drawn = 0;
    for (int i = 0; i < count && drawn < MAX_DRAWN; i++) {
        gpr zone = (gpr)(int32_t)(zones + i * ZONE_SIZE);
        float cx = read_float(rdram, zone), cz = read_float(rdram, zone + 4), half = read_float(rdram, zone + 8);
        float base = zone_stretches[i].empty() ? car_y : (float)zone_stretches[i].front().height;
        if (std::abs(cx - cam[0]) * scale > DRAW_RANGE || std::abs(cz - cam[2]) * scale > DRAW_RANGE ||
            std::abs(base - cam[1]) * scale > DRAW_RANGE) {
            continue;
        }
        bool reached = (std::abs(cx - car_x) < half && std::abs(cz - car_z) < half) || car_on_stretch(rdram, i, car);
        uint32_t color = zone_color(MEM_W(0xC, zone), reached);

        uint32_t mtx_addr = list_addr + COMMANDS_SIZE + drawn * MATRIX_SIZE;
        uint32_t vtx_addr = list_addr + COMMANDS_SIZE + MAX_DRAWN * MATRIX_SIZE + drawn * VERTICES_SIZE;
        write_matrix(rdram, (gpr)(int32_t)mtx_addr, scale,
                     (cx - cam[0]) * 16.0f * scale, (base - cam[1]) * 16.0f * scale, (cz - cam[2]) * 16.0f * scale);
        int16_t h = (int16_t)std::lround(half * 16.0f), top = (int16_t)std::lround(WALL_HEIGHT * 16.0f);
        int16_t corners[4][2] = { { (int16_t)-h, (int16_t)-h }, { h, (int16_t)-h }, { h, h }, { (int16_t)-h, h } };
        for (int v = 0; v < 8; v++) {
            gpr vtx = (gpr)(int32_t)(vtx_addr + v * 0x10);
            MEM_H(0x0, vtx) = corners[v % 4][0];
            MEM_H(0x2, vtx) = v < 4 ? 0 : top;
            MEM_H(0x4, vtx) = corners[v % 4][1];
            MEM_H(0x6, vtx) = 0;
            MEM_H(0x8, vtx) = 0;
            MEM_H(0xA, vtx) = 0;
            MEM_W(0xC, vtx) = (int32_t)color;
        }
        emit(0xDA380000, mtx_addr);          // gSPMatrix(G_MTX_MODELVIEW | G_MTX_MUL | G_MTX_PUSH), as the game does
        emit(0x01008010, vtx_addr);          // gSPVertex(8 vertices, from 0)
        for (int side = 0; side < 4; side++) {  // gSP2Triangles: one wall each
            uint32_t a = side, b = (side + 1) % 4, c = 4 + b, d = 4 + a;
            emit(0x06000000 | (a * 2) << 16 | (b * 2) << 8 | (c * 2), (a * 2) << 16 | (c * 2) << 8 | (d * 2));
        }
        emit(0xD8380002, MATRIX_SIZE);       // gSPPopMatrix
        drawn++;
    }
    emit(0xE7000000, 0);                    // gDPPipeSync
    if (!replaced_first) {
        emit(replaced_w0, replaced_w1);
    }
    emit(0xDF000000, 0);                    // gSPEndDisplayList
    MEM_W(0, slot) = (int32_t)0xDE000000;   // gSPDisplayList: call it instead
    MEM_W(4, slot) = (int32_t)list_addr;
}
