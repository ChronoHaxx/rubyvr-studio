void focus_connected(App& a) {
    vr::part_geometry::Vec lo{},hi{};
    if(!vr::diorama::region_bounds(&lo,&hi))return;
    a.camera.yaw=.2f;a.camera.pitch=1.1f;
    camera_frame(a.camera,lo,hi,int(a.layout.view.w),int(a.layout.view.h));
}
bool enter_connected(App& a) {
    if(a.exploring || a.mode!=3 || a.document.draft_dirty() || a.pending!=App::Action::None)return false;
    studio::connected::Scene candidate;std::string error;
    if(!studio::connected::build(a.decomp,a.map_id,a.working,&candidate,&error) ||
       !vr::diorama::build_region(candidate.inputs(),a.working,&error)) {
        a.status=error;std::fprintf(stderr,"[connected] %s\n",error.c_str());return false;
    }
    a.connected=std::move(candidate);a.exploring=true;
    a.explore_saved_camera=a.camera;a.explore_saved_fly=a.fly_mode;a.explore_saved_grid=a.show_grid;
    a.explore_saved_terrain=a.terrain.open;
    a.fly_mode=true;a.show_grid=false;a.terrain.open=false;
    a.status="Connected area ready. Hold right mouse + WASD to fly; Q/E changes height. Escape returns to editing.";
    const auto& s=vr::diorama::region_stats();
    std::fprintf(stderr,"[connected] maps=%zu vertices=%zu gpu_bytes=%zu hidden=%zu terrain_rejected=%zu unresolved=%zu hash=%016llx\n",
        s.maps,s.vertices,s.gpu_bytes,s.hidden_cells,s.terrain_rejected,s.unresolved_placements,static_cast<unsigned long long>(s.geometry_hash));
    return true;
}
void leave_connected(App& a) {
    if(!a.exploring)return;
    stop_fly_look(a);vr::diorama::clear_region();a.connected={};a.exploring=false;
    a.camera=a.explore_saved_camera;a.fly_mode=a.explore_saved_fly;a.show_grid=a.explore_saved_grid;
    a.terrain.open=a.explore_saved_terrain;a.status="Returned to editing.";
}
void connected_inspector(App& a) {
    const auto& s=vr::diorama::region_stats();
    ImGui::TextUnformatted("EXPLORE CONNECTED MAPS");ImGui::Separator();
    ImGui::Text("%zu maps loaded",s.maps);
    ImGui::Text("%zu triangles",s.vertices/3);
    ImGui::Text("Mesh + art: %.1f MiB",double(s.gpu_bytes)/1048576);
    ImGui::TextWrapped("Hold right mouse in the view: WASD moves; Q/E down/up; Shift faster. Release to stop.");
    if(ImGui::Button("Fit whole area",ImVec2(-1,26)))focus_connected(a);
    if(ImGui::Button("Return to editing",ImVec2(-1,26)))leave_connected(a);
    if(!a.exploring)return;
    ImGui::Separator();
    for(const auto& m:a.connected.maps)ImGui::TextWrapped("%s",room_title(m.id).c_str());
    ImGui::Separator();
    ImGui::TextWrapped("Desktop exploration preview. Game movement and collision are not active.");
    if(s.terrain_rejected || s.unresolved_placements)ImGui::TextWrapped("Needs review: %zu terrain cells / %zu placements",s.terrain_rejected,s.unresolved_placements);
    if(!a.connected.frontiers.empty()) {
        ImGui::TextWrapped("This area's outer edges remain incomplete:");
        for(const auto& f:a.connected.frontiers)ImGui::TextWrapped("%s",f.c_str());
    }
}
