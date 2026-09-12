// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "live_scene.h"

namespace vr::presentation {

enum class Mode { Field, Bag, Party, Options, ReturnToField, Battle, Interior, Original };
struct Identity {
    int group=-1, number=-1;
    uint32_t layout=0;
    bool operator==(const Identity&) const = default;
};
struct Input {
    Mode mode=Mode::Original;
    Identity identity;
    uint32_t callback=0;
    uint64_t epoch=0;
    bool fading=false;
};
// Hash-gated pinned Ruby signals only. An unknown callback is never a menu.
Input inspect(const world::live::Memory& memory, uint64_t epoch=0);
const char* name(Mode);

enum class Overlay { FieldUi, Original, None };
struct Decision {
    bool world=false, update=false, retained=false;
    Overlay overlay=Overlay::Original;
};
// Presentation lifetime is independent of capture validity. No guest writes,
// graphics or game assets; each reset/load epoch invalidates the retained world.
class Lifetime {
public:
    Decision next(const Input&, bool valid_field);
    void reset();
private:
    Identity identity_;
    uint64_t epoch_=0;
    bool cached_=false, menu_=false;
};

// Extract field BG0 ownership, then use the ORIGINAL composited RGB at those
// pixels (including cursors/blending). Transparent palette index zero is not UI.
// Unsupported display layouts refuse instead of inventing UI from pixel colors.
bool field_ui(std::span<const uint8_t> vram, std::span<const uint8_t> registers,
              std::span<const uint8_t> rgb, std::vector<uint8_t>& rgba);

// Native adapter, called on the quiescent frame thread after world::capture.
Input capture_input();
bool capture_field_ui(std::span<const uint8_t> rgb, std::vector<uint8_t>& rgba);
} // namespace vr::presentation
