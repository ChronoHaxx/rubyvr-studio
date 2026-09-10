// SDL event replay for the recorded acceptance journey. Included after the
// ordinary probe helpers; all authoring still runs through the interactive UI.
bool load_showcase(const char* path, Probe& p) {
    if(!vr::json::parse_file(path,&p.showcase) || !p.showcase.is_object()) return false;
    const auto* frames=p.showcase.find("frames");
    const auto* events=p.showcase.find("events");
    const auto* points=p.showcase.find("checkpoints");
    if(!frames || frames->as_int()<1 || frames->as_int()>1800 ||
       !events || !events->is_array() || !points || !points->is_array()) return false;
    for(const auto& e:events->items) {
        const auto* frame=e.find("frame");const auto* type=e.find("type");
        if(!frame || frame->as_int(-1)<0 || frame->as_int()>=frames->as_int() || !type) return false;
        const auto& t=type->string;
        if(t!="motion" && t!="down" && t!="up" && t!="wheel" && t!="focus-lost" && t!="key-down" && t!="key-up" && t!="text" && t!="quit") return false;
        if((t=="down" || t=="up") && e.find("button") && (e.find("button")->as_int()<1 || e.find("button")->as_int()>3)) return false;
        if(t=="text" && (!e.find("text") || e.find("text")->string.size()>=SDL_TEXTINPUTEVENT_TEXT_SIZE)) return false;
    }
    std::vector<std::string> names;
    for(const auto& point:points->items) {
        const auto* frame=point.find("frame");const auto* name=point.find("name");
        if(!frame || frame->as_int(-1)<0 || frame->as_int()>=frames->as_int() || !name || name->string.empty()) return false;
        for(unsigned char c:name->string) if(!std::isalnum(c) && c!='-') return false;
        if(std::find(names.begin(),names.end(),name->string)!=names.end()) return false;
        names.push_back(name->string);
    }
    p.showcasing=true;
    return true;
}

bool showcase_open(Probe& p) {
    p.checkpoints=std::fopen(p.json_path.c_str(),"wb");
    if(!p.checkpoints) return false;
    std::fprintf(p.checkpoints,"{\"scripted\":true,\"fps\":30,\"width\":%d,\"height\":%d,\"checkpoints\":[\n",p.fbw,p.fbh);
    const auto* record=p.showcase.find("record");
    if(record && record->as_int()!=0) {
        p.recording=std::fopen((p.png_path+".rgb").c_str(),"wb");
        if(!p.recording) {std::fclose(p.checkpoints);p.checkpoints=nullptr;return false;}
    }
    return true;
}

void showcase_caption(Probe& p, const App& a) {
    const auto* chapters=p.showcase.find("chapters");
    std::string caption;
    if(chapters) for(const auto& c:chapters->items)
        if(c.find("frame") && c.find("text") && c.find("frame")->as_int()<=p.it) caption=c.find("text")->string;
    auto* dl=ImGui::GetForegroundDrawList();
    const auto& r=a.mode==1?a.layout.map:a.layout.view;
    const ImVec2 pos(r.x+12,r.y+12);
    if(!caption.empty()) {
        const auto size=ImGui::CalcTextSize(caption.c_str());
        dl->AddRectFilled(ImVec2(pos.x-6,pos.y-6),ImVec2(pos.x+size.x+6,pos.y+36),IM_COL32(12,17,24,235),4);
        dl->AddText(pos,IM_COL32(255,232,160,255),caption.c_str());
        dl->AddText(ImVec2(pos.x,pos.y+18),IM_COL32(180,194,210,255),"SCRIPTED DEMONSTRATION / 30 fps playback");
    }
    // Hidden probe windows have no OS pointer in their FBO. Show the actual
    // ImGui position and pressed state so the recording exposes the input.
    const auto& io=ImGui::GetIO();
    dl->AddCircle(io.MousePos,ImGui::IsMouseDown(0)?8.f:5.f,IM_COL32(255,235,150,255),16,2);
}

