// SPDX-License-Identifier: GPL-3.0-or-later
#include "dev_runtime.h"
#include "runtime.h"
#include "recomp_runtime_ui.h"
#include "runtime_bus_bridge.h"
#include "gba_bus.h"
#include "sha1.h"
#include "live_scene.h"
#include "mod_function_hooks.h"
#include "viewer.h"
#include "game_input.h"
#include "camera_input.h"
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <algorithm>
#include <stdexcept>
#include <thread>

namespace vr::dev {
namespace {
using Clock = std::chrono::steady_clock;
std::unique_ptr<rubyvr::dev::Session> session;
rubyvr::dev::Transport transport;
std::string name = "checkpoint", error, scene = "Waiting for game";
bool noclip = false, verified = false;
const uint8_t* checked_rom = nullptr;
uint64_t frame_number = 0, bypasses = 0;
Clock::time_point deadline{}, last_present{};
constexpr char hook_id[] = "rubyvr.dev.noclip";
constexpr uint32_t collision_entry = 0x08058DD4;

world::live::Memory memory() {
    auto* bus = gbarecomp::active_bus();
    if (!bus) return {};
    if (checked_rom != bus->rom_ptr()) {
        checked_rom = bus->rom_ptr();
        verified = checked_rom && gba::sha1(checked_rom, bus->rom_size()).hex() == world::live::kRubySha1;
    }
    return {{bus->rom_ptr(), bus->rom_size()}, {bus->ewram_ptr(), 0x40000},
            {bus->iwram_ptr(), 0x8000}, verified};
}
uint16_t u16(const uint8_t* p) { return uint16_t(p[0]) | uint16_t(p[1]) << 8; }
int collision(uint32_t address, int thumb, ArmCpuState* cpu) {
    if (!session || !noclip || address != collision_entry || !thumb || !cpu) return 0;
    const auto m = memory();
    const auto s = world::live::inspect(m);
    if (s.status != world::live::Status::Field) return 0;
    const auto* avatar = m.read(world::kGPlayerAvatar, 6);
    if (!avatar || !(avatar[0] & 1) || (avatar[0] & 0x1e) || avatar[5] >= 16) return 0;
    const auto* object = m.read(world::kGObjectEvents + avatar[5] * 0x24, 0x24);
    if (!object || !(object[0] & 1) || !(object[2] & 1)) return 0;
    const unsigned direction = cpu->R[0];
    if (direction < 1 || direction > 4 || !(cpu->R[14] & 1)) return 0;
    int x = static_cast<int16_t>(u16(object + 0x10));
    int y = static_cast<int16_t>(u16(object + 0x12));
    x += direction == 4 ? 1 : direction == 3 ? -1 : 0;
    y += direction == 1 ? 1 : direction == 2 ? -1 : 0;
    // Preserve the original transition path at borders. No raw object, map,
    // script, collision or save bytes are patched, even while clipping.
    if (x < 7 || y < 7 || x >= s.width - 8 || y >= s.height - 7) return 0;
    cpu->R[0] = 0;
    cpu->R[15] = cpu->R[14] & ~1u;
    ++bypasses;
    return 1;
}
const bool registered = gba_mod_register_function_entry_plugin(hook_id, collision_entry, 1, collision) != 0;

const char* const speeds[] = {"1x (normal)", "2x", "4x", "8x", "16x", "32x", "64x", "MAX (uncapped)"};
constexpr int speed_values[] = {1, 2, 4, 8, 16, 32, 64, 0};
const char* const views[]={"North up","West up","South up","East up"};
RecompRuntimeUiItem items[] = {
    {"camera.view", "Camera", "View direction", "Choose which compass direction appears toward the top of the 3D view.", RECOMP_RUNTIME_UI_CHOICE, 0, 3, 1, views, 4},
    {"camera.relative", "Camera", "Movement follows 3D camera", "Applies while the 3D window is focused. Original game and menu directions stay unchanged.", RECOMP_RUNTIME_UI_BOOL, 0, 1, 1},
    {"camera.reset", "Camera", "Reset north-up", "Restore the north-up tilted view, normal zoom and player following.", RECOMP_RUNTIME_UI_ACTION},
    {"dev.speed", "Developer", "Game speed", "Whole-game speed. MAX is hardware-limited. Accelerated audio is muted.", RECOMP_RUNTIME_UI_CHOICE, 0, 7, 1, speeds, 8},
    {"dev.pause", "Developer", "Pause game", "Freeze guest simulation while the menu remains responsive.", RECOMP_RUNTIME_UI_BOOL, 0, 1, 1},
    {"dev.step", "Developer", "Advance one frame", "Run to the next VBlank and pause again.", RECOMP_RUNTIME_UI_ACTION},
    {"dev.noclip", "Developer", "Walk through obstacles", "On foot within this map. Events still run.", RECOMP_RUNTIME_UI_BOOL, 0, 1, 1},
    {"dev.scene", "Developer", "Location / frame", "", RECOMP_RUNTIME_UI_TEXT},
    {"dev.selected", "Checkpoints", "Checkpoint", "", RECOMP_RUNTIME_UI_TEXT},
    {"dev.previous", "Checkpoints", "Previous checkpoint", "Select the previous named situation.", RECOMP_RUNTIME_UI_ACTION},
    {"dev.next", "Checkpoints", "Next checkpoint", "Select the next named situation.", RECOMP_RUNTIME_UI_ACTION},
    {"dev.load", "Checkpoints", "Load checkpoint", "Restore the selected situation. Noclip switches off; speed returns to normal.", RECOMP_RUNTIME_UI_ACTION},
    {"dev.name", "Checkpoints", "New checkpoint name", "Examples: Before trainer, Oldale forest, Before battle.", RECOMP_RUNTIME_UI_TEXT},
    {"dev.save", "Checkpoints", "Save new checkpoint", "Capture this situation. Existing names are never overwritten.", RECOMP_RUNTIME_UI_ACTION},
    {"dev.status", "Checkpoints", "Last action", "", RECOMP_RUNTIME_UI_TEXT},
};
int get(const char* key, int* value) {
    if (!std::strcmp(key,"camera.view")) *value=camera_input::quadrant(viewer::yaw_radians());
    else if (!std::strcmp(key,"camera.relative")) *value=viewer::camera_relative();
    else if (!std::strcmp(key, "dev.pause")) *value = transport.paused();
    else if (!std::strcmp(key, "dev.noclip")) *value = noclip;
    else if (!std::strcmp(key, "dev.speed")) {
        *value = 0;
        for (int i = 0; i < 8; ++i) if (transport.speed() == speed_values[i]) *value = i;
    } else return 0;
    return 1;
}
int set(const char* key, int value) {
    if (!std::strcmp(key,"camera.view") && value>=0 && value<4) viewer::set_yaw_radians(value*1.570796327f);
    else if (!std::strcmp(key,"camera.relative")) viewer::set_camera_relative(value!=0);
    else if (!std::strcmp(key, "dev.pause")) pause(value != 0);
    else if (!std::strcmp(key, "dev.noclip")) {
        noclip = value && verified && registered;
        const bool found = gba_mod_set_function_hook_enabled(hook_id, noclip);
        if (!found) noclip = false;
    } else if (!std::strcmp(key, "dev.speed") && value >= 0 && value < 8) {
        transport.set_speed(speed_values[value]); deadline = {};
    } else return 0;
    return 1;
}
int action(const char* key) {
    if (!session) return 0;
    try {
        error.clear();
        if (!std::strcmp(key,"camera.reset")) viewer::reset_camera();
        else if (!std::strcmp(key, "dev.previous")) session->select(session->selected() - 1);
        else if (!std::strcmp(key, "dev.next")) session->select(session->selected() + 1);
        else if (!std::strcmp(key, "dev.save")) session->save_new(name);
        else if (!std::strcmp(key, "dev.load")) session->load_selected();
        else if (!std::strcmp(key, "dev.step")) { transport.step(); deadline = {}; }
        else return 0;
    } catch (const std::exception& e) { error = e.what(); }
    return 1;
}
int get_text(const char* key, char* buf, size_t size) {
    std::string text;
    if (!std::strcmp(key, "dev.name")) text = name;
    else if (!std::strcmp(key, "dev.scene")) text = scene;
    else if (!std::strcmp(key, "dev.status")) text = error.empty() ? session->message() : error;
    else if (!std::strcmp(key, "dev.selected")) {
        if (session->names().empty()) text = "No checkpoints yet";
        else text = std::to_string(session->selected()+1) + "/" + std::to_string(session->names().size()) + "  " + session->names()[session->selected()];
    } else return 0;
    std::snprintf(buf, size, "%s", text.c_str());
    return 1;
}
int set_text(const char* key, const char* value) {
    if (std::strcmp(key, "dev.name")) return 0;
    name = value; return 1;
}
int selectable(const char* key) {
    if (!std::strcmp(key, "system.save_state") || !std::strcmp(key, "system.load_state")) return 0;
    if (!std::strcmp(key, "dev.scene") || !std::strcmp(key, "dev.status") || !std::strcmp(key, "dev.selected")) return 0;
    if (!std::strcmp(key, "dev.noclip")) return verified && registered;
    if (!std::strcmp(key, "dev.load")) return !session->busy() && !session->names().empty();
    if (!std::strcmp(key, "dev.save")) return !session->busy();
    if (!std::strcmp(key, "dev.step")) return transport.paused();
    return 1;
}
}
void configure(gbarecomp::RunOptions& options) {
    const char* directory = std::getenv("RUBYVR_DEV_DIR");
    if (!directory || !*directory) return;
    try { session = std::make_unique<rubyvr::dev::Session>(directory); }
    catch (const std::exception& e) { throw std::runtime_error(std::string("Developer session: ") + e.what()); }
    if (const char* initial = std::getenv("RUBYVR_DEV_START_CHECKPOINT")) {
        const auto& names = session->names();
        const auto found = std::find(names.begin(), names.end(), initial);
        if (found != names.end()) session->select(static_cast<int>(found - names.begin()));
    }
    options.ui_extra_items = items;
    options.ui_extra_item_count = std::size(items);
    options.ui_get = get; options.ui_set = set; options.ui_action = action;
    options.ui_get_text = get_text; options.ui_set_text = set_text;
    options.ui_enabled = selectable;
    std::fprintf(stderr, "[rubyvr:dev] enabled; Esc > Developer; checkpoints=%s\n", directory);
}
bool enabled() { return bool(session); }
bool pending() { return session && session->pending(); }
std::optional<rubyvr::dev::Request> take_request() { return session ? session->take_request() : std::nullopt; }
void reset_after_load() {
    game_input::reset();
    noclip = false;
    gba_mod_set_function_hook_enabled(hook_id, 0);
    transport.set_speed(1);
    world::reset_capture();
}
bool accelerated() { return enabled() && transport.speed() != 1; }
void complete(bool success, const std::string& reason) {
    if (!session) return;
    try { session->complete(success, reason); }
    catch (const std::exception& e) { error = e.what(); }
    std::fprintf(stderr, "[rubyvr:dev] frame=%llu %s\n", static_cast<unsigned long long>(frame_number), session->message().c_str());
    deadline = {}; last_present = {};
}
void pause(bool value) { transport.pause(value); deadline = {}; }
bool paused() { return enabled() && transport.paused(); }
void next_frame(uint64_t frame) {
    if (frame == frame_number) return;
    frame_number = frame;
    transport.next_frame();
}
bool present_due(bool menu_open) {
    (void)menu_open; // An open menu needs responsive host frames, not every guest frame.
    const auto now = Clock::now();
    if (now - last_present >= std::chrono::milliseconds(accelerated() ? 50 : 16)) { last_present = now; return true; }
    return false;
}
void pace(int held_fast_forward) {
    if (!enabled() || paused()) { deadline = {}; return; }
    const int speed = transport.speed() == 1 ? std::max(1, held_fast_forward) : transport.speed();
    if (!speed) { deadline = {}; return; }
    const auto now = Clock::now();
    if (deadline == Clock::time_point{} || now - deadline > std::chrono::milliseconds(100)) deadline = now;
    deadline += std::chrono::nanoseconds(static_cast<int64_t>(16742706.0 / speed));
    if (deadline > now) std::this_thread::sleep_until(deadline);
}
void refresh_scene() {
    const auto m = memory();
    const auto s = world::live::inspect(m);
    scene = std::string(world::live::status_name(s.status)) + " " + std::to_string(s.group) + ":" + std::to_string(s.number);
    if (s.status == world::live::Status::Field) {
        const auto* avatar = m.read(world::kGPlayerAvatar, 6);
        const auto* object = avatar && avatar[5] < 16 ? m.read(world::kGObjectEvents + avatar[5]*0x24, 0x24) : nullptr;
        if (object && (object[0] & 1))
            scene += " | cell " + std::to_string(static_cast<int16_t>(u16(object+0x10))-7) + "," +
                std::to_string(static_cast<int16_t>(u16(object+0x12))-7) + " L" + std::to_string(object[0x0b]&15);
    }
    scene += " | frame " + std::to_string(frame_number);
    if (noclip) scene += " / NOCLIP " + std::to_string(bypasses);
}
}
