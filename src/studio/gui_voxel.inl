// Manual source-pixel workflow. Included after the ordinary document actions.
namespace ve=studio::voxel_edit;

bool voxel_commit(App& a,const studio::EditorState& before,const char* message) {
    if(!vr::overrides::valid_parts(a.draft)) {
        a.document.state=before;remesh(a);
        a.status="Edit refused: keep the model within its size limit and use object pixels for solid surface art.";
        return false;
    }
    if(a.draft.id.empty()) a.draft.id=a.document.new_id();
    a.document.record(before);remesh(a);a.status=message;return true;
}
std::string voxel_part_id(App& a) {
    std::string id;
    do {id="part-"+a.document.new_id();}
    while(std::any_of(a.draft.parts.begin(),a.draft.parts.end(),[&](const auto& part){return part.id==id;}));
    return id;
}
void voxel_start(App& a) {
    if(!a.has_draft || a.mask_art.pixels.empty()) return;
    const auto before=a.document.state;
    ve::ownership_from_mask(a.draft,a.mask_art);
    a.draft.parts.clear();a.draft.model_seeded=true;
    vr::overrides::Part part;std::string error;
    if(!ve::make_part(a.draft,a.mask_art,vr::overrides::PartKind::Billboard,{0,0,a.mask_art.w,a.mask_art.h},voxel_part_id(a),&part,&error)) {
        a.document.state=before;a.status=error;return;
    }
    part.name="Source relief";
    a.document.state.selected_part=part.id;a.draft.parts.push_back(std::move(part));
    if(voxel_commit(a,before,"Voxel model started from the mask. Split a region to give it separate depth. Undo restores the previous model.")) {
        a.pixels={};switch_mode(a,2);focus_selection(a);
    }
}
void voxel_add(App& a,vr::overrides::PartKind kind) {
    if(a.draft.parts.size()>=64) {a.status="This model already has 64 parts.";return;}
    const auto before=a.document.state;
    vr::overrides::Part part;std::string error;
    if(!ve::make_part(a.draft,a.mask_art,kind,a.pixels.selection,voxel_part_id(a),&part,&error)) {a.status=error;return;}
    a.document.state.selected_part=part.id;a.draft.parts.push_back(std::move(part));a.draft.model_seeded=true;
    voxel_commit(a,before,"Part added from the selected source rectangle. Edit its depth and position below.");
}
void voxel_split(App& a) {
    const auto before=a.document.state;std::string error;const auto id=voxel_part_id(a);
    if(!ve::split(a.draft,a.mask_art,a.document.state.selected_part,a.pixels.selection,id,&error)) {a.status=error;return;}
    a.document.state.selected_part=id;a.pixels.tool=0;
    voxel_commit(a,before,"Region split into a separate relief. Front -1 recesses it by one pixel; +1 raises it.");
}
void voxel_surface(App& a,bool all) {
    auto* part=selected_part(a);
    if(!part) return;
    if(!ve::material_region(a.draft,a.mask_art,a.pixels.selection)) {
        a.status="Choose an opaque, object-only rectangle. Ground and shadow pixels cannot texture a solid.";return;
    }
    const auto before=a.document.state;
    if(part->kind==vr::overrides::PartKind::Billboard) part->side_art=vr::overrides::Surface{a.pixels.selection};
    else if(all) for(auto& surface:part->surfaces) surface.region=a.pixels.selection;
    else part->surfaces[size_t(a.pixels.face)].region=a.pixels.selection;
    voxel_commit(a,before,"Source art assigned at one pixel per voxel; it repeats instead of stretching.");
}
void voxel_cycle(App& a,int step) {
    const int n=int(a.draft.parts.size());if(!n) return;
    auto* current=selected_part(a);const int at=current?int(current-a.draft.parts.data()):(step>0?-1:0);
    a.document.state.selected_part=a.draft.parts[size_t((at+step+n)%n)].id;
    a.reveal_selected_part=true;
}
void voxel_reorder(App& a,int direction) {
    auto* part=selected_part(a);if(!part) return;
    const int at=int(part-a.draft.parts.data()),next=at+direction;
    if(next<0 || next>=int(a.draft.parts.size())) return;
    const auto before=a.document.state;std::swap(a.draft.parts[size_t(at)],a.draft.parts[size_t(next)]);
    voxel_commit(a,before,"Part order changed. Earlier parts own the surface where solids overlap.");
}
// Numeric edits commit once on deactivation, just like a drag stroke.
bool voxel_field(App& a,const studio::EditorState& before) {
    if(ImGui::IsItemActivated()) a.field_before=before;
    if(!ImGui::IsItemDeactivatedAfterEdit()) return false;
    voxel_commit(a,a.field_before,"Part updated.");return true;
}
void voxel_tools(App& a) {
    auto& e=a.pixels;
    ImGui::TextUnformatted("SOURCE TOOL");ImGui::SetNextItemWidth(-1);
    ImGui::Combo("##pixel-tool",&e.tool,"Select rectangle\0Object pixels\0Ground pixels\0Shadow pixels\0Erase part pixels\0Restore part pixels\0");
    ImGui::BeginDisabled(e.tool==0);
    ImGui::SetNextItemWidth(-1);
    ImGui::Combo("##pixel-shape",&e.shape,"Brush\0Filled rectangle\0Flood connected color\0");
    if(e.shape==0) {
        int brush=e.brush==1?0:e.brush==3?1:2;
        ImGui::SetNextItemWidth(-1);
        if(ImGui::Combo("##pixel-brush",&brush,"1 pixel\0 3 pixels\0 5 pixels\0")) e.brush=brush==0?1:brush==1?3:5;
    }
    ImGui::EndDisabled();
    if(e.tool==0) ImGui::TextWrapped("Drag a source rectangle. Ctrl+Enter splits a relief or assigns a solid face. The buttons do the same.");
    else if(e.tool>=4) ImGui::TextWrapped("Edits only the selected relief. Other parts keep their pixels. E erases; R restores.");
    else ImGui::TextWrapped("Object becomes geometry. Ground stays on the floor. Shadow is replaced with recovered ground.");
    ImGui::Checkbox("Show mask colors",&e.overlay);
}

