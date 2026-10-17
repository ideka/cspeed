#include "cspeed_config.h"
#include "recompui/recompui.h"
#include "recompui/config.h"
#include "recompinput/recompinput.h"
#include "cspeed_support.h"
#include "ultramodern/config.hpp"
#include "librecomp/files.hpp"
#include "librecomp/config.hpp"
#include "util/file.h"
#include <filesystem>
#include <fstream>
#include <atomic>
#include <iomanip>

#if defined(_WIN32)
#include <Shlobj.h>
#elif defined(__linux__)
#include <unistd.h>
#include <pwd.h>
#elif defined(__APPLE__)
#include "apple/rt64_apple.h"
#endif

namespace {
    std::atomic<bool> analog_controls_enabled = true;
    std::atomic<bool> max_lod_enabled = true;
    std::atomic<bool> skip_copyright_enabled = false;
    std::atomic<bool> smooth_radar_enabled = true;
    std::atomic<uint32_t> sample_rate_value = 48000;
}

bool cspeed::full_analog_controls() { return analog_controls_enabled; }
bool cspeed::max_level_of_detail() { return max_lod_enabled; }
bool cspeed::skip_copyright_screen() { return skip_copyright_enabled; }
bool cspeed::smooth_radar() { return smooth_radar_enabled; }
uint32_t cspeed::audio_sample_rate() { return sample_rate_value; }

namespace keys = cspeed::configkeys::enhancements;


static void add_general_options(recomp::config::Config &config) {
}

template <typename T = uint32_t>
T get_general_config_enum_value(const std::string& option_id) {
    return static_cast<T>(std::get<uint32_t>(recompui::config::get_general_config().get_option_value(option_id)));
}

template <typename T = uint32_t>
T get_general_config_number_value(const std::string& option_id) {
    return static_cast<T>(std::get<double>(recompui::config::get_general_config().get_option_value(option_id)));
}

template <typename T = uint32_t>
T get_graphics_config_enum_value(const std::string& option_id) {
    return static_cast<T>(std::get<uint32_t>(recompui::config::get_graphics_config().get_option_value(option_id)));
}

static void add_graphics_options(recomp::config::Config &config) {
}

// The game's enhancements, each optional. Changes apply right away (no Apply button), except the sample
// rate, which needs a restart.
static void add_enhancement_options(recomp::config::Config &config) {
    using recomp::config::ConfigValueVariant;
    using recomp::config::OptionChangeContext;

    config.add_bool_option(
        keys::analog_controls,
        "Full Analog Controls",
        "Steers with the full precision of the analog stick instead of the N64 stick's steps, and makes gas and "
        "brake analog when they are bound to analog inputs, such as the triggers.",
        true
    );
    config.add_option_change_callback(keys::analog_controls, [](ConfigValueVariant cur, ConfigValueVariant, OptionChangeContext) {
        analog_controls_enabled = std::get<bool>(cur);
    });

    config.add_bool_option(
        keys::level_of_detail,
        "Maximum Level of Detail",
        "Always draws models at their highest detail and does not hide distant objects early.",
        true
    );
    config.add_option_change_callback(keys::level_of_detail, [](ConfigValueVariant cur, ConfigValueVariant, OptionChangeContext) {
        max_lod_enabled = std::get<bool>(cur);
    });

    config.add_bool_option(
        keys::skip_copyright,
        "Skip Copyright Screen",
        "Goes straight to the intro at startup, without the copyright screen.",
        false
    );
    config.add_option_change_callback(keys::skip_copyright, [](ConfigValueVariant cur, ConfigValueVariant, OptionChangeContext) {
        skip_copyright_enabled = std::get<bool>(cur);
    });

    config.add_bool_option(
        keys::smooth_radar,
        "Smooth Radar",
        "Moves the cars on the radar smoothly at the display resolution, instead of jumping from one "
        "low-resolution pixel to the next.",
        true
    );
    config.add_option_change_callback(keys::smooth_radar, [](ConfigValueVariant cur, ConfigValueVariant, OptionChangeContext) {
        smooth_radar_enabled = std::get<bool>(cur);
    });

    static const std::vector<uint32_t> rates = { 22050, 32000, 44100, 48000 };
    config.add_enum_option(
        keys::sample_rate,
        "Sample Rate",
        "The rate the game mixes its sound at. The original 22050 Hz loses the treble of the sound effects. "
        "<b>Takes effect after restarting the game.</b>",
        {
            { 0, "22050", "22050 Hz (Original)" },
            { 1, "32000", "32000 Hz" },
            { 2, "44100", "44100 Hz" },
            { 3, "48000", "48000 Hz" },
        },
        3u
    );
    config.add_option_change_callback(keys::sample_rate, [](ConfigValueVariant cur, ConfigValueVariant, OptionChangeContext) {
        uint32_t index = std::get<uint32_t>(cur);
        sample_rate_value = index < rates.size() ? rates[index] : rates.back();
    });

}

