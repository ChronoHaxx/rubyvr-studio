int voxel_selftest(const vr::world::Snapshot& source,vr::overrides::Pattern p,const char* output) {
    using namespace vr::overrides;using namespace vr::part_geometry;
    using Mesh=std::vector<vr::diorama::AuthoredVertex>;
    int count=0;auto check=[&](bool ok,const char* text) {if(ok) ++count;else std::fprintf(stderr,"[voxel-selftest] FAIL %s\n",text);return ok;};
    p.cutout=Cutout{16,16,std::vector<uint8_t>(256,1)};
    p.voxel=Voxel{16,Cutout{16,16,std::vector<uint8_t>(256,0)},Cutout{16,16,std::vector<uint8_t>(256,0)}};
    Part box;box.id="voxel-box";box.transform.size={1,1,.5f};box.surfaces.assign(6,Surface{{0,0,16,16}});p.parts={box};
    auto emit=[&](const Pattern& pattern,Mesh& mesh){mesh.clear();return vr::diorama::inspect_authored_mesh(source,pattern,&mesh);};
    auto equal=[](const Mesh& a,const Mesh& b) {
        if(a.size()!=b.size()) return false;
        for(size_t i=0;i<a.size();++i) if(a[i].position!=b[i].position || a[i].u!=b[i].u || a[i].v!=b[i].v || a[i].tile!=b[i].tile || a[i].palette!=b[i].palette) return false;
        return true;
    };
    Mesh mesh;
    if(!check(emit(p,mesh) && audit_mesh(mesh).closed_connected() && std::abs(audit_mesh(mesh).volume-.5)<1e-6,"native box is a closed solid with expected volume")) return -1;
    bool orientation=true;
    for(size_t i=0;i<mesh.size();i+=3) {
        const auto c=(mesh[i].position+mesh[i+1].position+mesh[i+2].position)*(1.f/3);
        const auto n=cross(mesh[i+2].position-mesh[i].position,mesh[i+1].position-mesh[i].position);
        float u=c.x,v=1-c.y;
        if(n.z<0) u=1-c.x;
        else if(n.x>0) u=.25f-c.z;
        else if(n.x<0) u=c.z+.25f;
        else if(n.y>0) v=c.z+.25f;
        else if(n.y<0) v=.25f-c.z;
        const int x=std::clamp(int(std::floor(u*16)),0,15),y=std::clamp(int(std::floor(v*16)),0,15);
        const int tile=900+(x/8)+(y/8)*2;
        const int tx=std::clamp(int((mesh[i].u+mesh[i+1].u+mesh[i+2].u)*8/3),0,7);
        const int ty=std::clamp(int((mesh[i].v+mesh[i+1].v+mesh[i+2].v)*8/3),0,7);
        orientation &= mesh[i].tile==tile && tx==x%8 && ty==y%8;
    }
    if(!check(orientation,"all six native-scale faces retain exact asymmetric source addresses after strip merging and conformity") ||
       !check(mesh.size()<6144,"exposed strips cost less than individual exposed voxel faces")) return -1;
    auto changed=source;changed.bg_palette[1]=changed.bg_palette[2];Mesh same;
    if(!check(vr::diorama::inspect_authored_mesh(changed,p,&same) && equal(mesh,same),"equal-looking palette entries do not merge material identities")) return -1;
    changed=source;std::fill_n(changed.vram_tiles.begin()+900*32,32,0x22);
    if(!check(vr::diorama::inspect_authored_mesh(changed,p,&same) && equal(mesh,same),"opaque tile animation keeps source addresses live")) return -1;
    std::fill_n(changed.vram_tiles.begin()+900*32,32,0);
    if(!check(!vr::diorama::inspect_authored_mesh(changed,p,&same),"source opacity change invalidates the cache and refuses missing solid material")) return -1;
    auto moved=p;moved.parts[0].transform.position.x=.125f;
    if(!check(emit(moved,same) && !equal(mesh,same),"authored transform changes invalidate cached geometry")) return -1;
    auto bad=p;bad.cutout->opacity[0]=0;bad.voxel->ground.opacity[0]=1;
    if(!check(!valid_parts(bad),"explicit ground cannot supply any solid surface") ||
       !check(valid_parts(p),"legitimate green object quadrant remains valid")) return -1;
    bad=p;bad.voxel->shadow.opacity[0]=1;
    if(!check(!valid_parts(bad),"overlapping object and shadow ownership rejected")) return -1;
    bad=p;bad.voxel->pixels_per_cell=32;
    if(!check(!valid_parts(bad),"unsupported pixel scale refused without reinterpretation")) return -1;
    bad=p;bad.parts[0].transform.size={64,64,64};
    if(!check(!valid_parts(bad),"oversized local grid refused before allocation")) return -1;
    auto hole=p;auto& slab=hole.parts[0];slab.kind=PartKind::Billboard;slab.surfaces.clear();slab.transform.size.z=.125f;
    slab.local_mask=Cutout{16,16,std::vector<uint8_t>(256,1)};slab.local_mask->opacity[7*16+7]=0;
    if(!check(emit(hole,same) && audit_mesh(same).closed_connected() && std::abs(audit_mesh(same).volume-255*2/4096.)<1e-6,"masked relief preserves a real through opening, including its inner faces")) return -1;
    auto wrapped=hole;wrapped.parts[0].side_art=Surface{{0,0,8,8}};
    Mesh edged;
    if(!check(emit(wrapped,edged) && audit_mesh(edged).closed_connected() && std::abs(audit_mesh(edged).volume-audit_mesh(same).volume)<1e-6,
              "edge artwork changes the material while preserving the masked relief volume and opening")) return -1;
    bool edges_native=true,front_preserved=true;std::set<int> edge_columns;
    for(size_t i=0;i<edged.size();i+=3) {
        const auto n=cross(edged[i+2].position-edged[i].position,edged[i+1].position-edged[i].position);
        const auto c=(edged[i].position+edged[i+1].position+edged[i+2].position)*(1.f/3);
        if(std::abs(n.z)>1e-8) {
            const int x=std::clamp(int(std::floor(c.x*16)),0,15),y=std::clamp(int(std::floor((1-c.y)*16)),0,15);
            front_preserved &= edged[i].tile==900+(x/8)+(y/8)*2;
        } else {
            edges_native &= edged[i].tile==900;
            edge_columns.insert(int((edged[i].u+edged[i+1].u+edged[i+2].u)*8/3));
        }
    }
    if(!check(edges_native && edge_columns.size()>1 && front_preserved,"relief edges use varying native texels from the chosen patch and preserve both source faces")) return -1;
    wrapped.parts[0].side_art->region={8,0,8,8};Mesh alternate;
    if(!check(emit(wrapped,alternate) && !equal(edged,alternate),"changing relief edge artwork invalidates the cached material")) return -1;
    wrapped.parts[0].side_art->offset={1,-2};Mesh shifted;
    if(!check(emit(wrapped,shifted) && !equal(alternate,shifted) && audit_mesh(shifted).closed_connected(),"native artwork offsets shift texels without changing closed geometry")) return -1;
    bad=wrapped;bad.cutout->opacity[8]=0;bad.voxel->ground.opacity[8]=1;
    if(!check(!valid_parts(bad),"ground pixels cannot become relief edge artwork")) return -1;
    bad=p;bad.parts[0].side_art=Surface{{0,0,8,8}};
    if(!check(!valid_parts(bad),"relief-only edge artwork is rejected on solid parts")) return -1;
    {
        // Water dominates this map. Two copies of one sign stand beside two
        // different grass metatiles, with the same palette/index evidence.
        auto field=source;field.width=17;field.height=9;field.grid.assign(17*9,2);
        field.grid[4*17+3]=field.grid[4*17+13]=0;
        field.grid[4*17+5]=1;field.grid[4*17+15]=3;
        std::fill_n(field.vram_tiles.begin()+905*32,32,0x11);
        for(int id=0;id<4;++id) field.attributes[id]=0;
        for(int id=1;id<4;++id) for(int k=0;k<8;++k)
            field.metatiles[id*8+k]=k>=4?904:id==1?900:id==2?901:905;
        Pattern sign;
        if(!check(pattern_io::from_cells(field,{{3,4}},"Ground recovery sign",&sign),"construct mixed-ground sign fixture")) return -1;
        sign.cutout=Cutout{16,16,std::vector<uint8_t>(256,1)};
        sign.voxel=Voxel{16,Cutout{16,16,std::vector<uint8_t>(256,0)},Cutout{16,16,std::vector<uint8_t>(256,0)}};
        sign.cutout->opacity[0]=0;sign.voxel->ground.opacity[0]=1;
        Part face;face.id="sign-face";face.kind=PartKind::Billboard;face.transform.size={1,1,.125f};sign.parts={face};
        OverrideSet scenery;scenery.version=kVoxelVersion;scenery.patterns={sign};
        Mesh placed;vr::diorama::DioramaStats stats;
        auto ground_is=[&](int x,int y,int tile) {
            const size_t start=size_t(y*field.width+x)*48;
            if(placed.size()<start+24) return false;
            for(size_t i=start;i<start+24;++i) if(placed[i].tile!=tile || placed[i].position.y!=0) return false;
            return true;
        };
        if(!check(vr::diorama::inspect_diorama_mesh(field,scenery,&placed,&stats) && stats.ground_tile==2 &&
                  stats.raised_instances==2 && ground_is(3,4,900) && ground_is(13,4,905) && ground_is(3,3,901),
                  "each sign recovers its local grass instead of dominant water, leaving unclaimed water unchanged")) return -1;
        field.bg_palette[1]=field.bg_palette[2];
        if(!check(vr::diorama::inspect_diorama_mesh(field,scenery,&placed,&stats) && ground_is(3,4,900) && ground_is(13,4,905),
                  "local ground selection uses indexed source roles even when grass and water RGB become equal")) return -1;
        scenery.version=kVersion;scenery.patterns[0].voxel.reset();
        if(!check(vr::diorama::inspect_diorama_mesh(field,scenery,&placed,&stats) && ground_is(3,4,901) && ground_is(13,4,901),
                  "v5 retains its established floor recovery semantics")) return -1;
    }
    hole=wrapped;
    auto roof=p;roof.parts[0].kind=PartKind::Wedge;roof.parts[0].wedge_axis=2;roof.parts[0].transform.size={1,.5f,1};
    int voxels=0;for(int z=0;z<16;++z) for(int y=0;y<8;++y) if((2*y+1)*16<(2*z+1)*8) voxels+=16;
    if(!check(emit(roof,same) && audit_mesh(same).closed_connected() && std::abs(audit_mesh(same).volume-voxels/4096.)<1e-6,"wedge volume agrees with independent integer center-sampling count")) return -1;
    bool axes=true;std::set<int> levels;
    for(size_t i=0;i<same.size();i+=3) {
        const auto n=cross(same[i+2].position-same[i].position,same[i+1].position-same[i].position);
        axes&=(std::abs(n.x)>1e-8)+(std::abs(n.y)>1e-8)+(std::abs(n.z)>1e-8)==1;
        if(n.y>0) levels.insert(int(std::lround(same[i].position.y*16)));
    }
    if(!check(axes && levels.size()>=7,"roof contains axis-aligned steps across multiple actual height levels")) return -1;
    OverrideSet set;set.version=kVoxelVersion;set.patterns={hole};const std::string path=std::string(output)+".voxel.json";OverrideSet loaded;
    if(!check(pattern_io::write(path.c_str(),set) && load(path.c_str(),&loaded) && loaded==set,"v6 ownership, relief and scale save and reopen exactly")) return -1;
    set.version=kVersion;
    if(!check(!pattern_io::write(path.c_str(),set) && load(path.c_str(),&loaded) && loaded.version==kVoxelVersion,"v5 cannot encode voxel meaning or replace the previous valid document")) return -1;
    Document doc;doc.state.working=loaded;doc.state.draft=hole;doc.state.has_draft=true;const auto before=doc.state;
    doc.state.draft.parts[0].local_mask->opacity[1]=0;doc.record(before);const auto after=doc.state;
    if(!check(doc.undo() && doc.state==before && doc.redo() && doc.state==after,"voxel masks participate in exact document undo and redo")) return -1;
    std::fprintf(stdout,"[voxel-selftest] PASS checks=%d\n",count);return count;
}