void voxel_properties(App& a) {
    auto* part=selected_part(a);if(!part) {ImGui::TextWrapped("Select a part, or select source pixels and add one.");return;}
    ImGui::PushID(part->id.c_str());
    char name[257];std::snprintf(name,sizeof(name),"%s",part->name.c_str());
    auto before=a.document.state;ImGui::SetNextItemWidth(-1);
    if(ImGui::InputText("##voxel-part-name",name,sizeof(name))) part->name=name;
    if(voxel_field(a,before)) {ImGui::PopID();return;}
    const bool relief=part->kind==vr::overrides::PartKind::Billboard;
    ImGui::TextUnformatted("Position / X Y Z (pixels)");
    float pos[]={part->transform.position.x*16,part->transform.position.y*16,part->transform.position.z*16};
    before=a.document.state;ImGui::SetNextItemWidth(-1);
    if(ImGui::InputFloat3("##voxel-position",pos,"%.1f")) {
        for(auto& n:pos) n=std::isfinite(n)?std::clamp(n,-2048.f,2048.f):0;
        part->transform.position={pos[0]/16,pos[1]/16,pos[2]/16};
    }
    if(voxel_field(a,before)) {ImGui::PopID();return;}
    if(relief) {
        ImGui::Text("Width %d / height %d px",int(part->transform.size.x*16),int(part->transform.size.y*16));
        ImGui::TextDisabled("Source pixels set width / height");
        int depth=int(std::lround(part->transform.size.z*16));
        before=a.document.state;ImGui::SetNextItemWidth(105);
        if(ImGui::InputInt("Depth (px)",&depth)) ve::set_depth(*part,depth);
        if(voxel_field(a,before)) {ImGui::PopID();return;}
        float front=ve::front_pixels(*part);
        before=a.document.state;ImGui::SetNextItemWidth(105);
        if(ImGui::InputFloat("Front (px)",&front,1,4,"%.1f") && std::isfinite(front))
            ve::set_front(*part,std::clamp(front,-2048.f,2048.f));
        if(voxel_field(a,before)) {ImGui::PopID();return;}
        ImGui::TextDisabled("Front + raises / - recesses");
        if(ImGui::Button("SPLIT SELECTION",ImVec2(-1,0))) {voxel_split(a);ImGui::PopID();return;}
        if(ImGui::Button("Erase part pixels")) a.pixels.tool=4;
        ImGui::SameLine();if(ImGui::Button("Restore")) a.pixels.tool=5;
    } else {
        ImGui::TextUnformatted("Resize / Width Height Depth (px)");
        float size[]={part->transform.size.x*16,part->transform.size.y*16,part->transform.size.z*16};
        before=a.document.state;ImGui::SetNextItemWidth(-1);
        if(ImGui::InputFloat3("##voxel-size",size,"%.0f")) {
            for(auto& n:size) n=std::isfinite(n)?std::clamp(std::round(n),1.f,1024.f):1;
            part->transform.size={size[0]/16,size[1]/16,size[2]/16};
        }
        if(voxel_field(a,before)) {ImGui::PopID();return;}
        if(part->kind==vr::overrides::PartKind::Wedge) {
            int axis=part->wedge_axis==0?0:1;before=a.document.state;
            if(ImGui::Combo("Slope axis",&axis,"X\0Z\0")) {
                part->wedge_axis=axis==0?0:2;voxel_commit(a,before,"Roof slope axis updated.");ImGui::PopID();return;
            }
            bool positive=part->wedge_direction>0;before=a.document.state;
            if(ImGui::Checkbox("High edge at +axis",&positive)) {
                part->wedge_direction=positive?1:-1;voxel_commit(a,before,"Roof direction updated.");ImGui::PopID();return;
            }
        }
        ImGui::Separator();ImGui::TextUnformatted("FACE ART");
        ImGui::SetNextItemWidth(-1);
        ImGui::Combo("##voxel-face",&a.pixels.face,"Front (+Z)\0Back (-Z)\0Right (+X)\0Left (-X)\0Top / roof\0Bottom\0");
        const auto& r=part->surfaces[size_t(a.pixels.face)].region;
        ImGui::Text("Source %d,%d / %d x %d px",r[0],r[1],r[2],r[3]);
        if(ImGui::Button("USE SELECTION")) {voxel_surface(a,false);ImGui::PopID();return;}
        ImGui::SameLine();if(ImGui::Button("All faces")) {voxel_surface(a,true);ImGui::PopID();return;}
        before=a.document.state;
        bool changed=ImGui::Checkbox("Flip U",&part->surfaces[size_t(a.pixels.face)].flip_u);
        ImGui::SameLine();changed|=ImGui::Checkbox("Flip V",&part->surfaces[size_t(a.pixels.face)].flip_v);
        if(changed) {voxel_commit(a,before,"Face art flipped.");ImGui::PopID();return;}
        ImGui::TextUnformatted("Artwork offset / U V (pixels)");
        before=a.document.state;ImGui::SetNextItemWidth(-1);
        if(ImGui::InputInt2("##artwork-offset",part->surfaces[size_t(a.pixels.face)].offset.data()))
            for(auto& n:part->surfaces[size_t(a.pixels.face)].offset) n=std::clamp(n,-1024,1024);
        if(voxel_field(a,before)) {ImGui::PopID();return;}
    }
    if(ImGui::CollapsingHeader("Rotation (degrees)")) {
        float angle[]={part->transform.angles.x,part->transform.angles.y,part->transform.angles.z};
        before=a.document.state;ImGui::SetNextItemWidth(-1);
        if(ImGui::InputFloat3("##voxel-angles",angle,"%.0f")) {
            for(auto& n:angle) n=std::isfinite(n)?std::clamp(n,-360.f,360.f):0;
            part->transform.angles={angle[0],angle[1],angle[2]};
        }
        if(voxel_field(a,before)) {ImGui::PopID();return;}
        ImGui::TextWrapped("Front moves along the relief's depth axis. Depth grows behind its front face, including after rotation.");
    }
    if(relief && ImGui::CollapsingHeader("Edge artwork")) {
        ImGui::TextWrapped("Repeat selected artwork on the sides, top and bottom. Front and back keep the source drawing.");
        if(part->side_art) {
            const auto& r=part->side_art->region;
            ImGui::Text("Source %d,%d / %d x %d px",r[0],r[1],r[2],r[3]);
        }
        if(ImGui::Button("USE EDGE SELECTION")) {voxel_surface(a,false);ImGui::PopID();return;}
        if(part->side_art && ImGui::Button("Reset edge artwork")) {
            const auto prior=a.document.state;part->side_art.reset();
            voxel_commit(a,prior,"Relief edges use their original pixel colors again.");ImGui::PopID();return;
        }
    }
    ImGui::PopID();
}

