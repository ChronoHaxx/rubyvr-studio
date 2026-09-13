// SPDX-License-Identifier: GPL-3.0-or-later
#include "game_input.h"
#include "viewer.h"
#include "live_scene.h"
#include "camera_input.h"
#include "runtime_bus_bridge.h"
#include "gba_bus.h"
#include "sha1.h"
#include "dev_runtime.h"
#include "free_walk_runtime.h"

namespace vr::game_input {
namespace {
camera_input::Mapper mapper;
const uint8_t* checked_rom=nullptr;
bool verified=false;
}
void reset() { mapper.reset();free_walk::runtime::reset();viewer::release_mouse(); }
uint16_t filter(uint16_t keys, bool host_menu_open) {
    if (!viewer::active()) return keys;
    const auto source=(!SDL_GetKeyboardFocus() || host_menu_open || dev::viewer_menu_open())?Source::Inactive:
        viewer::focused()?Source::Viewer:Source::Original;
    if(source==Source::Viewer) {
        const auto* k=SDL_GetKeyboardState(nullptr);
        keys=camera_input::with_wasd(keys,k[SDL_SCANCODE_W],k[SDL_SCANCODE_A],
                                    k[SDL_SCANCODE_S],k[SDL_SCANCODE_D]);
    }
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
            if (viewer::uses_world_controls() && (viewer::camera_relative() || viewer::continuous_movement()) && world::live::field_controls_available(m))
                context=camera_input::Context::Camera;
        }
    }
    auto result=mapper.update(keys,viewer::yaw_radians(),context);
    free_walk::Point vector{};
    if(context==camera_input::Context::Camera && viewer::continuous_movement() && (result&0xf0)!=0xf0) {
        vector=free_walk::direction(keys,viewer::yaw_radians());
        const int dir=free_walk::facing(vector);
        constexpr uint16_t masks[]={0,0x80,0x40,0x20,0x10};
        result=uint16_t((result|0xf0)&~masks[dir]);
    }
    free_walk::runtime::input(vector,source!=Source::Original);
    return result;
}
}
