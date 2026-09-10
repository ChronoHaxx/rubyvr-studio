// Terrain shares the document, save path, history and production renderer.
// Selecting a rectangle changes no authored data; one button press is one edit.
bool level_foundation(App& a) {
    if(a.document.draft_dirty() || !a.has_draft || a.draft_slot<0) return false;
    studio::foundation::Result result;vr::overrides::OverrideSet candidate;
    if(!studio::foundation::level(a.snap,a.working,{a.draft_slot,a.origin_x,a.origin_y},&candidate,&result)) {
        a.status=result.message;return false;
    }
    if(result.changed_cells) {
        const auto before=a.document.state;
        a.working=std::move(candidate);a.document.record(before);remesh(a);
    }
    a.terrain.selected=true;a.terrain.room=a.map_id;
    a.terrain.x0=result.x0;a.terrain.x1=result.x1;a.terrain.y0=result.y0;a.terrain.y1=result.y1;
    a.terrain.height=result.height;
    a.status=result.message;
    return true;
}
void terrain_motion(App& a,float mx,float my) {
    int x=0,y=0;
    mx=std::clamp(mx,a.layout.map.x,a.layout.map.x+a.layout.map.w-.5f);
    my=std::clamp(my,a.layout.map.y,a.layout.map.y+a.layout.map.h-.5f);
    if(studio::map_cell(a.layout.map,a.pan_x,a.pan_y,mx,my,a.snap.width,a.snap.height,&x,&y) &&
       vr::terrain::primary_cell(a.snap,x,y)) {a.terrain.x1=x;a.terrain.y1=y;}
}
void terrain_press(App& a,float mx,float my) {
    int x=0,y=0;
    if(!studio::map_cell(a.layout.map,a.pan_x,a.pan_y,mx,my,a.snap.width,a.snap.height,&x,&y) ||
       !vr::terrain::primary_cell(a.snap,x,y)) {a.status="Terrain edits use the primary map; connection padding is read-only.";return;}
    auto& t=a.terrain;
    if(t.pick) {
        const int id=a.snap.metatile_id(x,y);
        if(t.pick==1) t.top=id;else if(t.pick==2) t.side=id;else t.underlay=id;
        t.pick=0;a.status="Material picked. Apply it to the selected terrain region.";return;
    }
    t.x0=t.x1=x;t.y0=t.y1=y;t.selected=t.selecting=true;t.room=a.map_id;
}
bool terrain_edit(App& a,int action) {
    using namespace vr::terrain;
    auto& t=a.terrain;
    if(!t.selected || t.room!=a.map_id || !a.snap.has_map_identity()) return false;
    if(action!=3 && (t.height<0 || t.height>256 || t.step<1 || t.step>256 || t.layer < -1 || t.layer>15 ||
       t.top < -1 || t.top>1023 || t.side < -1 || t.side>1023 || t.underlay < -1 || t.underlay>1023)) {
        a.status="Use heights 0..256, positive stair rise, layers -1..15, and art -1..1023.";return false;
    }
    auto before=a.document.state;
    auto candidate=a.working;
    auto found=std::find_if(candidate.terrain.begin(),candidate.terrain.end(),[&](const auto& m){return m.group==a.snap.map_group && m.number==a.snap.map_number;});
    if(found==candidate.terrain.end()) {
        TerrainMap map;map.group=a.snap.map_group;map.number=a.snap.map_number;
        map.width=a.snap.width;map.height=a.snap.height;
        candidate.terrain.push_back(map);found=candidate.terrain.end()-1;
    }
    if(found->width!=a.snap.width || found->height!=a.snap.height) {
        a.status="Map dimensions changed. Existing terrain needs source review before editing.";return false;
    }
    auto& map=*found;
    // Avoid overwriting a changed material guard belonging to another cell.
    const auto existing=resolve(a.snap,candidate.terrain);
    const bool primary_changed=std::any_of(map.cells.begin(),map.cells.end(),[&](const auto& c){return existing.mismatched[size_t(c.y)*a.snap.width+c.x];});
    if(primary_changed && action!=3) {
        a.status="Terrain source changed. Erase affected cells before applying new heights.";return false;
    }
    const int x0=std::min(t.x0,t.x1),x1=std::max(t.x0,t.x1),y0=std::min(t.y0,t.y1),y1=std::max(t.y0,t.y1);
    for(int y=y0;y<=y1;++y) for(int x=x0;x<=x1;++x) {
        if(!primary_cell(a.snap,x,y)) {a.status="Selection includes undefined cells or connection padding.";return false;}
        auto cell=std::find_if(map.cells.begin(),map.cells.end(),[&](const auto& c){return c.x==x && c.y==y;});
        if(action==3) {if(cell!=map.cells.end()) map.cells.erase(cell);continue;}
        if(cell==map.cells.end()) {TerrainCell c;c.x=x;c.y=y;c.expected=a.snap.cell(x,y);map.cells.push_back(c);cell=map.cells.end()-1;}
        const int height=t.height-((action==1 || action==5)?(y-y0)*t.step:action==2?(y1-y)*t.step:action==6?(y1-y+1)*t.step:0);
        if(height<0 || height>256) {a.status="Steps must stay between 0 and 256 pixels. Adjust height or rise.";return false;}
        TerrainSurface surface;surface.layer=t.layer<0?a.snap.elevation(x,y):t.layer;
        surface.height=height;surface.thickness=height;surface.top=t.top;surface.side=t.side;
        if(action==5) surface.rise_z=-t.step;
        if(action==6) surface.rise_z=t.step;
        if(action==4) {
            surface.kind=TerrainKind::Deck;surface.thickness=t.thickness;
            if(cell->surfaces.empty()) {
                TerrainSurface base;base.layer=a.snap.elevation(x,y);cell->surfaces.push_back(base);
            }
            auto prior=std::find_if(cell->surfaces.begin(),cell->surfaces.end(),[&](const auto& p){return p.layer==surface.layer;});
            if(prior!=cell->surfaces.end()) {a.status="Choose a separate explicit layer for the deck.";return false;}
            cell->surfaces.push_back(surface);
        } else cell->surfaces={surface};
        cell->underlay=t.underlay;
        if(!guard_tile(a.snap,cell->expected&1023,&map)) return false;
        for(int id:{t.top,t.side,t.underlay}) if(id>=0 && !guard_tile(a.snap,uint16_t(id),&map)) return false;
    }
    // Deterministic coordinates/material dependencies, independent of gesture direction.
    std::sort(map.cells.begin(),map.cells.end(),[](const auto& l,const auto& r){return std::pair{l.y,l.x}<std::pair{r.y,r.x};});
    std::set<int> used;
    for(const auto& c:map.cells) {
        used.insert(c.expected&1023);if(c.underlay>=0) used.insert(c.underlay);
        for(const auto& p:c.surfaces) {if(p.top>=0) used.insert(p.top);if(p.side>=0) used.insert(p.side);}
    }
    for(auto it=map.tiles.begin();it!=map.tiles.end();) {if(!used.count(it->first)) it=map.tiles.erase(it);else ++it;}
    if(map.cells.empty()) candidate.terrain.erase(found);
    if(!valid(candidate.terrain)) {
        a.status="Invalid surface: check layer, height, deck thickness and overlap. No changes applied.";return false;
    }
    candidate.version=vr::overrides::kTerrainVersion;
    a.working=std::move(candidate);a.document.record(before);remesh(a);
    a.status=action==3?"Terrain erased. Original floor restored.":"Terrain applied. Ctrl+Z undoes the whole region; Ctrl+S saves.";
    return true;
}
void terrain_inspector(App& a) {
    auto& t=a.terrain;
    ImGui::TextUnformatted("TERRAIN");ImGui::Separator();
    ImGui::TextWrapped("Select a map region, then set its height, steps or slope.");
    if(t.pick) ImGui::TextColored(ImVec4(.72f,.20f,.12f,1),"Click a source tile to pick art.");
    if(t.selected && t.room==a.map_id) ImGui::Text("Region %d,%d to %d,%d",std::min(t.x0,t.x1),std::min(t.y0,t.y1),std::max(t.x0,t.x1),std::max(t.y0,t.y1));
    else ImGui::TextDisabled("No region selected");
    ImGui::SetNextItemWidth(118);ImGui::InputInt("Height px",&t.height,8,16);
    ImGui::SetNextItemWidth(118);ImGui::InputInt("Stair rise px",&t.step,1,4);
    ImGui::SetNextItemWidth(118);ImGui::InputInt("Layer",&t.layer,1,1);
    ImGui::TextDisabled("-1 uses each source layer");
    auto material=[&](const char* label,int& value,int pick) {
        ImGui::PushID(pick);ImGui::SetNextItemWidth(76);ImGui::InputInt(label,&value,0,0);
        ImGui::SameLine();if(ImGui::SmallButton("Pick")) t.pick=pick;ImGui::PopID();
    };
    material("Top",t.top,1);material("Sides",t.side,2);material("Underlay",t.underlay,3);
    ImGui::TextDisabled("Art -1 keeps the existing floor");
    const bool selected=t.selected && t.room==a.map_id && a.snap.has_map_identity();
    ImGui::BeginDisabled(!selected || t.selecting || a.pending!=App::Action::None);
    if(ImGui::Button("Set plateau",ImVec2(-1,26))) terrain_edit(a,0);
    if(ImGui::Button("Steps north",ImVec2(116,0))) terrain_edit(a,1);
    ImGui::SameLine();if(ImGui::Button("Steps south",ImVec2(-1,0))) terrain_edit(a,2);
    if(ImGui::Button("Erase terrain",ImVec2(-1,0))) terrain_edit(a,3);
    if(ImGui::CollapsingHeader("Deck over a lower surface")) {
        ImGui::TextWrapped("Choose a separate layer and a height above the lower surface.");
        ImGui::SetNextItemWidth(100);ImGui::InputInt("Thickness px",&t.thickness,1,4);
        if(ImGui::Button("Add deck",ImVec2(-1,0))) terrain_edit(a,4);
    }
    ImGui::EndDisabled();
    if(ImGui::Button("Focus region",ImVec2(-1,0)) && selected) {
        a.camera.tx=(t.x0+t.x1+1)/2.f;a.camera.tz=(t.y0+t.y1+1)/2.f;
        a.camera.ty=t.height/32.f;a.camera.dist=std::max(8.f,float(std::max(std::abs(t.x1-t.x0),std::abs(t.y1-t.y0)))*1.8f+8);
    }
    if(ImGui::Button("Save terrain + models",ImVec2(-1,26))) save_changes(a);
    if(ImGui::CollapsingHeader("Sloping ground")) {
        ImGui::TextWrapped("Height is the high end; Stair rise sets the drop per row. Grass follows the slope.");
        ImGui::BeginDisabled(!selected || t.selecting || a.pending!=App::Action::None);
        if(ImGui::Button("Slope north",ImVec2(116,0))) terrain_edit(a,5);
        ImGui::SameLine();if(ImGui::Button("Slope south",ImVec2(-1,0))) terrain_edit(a,6);
        ImGui::EndDisabled();
    }
    const auto& s=vr::diorama::diorama_stats();
    ImGui::Separator();ImGui::Text("%zu terrain cells",s.terrain_cells);
    if(s.terrain_rejected || s.unresolved_placements) ImGui::TextColored(ImVec4(.75f,.15f,.1f,1),"Review: %zu cells / %zu placements",s.terrain_rejected,s.unresolved_placements);
    ImGui::TextWrapped("Drag in 3D to inspect the sides. Underlay replaces the floor beneath removed object pixels. Undo restores the whole region.");
    ImGui::TextWrapped("Orange: this map. Green: neighbouring terrain, edited in its own map.");
}
void terrain_overlay(const App& a,ImDrawList* dl,ImVec2 origin) {
    const auto resolved=vr::terrain::resolve(a.snap,a.working.terrain);
    dl->AddRect(ImVec2(origin.x+7*16,origin.y+7*16),ImVec2(origin.x+(a.snap.width-8)*16,origin.y+(a.snap.height-7)*16),IM_COL32(255,180,65,230),0,0,2);
    for(int y=0;y<a.snap.height;++y) for(int x=0;x<a.snap.width;++x) if(resolved.cell(x,y) || resolved.mismatched[size_t(y)*a.snap.width+x]) {
        const bool bad=resolved.mismatched[size_t(y)*a.snap.width+x];
        const auto color=bad?IM_COL32(255,80,60,230):vr::terrain::primary_cell(a.snap,x,y)?IM_COL32(80,205,230,190):IM_COL32(140,230,150,190);
        dl->AddRect(ImVec2(origin.x+x*16,origin.y+y*16),ImVec2(origin.x+(x+1)*16,origin.y+(y+1)*16),color);
    }
    const auto& t=a.terrain;if(!t.selected || t.room!=a.map_id) return;
    const ImVec2 lo(origin.x+std::min(t.x0,t.x1)*16,origin.y+std::min(t.y0,t.y1)*16);
    const ImVec2 hi(origin.x+(std::max(t.x0,t.x1)+1)*16,origin.y+(std::max(t.y0,t.y1)+1)*16);
    dl->AddRectFilled(lo,hi,IM_COL32(80,190,245,55));dl->AddRect(lo,hi,IM_COL32(110,220,255,255),0,0,2);
}
