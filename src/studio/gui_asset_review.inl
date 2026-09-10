// Uses the editor's hidden framebuffer and the production mesher. Images are
// captured evidence, never a substitute geometry path or generated concept art.
int run_asset_review(App& a,const char* directory) {
    namespace fs=std::filesystem;
    fs::create_directories(directory);
    const std::string root=std::string(directory)+"/";
    bool ok=!a.working.empty();
    auto* report=std::fopen((root+"renders.json").c_str(),"wb");if(!report) return 1;
    std::fputs("{\"visual_acceptance\":\"pending\",\"assets\":[",report);
    for(size_t pi=0;pi<a.working.patterns.size();++pi) {
        const auto& p=a.working.patterns[pi];const std::string prefix=root+p.id;
        studio::BuildInfo info;Snapshot snap;
        if(!studio::build_snapshot(a.decomp,p.source.room,&snap,&info)) {ok=false;continue;}
        vr::cutout::Art art;if(!vr::cutout::compose(snap,p,&art)) {ok=false;continue;}
        auto source_png=[&](const char* suffix,const vr::overrides::Cutout* mask) {
            std::vector<uint8_t> rgb(art.pixels.size()*3);
            for(size_t i=0;i<art.pixels.size();++i) {
                const auto c=(!mask || mask->opacity[i])?art.pixels[i].rgba:0xff201d19u;
                rgb[i*3]=c&255;rgb[i*3+1]=(c>>8)&255;rgb[i*3+2]=(c>>16)&255;
            }
            return studio::png::write_rgb((prefix+suffix).c_str(),art.w,art.h,rgb.data());
        };
        ok=source_png("-source.png",nullptr)&&ok;
        if(p.cutout) ok=source_png("-object.png",&*p.cutout)&&ok;
        if(p.voxel) {ok=source_png("-ground.png",&p.voxel->ground)&&ok;ok=source_png("-shadow.png",&p.voxel->shadow)&&ok;}
        std::vector<vr::diorama::AuthoredVertex> mesh;
        vr::diorama::set_build_mode(vr::diorama::BuildMode::Diorama);
        vr::diorama::set_overrides({});vr::diorama::update(snap);glFinish();
        glFinish();const auto start=SDL_GetPerformanceCounter();uint64_t hash=0;int vertices=0;
        const bool built=vr::diorama::build_cutout_preview(snap,p,&hash,&vertices);
        glFinish();const double rebuild_ms=1000.0*(SDL_GetPerformanceCounter()-start)/SDL_GetPerformanceFrequency();
        const bool ground_only=p.ground_only();
        if(!built || !vr::diorama::inspect_authored_mesh(snap,p,&mesh) || (mesh.empty()!=ground_only)) {ok=false;continue;}
        const auto audit=studio::audit_mesh(mesh);ok=(ground_only?mesh.empty():audit.closed_connected())&&ok;
        pg::Vec lo=ground_only?pg::Vec{0,0,0}:mesh.front().position,hi=ground_only?pg::Vec{float(p.w),0,0}:lo;
        for(const auto& v:mesh) {const auto q=v.position;
            lo={std::min(lo.x,q.x),std::min(lo.y,q.y),std::min(lo.z,q.z)};
            hi={std::max(hi.x,q.x),std::max(hi.y,q.y),std::max(hi.z,q.z)};}
        const auto target=(lo+hi)*.5f;
        Probe capture;if(!probe_fbo_create(capture,768,768)) {ok=false;continue;}
        vr::gl::glBindFramebuffer(GL_FRAMEBUFFER,capture.fbo);glViewport(0,0,768,768);
        glDisable(GL_SCISSOR_TEST);glEnable(GL_DEPTH_TEST);glDepthFunc(GL_LESS);
        glClearColor(.07f,.08f,.11f,1);
        double draw_ms=0;
        auto shot=[&](const char* name,float yaw,float pitch,pg::Vec aim,float width,int debug) {
            const float cp=std::cos(pitch);
            const auto view=vr::math::look_at(aim.x+20*std::sin(yaw)*cp,aim.y+20*std::sin(pitch),aim.z+20*std::cos(yaw)*cp,aim.x,aim.y,aim.z);
            auto ortho=vr::math::identity();ortho.m[0]=ortho.m[5]=2/width;ortho.m[10]=-2/100.f;ortho.m[14]=-1;
            const auto vp=vr::math::multiply(ortho,view);
            glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);vr::diorama::draw_raw(vp,vr::math::identity(),debug);glFinish();
            const auto begin=SDL_GetPerformanceCounter();
            for(int i=0;i<16;++i) {glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);vr::diorama::draw_raw(vp,vr::math::identity(),debug);glFinish();}
            draw_ms=1000.0*(SDL_GetPerformanceCounter()-begin)/SDL_GetPerformanceFrequency()/16;
            probe_read_frame(capture);return studio::png::write_rgb((prefix+"-"+name+".png").c_str(),768,768,capture.rgb.data());
        };
        // Width 8 cells => six output pixels per source pixel, fixed across
        // source-scale model views and the retained failing v5 reference.
        if(!ground_only) {
        ok=shot("front",0,0,target,8,0)&&ok;
        ok=shot("back",3.14159265f,0,target,8,0)&&ok;
        ok=shot("right",1.5707963f,.12f,target,8,0)&&ok;
        ok=shot("left",-1.5707963f,.12f,target,8,0)&&ok;
        ok=shot("roof",0,1.5707963f,target,8,0)&&ok;
        ok=shot("oblique",.55f,.7f,target,8,0)&&ok;
        ok=shot("low-eave",.65f,-.12f,target,8,0)&&ok;
        ok=shot("neutral",.75f,.42f,target,6,2)&&ok;
        ok=shot("right-neutral",1.5707963f,.12f,target,8,2)&&ok;
        // A complete turn exposes asymmetric front flanges and roof/body
        // alignment that the source-facing textured view can conceal.
        // Fit the entire bounding diagonal so long/tall buildings stay in
        // frame throughout the turn. The source-scale views above stay fixed.
        const auto span=hi-lo;
        const float shape_width=std::max(8.f,1.1f*std::sqrt(span.x*span.x+span.y*span.y+span.z*span.z));
        for(int view=0;view<8;++view) {
            const std::string label="shape-"+std::to_string(view*45);
            ok=shot(label.c_str(),view*.7853981634f,.38f,target,shape_width,2)&&ok;
        }
        ok=shot("detail",.55f,.25f,{p.w*.45f,.65f,0},3,2)&&ok;
        }
        const double isolated_draw_ms=draw_ms;
        uint64_t reopened_hash=0;int reopened_vertices=0;
        OverrideSet one;one.version=a.working.version;one.patterns.push_back(p);
        const std::string saved=prefix+"-saved.json";OverrideSet reloaded;
        const bool resaved=studio::pattern_io::write(saved.c_str(),one) && vr::overrides::load(saved.c_str(),&reloaded) && reloaded==one &&
            vr::diorama::build_cutout_preview(snap,reloaded.patterns[0],&reopened_hash,&reopened_vertices) && reopened_hash==hash && reopened_vertices==vertices;
        ok=resaved&&ok;
        vr::diorama::set_build_mode(vr::diorama::BuildMode::Diorama);vr::diorama::set_overrides(one);
        glFinish();const auto room_start=SDL_GetPerformanceCounter();vr::diorama::update(snap);glFinish();
        const double room_rebuild_ms=1000.0*(SDL_GetPerformanceCounter()-room_start)/SDL_GetPerformanceFrequency();
        const auto stats=vr::diorama::diorama_stats();
        const pg::Vec placed=target+pg::Vec{float(p.source.x),.002f,float(p.source.y+p.extent)-.5f};
        ok=shot("placed",.55f,.7f,placed,9,0)&&ok;
        ok=shot("ground-contact",.25f,.04f,placed+pg::Vec{0,-.7f,0},8,0)&&ok;
        ok=shot("room",0,1.5707963f,{snap.width*.5f,0,snap.height*.5f},float(std::max(snap.width,snap.height)),0)&&ok;
        probe_fbo_destroy(capture);
        if(pi) std::fputc(',',report);
        std::fprintf(report,"{\"id\":\"%s\",\"vertices\":%d,\"triangles\":%zu,\"closed_connected\":%s,\"exact_resave\":%s,\"geometry_hash\":\"%016llx\",\"first_preview_ms\":%.3f,\"detail_draw_768_ms\":%.3f,\"room_rebuild_upload_ms\":%.3f,\"room_draw_768_ms\":%.3f,\"room_vertices\":%zu,\"placements\":%zu,\"ground_only\":%s,\"bounds_min\":[%.6f,%.6f,%.6f],\"bounds_max\":[%.6f,%.6f,%.6f]}",
            p.id.c_str(),vertices,audit.triangles,audit.closed_connected()?"true":"false",resaved?"true":"false",(unsigned long long)hash,
            rebuild_ms,isolated_draw_ms,room_rebuild_ms,draw_ms,stats.flat_vertices+stats.authored_vertices,stats.raised_instances,ground_only?"true":"false",
            lo.x,lo.y,lo.z,hi.x,hi.y,hi.z);
        std::fprintf(stdout,"[asset-review] %s vertices=%d triangles=%zu closed=%d resave=%d preview_ms=%.3f\n",p.id.c_str(),vertices,audit.triangles,int(audit.closed_connected()),int(resaved),rebuild_ms);
    }
    std::fprintf(report,"],\"checks_passed\":%s,\"headset_tested\":false}\n",ok?"true":"false");std::fclose(report);
    return ok?0:1;
}
