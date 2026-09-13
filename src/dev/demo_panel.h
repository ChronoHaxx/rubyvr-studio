// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <SDL2/SDL.h>
#include <string>
#include <vector>

namespace rubyvr::dev::panel {
// Value-owned UI state; only the runtime adapter owns transport and save actions.
struct Model {
    bool available=false, paused=false, noclip=false, can_noclip=false, busy=false;
    int speed=0, selected=0;
    std::vector<std::string> checkpoints;
    std::string location, status;
};
struct Callbacks {
    Model (*read)()=nullptr;
    void (*change)(const char* key,int value,const char* text)=nullptr;
};
void configure(Callbacks);
bool init(SDL_Window*);
bool event(const SDL_Event&);
void draw(SDL_Window*);
bool open();
void set_open(bool);
void shutdown();
}