void voxel_inspector(App& a) {
    ImGui::TextWrapped("%s",a.draft.name.c_str());
    ImGui::TextDisabled("%d x %d source pixels",a.draft.w*16,a.draft.extent*16);
    const float height=std::max(100.f,ImGui::GetContentRegionAvail().y-66);
    ImGui::BeginChild("voxel-controls",ImVec2(0,height),false);
    if(a.mode==1) voxel_tools(a);
    else {
        ImGui::TextUnformatted("PARTS");ImGui::SameLine();ImGui::TextDisabled("Click model or list");
        ImGui::BeginChild("voxel-parts",ImVec2(0,105),true);
        for(const auto& part:a.draft.parts) {
            const std::string label=(part.name.empty()?part.id:part.name)+"##"+part.id;
            if(ImGui::Selectable(label.c_str(),part.id==a.document.state.selected_part)) a.document.state.selected_part=part.id;
            if(a.reveal_selected_part && part.id==a.document.state.selected_part) ImGui::SetScrollHereY();
        }
        a.reveal_selected_part=false;
        ImGui::EndChild();
        if(ImGui::Button("+ Relief")) {voxel_add(a,vr::overrides::PartKind::Billboard);}
        ImGui::SameLine();if(ImGui::Button("+ Box")) {voxel_add(a,vr::overrides::PartKind::Box);}
        ImGui::SameLine();if(ImGui::Button("+ Roof")) {voxel_add(a,vr::overrides::PartKind::Wedge);}
        auto* part=selected_part(a);
        ImGui::BeginDisabled(!part);
        if(ImGui::Button("Duplicate")) {add_part(a,part->kind,true);}
        ImGui::SameLine();if(ImGui::Button("Delete")) delete_part(a);
        ImGui::SameLine();if(ImGui::Button("Up")) voxel_reorder(a,-1);
        ImGui::SameLine();if(ImGui::Button("Down")) voxel_reorder(a,1);
        ImGui::EndDisabled();
        if(ImGui::IsItemHovered()) ImGui::SetTooltip("Earlier parts own overlapping surfaces. Up / Down changes that order.");
        ImGui::Separator();voxel_properties(a);
        ImGui::Separator();voxel_tools(a);
    }
    ImGui::EndChild();
    ImGui::Separator();
    if(ImGui::Button("SAVE MODEL",ImVec2(-1,0))) {if(apply_draft(a)) do_save(a);}
    if(ImGui::IsItemHovered()) ImGui::SetTooltip("Save this model and the rest of your working copy. Reused placements update together.");
    if(ImGui::Button("Apply")) apply_draft(a);
    ImGui::SameLine();if(ImGui::Button("Revert")) revert_draft(a);
    ImGui::SameLine();ImGui::TextDisabled(a.document.draft_dirty()?"Draft edited":"Applied");
}

