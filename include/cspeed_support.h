#ifndef __CSPEED_SUPPORT_H__
#define __CSPEED_SUPPORT_H__

#include <functional>
#include <filesystem>
#include <vector>
#include <optional>
#include <list>

namespace cspeed {
    std::filesystem::path get_program_path();
    std::filesystem::path get_asset_path(const char* asset);
    void open_file_dialog(std::function<void(bool success, const std::filesystem::path& path)> callback);
    void open_file_dialog_multiple(std::function<void(bool success, const std::list<std::filesystem::path>& paths)> callback);
    void show_error_message_box(const char *title, const char *message);
    // The stick as the input layer gave it (-1 to 1), for the full analog controls.
    void set_host_stick(int controller, float x, float y);
    // CSPEED_DEBUG=1 (src/game/debug_tools.cpp).
    bool debug_tools_enabled();
    bool debug_no_fog();

// Apple specific methods that usually require Objective-C. Implemented in support_apple.mm.
#ifdef __APPLE__
    void dispatch_on_ui_thread(std::function<void()> func);
    std::optional<std::filesystem::path> get_application_support_directory();
    std::filesystem::path get_bundle_resource_directory();
    std::filesystem::path get_bundle_directory();
#endif
}

#endif