void showcase_frame(Probe& p, const App& a) {
    probe_read_frame(p);
    if(p.recording && std::fwrite(p.rgb.data(),1,p.rgb.size(),p.recording)!=p.rgb.size()) p.showcase_ok=false;
    for(const auto& point:p.showcase.find("checkpoints")->items) {
        if(point.find("frame")->as_int()!=p.it) continue;
        const std::string name=point.find("name")->string, prefix=p.png_path+"."+name;
        p.showcase_ok=studio::png::write_rgb((prefix+".png").c_str(),p.fbw,p.fbh,p.rgb.data()) && p.showcase_ok;
        p.showcase_ok=studio::pattern_io::write((prefix+".working.json").c_str(),a.working) && p.showcase_ok;
        if(a.has_draft && populated(a.draft)) {
            OverrideSet selected;selected.version=a.draft.voxel?vr::overrides::kVoxelVersion:a.working.version;selected.patterns.push_back(a.draft);
            p.showcase_ok=studio::pattern_io::write((prefix+".draft.json").c_str(),selected) && p.showcase_ok;
        }
        auto* f=p.checkpoints;
        if(p.checkpoint_count++) std::fputs(",\n",f);
        std::fputs("{\"name\":",f);json_string(f,name);
        const auto resolved=vr::overrides::resolve(a.snap,a.working);
        std::fprintf(f,",\"frame\":%d,\"mode\":%d,\"has_draft\":%s,\"members\":%zu,\"parts\":%zu,\"undo\":%zu,\"draft_dirty\":%s,\"unsaved\":%s,\"working\":%zu,\"accepted\":%zu,\"draft_accepted\":%zu,\"show_matches\":%s,\"handle_active\":%s,\"raised\":%zu,\"preview_vertices\":%d,\"preview_hash\":\"%016llx\",\"room_hash\":",
            p.it,a.mode,a.has_draft?"true":"false",size_t(std::count(a.draft.mask.begin(),a.draft.mask.end(),uint8_t(1))),
            a.draft.parts.size(),a.document.undo_count(),a.document.draft_dirty()?"true":"false",a.document.unsaved()?"true":"false",
            a.working.patterns.size(),resolved.accepted.size(),a.accepted_matches,a.show_matches?"true":"false",a.handle.active?"true":"false",
            vr::diorama::diorama_stats().raised_instances,a.preview_vertices,static_cast<unsigned long long>(a.preview_hash));
        json_string(f,stats_value(a.last_stats,"geom"));
        std::fputs(",\"map_id\":",f);json_string(f,a.map_id);
        const auto& region=vr::diorama::region_stats();
        std::fprintf(f,",\"region_stored_vertices\":%zu,\"region_models\":%zu,\"region_model_instances\":%zu,\"region_deformed_instances\":%zu,\"region_batches\":%zu",
            region.stored_vertices,region.models,region.model_instances,region.deformed_instances,region.batches);
        std::fprintf(f,",\"region_draw_calls\":%zu,\"region_drawn_vertices\":%zu",region.draw_calls,region.drawn_vertices);
        std::fprintf(f,",\"exploring\":%s,\"region_maps\":%zu,\"region_vertices\":%zu,\"region_bytes\":%zu,\"region_hidden\":%zu,\"region_rejected\":%zu,\"region_unresolved\":%zu,\"region_hash\":\"%016llx\"",
            a.exploring?"true":"false",region.maps,region.vertices,region.gpu_bytes,region.hidden_cells,
            region.terrain_rejected,region.unresolved_placements,static_cast<unsigned long long>(region.geometry_hash));
        const auto& terrain=vr::diorama::diorama_stats();
        std::fprintf(f,",\"terrain_cells\":%zu,\"terrain_rejected\":%zu,\"terrain_vertices\":%zu,\"unresolved_placements\":%zu,\"terrain_open\":%s,\"terrain_region\":[%d,%d,%d,%d]",
            terrain.terrain_cells,terrain.terrain_rejected,terrain.terrain_vertices,terrain.unresolved_placements,
            a.terrain.open?"true":"false",a.terrain.x0,a.terrain.y0,a.terrain.x1,a.terrain.y1);
        std::fputs(",\"review_map\":",f);json_string(f,a.review.room.map);
        std::fputs(",\"review_focus\":",f);json_string(f,a.review.focused.id);
        std::fputs(",\"review_selected\":",f);
        json_string(f,a.review.selected>=0 && size_t(a.review.selected)<a.review.room.rows.size()?a.review.room.rows[size_t(a.review.selected)].id:"");
        std::fprintf(f,",\"review_open\":%s,\"review_filter\":%d,\"review_visible\":%zu,\"review_flat\":%s,\"review_padding\":%s,\"review_historical\":%s",
            a.review.open?"true":"false",a.review.filter,a.review.visible.size(),a.review.include_flat?"true":"false",a.review.include_padding?"true":"false",
            a.review.input_fingerprint!=a.review.index.pack_fingerprint || a.working!=a.review.baseline || a.document.draft_dirty()?"true":"false");
        std::fprintf(f,",\"camera\":[%.9g,%.9g,%.9g],\"camera_target\":[%.9g,%.9g,%.9g],\"fly_mode\":%s,\"fly_looking\":%s,\"typing\":%s,\"pending\":%d,\"accepted_placements\":[",
            double(a.camera.yaw),double(a.camera.pitch),double(a.camera.dist),double(a.camera.tx),double(a.camera.ty),double(a.camera.tz),
            a.fly_mode?"true":"false",a.fly_looking?"true":"false",ImGui::GetIO().WantTextInput?"true":"false",int(a.pending));
        for(size_t i=0;i<resolved.accepted.size();++i) {
            const auto& m=resolved.accepted[i];std::fprintf(f,"%s[%d,%d,%d]",i?",":"",m.pattern,m.x,m.y);
        }
        std::fputs("],\"selected_part\":",f);json_string(f,a.document.state.selected_part);
        std::fputs(",\"lighting\":",f);json_string(f,studio::environment::name(a.lighting));
        double preview_ms=0;const unsigned samples=std::min(16u,a.preview_draw_samples);
        for(unsigned i=0;i<samples;++i) preview_ms+=a.preview_draw_ms[i];
        std::fprintf(f,",\"sky_ok\":%s,\"mesh_uploads\":%llu,\"preview_draw_ms\":%.6f,\"preview_draw_samples\":%u,\"view\":[%.3f,%.3f,%.3f,%.3f]",
            a.sky_ok?"true":"false",static_cast<unsigned long long>(vr::diorama::mesh_upload_count()),
            samples?preview_ms/samples:0,samples,double(a.layout.view.x),double(a.layout.view.y),
            double(a.layout.view.w),double(a.layout.view.h));
        std::fprintf(f,",\"pixel_tool\":%d,\"pixel_shape\":%d,\"pixel_selection\":[%d,%d,%d,%d],\"source_canvas\":[%.3f,%.3f,%.3f,%.3f],\"orthographic\":%s,\"neutral\":%s}",
            a.pixels.tool,a.pixels.shape,a.pixels.selection[0],a.pixels.selection[1],a.pixels.selection[2],a.pixels.selection[3],
            double(a.mask_canvas.x),double(a.mask_canvas.y),double(a.mask_canvas.w),double(a.mask_canvas.h),
            a.camera.orthographic?"true":"false",a.neutral?"true":"false");
        std::fflush(f);
        std::fprintf(stdout,"[showcase] frame=%d checkpoint=%s mode=%d members=%zu parts=%zu handle=%d\n",p.it,name.c_str(),a.mode,a.draft.mask.size(),a.draft.parts.size(),a.handle.active);
    }
}

int showcase_report(Probe& p, const App& a) {
    if(p.recording) {if(std::fclose(p.recording)) p.showcase_ok=false;p.recording=nullptr;}
    if(p.checkpoint_count!=int(p.showcase.find("checkpoints")->items.size())) p.showcase_ok=false;
    std::fprintf(p.checkpoints,"\n],\"frames\":%d,\"closed\":%s,\"ok\":%s}\n",p.it,a.close_ready?"true":"false",p.showcase_ok?"true":"false");
    if(std::fclose(p.checkpoints)) p.showcase_ok=false;p.checkpoints=nullptr;
    std::fprintf(stdout,"[showcase] %s checkpoints=%d frames=%d closed=%s\n",p.showcase_ok?"PASS":"FAIL",p.checkpoint_count,p.it,a.close_ready?"yes":"no");
    return p.showcase_ok?0:1;
}
