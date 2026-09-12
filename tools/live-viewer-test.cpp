// SPDX-License-Identifier: GPL-3.0-or-later
// Local GL regression with original synthetic pixels. No guest/game assets.
#include "viewer.h"
#include "diorama.h"
#include "gl_loader.h"
#include "actor_render.h"
#include "dev/demo_panel.h"
#include "imgui.h"
#include <cstdlib>
#include <cstring>
#include <iostream>

std::vector<vr::world::Snapshot> neighbours;
bool load_neighbour(int group,int number,vr::world::Snapshot& out) {
    for(const auto& s:neighbours) if(s.map_group==group && s.map_number==number){out=s;return true;}
    return false;
}

void expect(bool ok,const char* label) {
    if(!ok) { std::cerr<<"FAIL: "<<label<<" ("<<SDL_GetError()<<")\n"; std::exit(1); }
}
int main(int,char**) {
    SDL_SetMainReady();
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
    expect(vr::viewer::yaw_radians()==0,"gameplay starts north-up");
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
    auto& object=field.objects[0];object.active=object.is_player=true;object.elevation=3;object.x=object.y=12;
    field.player_index=0;field.obj_mapping_1d=true;
    auto& sprite=field.actor_sources[0];sprite.present=true;sprite.sprite[0x3e]=3;
    auto put=[&](int p,int v){sprite.sprite[p]=uint8_t(v);sprite.sprite[p+1]=uint8_t(unsigned(v)>>8);};
    put(0,2<<14);put(2,2<<14);put(0x20,200);put(0x22,192);
    sprite.sprite[0x28]=248;sprite.sprite[0x29]=240;
    field.obj_tiles.assign(32768,0x11);field.obj_palette.assign(256,0);field.obj_palette[1]=31;
    vr::viewer::frame(field,false);
    expect(vr::actor_render::stats().visible==1 && vr::actor_render::stats().player,"live player rendered");
    float px=0,py=0,pz=0;vr::diorama::player_cell(&px,&py,&pz);
    expect(px==12.5f && pz==12.5f && py==0,"camera follows exact foot on legacy flat terrain");
    std::vector<uint8_t> actor_pixels(1280*800*4);glReadBuffer(GL_BACK);
    glReadPixels(0,0,1280,800,GL_RGBA,GL_UNSIGNED_BYTE,actor_pixels.data());
    int red=0;for(size_t i=0;i<actor_pixels.size();i+=4)red+=actor_pixels[i]>240 && actor_pixels[i+1]<10;
    expect(red>100,"actual player pixels in GL frame");
    vr::viewer::set_yaw_radians(1.570796327f);vr::viewer::frame(field,false);
    expect(vr::diorama::diorama_stats().geometry_hash==first,"orbit keeps the same scenery");
    expect(vr::actor_render::stats().visible==1,"quarter-turn keeps the original actor visible");
    // A stationary north-facing actor must show back/right/front/left art as
    // the camera moves around it. Test actual GL colors, not only a facing enum.
    sprite.world_facing=2;
    for(int d=0;d<4;++d)sprite.directions[d].tiles.fill(uint8_t((d+1)*17));
    field.obj_palette[2]=31<<10;field.obj_palette[3]=31|(31<<5);field.obj_palette[4]=0x7fff;
    const uint8_t colors[4][3]={{0,0,255},{255,255,255},{255,0,0},{255,255,0}};
    for(int q=0;q<4;++q) {
        vr::viewer::set_yaw_radians(q*1.570796327f);vr::viewer::frame(field,false);
        glReadPixels(0,0,1280,800,GL_RGBA,GL_UNSIGNED_BYTE,actor_pixels.data());
        int count=0;for(size_t i=0;i<actor_pixels.size();i+=4)
            count+=actor_pixels[i]==colors[q][0] && actor_pixels[i+1]==colors[q][1] && actor_pixels[i+2]==colors[q][2];
        expect(count>100,"camera selects the corresponding original directional pixels in GL");
        vr::diorama::player_cell(&px,&py,&pz);
        expect(px==12.5f && pz==12.5f && py==0,"view-only change keeps actor foot stationary");
        expect(vr::diorama::diorama_stats().geometry_hash==first,"directional art does not rebuild scenery");
    }
    sprite.world_facing=0;
    // A short NPC owns its own directional profile independently of the player.
    // Its source invisible bit is still present: only verified viewport culling
    // may bypass it, while explicit script hiding/despawn must still win.
    auto& npc=field.objects[1];npc=object;npc.is_player=false;npc.x=14;
    auto& npc_source=field.actor_sources[1];npc_source=sprite;
    npc_source.world_facing=2;npc_source.sprite[0]=npc_source.sprite[1]=0;
    npc_source.sprite[2]=0;npc_source.sprite[3]=0x40;
    npc_source.sprite[0x28]=npc_source.sprite[0x29]=248;
    npc_source.sprite[0x20]=232;npc_source.sprite[0x22]=200;
    npc_source.sprite[0x3e]|=4;npc_source.viewport_culled=true;
    for(auto& d:npc_source.directions)d.byte_count=128;
    for(int q=0;q<4;++q) {
        vr::viewer::set_yaw_radians(q*1.570796327f);vr::viewer::frame(field,false);
        expect(vr::actor_render::stats().visible==2,"player and viewport-culled NPC coexist");
        glReadPixels(0,0,1280,800,GL_RGBA,GL_UNSIGNED_BYTE,actor_pixels.data());
        int count=0;for(size_t i=0;i<actor_pixels.size();i+=4)
            count+=actor_pixels[i]==colors[q][0] && actor_pixels[i+1]==colors[q][1] && actor_pixels[i+2]==colors[q][2];
        expect(count>50,"short NPC shows camera-relative directional pixels");
    }
    npc.invisible=true;vr::viewer::frame(field,false);
    expect(vr::actor_render::stats().visible==1,"script-hidden NPC cannot bypass visibility with cull flag");
    npc.invisible=false;npc_source.viewport_culled=false;vr::viewer::frame(field,false);
    expect(vr::actor_render::stats().visible==1,"unexplained source hiding stays hidden");
    npc_source.viewport_culled=true;npc.active=false;vr::viewer::frame(field,false);
    expect(vr::actor_render::stats().visible==1,"inactive NPC does not reuse previous GPU pose");
    field.actor_sources[1]={};field.objects[1]={};
    // Presentation retains a previously observed ordinary NPC past Ruby's
    // despawn rectangle. It still selects view-correct pixels, without OBJ RAM.
    field.actor_range_safe=true;
    vr::world::ActorTemplate definition;definition.bytes[0]=1;definition.hidden=false;
    field.actor_templates={definition};
    field.objects[1]=object;field.objects[1].is_player=false;field.objects[1].x=14;field.objects[1].local_id=1;
    field.objects[1].map_number=16;field.objects[1].initial_x=14;field.objects[1].initial_y=12;
    field.actor_sources[1]=sprite;field.actor_sources[1].world_facing=2;
    field.actor_sources[1].sprite[0x20]=232;
    vr::viewer::frame(field,false);
    field.objects[1].active=false;field.actor_sources[1]={};field.view_x=40;
    vr::viewer::frame(field,false);
    expect(vr::actor_render::stats().distant==1,"distant NPC owns an independent GPU item");
    for(int q=0;q<4;++q) {
        vr::viewer::set_yaw_radians(q*1.570796327f);vr::viewer::frame(field,false);
        expect(vr::actor_render::stats().visible==2,"all views retain a known distant NPC beside live player");
    }
    field.actor_templates[0].hidden=true;vr::viewer::frame(field,false);
    expect(vr::actor_render::stats().distant==0,"source hide removes distant GPU pose immediately");
    field.actor_range_safe=false;field.actor_templates.clear();field.view_x=0;field.objects[1]={};
    vr::viewer::reset_camera();
    expect(vr::viewer::yaw_radians()==0,"north-up reset is exact");
    vr::viewer::set_yaw_radians(0.4f);
    expect(vr::viewer::yaw_radians()==0,"grid view cannot settle between cardinal angles");
    vr::viewer::set_yaw_radians(1.1f);
    expect(vr::viewer::yaw_radians()==1.570796327f,"off-angle view request selects nearest cardinal angle");
    vr::viewer::reset_camera();
    put(0x20,196);vr::viewer::frame(field,false);vr::diorama::player_cell(&px,&py,&pz);
    expect(px==12.25f && vr::diorama::diorama_stats().geometry_hash==first,"subtile actor move does not rebuild scenery");
    // Explicit terrain layer chooses the surface; visual jump leaves it alone.
    vr::overrides::OverrideSet authored;authored.version=vr::overrides::kTerrainVersion;vr::overrides::TerrainMap tm;
    tm.group=0;tm.number=16;tm.width=25;tm.height=24;
    vr::overrides::TerrainCell tc;tc.x=tc.y=12;tc.expected=0;
    vr::overrides::TerrainSurface top;top.layer=3;top.height=top.thickness=16;tc.surfaces.push_back(top);tm.cells.push_back(tc);
    expect(vr::terrain::guard_tile(field,0,&tm),"guard original synthetic terrain art");
    authored.terrain.push_back(tm);expect(vr::terrain::valid(authored.terrain),"valid authored terrain fixture");
    vr::diorama::set_overrides(authored);
    put(0x26,-8);vr::viewer::frame(field,false);vr::diorama::player_cell(&px,&py,&pz);
    expect(py==1.f && vr::diorama::has_geometry(),"camera uses authored height independently of jump");
    authored.terrain[0].cells[0].surfaces[0].layer=0;
    vr::diorama::set_overrides(authored);vr::viewer::frame(field,false);
    expect(vr::actor_render::stats().visible==1 && vr::actor_render::stats().player_y==1.f,"jumping player remains on sole source-neutral ground");
    authored.terrain[0].cells[0].surfaces[0].layer=3;vr::diorama::set_overrides(authored);
    field.objects[0].elevation=4;vr::viewer::frame(field,false);
    expect(vr::actor_render::stats().visible==0 && vr::actor_render::stats().unresolved==1,"wrong layer is unresolved rather than guessed");
    authored.version=vr::overrides::kVersion;vr::diorama::set_overrides(authored);
    field.objects[0].elevation=3;vr::viewer::frame(field,false);
    expect(!vr::diorama::has_geometry() && vr::actor_render::stats().visible==0,"rejected scenery also hides actors");
    vr::diorama::set_overrides({});field.objects[0].elevation=3;put(0x26,0);
    vr::world::Snapshot unavailable;
    vr::viewer::frame(unavailable,false);
    expect(!vr::diorama::has_geometry(),"invalid scene clears geometry");
    expect(vr::actor_render::stats().visible==0,"invalid scene clears actors");
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
    field.map_number=16;field.grid.assign(600,0);
    field.connections={{0,17,25,24,7,0,7,10,10,7,false}};
    auto north=field;north.map_number=17;north.connections={{0,16,25,24,7,17,7,7,10,7,false}};
    north.bg_palette[1]=31<<10;neighbours={north};
    expect(vr::viewer::init(window,false,load_neighbour),"connected viewer init");
    const auto start=SDL_GetTicks64();
    while(vr::viewer::connected_maps()!=2 && SDL_GetTicks64()-start<10000) {
        vr::viewer::frame(field,false);SDL_Delay(5);
    }
    expect(vr::viewer::connected_maps()==2,"background CPU build publishes complete neighbour on GL thread");
    expect(!vr::diorama::has_geometry(),"connected consumer releases duplicate single-map mesh");
    expect(vr::actor_render::stats().player,"connected scene retains live player");
    int nx=0,nz=0;
    expect(vr::viewer::map_origin(0,17,&nx,&nz) && nx==0 && nz==-10,"published north map origin");
    glReadPixels(0,0,1280,800,GL_RGBA,GL_UNSIGNED_BYTE,actor_pixels.data());
    int green=0,blue=0;
    for(size_t i=0;i<actor_pixels.size();i+=4) {
        green+=actor_pixels[i+1]>120 && actor_pixels[i]<10 && actor_pixels[i+2]<10;
        blue+=actor_pixels[i+2]>120 && actor_pixels[i]<10 && actor_pixels[i+1]<10;
    }
    expect(green>100 && blue>100,"both complete maps draw through their own palettes");
    auto uploads=vr::diorama::mesh_upload_count();field.bg_palette[1]=31|(31<<5);
    vr::viewer::frame(field,false);
    expect(vr::diorama::mesh_upload_count()==uploads,"live palette animation does not rebuild region geometry");
    glReadPixels(0,0,1280,800,GL_RGBA,GL_UNSIGNED_BYTE,actor_pixels.data());
    int yellow=0;for(size_t i=0;i<actor_pixels.size();i+=4)
        yellow+=actor_pixels[i]>120 && actor_pixels[i+1]>120 && actor_pixels[i+2]<10;
    expect(yellow>100,"current map palette refresh reaches actual GL output");
    vr::viewer::frame(north,false);
    expect(vr::viewer::map_origin(0,16,&nx,&nz) && nx==0 && nz==0,"crossing retains outgoing map while rebuilding");
    vr::viewer::frame({},false);
    expect(vr::actor_render::stats().visible==0,"invalid scene never draws cached actors");
    expect(std::strstr(SDL_GetWindowTitle(window),"scene unavailable"),"cached region is not presented as an active menu scene");
    vr::viewer::frame(field,false);
    expect(vr::viewer::map_origin(0,17,&nx,&nz) && nz==-10,"return keeps stable neighbour placement");

    // The native presentation path must retain the complete host scene in a
    // menu, show the actual original UI, and stop borrowing it at loads/battles.
    using namespace vr::presentation;
    Input signal{Mode::Field,{field.map_group,field.map_number,field.layout_ptr},0,1,false};
    std::vector<uint8_t> original(240*160*3),transparent_ui(240*160*4);
    for(int y=0;y<160;++y)for(int x=0;x<240;++x) {
        const int p=(y*240+x)*3;original[p]=y<80?250:10;original[p+1]=30;original[p+2]=y<80?10:250;
    }
    vr::viewer::game_frame(field,signal,original,240,160,transparent_ui,false);
    expect(vr::viewer::presentation_state().world && vr::viewer::uses_world_controls(),"game field owns camera-relative controls");
    uploads=vr::diorama::mesh_upload_count();
    signal.mode=Mode::Bag;
    vr::viewer::game_frame({},signal,original,240,160,{},false);
    expect(vr::viewer::presentation_state().retained && vr::actor_render::stats().player,"Bag retains complete scenery and last valid actor");
    expect(vr::diorama::mesh_upload_count()==uploads,"opening menu does not re-upload geometry");
    expect(!vr::viewer::uses_world_controls(),"Bag directions stay in UI coordinates");
    glReadBuffer(GL_BACK);glReadPixels(0,0,1280,800,GL_RGBA,GL_UNSIGNED_BYTE,actor_pixels.data());
    auto color=[&](int x,int y,int r,int b){const size_t p=(size_t(y)*1280+x)*4;
        return actor_pixels[p]==r && actor_pixels[p+1]==30 && actor_pixels[p+2]==b;};
    expect(color(640,600,250,10) && color(640,200,10,250),"menu shows original RGB upright at integer scale");
    expect(!color(100,400,250,10) && !color(100,400,10,250),"menu is inset with surrounding world visible");
    vr::viewer::set_yaw_radians(1.570796327f);
    vr::viewer::game_frame({},signal,original,240,160,{},false);
    expect(vr::viewer::presentation_state().retained && vr::diorama::mesh_upload_count()==uploads,"view can turn in retained world without menu pixels entering scenery");
    signal.mode=Mode::ReturnToField;vr::viewer::game_frame({},signal,original,240,160,{},false);
    expect(vr::viewer::presentation_state().overlay==Overlay::None,"return does not display partially reloaded field as menu");
    signal.mode=Mode::Field;vr::viewer::game_frame(field,signal,original,240,160,transparent_ui,false);
    expect(vr::viewer::presentation_state().update && vr::viewer::uses_world_controls(),"return restores live world and field controls");
    // Transparent dialog canvas contributes only its intended window pixels.
    for(int y=120;y<150;++y)for(int x=10;x<230;++x) {
        const size_t p=(size_t(y)*240+x)*4;transparent_ui[p]=234;transparent_ui[p+1]=56;
        transparent_ui[p+2]=123;transparent_ui[p+3]=255;
    }
    vr::viewer::game_frame(field,signal,original,240,160,transparent_ui,false);
    glReadPixels(0,0,1280,800,GL_RGBA,GL_UNSIGNED_BYTE,actor_pixels.data());
    const size_t dialog=(size_t(180)*1280+640)*4;
    expect(actor_pixels[dialog]==234 && actor_pixels[dialog+1]==56 && actor_pixels[dialog+2]==123,"dialog pixels overlay shared world at readable screen position");
    signal.mode=Mode::Battle;vr::viewer::game_frame({},signal,original,240,160,{},false);
    expect(!vr::viewer::presentation_state().world && vr::actor_render::stats().visible==0,"battle uses original game and clears stale actors");
    glReadPixels(0,0,1280,800,GL_RGBA,GL_UNSIGNED_BYTE,actor_pixels.data());
    expect(color(640,780,250,10) && color(640,20,10,250),"battle original frame fills available aspect without cropping");
    signal.mode=Mode::Field;vr::viewer::game_frame(field,signal,original,240,160,transparent_ui,false);
    ++signal.epoch;signal.mode=Mode::Bag;vr::viewer::game_frame({},signal,original,240,160,{},false);
    expect(!vr::viewer::presentation_state().world && !vr::viewer::uses_world_controls(),"loading directly into Bag never reuses preceding world");
    signal.mode=Mode::Interior;vr::viewer::game_frame(field,signal,original,240,160,{},false);
    expect(!vr::viewer::presentation_state().world && !vr::viewer::uses_world_controls(),"interior original view uses original directions even with valid field data");
    // Two UI contexts coexist in the native runner. The demo panel must not
    // initialize its SDL backend on the game's context, steal another window's
    // keys or lose its callbacks when viewer init clears the previous scene.
    auto* game_ui=ImGui::CreateContext();
    ImGui::GetIO().BackendPlatformUserData=reinterpret_cast<void*>(1);
    rubyvr::dev::panel::configure({[](){rubyvr::dev::panel::Model m;m.available=true;m.checkpoints={"Synthetic A","Synthetic B"};return m;},nullptr});
    vr::viewer::set_overlay(rubyvr::dev::panel::draw,rubyvr::dev::panel::open,rubyvr::dev::panel::shutdown);
    expect(vr::viewer::init(window,false),"viewer reinitializes with overlay hooks");
    vr::viewer::game_frame(field,signal,original,240,160,{},false);
    expect(ImGui::GetCurrentContext()==game_ui && ImGui::GetIO().BackendPlatformUserData==reinterpret_cast<void*>(1),"panel restores original game UI context");
    SDL_Event escape{};escape.type=SDL_KEYDOWN;escape.key.keysym.scancode=SDL_SCANCODE_ESCAPE;
    escape.key.windowID=SDL_GetWindowID(window)+100;
    expect(!rubyvr::dev::panel::event(escape) && !rubyvr::dev::panel::open(),"other window Escape stays with its owner");
    escape.key.windowID=SDL_GetWindowID(window);
    expect(rubyvr::dev::panel::event(escape) && rubyvr::dev::panel::open(),"viewer Escape opens controls through retained hooks");
    escape.key.repeat=1;rubyvr::dev::panel::event(escape);
    expect(rubyvr::dev::panel::open(),"held Escape does not flicker panel");
    SDL_Event close{};close.type=SDL_WINDOWEVENT;close.window.windowID=SDL_GetWindowID(window);close.window.event=SDL_WINDOWEVENT_CLOSE;
    expect(!rubyvr::dev::panel::event(close),"panel never eats window close");
    vr::viewer::game_frame(field,signal,original,240,160,{},false);
    vr::viewer::shutdown();
    expect(!rubyvr::dev::panel::open() && ImGui::GetCurrentContext()==game_ui,"shutdown releases only viewer UI");
    ImGui::GetIO().BackendPlatformUserData=nullptr;ImGui::DestroyContext(game_ui);
    vr::diorama::shutdown();SDL_GL_DeleteContext(context);SDL_DestroyWindow(window);SDL_Quit();
    std::cout<<"PASS: live viewer actors/connected world, menu retention, original-frame/UI pixels, return/load controls (local GL; synthetic art)\n";
}
