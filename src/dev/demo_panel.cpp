// SPDX-License-Identifier: GPL-3.0-or-later
#include "demo_panel.h"
#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_opengl3.h"
#include <algorithm>

namespace rubyvr::dev::panel {
namespace {
SDL_Window* window=nullptr;
ImGuiContext* context=nullptr;
Callbacks callbacks;
bool visible=false;
char name[49]="New checkpoint";
struct Context {
    ImGuiContext* previous=ImGui::GetCurrentContext();
    Context(){ImGui::SetCurrentContext(context);}
    ~Context(){ImGui::SetCurrentContext(previous);}
};
void change(const char* key,int value=0,const char* text="") {
    if(callbacks.change)callbacks.change(key,value,text);
}
}
void configure(Callbacks value){callbacks=value;}
bool open(){return context && visible;}
void set_open(bool value){visible=value;}
bool init(SDL_Window* win) {
    if(context)return win==window;
    if(!win || !callbacks.read)return false;
    const auto previous=ImGui::GetCurrentContext();
    context=ImGui::CreateContext();window=win;ImGui::SetCurrentContext(context);
    auto& io=ImGui::GetIO();io.IniFilename=nullptr;io.LogFilename=nullptr;
    io.ConfigFlags|=ImGuiConfigFlags_NavEnableKeyboard;
    io.FontGlobalScale=1.2f;
    ImGui::StyleColorsDark();auto& style=ImGui::GetStyle();
    style.WindowRounding=4;style.FrameRounding=3;style.FramePadding={8,6};
    style.ItemSpacing={8,8};style.WindowPadding={14,12};
    style.Colors[ImGuiCol_WindowBg]={.065f,.08f,.10f,.97f};
    style.Colors[ImGuiCol_Button]={.24f,.32f,.34f,1};
    style.Colors[ImGuiCol_ButtonHovered]={.32f,.46f,.46f,1};
    const bool platform=ImGui_ImplSDL2_InitForOpenGL(win,SDL_GL_GetCurrentContext());
    const bool renderer=platform && ImGui_ImplOpenGL3_Init("#version 330 core");
    if(!renderer){if(platform)ImGui_ImplSDL2_Shutdown();ImGui::DestroyContext(context);context=nullptr;window=nullptr;}
    ImGui::SetCurrentContext(previous);return renderer;
}
bool event(const SDL_Event& e) {
    if(!context)return false;
    Uint32 id=0;
    switch(e.type) {
    case SDL_KEYDOWN:case SDL_KEYUP:id=e.key.windowID;break;
    case SDL_TEXTINPUT:id=e.text.windowID;break;
    case SDL_TEXTEDITING:id=e.edit.windowID;break;
    case SDL_MOUSEMOTION:id=e.motion.windowID;break;
    case SDL_MOUSEBUTTONDOWN:case SDL_MOUSEBUTTONUP:id=e.button.windowID;break;
    case SDL_MOUSEWHEEL:id=e.wheel.windowID;break;
    case SDL_WINDOWEVENT:id=e.window.windowID;break;
    default:return false;
    }
    if(id!=SDL_GetWindowID(window))return false;
    Context guard;ImGui_ImplSDL2_ProcessEvent(&e);
    if(e.type==SDL_KEYDOWN && e.key.keysym.scancode==SDL_SCANCODE_ESCAPE) {
        if(!e.key.repeat)visible=!visible;
        return true;
    }
    // Never consume window lifecycle events (especially close/focus loss).
    if(e.type==SDL_WINDOWEVENT)return false;
    const auto& io=ImGui::GetIO();
    const bool mouse=e.type==SDL_MOUSEMOTION || e.type==SDL_MOUSEBUTTONDOWN || e.type==SDL_MOUSEBUTTONUP || e.type==SDL_MOUSEWHEEL;
    return visible || (mouse && io.WantCaptureMouse);
}
void draw(SDL_Window* win) {
    if(!context && !init(win))return;
    Context guard;
    ImGui_ImplOpenGL3_NewFrame();ImGui_ImplSDL2_NewFrame();ImGui::NewFrame();
    const auto model=callbacks.read?callbacks.read():Model{};
    const auto size=ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowPos({12,12});ImGui::SetNextWindowSize({std::max(260.f,size.x-24),48});
    constexpr auto flags=ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoSavedSettings;
    ImGui::Begin("RubyVR toolbar",nullptr,flags);
    if(ImGui::Button(visible?"Close menu [Esc]":"Play & test [Esc]"))visible=!visible;
    ImGui::SameLine();if(ImGui::Button("Reset view"))change("camera.reset");
    ImGui::SameLine();ImGui::TextUnformatted(model.paused?"PAUSED":model.camera_mode?"WASD: walk   Right-click: mouse look   Esc: release / controls":"WASD/Arrows: walk   J/L: turn   Enter: Start   X/Z: confirm/back");
    ImGui::End();
    if(visible) {
        ImGui::SetNextWindowPos({12,70},ImGuiCond_Always);
        ImGui::SetNextWindowSize({std::min(560.f,std::max(260.f,size.x-24)),std::min(710.f,std::max(240.f,size.y-82))});
        ImGui::Begin("Demo controls",nullptr,flags);
        ImGui::TextUnformatted("RUBYVR / LOCAL DEMO");
        ImGui::TextWrapped("Littleroot - Route 101 - Oldale");
        if(ImGui::Button(model.paused?"Return to paused game":"Continue playing",{-1,36}))visible=false;
        ImGui::Separator();
        if(!model.available)ImGui::TextWrapped("Developer controls need a prepared local game session.");
        ImGui::BeginDisabled(!model.available);
        if(ImGui::BeginTabBar("Demo pages")) {
            if(ImGui::BeginTabItem("Play")) {
                ImGui::TextUnformatted("CAMERA");
                constexpr const char* cameras[]={"Grid","Third person","First person"};
                for(int i=0;i<3;++i) {
                    if(i)ImGui::SameLine();
                    if(ImGui::RadioButton(cameras[i],model.camera_mode==i))change("camera.mode",i);
                }
                if(model.camera_mode){const char* mouse_speeds[]={"Slow","Normal","Fast"};int look=model.mouse_speed;
                    ImGui::SetNextItemWidth(135);if(ImGui::Combo("Mouse speed",&look,mouse_speeds,3))change("camera.mouse_speed",look);}
                ImGui::TextWrapped(model.camera_mode?"WASD: walk. Right-click: toggle mouse look. Esc: release. J/L: turn, I/K: tilt, U/O: zoom.":"WASD or arrows: walk. J/L: turn 90 degrees. I/K: tilt. U/O: zoom.");
                ImGui::TextWrapped("Enter: Start menu. X: confirm/interact. Z: back. Camera settings are remembered.");
                ImGui::Separator();ImGui::TextUnformatted("EXPLORE A SITUATION");
                ImGui::BeginDisabled(model.busy);
                const char* labels[]={"Route 101","House","Shop","Center","Lab"};
                const char* states[]={"Route 101","Oldale house ready","Oldale Mart ready","Oldale Center ready","Birch lab ready"};
                for(int i=0;i<5;++i) {
                    if(i)ImGui::SameLine();
                    auto it=std::find(model.checkpoints.begin(),model.checkpoints.end(),states[i]);
                    ImGui::BeginDisabled(it==model.checkpoints.end());
                    if(ImGui::Button(labels[i])){change("dev.select",int(it-model.checkpoints.begin()));change("dev.load");}
                    ImGui::EndDisabled();
                }
                const bool selected=model.selected>=0 && model.selected<int(model.checkpoints.size());
                ImGui::SetNextItemWidth(310);
                if(ImGui::BeginCombo("Checkpoint",selected?model.checkpoints[model.selected].c_str():"No checkpoints")) {
                    for(size_t i=0;i<model.checkpoints.size();++i) {
                        const bool active=int(i)==model.selected;
                        if(ImGui::Selectable(model.checkpoints[i].c_str(),active))change("dev.select",int(i));
                        if(active)ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }
                ImGui::BeginDisabled(model.checkpoints.empty());
                if(ImGui::Button("Open selected checkpoint"))change("dev.load");
                ImGui::EndDisabled();
                ImGui::TextWrapped("Opening a checkpoint resets speed and obstacle bypass. Loading can take a moment.");
                ImGui::Separator();ImGui::TextUnformatted("KEEP THIS MOMENT");
                ImGui::SetNextItemWidth(245);ImGui::InputText("New name",name,sizeof(name));
                if(ImGui::Button("Save new checkpoint"))change("dev.save",0,name);
                ImGui::TextWrapped("Save before closing to reopen here next time. Existing names stay protected. The in-game save is separate.");
                ImGui::EndDisabled();
                ImGui::EndTabItem();
            }
            if(ImGui::BeginTabItem("Test tools")) {
                ImGui::TextWrapped("%s",model.location.c_str());
                bool paused=model.paused;
                if(ImGui::Checkbox("Pause game",&paused))change("dev.pause",paused);
                ImGui::SameLine();ImGui::BeginDisabled(!paused);
                if(ImGui::Button("Advance one frame"))change("dev.step");
                ImGui::EndDisabled();
                constexpr const char* speeds[]={"1x - normal","2x","4x","8x","16x","32x","64x","MAX - hardware limit"};
                int speed=std::clamp(model.speed,0,7);ImGui::SetNextItemWidth(220);
                if(ImGui::Combo("Game speed",&speed,speeds,8))change("dev.speed",speed);
                ImGui::TextWrapped("Speeds up dialogue and battles too. Accelerated audio is muted.");
                ImGui::BeginDisabled(!model.can_noclip);
                bool noclip=model.noclip;
                if(ImGui::Checkbox("Walk through obstacles",&noclip))change("dev.noclip",noclip);
                ImGui::EndDisabled();
                ImGui::TextWrapped("On foot inside the map. Story events and map borders still apply. Speed, pause and obstacle bypass are not remembered after closing.");
                ImGui::Separator();ImGui::TextWrapped("Rooms, trees and ledge corners are provisional. This is a local desktop prototype.");
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
        ImGui::EndDisabled();
        // Read again so a button's Loading/Saving feedback is drawn in this
        // frame, before a potentially slow native checkpoint dispatch.
        const auto latest=callbacks.read?callbacks.read():model;
        ImGui::TextWrapped("%s",latest.status.c_str());
        if(!latest.preferences_status.empty())ImGui::TextWrapped("Settings: %s",latest.preferences_status.c_str());
        ImGui::End();
    }
    ImGui::Render();ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}
void shutdown() {
    if(!context)return;
    auto* previous=ImGui::GetCurrentContext();ImGui::SetCurrentContext(context);
    ImGui_ImplOpenGL3_Shutdown();ImGui_ImplSDL2_Shutdown();ImGui::DestroyContext(context);
    ImGui::SetCurrentContext(previous==context?nullptr:previous);
    context=nullptr;window=nullptr;visible=false;
}
}
