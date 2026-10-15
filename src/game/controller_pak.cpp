// Controller Pak emulation.
//
// The runtime has no Controller Pak support, so the game's own libultra filesystem code (osPfs*,
// __osGetId, __osPfsRWInode, ...) is recompiled as-is (see symbols/recomp_names.json) and only the
// functions that talk to the pak over the Serial Interface are replaced here. Each controller gets
// a 32 KiB single-bank pak backed by a standard .mpk image, the format emulators use, stored next
// to the runtime's save file. A pak that was never written to starts out freshly formatted and is
// not written to disk until the game saves something.

#include <array>
#include <chrono>
#include <cstring>
#include <fstream>
#include <mutex>
#include <thread>

#include "recomp.h"
#include "librecomp/helpers.hpp"
#include "librecomp/files.hpp"
#include "ultramodern/ultramodern.hpp"
#include "ultramodern/input.hpp"

ultramodern::input::connected_device_info_t get_connected_device_info(int controller_num);

namespace {

constexpr int32_t PFS_ERR_NOPACK = 1;
constexpr size_t PAK_SIZE = 0x8000;
constexpr size_t BLOCK_SIZE = 32;
constexpr uint16_t PAK_BLOCKS = PAK_SIZE / BLOCK_SIZE;  // 0x400; addresses past this are pak registers
constexpr int NUM_CHANNELS = 4;

struct Pak {
    std::array<uint8_t, PAK_SIZE> data{};
    bool loaded = false;
    bool dirty = false;
    std::chrono::steady_clock::time_point last_write{};
};

std::mutex pak_mutex;
std::array<Pak, NUM_CHANNELS> paks;
std::once_flag flush_thread_started;

std::filesystem::path pak_path(int channel) {
    std::filesystem::path save = ultramodern::get_save_file_path();
    std::filesystem::path name = save.stem();
    name += "_controller_pak_" + std::to_string(channel + 1) + ".mpk";
    return save.parent_path() / name;
}

uint16_t be16(const uint8_t* p) { return (uint16_t)((p[0] << 8) | p[1]); }
void put_be16(uint8_t* p, uint16_t v) { p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v; }

// Lays out an empty, valid 1-bank filesystem the way libultra expects it (ID block and its three
// backups, inode table and its backup with all data pages free, empty note table).
void format_pak(std::array<uint8_t, PAK_SIZE>& d) {
    d.fill(0);
    uint8_t id[32]{};
    memset(id + 0x00, 0xFF, 4);                       // repaired flag: 0xFFFFFFFF = never repaired
    const uint8_t random[4] = { 0x43, 0x53, 0x50, 0x44 };
    memcpy(id + 0x04, random, 4);                     // random
    memset(id + 0x10, 0xFF, 8);                       // serial (low)
    put_be16(id + 0x18, 0x0001);                      // device id (bit 0 set: Controller Pak)
    id[0x1A] = 1;                                     // banks
    id[0x1B] = 0;                                     // version
    uint16_t sum = 0;
    for (int i = 0; i < 0x1C; i += 2) {
        sum += be16(id + i);
    }
    put_be16(id + 0x1C, sum);
    put_be16(id + 0x1E, (uint16_t)(0xFFF2 - sum));
    for (size_t off : { 0x20, 0x60, 0x80, 0xC0 }) {
        memcpy(&d[off], id, sizeof(id));
    }

    // Pages 0-4 hold the ID, inode table, its backup and the two note table pages; 5-127 are data.
    constexpr int inode_start_page = 5;
    for (size_t table : { 0x100, 0x200 }) {
        uint8_t checksum = 0;
        for (int page = inode_start_page; page < 128; page++) {
            put_be16(&d[table + page * 2], 0x0003);   // free page
            checksum += 0x03;
        }
        d[table + 1] = checksum;                      // inode_page[0].page holds the checksum
    }
}

Pak& get_pak(int channel) {
    Pak& pak = paks[channel];
    if (!pak.loaded) {
        pak.loaded = true;
        std::ifstream file = recomp::open_input_file_with_backup(pak_path(channel), std::ios_base::binary);
        if (file.good() && file.read(reinterpret_cast<char*>(pak.data.data()), PAK_SIZE)) {
            return pak;
        }
        format_pak(pak.data);
    }
    return pak;
}

void write_pak_file(int channel, const std::array<uint8_t, PAK_SIZE>& data) {
    std::filesystem::path path = pak_path(channel);
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    bool ok = false;
    {
        std::ofstream file = recomp::open_output_file_with_backup(path, std::ios_base::binary);
        ok = file.good() && file.write(reinterpret_cast<const char*>(data.data()), PAK_SIZE).good();
    }
    if (!ok || !recomp::finalize_output_file_with_backup(path)) {
        ultramodern::error_handling::message_box("Failed to write the Controller Pak file. Check your file permissions.");
    }
}

// Writes paks to disk once the game has stopped writing to them for a moment, so a save that is
// made of hundreds of 32-byte writes turns into one file write.
void flush_thread_func() {
    using namespace std::chrono_literals;
    while (true) {
        std::this_thread::sleep_for(100ms);
        for (int channel = 0; channel < NUM_CHANNELS; channel++) {
            std::array<uint8_t, PAK_SIZE> snapshot;
            {
                std::lock_guard lock{ pak_mutex };
                Pak& pak = paks[channel];
                if (!pak.dirty || std::chrono::steady_clock::now() - pak.last_write < 250ms) {
                    continue;
                }
                pak.dirty = false;
                snapshot = pak.data;
            }
            write_pak_file(channel, snapshot);
        }
    }
}

bool pak_present(int channel) {
    if (channel < 0 || channel >= NUM_CHANNELS) {
        return false;
    }
    return get_connected_device_info(channel).connected_device != ultramodern::input::Device::None;
}

} // namespace

