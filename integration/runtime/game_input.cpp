// SPDX-License-Identifier: GPL-3.0-or-later
#include "game_input.h"
#include "viewer.h"
#include "live_scene.h"
#include "camera_input.h"
#include "runtime_bus_bridge.h"
#include "gba_bus.h"
#include "sha1.h"

namespace vr::game_input {
namespace {
camera_input::Mapper mapper;
const uint8_t* checked_rom=nullptr;
bool verified=false;
}
void reset() { mapper.reset(); }
uint16_t filter(uint16_t keys, bool host_menu_open) {
    if (!viewer::active()) return keys;
    const auto source=(!SDL_GetKeyboardFocus() || host_menu_open)?Source::Inactive:
        viewer::focused()?Source::Viewer:Source::Original;
    return filter_from_source(keys,source);
}
uint16_t filter_from_source(uint16_t keys, Source source) {
    if (!viewer::active()) return keys;
    auto context=camera_input::Context::Original;
    if (source==Source::Inactive) context=camera_input::Context::Inactive;
    else if (source==Source::Viewer) {
        context=camera_input::Context::Menu;
        auto* bus=gbarecomp::active_bus();
        if (bus) {
            if (checked_rom!=bus->rom_ptr()) {
                checked_rom=bus->rom_ptr();
                verified=checked_rom && gba::sha1(checked_rom,bus->rom_size()).hex()==world::live::kRubySha1;
            }
            const world::live::Memory m{{bus->rom_ptr(),bus->rom_size()},
                {bus->ewram_ptr(),0x40000},{bus->iwram_ptr(),0x8000},verified};
            if (viewer::camera_relative() && world::live::field_controls_available(m))
                context=camera_input::Context::Camera;
        }
    }
    return mapper.update(keys,viewer::yaw_radians(),context);
}
}
