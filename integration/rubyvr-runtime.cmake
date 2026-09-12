# SPDX-License-Identifier: GPL-3.0-or-later
# For the documented private Windows runner experiment. The caller supplies
# its runtime_bus_bridge/gba_bus/sha1 headers and libraries, plus the frame sink.
function(rubyvr_attach_runtime target)
    if(NOT WIN32)
        message(FATAL_ERROR "The current native runtime adapter uses Win32 OpenXR bindings")
    endif()
    set(root "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/..")
    find_package(OpenXR REQUIRED)
    find_package(OpenGL REQUIRED)
    find_package(SDL2 REQUIRED)
    target_sources(${target} PRIVATE
        "${root}/integration/runtime/vr_layer.cpp"
        "${root}/integration/runtime/ruby_world.cpp"
        "${root}/integration/runtime/live_scene.cpp"
        "${root}/integration/runtime/actor_rules.cpp"
        "${root}/integration/runtime/live_region.cpp"
        "${root}/integration/runtime/live_presentation.cpp"
        "${root}/integration/runtime/screen_overlay.cpp"
        "${root}/integration/runtime/map_view.cpp"
        "${root}/integration/runtime/renderer.cpp"
        "${root}/integration/runtime/viewer.cpp"
        "${root}/integration/runtime/dev_runtime.cpp"
        "${root}/integration/runtime/game_input.cpp"
        "${root}/src/vr/camera_input.cpp"
        "${root}/src/dev/session.cpp"
        "${root}/src/dev/demo_panel.cpp"
        "${RECOMP_UI_ROOT}/src/third_party/imgui/backends/imgui_impl_opengl3.cpp"
        "${root}/src/vr/tileset.cpp" "${root}/src/vr/gl_loader.cpp"
        "${root}/src/vr/diorama.cpp" "${root}/src/vr/world_io.cpp"
        "${root}/src/vr/json_scan.cpp" "${root}/src/vr/overrides.cpp"
        "${root}/src/vr/cutout.cpp" "${root}/src/vr/part_geometry.cpp"
        "${root}/src/vr/terrain.cpp" "${root}/src/vr/actor_frame.cpp" "${root}/src/vr/actor_render.cpp" "${root}/src/vr/actor_range.cpp")
    target_include_directories(${target} BEFORE PRIVATE
        "${root}/integration/runtime" "${root}/src/vr" "${root}/src")
    target_compile_features(${target} PRIVATE cxx_std_20)
    target_compile_definitions(${target} PRIVATE XR_USE_PLATFORM_WIN32 XR_USE_GRAPHICS_API_OPENGL)
    # The private runner supplies the documented dispatch-boundary adapter.
    target_compile_definitions(${target} PRIVATE RUBYVR_DEV_RUNTIME=1)
    target_include_directories(${target} PRIVATE "${RECOMP_UI_ROOT}/src"
        "${RECOMP_UI_ROOT}/src/third_party/imgui" "${RECOMP_UI_ROOT}/src/third_party/imgui/backends")
    target_compile_definitions(gbarecomp_runtime PRIVATE RUBYVR_DEV_RUNTIME=1)
    target_compile_definitions(gbarecomp_runtime PRIVATE RUBYVR_CAMERA_INPUT=1)
    target_include_directories(gbarecomp_runtime PRIVATE "${root}/integration/runtime" "${root}/src/vr" "${root}/src")
    target_link_libraries(${target} PRIVATE SDL2::SDL2 OpenXR::openxr_loader OpenGL::GL)
endfunction()
