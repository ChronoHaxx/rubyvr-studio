// SPDX-License-Identifier: GPL-3.0-or-later
// Exercise the production widgets in a hidden GL window. Including this unit
// lets the test address its ImGui context without a public test-only API.
#include "../src/dev/demo_panel.cpp"
#include <cstdio>
#include <cstring>

namespace panel = rubyvr::dev::panel;
namespace {
panel::Model model;
std::string last_action, saved_name;
int checks=0, failures=0, loads=0, steps=0;
SDL_Window* win=nullptr;
void check(bool ok,const char* name) {
    ++checks;failures+=!ok;std::printf("%s %s\n",ok?"PASS":"FAIL",name);
}
void changed(const char* key,int value,const char* text) {
    last_action=key;
    if(!std::strcmp(key,"camera.mode"))model.camera_mode=value;
    else if(!std::strcmp(key,"camera.mouse_speed"))model.mouse_speed=value;
    else if(!std::strcmp(key,"dev.select"))model.selected=value;
    else if(!std::strcmp(key,"dev.load"))++loads;
    else if(!std::strcmp(key,"dev.save"))saved_name=text;
    else if(!std::strcmp(key,"dev.pause"))model.paused=value;
    else if(!std::strcmp(key,"dev.noclip"))model.noclip=value;
    else if(!std::strcmp(key,"dev.speed"))model.speed=value;
    else if(!std::strcmp(key,"dev.step"))++steps;
}
void frame() {panel::draw(win);SDL_GL_SwapWindow(win);SDL_Delay(2);}
void click(float x,float y) {
    auto& io=ImGui::GetIO();io.AddMousePosEvent(x,y);frame();
    io.AddMouseButtonEvent(0,true);frame();io.AddMouseButtonEvent(0,false);frame();frame();
}
void key(ImGuiKey k) {
    auto& io=ImGui::GetIO();io.AddKeyEvent(k,true);frame();io.AddKeyEvent(k,false);frame();
}
}
int main() {
    SDL_SetMainReady();SDL_SetHint("SDL_MOUSE_AUTO_CAPTURE","0");
    if(SDL_Init(SDL_INIT_VIDEO)!=0){std::fprintf(stderr,"SDL: %s\n",SDL_GetError());return 2;}
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION,3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION,3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,SDL_GL_CONTEXT_PROFILE_CORE);
    win=SDL_CreateWindow("RubyVR hidden panel test",0,0,1280,800,SDL_WINDOW_OPENGL|SDL_WINDOW_HIDDEN);
    if(!win){std::fprintf(stderr,"Window: %s\n",SDL_GetError());SDL_Quit();return 2;}
    auto gl=SDL_GL_CreateContext(win);
    if(!gl){std::fprintf(stderr,"GL: %s\n",SDL_GetError());SDL_DestroyWindow(win);SDL_Quit();return 2;}
    model.available=true;model.can_noclip=true;
    model.checkpoints={"Route 101","Oldale house ready","Oldale Mart ready","Oldale Center ready","Birch lab ready"};
    panel::configure({[](){return model;},changed});
    if(!panel::init(win)){std::fprintf(stderr,"Panel initialization failed\n");return 2;}
    ImGui::SetCurrentContext(panel::context);
    auto& io=ImGui::GetIO();
    io.ConfigFlags|=ImGuiConfigFlags_NoMouseCursorChange;
    io.AddFocusEvent(true);panel::set_open(true);frame();frame();frame();
    check((SDL_GetWindowFlags(win)&SDL_WINDOW_SHOWN)==0,"test window stays hidden");
    check(SDL_GetKeyboardFocus()!=win,"test never owns desktop keyboard focus");
    click(113,252);check(model.camera_mode==1,"Third person radio dispatches mode");
    click(255,252);check(model.camera_mode==2,"First person radio dispatches mode");
    click(217,430);check(loads==1 && model.selected==2,"Shop selects and loads its checkpoint");
    model.busy=true;frame();click(154,430);check(loads==1,"pending operation disables quick loads");
    model.busy=false;frame();
    click(134,605);io.AddKeyEvent(ImGuiMod_Ctrl,true);key(ImGuiKey_A);io.AddKeyEvent(ImGuiMod_Ctrl,false);
    io.AddInputCharactersUTF8("Hidden UI moment");frame();frame();
    click(112,640);check(saved_name=="Hidden UI moment","text field and Save dispatch the entered name");
    click(112,194);frame();
    click(40,252);check(model.paused,"Pause widget dispatches pause");
    click(221,252);check(steps==1,"Step dispatches exactly once while paused");
    click(40,252);click(221,252);check(!model.paused && steps==1,"Step is disabled while running");
    click(40,345);check(model.noclip,"obstacle bypass widget dispatches toggle");
    click(296,146);check(!panel::open(),"Continue closes the play screen");
    click(101,38);check(panel::open(),"toolbar reopens the play screen");
    SDL_Event event{};event.type=SDL_KEYDOWN;event.key.windowID=SDL_GetWindowID(win);
    event.key.keysym.scancode=SDL_SCANCODE_ESCAPE;event.key.keysym.sym=SDLK_ESCAPE;
    panel::event(event);check(!panel::open(),"application-local Escape closes panel");
    event.key.windowID+=100;panel::event(event);check(!panel::open(),"foreign window input is ignored");
    check(SDL_GetKeyboardFocus()!=win && !SDL_GetRelativeMouseMode(),"no desktop focus or relative mouse capture at completion");
    panel::shutdown();SDL_GL_DeleteContext(gl);SDL_DestroyWindow(win);SDL_Quit();
    std::printf("Panel: %d checks, %d failures (synthetic UI, not physical-input acceptance)\n",checks,failures);
    return failures?1:0;
}
