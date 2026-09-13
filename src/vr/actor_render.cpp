// SPDX-License-Identifier: GPL-3.0-or-later
#include "actor_render.h"
#include "gl_loader.h"
#include "actor_range.h"
#include "billboard.h"
#include <cmath>
#include <cstdio>

namespace vr::actor_render {
namespace {
struct Item {
    GLuint texture=0; actor::Frame frame,uploaded;
    std::array<actor::Frame,4> directions;
    uint8_t world_facing=0;
    float x=0,y=0,z=0; bool visible=false,player=false;
};
Item items[world::kObjectEventCount+64];
actor::RangeCache range;
Stats result;
bool camera_set=false,hide_player=false;
billboard::Basis camera_basis;
GLuint program=0,vao=0,vbo=0;
GLint matrix=-1,sampler=-1;
bool init() {
    if(program)return true;
    const char* vs=R"(#version 330 core
layout(location=0) in vec3 position;
layout(location=1) in vec2 uv;
uniform mat4 matrix; out vec2 texcoord;
void main(){texcoord=uv;gl_Position=matrix*vec4(position,1);})";
    const char* fs=R"(#version 330 core
in vec2 texcoord; uniform sampler2D image; out vec4 color;
void main(){color=texture(image,texcoord);if(color.a<0.5)discard;})";
    auto compile=[](GLenum type,const char* text) {
        GLuint shader=gl::glCreateShader(type);gl::glShaderSource(shader,1,&text,nullptr);
        gl::glCompileShader(shader);GLint ok=0;gl::glGetShaderiv(shader,GL_COMPILE_STATUS,&ok);
        if(!ok){gl::glDeleteShader(shader);return GLuint(0);}return shader;
    };
    const auto v=compile(GL_VERTEX_SHADER,vs),f=compile(GL_FRAGMENT_SHADER,fs);
    if(!v || !f){if(v)gl::glDeleteShader(v);if(f)gl::glDeleteShader(f);return false;}
    program=gl::glCreateProgram();gl::glAttachShader(program,v);gl::glAttachShader(program,f);
    gl::glLinkProgram(program);gl::glDeleteShader(v);gl::glDeleteShader(f);
    GLint ok=0;gl::glGetProgramiv(program,GL_LINK_STATUS,&ok);
    if(!ok){gl::glDeleteProgram(program);program=0;return false;}
    matrix=gl::glGetUniformLocation(program,"matrix");sampler=gl::glGetUniformLocation(program,"image");
    gl::glGenVertexArrays(1,&vao);gl::glGenBuffers(1,&vbo);
    gl::glBindVertexArray(vao);gl::glBindBuffer(GL_ARRAY_BUFFER,vbo);
    gl::glEnableVertexAttribArray(0);gl::glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,5*sizeof(float),nullptr);
    gl::glEnableVertexAttribArray(1);gl::glVertexAttribPointer(1,2,GL_FLOAT,GL_FALSE,5*sizeof(float),reinterpret_cast<void*>(3*sizeof(float)));
    gl::glBindVertexArray(0);return true;
}
}
const Stats& stats(){return result;}
void set_camera(float yaw,float pitch,bool hide){camera_set=true;camera_basis=billboard::basis(yaw,pitch);hide_player=hide;}
void clear_camera(){camera_set=false;hide_player=false;}
void clear(){result={};range.reset();for(auto& item:items)item.visible=false;}
void update(const world::Snapshot& s,const terrain::Resolved& terrain) {
    result={};for(auto& item:items)item.visible=false;
    range.update(s);if(!s.valid)return;
    for(int i=0;i<world::kObjectEventCount;++i) {
        auto& item=items[i];const auto& object=s.objects[i];item.player=object.is_player && i==s.player_index;
        if(!object.active || object.invisible)continue;
        auto frame=actor::decode(s.actor_sources[i],s.obj_tiles,s.obj_palette,s.obj_mapping_1d);
        if(frame.status==actor::Status::Unsupported || frame.status==actor::Status::Truncated)++result.unsupported;
        if(frame.status!=actor::Status::Visible)continue;
        const auto p=actor::position(frame,s.view_x,s.view_y,s.view_base_x,s.view_base_y,
            s.actor_offset_x,s.actor_offset_y,object.x,object.y,
            item.player?&s.actor_sources[i].motion:nullptr);
        const int x=int(std::floor(p.x)),z=int(std::floor(p.z));
        const auto height=terrain.query(x,z,object.elevation,p.x-x,p.z-z);
        if(!height.resolved()){++result.unresolved;continue;}
        float ground=height.pixels,lift=p.lift;
        const auto jump=actor::jump_span(s.actor_sources[i],p);
        if(jump.active) {
            auto ground_at=[&](float px,float pz) {
                const int cx=int(std::floor(px)),cz=int(std::floor(pz));
                return terrain.query(cx,cz,object.elevation,px-cx,pz-cz);
            };
            const auto from=ground_at(jump.start_x,jump.start_z),to=ground_at(jump.end_x,jump.end_z);
            auto solid_ground=[](const terrain::Height& h) {
                return h.resolved() && (!h.surface || h.surface->kind==terrain::TerrainKind::Ground);
            };
            // A real cliff is discontinuous; an airborne actor must not snap
            // vertically when its projected feet pass that edge. Interpolate
            // the two ground contacts on Ruby's clock, preserving its arc.
            if(solid_ground(from) && solid_ground(to))ground=from.pixels+(to.pixels-from.pixels)*jump.progress;
            const float dx=(jump.end_x-jump.start_x)*.5f,dz=(jump.end_z-jump.start_z)*.5f;
            const auto approach_contact=ground_at(jump.start_x+.75f*dx,jump.start_z+.75f*dz);
            const auto slope_contact=ground_at(jump.start_x+1.25f*dx,jump.start_z+1.25f*dz);
            // On the authored sloping ledge with its original top-down rock
            // band, let the player approach before lifting. A real vertical
            // cliff keeps the original arc above; water/decks keep their own
            // semantics. The source still owns all X/Z motion and collision.
            const auto& source=s.actor_sources[i];
            if(object.is_player && source.jump_arc_valid && approach_contact.surface && approach_contact.surface==slope_contact.surface &&
               from.status==terrain::Status::Authored && to.status==terrain::Status::Authored &&
               solid_ground(from) && solid_ground(to) && solid_ground(approach_contact) && solid_ground(slope_contact) &&
               from.pixels>to.pixels && approach_contact.pixels>slope_contact.pixels) {
                ground=source.jump_ticks<=12?height.pixels:
                    approach_contact.pixels+(to.pixels-approach_contact.pixels)*(source.jump_ticks-12)/20.f;
                lift=actor::approach_jump_lift(source,p.lift);
            }
        }
        item.x=p.x;item.y=ground/16.f+lift;item.z=p.z;
        if(!item.texture) {
            glGenTextures(1,&item.texture);glBindTexture(GL_TEXTURE_2D,item.texture);
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
        }
        const auto& source=s.actor_sources[i];item.world_facing=source.world_facing;
        if(item.world_facing)for(uint8_t d=1;d<=4;++d) {
            item.directions[d-1]=actor::decode_direction(source,s.obj_tiles,s.obj_palette,s.obj_mapping_1d,d);
        }
        item.frame=std::move(frame);item.visible=true;++result.visible;
        if(i==s.player_index && object.is_player) {
            result.player=true;result.player_x=item.x;result.player_z=item.z;
            // Follow ground contact; a jump is actor motion, not a camera lift.
            result.player_y=ground/16.f;
            result.player_lift=lift;
        }
    }
    size_t index=world::kObjectEventCount;
    for(const auto& remembered:range.distant()) {
        auto& item=items[index++];item.player=false;const auto& p=remembered.position;
        const int x=int(std::floor(p.x)),z=int(std::floor(p.z));
        const auto height=terrain.query(x,z,remembered.object.elevation,p.x-x,p.z-z);
        if(!height.resolved()){++result.unresolved;continue;}
        item.x=p.x;item.y=height.pixels/16.f+p.lift;item.z=p.z;
        if(!item.texture) {
            glGenTextures(1,&item.texture);glBindTexture(GL_TEXTURE_2D,item.texture);
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
        }
        item.frame=remembered.frame;item.directions=remembered.directions;item.world_facing=remembered.facing;
        item.visible=true;++result.visible;++result.distant;
    }
    glBindTexture(GL_TEXTURE_2D,0);
}
void draw(const math::Mat4& view,const math::Mat4& model) {
    if(!result.visible || !init())return;
    const auto mvp=math::multiply(view,model);
    float rx=mvp.m[0],rz=mvp.m[8],length=std::hypot(rx,rz);
    if(length<1e-6f){rx=1;rz=0;}else{rx/=length;rz/=length;}
    if(camera_set){rx=camera_basis.right.x;rz=camera_basis.right.z;}
    const bool cull=glIsEnabled(GL_CULL_FACE),blend=glIsEnabled(GL_BLEND);
    glDisable(GL_CULL_FACE);glDisable(GL_BLEND);
    gl::glUseProgram(program);gl::glUniformMatrix4fv(matrix,1,GL_FALSE,mvp.m);
    gl::glUniform1i(sampler,0);gl::glActiveTexture(GL_TEXTURE0);gl::glBindVertexArray(vao);
    for(auto& item:items)if(item.visible && !(hide_player && item.player)) {
        const auto facing=actor::apparent_facing(item.world_facing,rx,rz);
        const auto& f=facing>=1 && facing<=4?item.directions[facing-1]:item.frame;
        glBindTexture(GL_TEXTURE_2D,item.texture);
        if(f.width!=item.uploaded.width || f.height!=item.uploaded.height || f.rgba!=item.uploaded.rgba) {
            glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,f.width,f.height,0,GL_RGBA,GL_UNSIGNED_BYTE,f.rgba.data());
            item.uploaded=f;
        }
        std::array<float,30> vertices;
        const billboard::Basis basis=camera_set?camera_basis:billboard::Basis{{rx,0,rz},{0,1,0}};
        if(!billboard::vertices(basis,{item.x,item.y,item.z},f.corner_x/16.f,f.width/16.f,f.height/16.f,vertices))continue;
        glBindTexture(GL_TEXTURE_2D,item.texture);gl::glBindBuffer(GL_ARRAY_BUFFER,vbo);
        gl::glBufferData(GL_ARRAY_BUFFER,sizeof(vertices),vertices.data(),GL_STREAM_DRAW);glDrawArrays(GL_TRIANGLES,0,6);
    }
    gl::glBindVertexArray(0);glBindTexture(GL_TEXTURE_2D,0);
    if(cull)glEnable(GL_CULL_FACE);if(blend)glEnable(GL_BLEND);
}
void shutdown(){clear();for(auto& item:items){if(item.texture)glDeleteTextures(1,&item.texture);item={};}
    if(program)gl::glDeleteProgram(program);if(vao)gl::glDeleteVertexArrays(1,&vao);if(vbo)gl::glDeleteBuffers(1,&vbo);
    program=vao=vbo=0;}
}