void voxel_source_canvas(App& a,studio::Rect area) {
    auto& e=a.pixels;
    if(e.group_id!=a.draft.id) {e.group_id=a.draft.id;e.selection={};e.pan={};e.zoom=1;e.start_x=-1;}
    ImGui::SetCursorScreenPos(ImVec2(area.x,area.y));
    ImGui::PushStyleColor(ImGuiCol_FrameBg,ImVec4(.23f,.27f,.32f,1));
    ImGui::PushStyleColor(ImGuiCol_PopupBg,ImVec4(.13f,.16f,.20f,1));
    ImGui::PushStyleColor(ImGuiCol_Button,ImVec4(.23f,.27f,.32f,1));
    ImGui::SetNextItemWidth(180);
    ImGui::Combo("##canvas-tool",&e.tool,"Select rectangle\0Paint object\0Paint ground\0Paint shadow\0Erase selected relief\0Restore selected relief\0");
    ImGui::SameLine();ImGui::SetNextItemWidth(140);ImGui::BeginDisabled(e.tool==0);
    ImGui::Combo("##canvas-shape",&e.shape,"Brush\0Filled rectangle\0Flood color\0");ImGui::EndDisabled();
    ImGui::SameLine();if(ImGui::SmallButton("Fit")) {e.zoom=1;e.pan={};}
    ImGui::SameLine();if(ImGui::SmallButton("-##source-zoom")) e.zoom=std::max(1.f,e.zoom/1.5f);
    ImGui::SameLine();if(ImGui::SmallButton("+##source-zoom")) e.zoom=std::min(16.f,e.zoom*1.5f);
    ImGui::PopStyleColor(3);
    ImGui::SameLine();ImGui::TextDisabled("wheel zoom / middle pan");
    area.y+=25;area.h-=25;e.clip=area;
    const ImVec2 origin(area.x,area.y),end(area.x+area.w,area.y+area.h);
    ImGui::SetCursorScreenPos(origin);
    ImGui::InvisibleButton("voxel-source",ImVec2(area.w,area.h),ImGuiButtonFlags_MouseButtonLeft|ImGuiButtonFlags_MouseButtonMiddle);
    a.mask_hover=ImGui::IsItemHovered();
    const auto& io=ImGui::GetIO();
    if(a.mask_hover && !a.mask_stroke) {
        if(io.MouseWheel) e.zoom=std::clamp(e.zoom*std::pow(1.25f,io.MouseWheel),1.f,16.f);
        if(ImGui::IsMouseDragging(ImGuiMouseButton_Middle)) {e.pan.x+=io.MouseDelta.x;e.pan.y+=io.MouseDelta.y;}
    }
    if(a.mask_art.w<=0 || a.mask_art.h<=0) return;
    const float scale=std::min((area.w-20)/a.mask_art.w,(area.h-20)/a.mask_art.h)*e.zoom;
    const float width=a.mask_art.w*scale,height=a.mask_art.h*scale;
    e.pan.x=std::clamp(e.pan.x,-std::max(width,area.w)/2,std::max(width,area.w)/2);
    e.pan.y=std::clamp(e.pan.y,-std::max(height,area.h)/2,std::max(height,area.h)/2);
    const ImVec2 pos(area.x+(area.w-width)/2+e.pan.x,area.y+(area.h-height)/2+e.pan.y);
    a.mask_canvas={pos.x,pos.y,width,height};
    if(a.mask_image_dirty && !a.mask_image.rgba.empty() && a.mask_image.upload()) a.mask_image_dirty=false;
    auto* dl=ImGui::GetWindowDrawList();dl->AddRectFilled(origin,end,IM_COL32(26,29,33,255));
    dl->PushClipRect(origin,end,true);
    for(int y=0;y<int(area.h);y+=12) for(int x=0;x<int(area.w);x+=12)
        dl->AddRectFilled(ImVec2(area.x+x,area.y+y),ImVec2(area.x+x+12,area.y+y+12),((x/12+y/12)&1)?IM_COL32(48,51,57,255):IM_COL32(57,60,66,255));
    dl->AddImage(ImTextureID(a.mask_image.texture),pos,ImVec2(pos.x+width,pos.y+height));
    const auto* part=selected_part(a);const bool local=e.tool>=4 && part && part->kind==vr::overrides::PartKind::Billboard;
    const auto local_region=local?ve::region(a.draft,*part):vr::overrides::ArtRegion{};
    for(int y=0;y<a.mask_art.h;++y) for(int x=0;x<a.mask_art.w;++x) {
        const size_t at=size_t(y)*a.mask_art.w+x;ImU32 tint=0;
        if(local) {
            bool keep=ve::inside(local_region,x,y) && a.draft.cutout->opacity[at];
            if(keep && part->local_mask) keep=part->local_mask->opacity[size_t(y-local_region[1])*local_region[2]+x-local_region[0]];
            if(!keep) tint=IM_COL32(20,23,30,205);
        } else if(e.overlay) {
            if(a.draft.voxel->ground.opacity[at]) tint=IM_COL32(65,225,145,105);
            else if(a.draft.voxel->shadow.opacity[at]) tint=IM_COL32(190,110,240,125);
            else if(e.tool>=1 && e.tool<=3 && a.draft.cutout->opacity[at]) tint=IM_COL32(70,150,250,60);
        }
        if(tint) dl->AddRectFilled(ImVec2(pos.x+x*scale,pos.y+y*scale),ImVec2(pos.x+(x+1)*scale,pos.y+(y+1)*scale),tint);
    }
    auto outline=[&](const vr::overrides::ArtRegion& r,ImU32 color) {
        if(r[2]>0 && r[3]>0) dl->AddRect(ImVec2(pos.x+r[0]*scale,pos.y+r[1]*scale),ImVec2(pos.x+(r[0]+r[2])*scale,pos.y+(r[1]+r[3])*scale),color,0,0,2);
    };
    if(part) outline(part->kind==vr::overrides::PartKind::Billboard?ve::region(a.draft,*part):part->surfaces[size_t(e.face)].region,IM_COL32(85,210,245,255));
    outline(e.selection,IM_COL32(255,215,70,255));
    int px=0,py=0;
    if(a.mask_hover && canvas_pixel(a,io.MousePos.x,io.MousePos.y,&px,&py)) {
        const int radius=e.tool && e.shape==0?e.brush/2:0;
        outline({px-radius,py-radius,1+2*radius,1+2*radius},IM_COL32(255,255,255,255));
    }
    dl->PopClipRect();dl->AddRect(origin,end,IM_COL32(125,130,140,255));
}

