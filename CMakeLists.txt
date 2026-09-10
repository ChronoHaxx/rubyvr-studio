cmake_minimum_required(VERSION 3.20)
project(RubyVRStudio VERSION 0.1.0 LANGUAGES CXX)

if(NOT WIN32)
    message(FATAL_ERROR "The editor currently uses Windows file and capture APIs. See the portability issue before adding another platform.")
endif()
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
find_package(SDL2 CONFIG REQUIRED)
find_package(ZLIB REQUIRED)
# The editor uses OpenXR pose/FOV types only. It does not link the XR loader.
find_path(OPENXR_INCLUDE_DIR openxr/openxr.h REQUIRED)

set(STUDIO_CORE
    src/studio/connected_scene.cpp
    src/studio/foundation.cpp
    src/studio/coverage_view.cpp
    src/studio/capture.cpp src/studio/decomp_source.cpp
    src/studio/pattern_io.cpp src/studio/png4bpp.cpp src/studio/snapshot_build.cpp
    src/vr/diorama.cpp src/vr/gl_loader.cpp src/vr/json_scan.cpp
    src/vr/overrides.cpp src/vr/cutout.cpp src/vr/part_geometry.cpp
    src/vr/tileset.cpp src/vr/world_io.cpp)
list(APPEND STUDIO_CORE src/vr/terrain.cpp)
add_executable(rubyvr_studio src/studio/compare.cpp src/studio/asset_catalog.cpp
    src/studio/terrain_test.cpp
    src/studio/foundation_test.cpp
    src/studio/connected_test.cpp
    src/studio/png_write.cpp src/studio/mesh_audit.cpp ${STUDIO_CORE})

set(IMGUI third_party/imgui)
add_executable(rubyvr_gui
    src/studio/gui.cpp src/studio/environment_preview.cpp src/studio/png_write.cpp src/studio/room2d.cpp
    src/studio/group.cpp src/studio/group_test.cpp src/studio/mesh_audit.cpp
    ${STUDIO_CORE}
    ${IMGUI}/imgui.cpp ${IMGUI}/imgui_draw.cpp ${IMGUI}/imgui_tables.cpp ${IMGUI}/imgui_widgets.cpp
    ${IMGUI}/backends/imgui_impl_sdl2.cpp ${IMGUI}/backends/imgui_impl_opengl3.cpp)
target_include_directories(rubyvr_gui PRIVATE ${IMGUI} ${IMGUI}/backends)
foreach(target rubyvr_gui rubyvr_studio)
    target_include_directories(${target} PRIVATE src src/vr src/studio ${OPENXR_INCLUDE_DIR})
    target_compile_definitions(${target} PRIVATE XR_USE_PLATFORM_WIN32 XR_USE_GRAPHICS_API_OPENGL)
    target_link_libraries(${target} PRIVATE SDL2::SDL2 ZLIB::ZLIB opengl32)
endforeach()
