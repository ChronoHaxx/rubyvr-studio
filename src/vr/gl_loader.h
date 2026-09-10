// gl_loader.h — reach OpenGL past version 1.1 on Windows.
//
// THE PROBLEM, which is a Windows-specific historical accident:
//
//   opengl32.dll has exported exactly the OpenGL 1.1 entry points since 1996
//   and never gained more. Everything after that — shaders, vertex buffers,
//   framebuffer objects, i.e. everything needed to render a 3D scene — exists
//   in the driver but is NOT exported by the system library. You get at it by
//   asking the driver for a function pointer at runtime, per context.
//
//   So <GL/gl.h> declaring only 1.1 is not the header being out of date. It is
//   telling the truth about what you can link against. That is why Phase 0-4's
//   quad layer could get away with plain gl.h (glTexSubImage2D is 1.1) and why
//   Phase 5 cannot.
//
// THE FIX: SDL_GL_GetProcAddress, which wraps wglGetProcAddress and its
// platform equivalents. SDL2 is already a dependency, so this costs no new
// library — GLEW, GLAD and the rest are conveniences over exactly this call.
//
// Two rules that bite:
//   1. A GL context must be CURRENT before loading. Pointers come from the
//      driver behind the current context, and there is nothing to ask before
//      one exists.
//   2. The pointers belong to that context. This layer has exactly one, made
//      in vr::start() and used only by the VR thread, so globals are honest
//      here. They would not be in a multi-context program.
//
// Declared as our own globals rather than via GL_GLEXT_PROTOTYPES: we want
// variables to assign, not prototypes to link against, and linking is the thing
// that cannot work.

#pragma once

#include <SDL2/SDL_opengl.h>       // GL 1.1 + the glext typedefs and enums

namespace vr {
namespace gl {

// Every function this project calls beyond GL 1.1. One list, expanded three
// times (declaration, definition, load) so a new call cannot be added to two of
// the three and silently null-pointer at runtime.
#define VR_GL_FUNCTIONS(X)                                                     \
    /* shaders and programs */                                                 \
    X(PFNGLCREATESHADERPROC,             glCreateShader)                       \
    X(PFNGLSHADERSOURCEPROC,             glShaderSource)                       \
    X(PFNGLCOMPILESHADERPROC,            glCompileShader)                      \
    X(PFNGLGETSHADERIVPROC,              glGetShaderiv)                        \
    X(PFNGLGETSHADERINFOLOGPROC,         glGetShaderInfoLog)                   \
    X(PFNGLDELETESHADERPROC,             glDeleteShader)                       \
    X(PFNGLCREATEPROGRAMPROC,            glCreateProgram)                      \
    X(PFNGLATTACHSHADERPROC,             glAttachShader)                       \
    X(PFNGLLINKPROGRAMPROC,              glLinkProgram)                        \
    X(PFNGLGETPROGRAMIVPROC,             glGetProgramiv)                       \
    X(PFNGLGETPROGRAMINFOLOGPROC,        glGetProgramInfoLog)                  \
    X(PFNGLUSEPROGRAMPROC,               glUseProgram)                         \
    X(PFNGLDELETEPROGRAMPROC,            glDeleteProgram)                      \
    /* uniforms */                                                             \
    X(PFNGLGETUNIFORMLOCATIONPROC,       glGetUniformLocation)                 \
    X(PFNGLUNIFORMMATRIX4FVPROC,         glUniformMatrix4fv)                   \
    X(PFNGLUNIFORM1IPROC,                glUniform1i)                          \
    X(PFNGLUNIFORM1FPROC,                glUniform1f)                          \
    X(PFNGLUNIFORM2FPROC,                glUniform2f)                          \
    X(PFNGLUNIFORM4FPROC,                glUniform4f)                          \
    /* buffers and vertex arrays */                                            \
    X(PFNGLGENBUFFERSPROC,               glGenBuffers)                         \
    X(PFNGLBINDBUFFERPROC,               glBindBuffer)                         \
    X(PFNGLBUFFERDATAPROC,               glBufferData)                         \
    X(PFNGLDELETEBUFFERSPROC,            glDeleteBuffers)                      \
    X(PFNGLGENVERTEXARRAYSPROC,          glGenVertexArrays)                    \
    X(PFNGLBINDVERTEXARRAYPROC,          glBindVertexArray)                    \
    X(PFNGLDELETEVERTEXARRAYSPROC,       glDeleteVertexArrays)                 \
    X(PFNGLENABLEVERTEXATTRIBARRAYPROC,  glEnableVertexAttribArray)            \
    X(PFNGLDISABLEVERTEXATTRIBARRAYPROC, glDisableVertexAttribArray)           \
    X(PFNGLVERTEXATTRIBDIVISORPROC,      glVertexAttribDivisor)                \
    X(PFNGLDRAWARRAYSINSTANCEDPROC,      glDrawArraysInstanced)                \
    X(PFNGLVERTEXATTRIBPOINTERPROC,      glVertexAttribPointer)                \
    X(PFNGLVERTEXATTRIBIPOINTERPROC,     glVertexAttribIPointer)               \
    X(PFNGLBINDATTRIBLOCATIONPROC,       glBindAttribLocation)                 \
    /* framebuffer objects — how we render into an OpenXR swapchain image */   \
    X(PFNGLGENFRAMEBUFFERSPROC,          glGenFramebuffers)                    \
    X(PFNGLBINDFRAMEBUFFERPROC,          glBindFramebuffer)                    \
    X(PFNGLFRAMEBUFFERTEXTURE2DPROC,     glFramebufferTexture2D)               \
    X(PFNGLDELETEFRAMEBUFFERSPROC,       glDeleteFramebuffers)                 \
    X(PFNGLCHECKFRAMEBUFFERSTATUSPROC,   glCheckFramebufferStatus)             \
    X(PFNGLGENRENDERBUFFERSPROC,         glGenRenderbuffers)                   \
    X(PFNGLBINDRENDERBUFFERPROC,         glBindRenderbuffer)                   \
    X(PFNGLRENDERBUFFERSTORAGEPROC,      glRenderbufferStorage)                \
    X(PFNGLFRAMEBUFFERRENDERBUFFERPROC,  glFramebufferRenderbuffer)            \
    X(PFNGLDELETERENDERBUFFERSPROC,      glDeleteRenderbuffers)                \
    /* texture units */                                                        \
    X(PFNGLACTIVETEXTUREPROC,            glActiveTexture)

#define X(type, name) extern type name;
VR_GL_FUNCTIONS(X)
#undef X

// Resolve every pointer above. A GL context must already be current.
//
// Reports the FIRST missing name rather than just failing: "glGenVertexArrays
// is null" says the driver gave a 2.1-era context, which is a completely
// different fix from "the loader was never called".
bool load();

// True once load() has succeeded. Cheap guard for teardown paths that may run
// before or after the context existed.
bool loaded();

}  // namespace gl
}  // namespace vr