void voxel_preset(App& a,int i) {
    stop_fly_look(a);
    const float yaws[]={.6f,0,3.14159265f,1.57079633f,-1.57079633f,0};
    a.camera.yaw=yaws[i];a.camera.pitch=i==0?.55f:i==5?1.57079633f:0;
    a.camera.orthographic=true;
}
void voxel_view_controls(App& a) {
    ImGui::SameLine();ImGui::SetNextItemWidth(90);
    ImGui::PushStyleColor(ImGuiCol_Text,ImVec4(.1f,.12f,.14f,1));
    ImGui::PushStyleColor(ImGuiCol_FrameBg,ImVec4(.85f,.85f,.85f,1));
    ImGui::PushStyleColor(ImGuiCol_PopupBg,ImVec4(.95f,.95f,.95f,1));
    if(ImGui::BeginCombo("##model-view","View")) {
        ImGui::BeginDisabled(!selected_part(a));
        if(ImGui::Selectable("Focus selected part (Shift+F)")) focus_part(a);
        ImGui::EndDisabled();ImGui::Separator();
        const char* labels[]={"Isometric (0)","Front (1)","Back","Right (3)","Left","Top (7)"};
        for(int i=0;i<6;++i) if(ImGui::Selectable(labels[i])) voxel_preset(a,i);
        ImGui::EndCombo();
    }
    ImGui::PopStyleColor(3);
    ImGui::SameLine();ImGui::Checkbox("Ortho",&a.camera.orthographic);
    ImGui::SameLine();ImGui::Checkbox("Neutral",&a.neutral);
    ImGui::SameLine();ImGui::TextDisabled("1 px snap");
}

