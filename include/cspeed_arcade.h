#ifndef __CSPEED_ARCADE_H__
#define __CSPEED_ARCADE_H__

#include <filesystem>
#include <functional>
#include <string>

// Optional arcade textures: the arcade version's hard drive image (calspeed.chd, provided by the user)
// is read to build an RT64 texture pack of its higher resolution textures, which replace the matching
// N64 ones. Only the mapping between the two (assets/arcade_textures.json) ships with the game.
namespace cspeed::arcade {
    // Where the generated pack is kept (in the app folder).
    std::filesystem::path pack_directory();

    // True if a pack was generated for the current mapping.
    bool pack_available();

    // Builds the pack from the arcade hard drive image. Returns an empty string on success, or an error
    // message. progress (optional) is called with 0..1.
    std::string generate_pack(const std::filesystem::path &chd_path, std::function<void(float)> progress = nullptr);

    // Loads or unloads the pack according to the Arcade Textures option.
    void apply_pack_setting();

    // Call once the config is loaded: loads the pack, rebuilding it first if the mapping changed.
    void startup();

    // Lets the user pick the hard drive image and builds the pack in the background.
    void select_arcade_data();
}

#endif
