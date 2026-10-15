#ifndef __CSPEED_CONFIG_H__
#define __CSPEED_CONFIG_H__

#include <filesystem>
#include <string>

namespace cspeed {
    inline const std::u8string program_id = u8"CaliforniaSpeedRecompiled";
    inline const std::string program_name = "California Speed: Recompiled";

    namespace configkeys {
        namespace general {
        }
    }

    // TODO: Move loading configs to the runtime once we have a way to allow per-project customization.
    void init_config();
};

#endif
