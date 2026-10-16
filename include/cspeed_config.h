#ifndef __CSPEED_CONFIG_H__
#define __CSPEED_CONFIG_H__

#include <filesystem>
#include <cstdint>
#include <string>

namespace cspeed {
    inline const std::u8string program_id = u8"CaliforniaSpeedRecompiled";
    inline const std::string program_name = "California Speed: Recompiled";

    namespace configkeys {
        namespace enhancements {
            inline const std::string id = "enhancements";
            inline const std::string tab_name = "Enhancements";
            inline const std::string analog_controls = "analog_controls";
            inline const std::string level_of_detail = "level_of_detail";
            inline const std::string sample_rate = "sample_rate";
        }
    }

    // TODO: Move loading configs to the runtime once we have a way to allow per-project customization.
    void init_config();

    // Current values of the game options, cheap to read from game code.
    bool full_analog_controls();
    bool max_level_of_detail();
    uint32_t audio_sample_rate();
};

#endif
