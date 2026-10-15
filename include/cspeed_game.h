#ifndef __CSPEED_GAME_H__
#define __CSPEED_GAME_H__

#include <cstdint>
#include <span>
#include <vector>
#include "recomp.h"

namespace cspeed {
    void game_on_init(uint8_t* rdram, recomp_context* ctx);
};

#endif
