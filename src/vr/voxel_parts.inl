// Included by the shared production mesher. Only explicit v6 patterns use this
// local lattice. The exposed-run approach was checked against DRAMALESS_SHAPE
// 5ccc0e2/lib/Buildings.lua; source identities, not current RGB, govern merging.
bool emit_voxel_parts(std::vector<Vertex>& out,const cutout::Art& art,const overrides::Pattern& pattern,
                      std::vector<size_t>* triangle_parts = nullptr) {
    namespace pg=part_geometry;
    constexpr float scale=16,unit=1/scale;
    using I3=std::array<int,3>;
    auto vec=[](I3 a){return pg::Vec{float(a[0]),float(a[1]),float(a[2])};};
    auto component=[](pg::Vec v,int a){return a==0?v.x:a==1?v.y:v.z;};
    // Palette RGB never affects geometry. Opacity and winning source address do.
    std::vector<uint64_t> source;source.reserve(art.pixels.size());
    for(const auto& p:art.pixels) source.push_back(uint64_t(p.tile.index)|(uint64_t(p.tile.palette)<<16)|
        (uint64_t(p.tx)<<32)|(uint64_t(p.ty)<<40)|(uint64_t(bool(p.rgba>>24))<<48));
    struct Cached {overrides::Pattern pattern;std::vector<uint64_t> source;std::vector<Vertex> mesh;};
    static thread_local std::vector<Cached> cache;
    if(!triangle_parts) for(const auto& c:cache) if(c.pattern==pattern && c.source==source) {out=c.mesh;return true;}
    pg::Vec lower{1000,1000,1000},upper{-1000,-1000,-1000};
    for(const auto& part:pattern.parts) {
        const auto& t=part.transform;
        for(int k=0;k<8;++k) {
            const auto q=pg::to_group({(k&1)?t.size.x:0,(k&2)?t.size.y:0,(k&4)?t.size.z/2:-t.size.z/2},t);
            lower={std::min(lower.x,q.x),std::min(lower.y,q.y),std::min(lower.z,q.z)};
            upper={std::max(upper.x,q.x),std::max(upper.y,q.y),std::max(upper.z,q.z)};
        }
    }
    I3 lo,dim;
    for(int a=0;a<3;++a) {lo[a]=int(std::floor(component(lower,a)*scale));dim[a]=int(std::ceil(component(upper,a)*scale))-lo[a];}
    const size_t count=size_t(dim[0])*dim[1]*dim[2];
    if(!count || count>1048576 || count*pattern.parts.size()>16777216) return false;
    std::vector<int16_t> owners(count,-1);
    auto at=[&](I3 q)->int {
        for(int a=0;a<3;++a) if(q[a]<0 || q[a]>=dim[a]) return -1;
        return owners[(size_t(q[2])*dim[1]+q[1])*dim[0]+q[0]];
    };
    auto center=[&](I3 q){return pg::Vec{(q[0]+lo[0]+.5f)*unit,(q[1]+lo[1]+.5f)*unit,(q[2]+lo[2]+.5f)*unit};};
    auto region=[&](const overrides::Part& p){return p.art_region==overrides::ArtRegion{}?
        overrides::ArtRegion{0,0,art.w,art.h}:p.art_region;};
    auto billboard_pixel=[&](const overrides::Part& part,pg::Vec q)->int {
        const auto r=region(part);
        const int x=std::clamp(int(std::floor(q.x*scale)),0,r[2]-1);
        const int y=std::clamp(int(std::floor((part.transform.size.y-q.y)*scale)),0,r[3]-1);
        const int i=(r[1]+y)*art.w+r[0]+x;
        if(!pattern.cutout->opacity[i] || !(art.pixels[i].rgba>>24) ||
            (part.local_mask && !part.local_mask->opacity[size_t(y)*r[2]+x])) return -1;
        return i;
    };
    size_t occupied=0;
    for(int z=0;z<dim[2];++z) for(int y=0;y<dim[1];++y) for(int x=0;x<dim[0];++x) {
        const I3 pos{x,y,z};const auto p=center(pos);
        for(size_t j=0;j<pattern.parts.size();++j) {
            const auto& part=pattern.parts[j];const auto& t=part.transform;const auto q=pg::to_local(p,t);
            if(q.x<0 || q.y<0 || q.z< -t.size.z/2 || q.x>=t.size.x || q.y>=t.size.y || q.z>=t.size.z/2) continue;
            if(part.kind==overrides::PartKind::Wedge) {
                float fraction=part.wedge_axis==0?q.x/t.size.x:(q.z+t.size.z/2)/t.size.z;
                if(part.wedge_direction<0) fraction=1-fraction;
                if(q.y>=fraction*t.size.y) continue;
            }
            if(part.kind==overrides::PartKind::Billboard && billboard_pixel(part,q)<0) continue;
            owners[(size_t(z)*dim[1]+y)*dim[0]+x]=int16_t(j);++occupied;break;
        }
    }
    // Outward surface orientation, independent of the material's local axes.
    struct Face {int axis,sign,u,v;};
    const Face faces[]={{2,1,0,1},{2,-1,0,1},{0,1,2,1},{0,-1,2,1},{1,1,0,2},{1,-1,0,2}};
    const uint8_t shades[]={kPropFront,kPropBack,kPropSide,kPropSide,kPropTop,kPropBottom};
    auto sample=[&](I3 pos,int face)->const cutout::Pixel* {
        const int owner=at(pos);if(owner<0) return nullptr;
        I3 next=pos;next[faces[face].axis]+=faces[face].sign;
        if(at(next)>=0) return nullptr;
        const auto& part=pattern.parts[owner];const auto& t=part.transform;
        const auto q=pg::to_local(center(pos),t);
        I3 normal{};normal[faces[face].axis]=faces[face].sign;
        const auto n=pg::inverse_rotate(vec(normal),t.angles);
        int role;
        if(std::abs(n.y)>=std::max(std::abs(n.x),std::abs(n.z))) role=n.y>0?4:5;
        else if(std::abs(n.x)>std::abs(n.z)) role=n.x>0?2:3;
        else role=n.z>0?0:1;
        const bool relief=part.kind==overrides::PartKind::Billboard;
        if(relief && (!part.side_art || role<2)) {
            const int i=billboard_pixel(part,q);return i<0?nullptr:&art.pixels[i];
        }
        // Stair risers inside a wedge's sloping span are roof, not gable art.
        if(part.kind==overrides::PartKind::Wedge && role!=4 && role!=5) {
            const bool slope_face=part.wedge_axis==0?(role==2 || role==3):(role==0 || role==1);
            const float c=part.wedge_axis==0?q.x:q.z+t.size.z/2;
            const float width=part.wedge_axis==0?t.size.x:t.size.z;
            if(slope_face && c>unit*.51f && c<width-unit*.51f) role=4;
        }
        const auto& surface=relief?*part.side_art:part.surfaces[role];const auto& r=surface.region;
        float u=q.x,v=t.size.y-q.y;
        if(role==1) u=t.size.x-q.x;
        if(role==2) u=t.size.z/2-q.z;
        if(role==3) u=q.z+t.size.z/2;
        if(role==4) v=q.z+t.size.z/2;
        if(role==5) v=t.size.z/2-q.z;
        if(relief) {
            // A common depth centre keeps adjacent relief bands in phase even
            // when their thickness differs. Source offsets survive cropping.
            const auto crop=region(part);
            u=role==2?-q.z:role==3?q.z:q.x+crop[0]/scale;
            v=role>=4?(role==4?q.z:-q.z):t.size.y-q.y+crop[1]/scale;
        }
        auto wrap=[](int n,int size){return (n%size+size)%size;};
        int sx=wrap(int(std::floor(u*scale))+surface.offset[0],r[2]),sy=wrap(int(std::floor(v*scale))+surface.offset[1],r[3]);
        if(surface.flip_u) sx=r[2]-1-sx;if(surface.flip_v) sy=r[3]-1-sy;
        const auto& pixel=art.pixels[size_t(r[1]+sy)*art.w+r[0]+sx];
        return &pixel;
    };
    size_t exposed=0,runs=0;
    for(int f=0;f<6;++f) {
        const auto face=faces[f];
        for(int plane=0;plane<dim[face.axis];++plane) for(int row=0;row<dim[face.v];++row) {
            for(int column=0;column<dim[face.u];) {
                I3 pos{};pos[face.axis]=plane;pos[face.v]=row;pos[face.u]=column;
                const auto first=sample(pos,f);
                if(!first) {++column;continue;}
                if(!(first->rgba>>24)) {out.clear();return false;} // no unmasked fallback colours
                int length=1,du=0,dv=0;bool mode_known=false;
                while(column+length<dim[face.u]) {
                    I3 p=pos;p[face.u]+=length;const auto candidate=sample(p,f);
                    // The GPU can merge identical material across two parts;
                    // the optional CPU picking mesh retains that ownership seam.
                    if(triangle_parts && at(p)!=at(pos)) break;
                    if(!candidate || !(candidate->rgba>>24) || candidate->tile.index!=first->tile.index || candidate->tile.palette!=first->tile.palette) break;
                    const int dx=int(candidate->tx)-int(first->tx),dy=int(candidate->ty)-int(first->ty);
                    if(!mode_known) {du=dx;dv=dy;if(std::abs(du)+std::abs(dv)>1) break;mode_known=true;}
                    if(dx!=du*length || dy!=dv*length) break;
                    ++length;
                }
                exposed+=length;++runs;
                // A strip stays within one original tile and palette. Uniform
                // runs keep constant UV; strips interpolate only the varying axis.
                I3 base=pos;for(int a=0;a<3;++a) base[a]+=lo[a];if(face.sign>0) ++base[face.axis];
                auto origin=vec(base)*unit;I3 ru{},rv{};ru[face.u]=length;rv[face.v]=1;
                const auto right=vec(ru)*unit,up=vec(rv)*unit;
                const float u0=(first->tx+.5f-(du?du*.5f:0))/8.f;
                const float v0=(first->ty+.5f-(dv?dv*.5f:0))/8.f;
                const float u1=u0+du*length/8.f,v1=v0+dv*length/8.f;
                auto vertex=[&](pg::Vec p,float u,float v){return Vertex{p.x,p.y,p.z,u,v,first->tile.index,first->tile.palette,shades[f],1};};
                const Vertex a=vertex(origin,u0,v0),b=vertex(origin+right,u1,v1),c=vertex(origin+right+up,u1,v1),d=vertex(origin+up,u0,v0);
                const bool positive=component(pg::cross(right,up),face.axis)*face.sign>0;
                if(positive) out.insert(out.end(),{d,c,b,d,b,a});
                else out.insert(out.end(),{a,b,c,a,c,d});
                if(triangle_parts) triangle_parts->insert(triangle_parts->end(),2,size_t(at(pos)));
                column+=length;
            }
        }
    }
    conform_part_edges(out,triangle_parts);
    if(triangle_parts) return true; // Do not replace the production mesh cache.
    std::fprintf(stderr,"[voxel] %s occupied=%zu grid_bytes=%zu exposed_faces=%zu merged_runs=%zu vertices=%zu mesh_bytes=%zu\n",
        pattern.id.c_str(),occupied,owners.size()*sizeof(int16_t),exposed,runs,out.size(),out.size()*sizeof(Vertex));
    size_t cached_vertices=0;for(const auto& c:cache) cached_vertices+=c.mesh.size();
    while(!cache.empty() && (cache.size()>=64 || cached_vertices+out.size()>1000000)) {
        cached_vertices-=cache.front().mesh.size();cache.erase(cache.begin());
    }
    if(out.size()<=1000000) cache.push_back({pattern,std::move(source),out});
    return true;
}
