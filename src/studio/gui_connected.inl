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
    a.connected_anchor=a.map_id;a.connected_failed.clear();
    a.stream_updates=a.stream_unloaded=a.stream_reused=a.stream_errors=a.stream_pending_frames=0;
    a.stream_loaded=a.connected.maps.size();a.stream_build_ms=a.stream_main_ms=0;
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
    if(a.connected_work)a.connected_work->cancel=true;
    stop_fly_look(a);vr::diorama::clear_region();a.connected={};a.exploring=false;
    a.camera=a.explore_saved_camera;a.fly_mode=a.explore_saved_fly;a.show_grid=a.explore_saved_grid;
    a.terrain.open=a.explore_saved_terrain;a.status="Returned to editing.";
}
void update_connected(App& a) {
    const auto began=std::chrono::steady_clock::now();
    if(a.connected_work && a.connected_work->done.load()) {
        auto work=std::move(a.connected_work);
        if(a.exploring && !work->cancel.load()) {
            if(work->prepared && vr::diorama::publish_region(work->prepared,&work->error)) {
                for(const auto& old:a.connected.maps)if(std::none_of(work->scene.maps.begin(),work->scene.maps.end(),[&](const auto& m){return m.id==old.id;}))++a.stream_unloaded;
                for(const auto& m:work->scene.maps)if(std::none_of(a.connected.maps.begin(),a.connected.maps.end(),[&](const auto& old){return m.id==old.id;}))++a.stream_loaded;
                a.connected=std::move(work->scene);a.connected_anchor=work->anchor;a.connected_failed.clear();
                a.stream_build_ms=work->milliseconds;++a.stream_updates;
                a.stream_reused+=vr::diorama::region_stats().reused_maps;
                a.status="Nearby maps ready. Continue flying, or return to editing.";
            } else {
                ++a.stream_errors;a.connected_failed=work->anchor;
                a.status="Could not load nearby maps: "+work->error;
                std::fprintf(stderr,"[stream] %s\n",a.status.c_str());
            }
        }
    }
    if(a.exploring && !a.connected_work) {
        float x,y,z;camera_eye(a.camera,&x,&y,&z);
        const auto anchor=studio::connected::camera_map(a.connected,a.connected_anchor,x,z);
        if(anchor!=a.connected_anchor && anchor!=a.connected_failed) {
            // Decomp lazily mutates its caches, so the worker gets its own copy.
            // All snapshot/recipe inputs are owned values; only completed work
            // crosses back to the GL thread. At most one worker exists per App.
            try {
                auto work=std::make_shared<ConnectedWork>();work->anchor=anchor;
                auto source=a.decomp;auto previous=a.connected;auto set=a.working;
                auto* result=work.get();
                work->thread=std::thread([result,source=std::move(source),previous=std::move(previous),set=std::move(set)]() mutable {
                    const auto start=std::chrono::steady_clock::now();
                    try {
                        if(!result->cancel.load() && studio::connected::recenter(source,previous,result->anchor,set,&result->scene,&result->error,&result->cancel))
                            result->prepared=vr::diorama::prepare_region(result->scene.inputs(),set,&result->error,&result->cancel);
                    } catch(const std::exception& e) {result->error=e.what();}
                    result->milliseconds=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
                    result->done.store(true);
                });
                a.connected_work=std::move(work);
            } catch(const std::exception& e) {
                a.connected_failed=anchor;++a.stream_errors;a.status="Could not start map loading: "+std::string(e.what());
            }
        }
    }
    if(a.connected_work)++a.stream_pending_frames;
    a.stream_main_ms=std::max(a.stream_main_ms,std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-began).count());
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
    ImGui::TextWrapped("Current map: %s",room_title(a.connected_anchor).c_str());
    if(a.connected_work && !a.connected_work->cancel.load())ImGui::TextWrapped("Loading nearby maps... You can keep flying.");
    else if(!a.connected_failed.empty()) {
        ImGui::TextWrapped("%s",a.status.c_str());
        if(ImGui::Button("Retry map loading"))a.connected_failed.clear();
    }
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
