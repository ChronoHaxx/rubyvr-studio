cmake_minimum_required(VERSION 3.20)
project(RubyVRStudio VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# The batch tool (geometry audit, terrain/foundation/connected checks and the
# original fixtures) is the supported native target. It links SDL2 and OpenGL
# because the shared production mesher reaches GL through function pointers,
# but its test entry points never create a window or a context, so it runs
# headless with DISPLAY unset. The GUI is a separate, optional target so a
# machine without a display can still build and test the batch boundary.
option(RUBYVR_BUILD_GUI "Build the SDL/ImGui studio editor (rubyvr_gui)" ON)

find_package(SDL2 CONFIG REQUIRED)
find_package(ZLIB REQUIRED)
# Threads/OpenGL discovery rather than platform-specific library names:
# opengl32 on Windows, libGL on Linux. The mesher resolves post-1.1 entry
# points at runtime (src/vr/gl_loader.h), but GL 1.1 calls and the loader still
# need the platform library at link time.
find_package(OpenGL REQUIRED)
find_package(Threads REQUIRED)
# The editor uses OpenXR pose/FOV types only. It does not link the XR loader.
find_path(OPENXR_INCLUDE_DIR openxr/openxr.h REQUIRED)

set(STUDIO_CORE
    src/studio/connected_scene.cpp
    src/studio/foundation.cpp
    src/studio/coverage_view.cpp
    src/studio/capture.cpp src/studio/decomp_source.cpp
    src/studio/pattern_io.cpp src/studio/png4bpp.cpp src/studio/snapshot_build.cpp
    src/studio/platform_io.cpp
    src/vr/diorama.cpp src/vr/gl_loader.cpp src/vr/json_scan.cpp
    src/vr/overrides.cpp src/vr/cutout.cpp src/vr/part_geometry.cpp
    src/vr/tileset.cpp src/vr/world_io.cpp)
list(APPEND STUDIO_CORE src/vr/terrain.cpp)

set(RUBYVR_TARGETS rubyvr_studio)
add_executable(rubyvr_studio src/studio/compare.cpp src/studio/asset_catalog.cpp
    src/studio/terrain_test.cpp
    src/studio/foundation_test.cpp
    src/studio/connected_test.cpp
    src/studio/portability_test.cpp
    src/studio/png_write.cpp src/studio/mesh_audit.cpp ${STUDIO_CORE})

if(RUBYVR_BUILD_GUI)
    set(IMGUI third_party/imgui)
    add_executable(rubyvr_gui
        src/studio/gui.cpp src/studio/environment_preview.cpp src/studio/png_write.cpp src/studio/room2d.cpp
        src/studio/group.cpp src/studio/group_test.cpp src/studio/mesh_audit.cpp
        ${STUDIO_CORE}
        ${IMGUI}/imgui.cpp ${IMGUI}/imgui_draw.cpp ${IMGUI}/imgui_tables.cpp ${IMGUI}/imgui_widgets.cpp
        ${IMGUI}/backends/imgui_impl_sdl2.cpp ${IMGUI}/backends/imgui_impl_opengl3.cpp)
    target_include_directories(rubyvr_gui PRIVATE ${IMGUI} ${IMGUI}/backends)
    list(APPEND RUBYVR_TARGETS rubyvr_gui)
endif()

foreach(target IN LISTS RUBYVR_TARGETS)
    target_include_directories(${target} PRIVATE src src/vr src/studio ${OPENXR_INCLUDE_DIR})
    target_compile_definitions(${target} PRIVATE XR_USE_GRAPHICS_API_OPENGL)
    if(WIN32)
        # The Windows integration prototype supplies a Win32 graphics binding.
        # The standalone tools only need the shared pose/FOV types, but keeping
        # the definition preserves the existing Windows translation units.
        target_compile_definitions(${target} PRIVATE XR_USE_PLATFORM_WIN32)
    endif()
    target_link_libraries(${target} PRIVATE SDL2::SDL2 ZLIB::ZLIB OpenGL::GL Threads::Threads)
endforeach()
