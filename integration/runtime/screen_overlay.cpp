// SPDX-License-Identifier: GPL-3.0-or-later
#include "screen_overlay.h"
#include "gl_loader.h"
#include <algorithm>
#include <cmath>

namespace vr::screen_overlay {
namespace {
GLuint program=0,vao=0,texture=0;
GLint extent=-1;
bool init() {
    if(program)return true;
    const char* vs=R"(#version 330 core
uniform vec2 extent; out vec2 uv;
void main(){vec2 p=vec2((gl_VertexID==1||gl_VertexID==2||gl_VertexID==4)?1:-1,
 (gl_VertexID==0||gl_VertexID==1||gl_VertexID==3)?1:-1);
 gl_Position=vec4(p*extent,0,1);uv=vec2((p.x+1)*0.5,(1-p.y)*0.5);})";
    const char* fs=R"(#version 330 core
in vec2 uv; uniform sampler2D image; out vec4 color;
void main(){color=texture(image,uv);if(color.a<0.5)discard;})";
    auto compile=[](GLenum kind,const char* source) {
        auto s=gl::glCreateShader(kind);gl::glShaderSource(s,1,&source,nullptr);gl::glCompileShader(s);
        GLint ok=0;gl::glGetShaderiv(s,GL_COMPILE_STATUS,&ok);
        if(!ok){gl::glDeleteShader(s);return GLuint(0);}return s;
    };
    const auto v=compile(GL_VERTEX_SHADER,vs),f=compile(GL_FRAGMENT_SHADER,fs);
    if(!v||!f){if(v)gl::glDeleteShader(v);if(f)gl::glDeleteShader(f);return false;}
    program=gl::glCreateProgram();gl::glAttachShader(program,v);gl::glAttachShader(program,f);
    gl::glLinkProgram(program);gl::glDeleteShader(v);gl::glDeleteShader(f);
    GLint ok=0;gl::glGetProgramiv(program,GL_LINK_STATUS,&ok);
    if(!ok){shutdown();return false;}
    extent=gl::glGetUniformLocation(program,"extent");
    gl::glGenVertexArrays(1,&vao);glGenTextures(1,&texture);
    glBindTexture(GL_TEXTURE_2D,texture);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
    return true;
}
}
bool draw(std::span<const uint8_t> rgba,int sw,int sh,int w,int h,bool inset) {
    if(sw<=0||sh<=0||w<=0||h<=0||sw>4096||sh>4096||rgba.size()!=size_t(sw)*sh*4||!init())return false;
    float scale=std::min(float(w)/sw,float(h)/sh)*(inset?0.82f:1.f);
    if(scale>=1)scale=std::floor(scale);
    const bool depth=glIsEnabled(GL_DEPTH_TEST),cull=glIsEnabled(GL_CULL_FACE),blend=glIsEnabled(GL_BLEND);
    glDisable(GL_DEPTH_TEST);glDisable(GL_CULL_FACE);glDisable(GL_BLEND);
    gl::glUseProgram(program);gl::glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D,texture);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,sw,sh,0,GL_RGBA,GL_UNSIGNED_BYTE,rgba.data());
    gl::glUniform2f(extent,sw*scale/w,sh*scale/h);
    gl::glBindVertexArray(vao);glDrawArrays(GL_TRIANGLES,0,6);gl::glBindVertexArray(0);
    glBindTexture(GL_TEXTURE_2D,0);
    if(depth)glEnable(GL_DEPTH_TEST);if(cull)glEnable(GL_CULL_FACE);if(blend)glEnable(GL_BLEND);
    return glGetError()==GL_NO_ERROR;
}
void shutdown(){if(texture)glDeleteTextures(1,&texture);if(vao)gl::glDeleteVertexArrays(1,&vao);
    if(program)gl::glDeleteProgram(program);texture=vao=program=0;}
}
