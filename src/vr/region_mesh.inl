// Region ownership and buffers reuse build_authored_diorama and submit.
// Full source snapshots remain intact for structural matching/ground recovery.
struct RegionCPU {
    std::vector<Vertex> mesh; PlacedMesh placed; DioramaStats stats;
    int x=0,z=0;float lo[3]{},hi[3]{};
    uint64_t hash=0;
};
struct RegionGPU {
    GLuint vao=0,vbo=0,offsets=0,tiles=0,palette=0; GLsizei count=0;
    std::vector<MeshDraw> draws;int x=0,z=0;
    float lo[3]{},hi[3]{};
    uint64_t hash=0;size_t stored=0,instances=0;
    int group=-1,number=-1;
    std::vector<uint8_t> source_tiles;
    std::vector<uint16_t> source_palette;
};
std::vector<RegionGPU> g_region;
RegionStats g_region_stats;

bool region_cpu(const std::vector<RegionMap>& maps,const overrides::OverrideSet& set,
                std::vector<RegionCPU>* out,RegionStats* stats,std::string* error,
                const std::atomic<bool>* cancel=nullptr) {
    auto fail=[&](const char* why){if(error)*error=why;return false;};
    if(!out || !stats || maps.empty() || maps.size()>9) return fail("Choose an area of one to nine maps.");
    size_t cells=0;
    std::vector<terrain::Resolved> land;
    for(size_t i=0;i<maps.size();++i) {
        if(cancel && cancel->load())return fail("Map preparation cancelled.");
        const auto* s=maps[i].source;
        if(!s || !s->valid || !s->has_map_identity() || !s->valid_connections() ||
           s->width<=15 || s->height<=14 || s->width>512 || s->height>512 ||
           s->grid.size()!=size_t(s->width)*s->height || s->metatiles.size()<8192 ||
           s->attributes.size()<1024 || s->vram_tiles.size()!=32768 || s->bg_palette.size()!=256 ||
           maps[i].x < -8192 || maps[i].x > 8192 || maps[i].z < -8192 || maps[i].z > 8192)
            return fail("A connected map has invalid source data or coordinates.");
        cells+=s->grid.size();
        if(cells>65536) return fail("This area exceeds the 65,536 source-cell preview limit.");
        for(size_t j=0;j<i;++j) {
            const auto& p=*maps[j].source;
            if(s->map_group==p.map_group && s->map_number==p.map_number)
                return fail("A map appears twice in this area.");
            if(std::max(maps[i].x,maps[j].x)<std::min(maps[i].x+s->width-15,maps[j].x+p.width-15) &&
               std::max(maps[i].z,maps[j].z)<std::min(maps[i].z+s->height-14,maps[j].z+p.height-14))
                return fail("Connected map bodies overlap. Review their source connections.");
        }
        land.push_back(terrain::resolve(*s,set.terrain));
        if(land.back().cells.size()!=s->grid.size()) return fail("Terrain could not resolve for this map.");
    }
    // Primary bodies always win. Remaining padding has one deterministic owner.
    auto owner=[&](int wx,int wz) {
        for(size_t j=0;j<maps.size();++j) if(terrain::primary_cell(*maps[j].source,wx-maps[j].x,wz-maps[j].z)) return int(j);
        for(size_t j=0;j<maps.size();++j) if(maps[j].source->cell(wx-maps[j].x,wz-maps[j].z)!=world::kGridUndefined) return int(j);
        return -1;
    };
    std::vector<RegionCPU> result;RegionStats total;total.maps=maps.size();
    for(size_t i=0;i<maps.size();++i) {
        const auto& m=maps[i];const auto& s=*m.source;
        if(cancel && cancel->load())return fail("Map preparation cancelled.");
        auto surfaces=land[i];std::vector<uint8_t> visible(s.grid.size(),0);
        for(int y=0;y<s.height;++y) for(int x=0;x<s.width;++x) {
            const int j=owner(x+m.x,y+m.z);const size_t k=size_t(y)*s.width+x;
            visible[k]=j==int(i) && s.cell(x,y)!=world::kGridUndefined;
            if(visible[k]) {
                if(terrain::primary_cell(s,x,y)) ++total.primary_cells;else ++total.padding_cells;
            } else if(s.cell(x,y)!=world::kGridUndefined) ++total.hidden_cells;
            if(j>=0 && j!=int(i)) {
                const auto& other=maps[size_t(j)];const int ox=x+m.x-other.x,oy=y+m.z-other.z;
                // Copy the owner's validated heights only. Its material indices
                // are used for occlusion, never sampled through this map's art.
                surfaces.cells[k]=land[size_t(j)].cell(ox,oy);
                surfaces.mismatched[k]=land[size_t(j)].mismatched[size_t(oy)*other.source->width+ox];
            }
        }
        RegionCPU chunk;
        chunk.placed.vertex_limit=8000000-total.stored_vertices;
        chunk.placed.expanded_limit=16000000-total.vertices;
        if(!build_authored_diorama(s,set,&chunk.mesh,&chunk.stats,&visible,&surfaces,&chunk.placed))
            return fail("A map failed validation or exceeded the desktop mesh budget. The previous view is retained.");
        total.vertices+=chunk.stats.flat_vertices+chunk.stats.authored_vertices;
        total.stored_vertices+=chunk.mesh.size();
        total.models+=chunk.placed.models;total.model_instances+=chunk.placed.offsets.size();
        total.deformed_instances+=chunk.placed.deformed_instances;total.batches+=chunk.placed.draws.size();
        if(total.stored_vertices>8000000 || total.vertices>16000000)
            return fail("This area exceeds eight million stored or sixteen million expanded vertices.");
        std::fprintf(stderr,"[region-mesh] map=%d,%d stored=%zu expanded=%zu models=%zu instances=%zu batches=%zu\n",
            s.map_group,s.map_number,chunk.mesh.size(),chunk.stats.flat_vertices+chunk.stats.authored_vertices,
            chunk.placed.models,chunk.placed.offsets.size(),chunk.placed.draws.size());
        chunk.x=m.x;chunk.z=m.z;
        std::fill_n(chunk.lo,3,1e9f);std::fill_n(chunk.hi,3,-1e9f);
        uint64_t hash=1469598103934665603ULL;
        visit_placed(chunk.mesh,chunk.placed,m.x,m.z,[&](const Vertex& v){
            hash=hash_vertex(hash,v);const float p[]{v.x,v.y,v.z};
            for(int k=0;k<3;++k){chunk.lo[k]=std::min(chunk.lo[k],p[k]);chunk.hi[k]=std::max(chunk.hi[k],p[k]);}
        });
        total.geometry_hash=fnv1a_mix(fnv1a_mix(total.geometry_hash,uint32_t(hash)),uint32_t(hash>>32));
        chunk.hash=hash;
        total.raised_instances+=chunk.stats.raised_instances;
        total.terrain_rejected+=chunk.stats.terrain_rejected;
        total.unresolved_placements+=chunk.stats.unresolved_placements;
        result.push_back(std::move(chunk));
    }
    total.gpu_bytes=total.stored_vertices*sizeof(Vertex)+total.model_instances*sizeof(MeshOffset)+maps.size()*(65536+1024);
    *out=std::move(result);*stats=total;if(error)error->clear();return true;
}

