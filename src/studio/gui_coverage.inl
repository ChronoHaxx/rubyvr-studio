// Read-only ledger snapshots. Filtering does not mutate artwork or review data.
bool review_load_map(App& a,const std::string& id) {
    auto& r=a.review;
    if(!studio::coverage::load_room(r.index,id,&r.room,&r.error)) return false;
    r.selected=-1;r.dirty=true;return true;
}

void review_panel(App& a) {
    auto& r=a.review;if(!r.open) return;
    const ImVec2 size(std::min(1120.f,a.layout.header.w-48),std::min(670.f,a.layout.status.y-36));
    ImGui::SetNextWindowSize(size,ImGuiCond_Always);
    ImGui::SetNextWindowPos(ImVec2(a.layout.header.w*.5f,(a.layout.status.y+42)*.5f),ImGuiCond_Appearing,ImVec2(.5f,.5f));
    if(!ImGui::Begin("Scenery review",&r.open,ImGuiWindowFlags_NoResize|ImGuiWindowFlags_NoCollapse)) {ImGui::End();return;}
    auto reload=[&]() {
        studio::coverage::Index index;
        if(!studio::coverage::load_index(r.path,&index,&r.error)) return;
        const std::string wanted=r.room.map.empty()?a.map_id:r.room.map;
        const auto found=std::find_if(index.maps.begin(),index.maps.end(),[&](const auto& m){return m.id==wanted;});
        studio::coverage::Room room;
        if(!studio::coverage::load_room(index,found!=index.maps.end()?wanted:index.maps.front().id,&room,&r.error)) return;
        r.index=std::move(index);r.room=std::move(room);r.loaded=true;r.selected=-1;r.dirty=true;
    };
    if(!r.loaded && r.error.empty()) reload();
    ImGui::TextUnformatted("Find an item, inspect its review, then open the exact map location.");
    ImGui::SameLine();if(ImGui::Button("Reload snapshot")) reload();
    if(!r.error.empty()) ImGui::TextWrapped("%s",r.error.c_str());
    if(!r.loaded) {
        ImGui::TextWrapped("No review data is ready. Sync the coverage ledger once, then start Studio with the launcher. Your models remain available outside this panel.");
        ImGui::End();return;
    }
    const bool different_pack=r.input_fingerprint!=r.index.pack_fingerprint || a.working!=r.baseline || a.document.draft_dirty();
    if(different_pack) ImGui::TextColored(ImVec4(.70f,.27f,.12f,1),"Pack has changed: this is historical coverage. Re-sync before accepting new work.");
    else ImGui::TextDisabled("Ledger snapshot exported %s. Source-state reports stay in the ledger tools.",r.index.created_at.c_str());
    ImGui::Separator();
    ImGui::BeginChild("review-maps",ImVec2(222,0),true);
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##review-map-search","Find a map...",r.map_search,sizeof(r.map_search));
    ImGui::TextDisabled("%zu maps / failed entries",r.index.maps.size());
    std::string map_query=r.map_search;
    auto lower=[](std::string value){for(char& c:value) if(c>='A'&&c<='Z') c=char(c-'A'+'a');return value;};
    map_query=lower(map_query);
    for(const auto& map:r.index.maps) {
        const std::string name=room_title(map.id);
        if(!map_query.empty() && lower(name+" "+map.id).find(map_query)==std::string::npos) continue;
        const std::string label=name+(map.failed?"  ! "+std::to_string(map.failed):"");
        ImGui::PushID(map.id.c_str());
        if(ImGui::Selectable(label.c_str(),r.room.map==map.id)) review_load_map(a,map.id);
        if(ImGui::IsItemHovered()) ImGui::SetTooltip("%s\n%d exact source/model placements\n%d with failed checks or open related defects",name.c_str(),map.rows,map.failed);
        ImGui::PopID();
    }
    ImGui::EndChild();ImGui::SameLine();
    ImGui::BeginChild("review-items",ImVec2(0,0),false);
    ImGui::TextUnformatted(room_title(r.room.map).c_str());
    ImGui::SetNextItemWidth(160);
    r.dirty|=ImGui::Combo("##review-filter",&r.filter,"All\0No model\0Unresolved\0Unreviewed\0Failed\0");
    ImGui::SameLine();ImGui::SetNextItemWidth(-1);
    r.dirty|=ImGui::InputTextWithHint("##review-search","Search objects or review notes...",r.search,sizeof(r.search));
    r.dirty|=ImGui::Checkbox("Flat / ground",&r.include_flat);ImGui::SameLine();
    r.dirty|=ImGui::Checkbox("Edge copies",&r.include_padding);
    if(r.dirty) {
        r.visible.clear();
        for(size_t i=0;i<r.room.rows.size();++i)
            if(studio::coverage::matches(r.room.rows[i],r.filter,r.include_flat,r.include_padding,r.search)) r.visible.push_back(i);
        r.dirty=false;
        if(r.selected>=0 && std::find(r.visible.begin(),r.visible.end(),size_t(r.selected))==r.visible.end()) r.selected=-1;
    }
    ImGui::SameLine();ImGui::TextDisabled("%zu / %zu entries",r.visible.size(),r.room.rows.size());
    if(!r.room.notes.empty() && ImGui::CollapsingHeader("Map-wide issues")) ImGui::TextWrapped("%s",r.room.notes.c_str());
    if(ImGui::BeginTable("review-rows",3,ImGuiTableFlags_BordersInnerV|ImGuiTableFlags_RowBg|ImGuiTableFlags_ScrollY,
                         ImVec2(0,std::max(110.f,ImGui::GetContentRegionAvail().y-184)))) {
        ImGui::TableSetupColumn("Object / source group",ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Model coverage",ImGuiTableColumnFlags_WidthFixed,115);
        ImGui::TableSetupColumn("Review status",ImGuiTableColumnFlags_WidthFixed,120);
        ImGui::TableSetupScrollFreeze(0,1);ImGui::TableHeadersRow();
        ImGuiListClipper clipper;clipper.Begin(int(r.visible.size()));
        while(clipper.Step()) for(int i=clipper.DisplayStart;i<clipper.DisplayEnd;++i) {
            const auto row_index=r.visible[size_t(i)];const auto& row=r.room.rows[row_index];
            ImGui::TableNextRow();ImGui::TableNextColumn();ImGui::PushID(row.id.c_str());
            if(ImGui::Selectable(row.label.c_str(),r.selected==int(row_index),ImGuiSelectableFlags_SpanAllColumns)) r.selected=int(row_index);
            ImGui::PopID();ImGui::TableNextColumn();ImGui::TextUnformatted(row.implementation.c_str());
            ImGui::TableNextColumn();
            if(row.flags&8) ImGui::TextColored(ImVec4(.72f,.21f,.12f,1),"Needs attention");
            else ImGui::TextUnformatted(row.visual.c_str());
        }
        ImGui::EndTable();
    }
    if(r.visible.empty()) ImGui::TextWrapped("No entries match these filters. Try All, another map, or include flat/ground and edge copies.");
    if(r.selected>=0 && r.selected<int(r.room.rows.size())) {
        const auto& row=r.room.rows[size_t(r.selected)];
        ImGui::Separator();ImGui::TextUnformatted(row.label.c_str());
        if(ImGui::Button("OPEN IN MAP",ImVec2(145,26))) {
            r.target=row;r.target_map=r.room.map;r.target_width=r.room.width;r.target_height=r.room.height;
            r.open=false;request_action(a,App::Action::Review);
        }
        ImGui::SameLine();ImGui::TextDisabled("%d,%d  /  %s",row.x,row.y,row.boundary.c_str());
        ImGui::Text("Visual: %s  |  Live: %s  |  Headset: %s",row.visual.c_str(),row.live.c_str(),row.headset.c_str());
        ImGui::Text("Disposition: %s",row.disposition.c_str());
        ImGui::BeginChild("review-evidence",ImVec2(0,0),false);
        ImGui::TextWrapped("%s",row.notes.empty()?"No review evidence recorded for this placement yet.":row.notes.c_str());
        ImGui::EndChild();
    } else ImGui::TextWrapped("Select a row to see evidence and open its location. Related failures are flagged without approving this placement.");
    ImGui::EndChild();ImGui::End();
}