static void set_control_defaults() {
    using namespace recompinput;

    // TODO default controls overrides
}

static void set_control_descriptions() {
    // @User Task: Set control descriptions for each game input. These descriptions will be shown in the UI when the
    // user is rebinding controls, so they should be clear and concise.
    recompinput::set_game_input_description(recompinput::GameInput::Y_AXIS_POS, "Add description");
    recompinput::set_game_input_description(recompinput::GameInput::Y_AXIS_NEG, "Add description");
    recompinput::set_game_input_description(recompinput::GameInput::X_AXIS_NEG, "Add description");
    recompinput::set_game_input_description(recompinput::GameInput::X_AXIS_POS, "Add description");
    recompinput::set_game_input_description(recompinput::GameInput::A,          "Add description");
    recompinput::set_game_input_description(recompinput::GameInput::B,          "Add description");
    recompinput::set_game_input_description(recompinput::GameInput::Z,          "Add description");
    recompinput::set_game_input_description(recompinput::GameInput::L,          "Add description");
    recompinput::set_game_input_description(recompinput::GameInput::R,          "Add description");
    recompinput::set_game_input_description(recompinput::GameInput::START,      "Add description");
    recompinput::set_game_input_description(recompinput::GameInput::C_UP,       "Add description");
    recompinput::set_game_input_description(recompinput::GameInput::C_DOWN,     "Add description");
    recompinput::set_game_input_description(recompinput::GameInput::C_LEFT,     "Add description");
    recompinput::set_game_input_description(recompinput::GameInput::C_RIGHT,    "Add description");
    recompinput::set_game_input_description(recompinput::GameInput::DPAD_UP,    "Add description");
    recompinput::set_game_input_description(recompinput::GameInput::DPAD_DOWN,  "Add description");
    recompinput::set_game_input_description(recompinput::GameInput::DPAD_LEFT,  "Add description");
    recompinput::set_game_input_description(recompinput::GameInput::DPAD_RIGHT, "Add description");
}

void cspeed::init_config() {
    std::filesystem::path recomp_dir = recompui::file::get_app_folder_path();

    if (!recomp_dir.empty()) {
        std::filesystem::create_directories(recomp_dir);
    }

    recompui::config::GeneralTabOptions general_options{};
    general_options.has_rumble_strength = true;
    general_options.has_gyro_sensitivity = false;
    general_options.has_mouse_sensitivity = false;

    auto &general_config = recompui::config::create_general_tab(general_options);
    add_general_options(general_config);

    auto &graphics_config = recompui::config::create_graphics_tab();
    add_graphics_options(graphics_config);

    set_control_defaults();
    set_control_descriptions();
    recompui::config::create_controls_tab();

    recompui::config::create_sound_tab();

    auto &enhancements_config = recompui::config::create_config_tab(keys::tab_name, keys::id, false);
    add_enhancement_options(enhancements_config);

    recompui::config::create_mods_tab();

    recompui::config::finalize();

}