void release_region(std::vector<RegionGPU>& chunks) {
    for(auto& c:chunks) {
        if(c.vao)gl::glDeleteVertexArrays(1,&c.vao);
        if(c.vbo)gl::glDeleteBuffers(1,&c.vbo);
        if(c.offsets)gl::glDeleteBuffers(1,&c.offsets);
        if(c.tiles)glDeleteTextures(1,&c.tiles);
        if(c.palette)glDeleteTextures(1,&c.palette);
    }
    chunks.clear();
}

bool upload_region_chunk(const RegionCPU& cpu,const world::Snapshot& s,RegionGPU* c) {
    c->hash=cpu.hash;c->stored=cpu.mesh.size();c->instances=cpu.placed.offsets.size();
    c->group=s.map_group;c->number=s.map_number;
    c->source_tiles=s.vram_tiles;c->source_palette=s.bg_palette;
    std::copy_n(cpu.lo,3,c->lo);std::copy_n(cpu.hi,3,c->hi);
    c->draws=cpu.placed.draws;c->x=cpu.x;c->z=cpu.z;
    gl::glGenVertexArrays(1,&c->vao);gl::glBindVertexArray(c->vao);
    gl::glGenBuffers(1,&c->vbo);gl::glBindBuffer(GL_ARRAY_BUFFER,c->vbo);
    gl::glBufferData(GL_ARRAY_BUFFER,GLsizeiptr(cpu.mesh.size()*sizeof(Vertex)),cpu.mesh.data(),GL_STATIC_DRAW);
    gl::glEnableVertexAttribArray(0);gl::glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,sizeof(Vertex),reinterpret_cast<void*>(offsetof(Vertex,x)));
    gl::glEnableVertexAttribArray(1);gl::glVertexAttribPointer(1,2,GL_FLOAT,GL_FALSE,sizeof(Vertex),reinterpret_cast<void*>(offsetof(Vertex,u)));
    gl::glEnableVertexAttribArray(2);gl::glVertexAttribIPointer(2,1,GL_UNSIGNED_SHORT,sizeof(Vertex),reinterpret_cast<void*>(offsetof(Vertex,tile)));
    gl::glEnableVertexAttribArray(3);gl::glVertexAttribIPointer(3,1,GL_UNSIGNED_SHORT,sizeof(Vertex),reinterpret_cast<void*>(offsetof(Vertex,pal)));
    gl::glEnableVertexAttribArray(4);gl::glVertexAttribPointer(4,1,GL_UNSIGNED_BYTE,GL_TRUE,sizeof(Vertex),reinterpret_cast<void*>(offsetof(Vertex,shade)));
    gl::glEnableVertexAttribArray(5);gl::glVertexAttribIPointer(5,1,GL_UNSIGNED_BYTE,sizeof(Vertex),reinterpret_cast<void*>(offsetof(Vertex,unit_h)));
    gl::glGenBuffers(1,&c->offsets);gl::glBindBuffer(GL_ARRAY_BUFFER,c->offsets);
    gl::glBufferData(GL_ARRAY_BUFFER,GLsizeiptr(cpu.placed.offsets.size()*sizeof(MeshOffset)),cpu.placed.offsets.data(),GL_STATIC_DRAW);
    gl::glVertexAttribDivisor(6,1);
    gl::glBindVertexArray(0);
    glGenTextures(1,&c->tiles);glGenTextures(1,&c->palette);
    for(const GLuint texture:{c->tiles,c->palette}) {
        glBindTexture(GL_TEXTURE_2D,texture);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
    }
    expand_tiles(s);glBindTexture(GL_TEXTURE_2D,c->tiles);
    glTexImage2D(GL_TEXTURE_2D,0,GL_R8UI,256,256,0,GL_RED_INTEGER,GL_UNSIGNED_BYTE,g_tile_scratch.data());
    uint32_t palette[world::kPaletteEntries];tileset::expand_palette(s,palette);
    glBindTexture(GL_TEXTURE_2D,c->palette);glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,16,16,0,GL_RGBA,GL_UNSIGNED_BYTE,palette);
    glBindTexture(GL_TEXTURE_2D,0);c->count=GLsizei(cpu.stats.flat_vertices+cpu.stats.authored_vertices);
    ++g_mesh_upload_count;
    return c->vao && c->vbo && c->offsets && c->tiles && c->palette && glGetError()==GL_NO_ERROR;
}

bool region_in_view(const RegionGPU& c,const math::Mat4& vp) {
    // Reject only when every AABB corner is outside the same clip plane.
    // Bounds include whole overhanging models, not just the map rectangle.
    unsigned outside=63;
    for(int i=0;i<8;++i) {
        const float p[]{(i&1)?c.hi[0]:c.lo[0],(i&2)?c.hi[1]:c.lo[1],(i&4)?c.hi[2]:c.lo[2]};
        float q[4];for(int k=0;k<4;++k)q[k]=vp.m[k]*p[0]+vp.m[4+k]*p[1]+vp.m[8+k]*p[2]+vp.m[12+k];
        unsigned bits=0;for(int k=0;k<3;++k){if(q[k]<-q[3])bits|=1u<<(2*k);if(q[k]>q[3])bits|=2u<<(2*k);}
        outside&=bits;
    }
    return outside==0;
}