// s32 __osContRamRead(OSMesgQueue* mq, int channel, u16 address, u8* buffer)
extern "C" void cspeed_osContRamRead(uint8_t* rdram, recomp_context* ctx) {
    int channel = (int)ctx->r5;
    uint16_t address = (uint16_t)ctx->r6;
    gpr buffer = ctx->r7;
    if (!pak_present(channel)) {
        ctx->r2 = PFS_ERR_NOPACK;
        return;
    }
    std::lock_guard lock{ pak_mutex };
    Pak& pak = get_pak(channel);
    for (size_t i = 0; i < BLOCK_SIZE; i++) {
        // Past the RAM area are the bank select and accessory registers; read them as zero.
        MEM_B(i, buffer) = address < PAK_BLOCKS ? pak.data[address * BLOCK_SIZE + i] : 0;
    }
    ctx->r2 = 0;
}

// s32 __osContRamWrite(OSMesgQueue* mq, int channel, u16 address, u8* buffer, int force)
extern "C" void cspeed_osContRamWrite(uint8_t* rdram, recomp_context* ctx) {
    int channel = (int)ctx->r5;
    uint16_t address = (uint16_t)ctx->r6;
    gpr buffer = ctx->r7;
    if (!pak_present(channel)) {
        ctx->r2 = PFS_ERR_NOPACK;
        return;
    }
    // Writes past the RAM area select a bank (only bank 0 exists) or drive accessories: ignore them.
    if (address < PAK_BLOCKS) {
        std::lock_guard lock{ pak_mutex };
        Pak& pak = get_pak(channel);
        for (size_t i = 0; i < BLOCK_SIZE; i++) {
            pak.data[address * BLOCK_SIZE + i] = MEM_B(i, buffer);
        }
        pak.dirty = true;
        pak.last_write = std::chrono::steady_clock::now();
        std::call_once(flush_thread_started, [] { std::thread{ flush_thread_func }.detach(); });
    }
    ctx->r2 = 0;
}

// s32 __osPfsGetStatus(OSMesgQueue* queue, int channel)
extern "C" void cspeed_osPfsGetStatus(uint8_t* rdram, recomp_context* ctx) {
    ctx->r2 = pak_present((int)ctx->r5) ? 0 : PFS_ERR_NOPACK;
}

// s32 osPfsIsPlug(OSMesgQueue* queue, u8* pattern)
extern "C" void cspeed_osPfsIsPlug(uint8_t* rdram, recomp_context* ctx) {
    uint8_t pattern = 0;
    for (int channel = 0; channel < NUM_CHANNELS; channel++) {
        if (pak_present(channel)) {
            pattern |= 1 << channel;
        }
    }
    MEM_B(0, ctx->r5) = pattern;
    ctx->r2 = 0;
}

// Hook at the end of each channel's iteration of the game's pak probe (func_8005D1C8, $s2 = channel).
// A real controller holds either a Controller Pak or a Rumble Pak, and the game asks the player to
// swap them around saves. The emulated pak is both at once: the probe has just found a Controller
// Pak (which clears the channel's rumble flag), so turn rumble on for that channel as well. The
// rumble code only looks at this bitmask and at the channel's motor OSPfs.
extern "C" void cspeed_after_pak_probe(uint8_t* rdram, recomp_context* ctx) {
    constexpr gpr rumble_mask = (gpr)(int32_t)0x800E1B81;    // bit n: Rumble Pak in controller n
    constexpr uint32_t motor_pfs = 0x800E1D38;               // OSPfs[4], 0x68 bytes each
    constexpr uint32_t si_queue = 0x8014AC58;                // queue the game passes to osMotorInit
    int channel = (int)ctx->r18;
    if (!pak_present(channel)) {
        return;
    }
    MEM_B(0, rumble_mask) = MEM_B(0, rumble_mask) | (1 << channel);
    gpr pfs = (gpr)(int32_t)(motor_pfs + channel * 0x68);
    MEM_W(0x4, pfs) = (int32_t)si_queue;   // queue
    MEM_W(0x8, pfs) = channel;             // channel (what the runtime's osMotorStart/Stop use)

    // The controller thread only assigns controllers to player slots (0x8016D17E[]) when this flag
    // asks it to re-query them. A failed pak probe sets it directly; after a successful one the game
    // only sets it on its next call of the probe, which may not come before input is needed (with
    // no slot assigned, all input is ignored). Ask for the re-query now.
    constexpr gpr requery_controllers = (gpr)(int32_t)0x800200B0;
    MEM_B(0, requery_controllers) = 1;
}