int voxel_ui_selftest(App& a,const char* path) {
    const auto saved_doc=a.document;const auto saved_camera=a.camera;
    const auto saved_mode=a.mode;const auto saved_out=a.out_path;
    int count=0;
    auto check=[&](bool ok,const char* message) {if(ok) ++count;else std::fprintf(stderr,"[voxel-ui-selftest] FAIL %s\n",message);return ok;};
    a.document={};a.mode=0;remesh(a);select_cell(a,21,0);initialize_cutout(a);
    voxel_start(a);
    if(!check(a.draft.voxel && a.draft.parts.size()==1 && a.mode==2 && vr::overrides::valid_parts(a.draft),"start from cutout creates a native relief") ||
       !check(a.working.version==5 && effective(a).version==6,"preview upgrades its version without rewriting the applied document")) return -1;
    const auto initial=a.document.state;const auto initial_hash=a.preview_hash;
    auto rotated=a.draft;rotated.parts[0].transform.position={1,2,3};rotated.parts[0].transform.angles={0,90,0};
    auto& rotated_part=rotated.parts[0];const float front=ve::front_pixels(rotated_part);
    ve::set_depth(rotated_part,4);
    if(!check(std::abs(ve::front_pixels(rotated_part)-front)<1e-4f && std::abs(rotated_part.transform.position.x-.9375f)<1e-4f &&
              std::abs(rotated_part.transform.position.z-3)<1e-4f,"rotated depth grows behind a fixed front face")) return -1;
    ve::set_front(rotated_part,front+1);
    if(!check(std::abs(rotated_part.transform.position.x-1)<1e-4f && std::abs(rotated_part.transform.position.z-3)<1e-4f,
              "rotated front nudge follows local depth instead of world Z")) return -1;
    std::string split_error;
    if(!check(ve::split(rotated,a.mask_art,rotated_part.id,{4,5,3,4},"rotated-split",&split_error) &&
              std::abs(rotated.parts.back().transform.position.x-1)<1e-4f &&
              std::abs(rotated.parts.back().transform.position.y-5.4375f)<1e-4f &&
              std::abs(rotated.parts.back().transform.position.z-2.75f)<1e-4f,"split preserves a rotated crop's independent world coordinates")) return -1;
    a.pixels.tool=2;a.pixels.shape=1;a.mask_stroke=true;a.stroke_before=a.document.state;a.pixels.start_x=-1;
    stroke_to(a,0,0);stroke_to(a,2,2);end_stroke(a,false);
    bool partition=true;
    for(size_t i=0;i<a.draft.cutout->opacity.size();++i)
        partition &= a.draft.cutout->opacity[i]+a.draft.voxel->ground.opacity[i]+a.draft.voxel->shadow.opacity[i]==1;
    if(!check(partition && a.draft.voxel->ground.opacity[0] && !a.draft.cutout->opacity[0],"rectangle role paint partitions source ownership")) return -1;
    const auto painted=a.document.state;history(a,false);
    if(!check(a.document.state==initial && a.preview_hash==initial_hash,"undo restores exact roles and mesh")) return -1;
    history(a,true);if(!check(a.document.state==painted,"redo restores the entire role edit")) return -1;
    a.pixels.tool=3;a.pixels.shape=2;a.mask_stroke=true;a.stroke_before=a.document.state;a.pixels.start_x=-1;
    stroke_to(a,0,0);end_stroke(a,false);
    if(!check(a.draft.voxel->shadow.opacity[0] && !a.draft.voxel->ground.opacity[0] && vr::overrides::valid_parts(a.draft),"connected color flood transfers ownership without overlap")) return -1;
    history(a,false);
    // Pick a real, opaque object source pixel; no color-derived shape assumptions.
    int material=-1;
    for(int i=0;i<int(a.mask_art.pixels.size());++i) if(ve::object_pixel(a.draft,a.mask_art,i%a.mask_art.w,i/a.mask_art.w)) {material=i;break;}
    if(material<0) return -1;
    const int mx=material%a.mask_art.w,my=material/a.mask_art.w;
    a.pixels.selection={mx,my,1,1};voxel_add(a,vr::overrides::PartKind::Box);
    if(!check(a.draft.parts.size()==2 && a.draft.parts.back().surfaces.size()==6,"add solid assigns six native source faces")) return -1;
    const auto solid=a.document.state;const size_t undo=a.document.undo_count();
    a.pixels.tool=2;a.pixels.shape=0;a.mask_stroke=true;a.stroke_before=a.document.state;a.stroke_x=-1;a.pixels.start_x=-1;
    stroke_to(a,mx,my);end_stroke(a,false);
    if(!check(a.document.state==solid && a.document.undo_count()==undo && a.mode==2,"painting away a referenced material rolls back without a phantom undo or mode change")) return -1;
    delete_part(a);a.document.state.selected_part=a.draft.parts.front().id;
    a.pixels.selection={mx,my,1,1};const auto before_split=a.document.state;
    voxel_split(a);
    const auto& remainder=a.draft.parts.front();const auto& separated=a.draft.parts.back();
    if(!check(a.draft.parts.size()==2 && !remainder.local_mask->opacity[size_t(material)] && separated.local_mask->opacity==std::vector<uint8_t>{1},
              "split transfers selected texels out of the parent relief")) return -1;
    const auto moved_before=a.document.state;const auto split_hash=a.preview_hash;
    selected_part(a)->transform.position.z+=1.f/16;voxel_commit(a,moved_before,"test nudge");
    if(!check(a.preview_hash!=split_hash && a.mode==2,"one-pixel relief nudge rebuilds visible depth")) return -1;
    history(a,false);history(a,false);
    if(!check(a.document.state==before_split,"split and depth can both be undone exactly")) return -1;
    a.out_path=std::string(path)+".manual-voxel.json";
    if(!check(apply_draft(a) && a.working.version==6 && do_save(a),"apply and save explicitly persist v6")) return -1;
    OverrideSet loaded;
    if(!check(vr::overrides::load(a.out_path.c_str(),&loaded) && loaded==a.working,"manual document reopens exactly")) return -1;
    history(a,false);
    if(!check(a.working.version==5 && a.draft.voxel,"undo apply restores the prior document format while retaining the draft")) return -1;
    a.document=saved_doc;a.mode=saved_mode;a.camera=saved_camera;a.out_path=saved_out;a.pixels={};remesh(a);
    std::fprintf(stdout,"[voxel-ui-selftest] PASS checks=%d\n",count);return count;
}
