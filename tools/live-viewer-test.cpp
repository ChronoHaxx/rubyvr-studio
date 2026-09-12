// SPDX-License-Identifier: GPL-3.0-or-later
// Local GL regression with original synthetic pixels. No guest/game assets.
#include "viewer.h"
#include "diorama.h"
#include "gl_loader.h"
#include <cstdlib>
#include <cstring>
#include <iostream>

void expect(bool ok,const char* label) {
    if(!ok) { std::cerr<<"FAIL: "<<label<<" ("<<SDL_GetError()<<")\n"; std::exit(1); }
}
int main(int,char**) {
    expect(SDL_Init(SDL_INIT_VIDEO)==0,"SDL init");
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION,3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION,3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER,1);
    // Match the viewer's size from creation: hidden WSLg drawables can retain
    // their initial allocation until mapped, despite reporting a resize.
    auto* window=SDL_CreateWindow("live scene regression",0,0,1280,800,SDL_WINDOW_OPENGL|SDL_WINDOW_HIDDEN);
    expect(window!=nullptr,"hidden window");
    const auto context=SDL_GL_CreateContext(window);
    expect(context!=nullptr && vr::gl::load(),"GL context");
    expect(vr::viewer::init(window,false),"viewer init");
    vr::world::Snapshot field;
    field.valid=true; field.map_group=0;field.map_number=16;
    field.identity_source=vr::world::Snapshot::IdentitySource::LiveCapture;
    field.layout_ptr=0x08001000;field.width=25;field.height=24;
    field.grid.assign(600,0);field.metatiles.assign(8192,0);field.attributes.assign(1024,0);
    field.vram_tiles.assign(vr::world::kTileSheetSize,0x11);
    field.bg_palette.assign(vr::world::kPaletteEntries,0);field.bg_palette[1]=0x3e0;
    vr::diorama::set_build_mode(vr::diorama::BuildMode::Diorama);
    vr::viewer::frame(field,false);
    expect(vr::diorama::has_geometry(),"valid scene has geometry");
    expect(std::strstr(SDL_GetWindowTitle(window),"live map 0.16")!=nullptr,"valid scene title");
    const auto first=vr::diorama::diorama_stats().geometry_hash;
    expect(first!=0,"geometry digest");
    vr::world::Snapshot unavailable;
    vr::viewer::frame(unavailable,false);
    expect(!vr::diorama::has_geometry(),"invalid scene clears geometry");
    expect(vr::diorama::diorama_stats().geometry_hash==0,"invalid scene clears diagnostics");
    expect(std::strstr(SDL_GetWindowTitle(window),"scene unavailable")!=nullptr,"explicit unavailable title");
    // Hidden-window front buffers are not reliable on WSLg. Inspect the actual
    // rendered back buffer before presentation, without clearing it in the test.
    int width=0,height=0;SDL_GL_GetDrawableSize(window,&width,&height);
    std::vector<unsigned char> pixels(size_t(width)*height*4);
    glReadBuffer(GL_BACK);glReadPixels(0,0,width,height,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());
    bool cleared=true;
    for(size_t i=0;i<pixels.size();i+=4)
        if(pixels[i]<17 || pixels[i]>19 || pixels[i+1]<19 || pixels[i+1]>21 ||
           pixels[i+2]<27 || pixels[i+2]>29) cleared=false;
    if(!cleared) {
        std::cerr<<"buffer "<<width<<'x'<<height<<" GL error="<<glGetError()<<'\n';
    }
    expect(cleared,"unavailable view has cleared rendered background");
    glReadBuffer(GL_BACK);
    vr::viewer::frame(field,false);
    expect(vr::diorama::has_geometry(),"return to same map rebuilds geometry");
    expect(vr::diorama::diorama_stats().geometry_hash==first,"return restores the same geometry");
    field.map_number=17;
    field.grid[12*25+12]=vr::world::kGridUndefined;
    vr::viewer::frame(field);
    expect(std::strstr(SDL_GetWindowTitle(window),"live map 0.17")!=nullptr,"identity can change with the same layout");
    expect(vr::diorama::diorama_stats().geometry_hash!=first,"same-layout map change rebuilds its different grid");
    vr::diorama::shutdown();SDL_GL_DeleteContext(context);SDL_DestroyWindow(window);SDL_Quit();
    std::cout<<"PASS: live viewer valid -> unavailable -> same map -> new identity (local GL; synthetic art)\n";
}
