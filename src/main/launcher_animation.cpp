#include "cspeed_launcher.h"
#include <atomic>

void cspeed::launcher_animation_setup(recompui::LauncherMenu *menu) {
    auto context = recompui::get_current_context();
    recompui::Element *background_container = menu->get_background_container();
    background_container->set_background_color({ 0x00, 0x00, 0x00, 0xFF });

    (void)context;
}

void cspeed::launcher_animation_update(recompui::LauncherMenu *menu) {
}
