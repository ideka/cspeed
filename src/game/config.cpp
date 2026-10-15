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
#include <iomanip>

#if defined(_WIN32)
#include <Shlobj.h>
#elif defined(__linux__)
#include <unistd.h>
#include <pwd.h>
#elif defined(__APPLE__)
#include "apple/rt64_apple.h"
#endif

static void add_general_options(recomp::config::Config &config) {
    using EnumOptionVector = const std::vector<recomp::config::ConfigOptionEnumOption>;

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
    // User Task: Add game-specific graphics options.
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

    recompui::config::create_mods_tab();

    recompui::config::finalize();

}
