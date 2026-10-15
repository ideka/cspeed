// Native replacements for game functions whose behaviour depends on N64 timing the runtime does not
// reproduce. Each one is renamed for the recompiler in symbols/recomp_names.json and listed as
// ignored in cspeed.us.toml.

#include "recomp.h"
#include "librecomp/helpers.hpp"

extern "C" void osRecvMesg_recomp(uint8_t* rdram, recomp_context* ctx);
extern "C" void romLockInit(uint8_t* rdram, recomp_context* ctx);

// romLockTryAcquire (0x80053A64) takes the lock that serializes compressed ROM loads, without
// blocking: if another thread holds it, it sets the "busy" flag (0x8014B188) and romLoadCompressed
// returns NULL. The asset bank loader does not check for that and registers an empty bank at the
// current heap top; the next bank loaded lands on the same memory, the bank's pointers get
// relocated twice and a later lookup (e.g. "PLATE3" in the menu) dereferences NULL.
//
// The lock is held by the music loader thread while it decompresses a song. On hardware that is
// effectively never interleaved with a bank load, but in the runtime every ROM DMA completes
// asynchronously, so pressing Start while the title music changes reliably hit it. Wait for the
// lock instead of failing; its only holders always release it.
extern "C" void cspeed_romLockTryAcquire(uint8_t* rdram, recomp_context* ctx) {
    constexpr gpr lock_initialized = (gpr)(int32_t)0x800553C9;
    constexpr int32_t lock_queue = (int32_t)0x800DD640;
    if (MEM_BU(0, lock_initialized) == 0) {
        romLockInit(rdram, ctx);
    }
    ctx->r4 = lock_queue;
    ctx->r5 = 0;                // no message pointer needed
    ctx->r6 = OS_MESG_BLOCK;   // wait for the lock
    osRecvMesg_recomp(rdram, ctx);
    ctx->r2 = 1;
}
