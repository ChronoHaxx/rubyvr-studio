// Included inside the production mesher. Partial 8-pixel strips repeat native
// tile UVs; cliff height never stretches a source pixel. Buried sides are cut
// against every neighboring solid interval, including decks over water.
//
// The connected explorer additionally closes every solid ground column to one
// explicit preview base at -16 source pixels: one map cell below the legacy
// y=0 floor. It is a desktop presentation aid, not geography or collision.
// `closed_base` is false for the single-map editor, whose mesh and hashes stay
// exactly as they were; only the region path opts in.
constexpr int kGroundBase = -16;

void terrain_plane(const world::Snapshot& s,std::vector<Vertex>& mesh,int x,int z,
                   int height,uint16_t id,bool bottom=false,const terrain::TerrainSurface* grade=nullptr) {
    for(int pair=0;pair<2;++pair) for(int k=0;k<4;++k) {
        const auto e=world::unpack_tile_entry(s.metatiles[size_t(id)*8+pair*4+k]);
        const float px=x+(k&1)*.5f,pz=z+(k>>1)*.5f,py=height/16.f+(bottom?-1:1)*pair*.002f;
        const size_t start=mesh.size();
        push_quad(mesh,e,bottom?kPropBottom:kShadeFlat,0,px,py,pz,px+.5f,py,pz,px+.5f,py,pz+.5f,px,py,pz+.5f,true);
        if(grade) for(size_t i=start;i<mesh.size();++i)
            mesh[i].y+=(terrain::surface_height(*grade,mesh[i].x-x,mesh[i].z-z)-height)/16.f;
        if(bottom) for(size_t i=start;i<mesh.size();i+=3) std::swap(mesh[i+1],mesh[i+2]);
    }
}
void terrain_side(const world::Snapshot& s,std::vector<Vertex>& mesh,int x,int z,
                  const terrain::TerrainSurface& surface,int side,int lo,int hi,uint16_t id) {
    const float rx[]={-1,1,0,0},rz[]={0,0,-1,1};
    const float ox[]={float(x+1),float(x),float(x+1),float(x)};
    const float oz[]={float(z),float(z+1),float(z+1),float(z)};
    const float nx[]={0,0,1,-1},nz[]={-1,1,0,0};
    const uint8_t shade[]={kPropBack,kPropFront,kPropSide,kPropSide};
    while(hi>lo) {
        const int row=(surface.height-hi+surface.side_offset)%16,band=std::min(hi-lo,8-row%8);
        for(int pair=0;pair<2;++pair) for(int half=0;half<2;++half) {
            const auto e=world::unpack_tile_entry(s.metatiles[size_t(id)*8+pair*4+(row/8)*2+half]);
            float u0=0,u1=1,v0=(row%8)/8.f,v1=(row%8+band)/8.f;
            if(e.hflip) {u0=1;u1=0;}if(e.vflip) {v0=1-v0;v1=1-v1;}
            const float ax=ox[side]+rx[side]*half*.5f+nx[side]*pair*.0005f;
            const float az=oz[side]+rz[side]*half*.5f+nz[side]*pair*.0005f;
            const float bx=ax+rx[side]*.5f,bz=az+rz[side]*.5f,top=hi/16.f,bottom=(hi-band)/16.f;
            Vertex a{ax,top,az,u0,v0,e.index,e.palette,shade[side],0};
            Vertex b{bx,top,bz,u1,v0,e.index,e.palette,shade[side],0};
            Vertex c{bx,bottom,bz,u1,v1,e.index,e.palette,shade[side],0};
            Vertex d{ax,bottom,az,u0,v1,e.index,e.palette,shade[side],0};
            mesh.insert(mesh.end(),{a,b,c,a,c,d});
        }
        hi-=band;
    }
}

// What one neighbouring world column contributes to a shared vertical face.
// `cell` is the owning map's validated terrain; padding copies are replaced by
// their owner before meshing. It is null for empty space and for legacy floors;
// `ground` separates those two, because a legacy floor is solid to the preview
// base even though it carries no authored surfaces.
struct TerrainNeighbor {
    const terrain::TerrainCell* cell=nullptr;
    bool ground=false;
};
TerrainNeighbor terrain_neighbor(const world::Snapshot& s,const terrain::Resolved& r,
                                 const std::vector<TerrainNeighbor>* neighbors,int x,int y) {
    TerrainNeighbor n;
    // Connected ownership includes a one-cell halo beyond this snapshot. A
    // neighbour can be real ground even when our copied padding is undefined.
    if(neighbors) {
        if(x>=-1 && y>=-1 && x<=s.width && y<=s.height)
            return (*neighbors)[size_t(y+1)*(s.width+2)+x+1];
        return n;
    }
    if(x<0 || y<0 || x>=s.width || y>=s.height) return n;
    n.cell=r.cell(x,y);
    if(n.cell) {for(const auto& p:n.cell->surfaces) n.ground|=p.kind==terrain::TerrainKind::Ground;}
    else n.ground=s.cell(x,y)!=world::kGridUndefined;
    return n;
}
// Remove the vertical span a neighbour already fills. Ground columns run to the
// preview base, so adjoining solid cells and maps never emit a buried seam.
// Water contributes nothing; decks keep their own floating span.
std::vector<std::pair<int,int>> terrain_exposed(std::vector<std::pair<int,int>> exposed,
                                                const TerrainNeighbor& n,bool closed_base) {
    auto cut=[&](int lo,int hi) {
        if(hi<=lo) return;
        std::vector<std::pair<int,int>> next;
        for(auto [a,b]:exposed) {
            if(hi<=a || lo>=b) next.emplace_back(a,b);
            else {
                if(lo>a) next.emplace_back(a,lo);
                if(hi<b) next.emplace_back(hi,b);
            }
        }
        exposed=std::move(next);
    };
    if(!n.cell) {
        if(n.ground) cut(closed_base?kGroundBase:0,0);
        return exposed;
    }
    bool ground=false;
    for(const auto& q:n.cell->surfaces) {
        if(q.kind==terrain::TerrainKind::Water) continue;
        if(q.kind==terrain::TerrainKind::Ground) {cut(closed_base?kGroundBase:0,q.height);ground=true;}
        else if(q.thickness) cut(q.height-q.thickness,q.height);
    }
    if(n.ground && !ground) cut(closed_base?kGroundBase:0,0);
    return exposed;
}

// Clip a graded edge against the neighbour's solid intervals, then against
// native 8px material bands. Matching ramp edges emit no buried seam faces.
void terrain_graded_side(const world::Snapshot& s,std::vector<Vertex>& mesh,int x,int z,
                         const terrain::TerrainSurface& p,int side,uint16_t id,
                         const TerrainNeighbor& neighbor,bool closed_base) {
    struct Point {float t,h;};
    using Poly=std::vector<Point>;
    auto clip=[](const Poly& poly,float a,float b,bool above) {
        Poly result;
        if(poly.empty()) return result;
        auto distance=[&](Point q){return (q.h-a-b*q.t)*(above?1.f:-1.f);};
        Point previous=poly.back();float before=distance(previous);
        for(const auto q:poly) {
            const float after=distance(q);
            if((before>=0)!=(after>=0)) {
                const float f=before/(before-after);
                result.push_back({previous.t+(q.t-previous.t)*f,previous.h+(q.h-previous.h)*f});
            }
            if(after>=0) result.push_back(q);
            previous=q;before=after;
        }
        return result;
    };
    const int rx[]={-1,1,0,0},rz[]={0,0,-1,1};
    const int ox[]={1,0,1,0},oz[]={0,1,1,0};
    const int dx[]={0,0,1,-1},dz[]={-1,1,0,0};
    const uint8_t shade[]={kPropBack,kPropFront,kPropSide,kPropSide};
    const float a=terrain::surface_height(p,float(ox[side]),float(oz[side]));
    const float b=terrain::surface_height(p,float(ox[side]+rx[side]),float(oz[side]+rz[side]))-a;
    const float bottom=p.kind==terrain::TerrainKind::Ground?(closed_base?float(kGroundBase):0.f):float(p.height-p.thickness);
    const int anchor=p.height+p.side_offset;
    const int ceiling=anchor+int(std::ceil((std::max(a,a+b)-anchor)/8.f))*8;
    for(int hi=ceiling;hi>bottom;hi-=8) for(int half=0;half<2;++half) {
        const float left=half*.5f,right=left+.5f;
        Poly own={{left,float(hi)},{right,float(hi)},{right,float(hi-8)},{left,float(hi-8)}};
        own=clip(clip(own,a,b,false),bottom,0,true);
        std::vector<Poly> visible;if(own.size()>=3) visible.push_back(std::move(own));
        auto subtract=[&](float qa,float qb,float low) {
            std::vector<Poly> next;
            for(const auto& polygon:visible) {
                auto upper=clip(polygon,qa,qb,true),lower=clip(polygon,low,0,false);
                if(upper.size()>=3) next.push_back(std::move(upper));
                if(lower.size()>=3) next.push_back(std::move(lower));
            }
            visible=std::move(next);
        };
        if(neighbor.cell) for(const auto& q:neighbor.cell->surfaces) {
            if(q.kind==terrain::TerrainKind::Water) continue;
            if(q.kind!=terrain::TerrainKind::Ground && !q.thickness) continue;
            const float qa=terrain::surface_height(q,float(ox[side]-dx[side]),float(oz[side]-dz[side]));
            const float qb=terrain::surface_height(q,float(ox[side]-dx[side]+rx[side]),float(oz[side]-dz[side]+rz[side]))-qa;
            const float low=q.kind==terrain::TerrainKind::Ground?(closed_base?float(kGroundBase):0.f):float(q.height-q.thickness);
            subtract(qa,qb,low);
        }
        else if(closed_base && neighbor.ground) subtract(0.f,0.f,float(kGroundBase));
        const int row=((anchor-hi)%16+16)%16;
        for(int pair=0;pair<2;++pair) {
            const auto e=world::unpack_tile_entry(s.metatiles[size_t(id)*8+pair*4+(row/8)*2+half]);
            auto vertex=[&](Point q) {
                float u=q.t*2-half,v=(hi-q.h)/8;
                if(e.hflip) u=1-u;if(e.vflip) v=1-v;
                return Vertex{x+ox[side]+rx[side]*q.t+dx[side]*pair*.0005f,q.h/16,
                    z+oz[side]+rz[side]*q.t+dz[side]*pair*.0005f,u,v,e.index,e.palette,shade[side],0};
            };
            for(const auto& polygon:visible) for(size_t i=1;i+1<polygon.size();++i) {
                const auto v0=polygon[0],v1=polygon[i],v2=polygon[i+1];
                const float area=(v1.t-v0.t)*(v2.h-v0.h)-(v2.t-v0.t)*(v1.h-v0.h);
                if(std::abs(area)>1e-6f) mesh.insert(mesh.end(),{vertex(v0),vertex(v1),vertex(v2)});
            }
        }
    }
}
void terrain_cell_mesh(const world::Snapshot& s,std::vector<Vertex>& mesh,
                       const terrain::Resolved& resolved,const std::vector<TerrainNeighbor>* neighbors,
                       const terrain::TerrainCell& c,int x,int y,uint16_t floor,bool closed_base) {
    const int dx[]={0,0,1,-1},dz[]={-1,1,0,0};
    for(const auto& p:c.surfaces) {
        const uint16_t top=p.top>=0?uint16_t(p.top):floor;
        const uint16_t side_id=p.side>=0?uint16_t(p.side):floor;
        terrain_plane(s,mesh,x,y,p.height,top,false,&p);
        if(p.kind==terrain::TerrainKind::Water) continue;
        const bool ground=p.kind==terrain::TerrainKind::Ground;
        if(!p.thickness && !terrain::maximum_height(p) && !(closed_base && ground)) continue;
        const int bottom=(closed_base && ground)?kGroundBase:(p.height-p.thickness);
        terrain_plane(s,mesh,x,y,bottom,side_id,true);
        for(int side=0;side<4;++side) {
            const auto neighbor=terrain_neighbor(s,resolved,neighbors,x+dx[side],y+dz[side]);
            bool graded=p.rise_x || p.rise_z || p.corner_delta;
            if(neighbor.cell) for(const auto& q:neighbor.cell->surfaces) graded|=q.rise_x || q.rise_z || q.corner_delta;
            if(graded) {
                terrain_graded_side(s,mesh,x,y,p,side,side_id,neighbor,closed_base);
                continue;
            }
            auto exposed=terrain_exposed({{bottom,p.height}},neighbor,closed_base);
            for(auto [lo,hi]:exposed) terrain_side(s,mesh,x,y,p,side,lo,hi,side_id);
        }
    }
}
// A legacy floor cell has no authored surfaces to mesh, so close its column
// here with the same base plane and outward walls an authored Ground gets.
void terrain_legacy_base(const world::Snapshot& s,std::vector<Vertex>& mesh,
                         const terrain::Resolved& resolved,const std::vector<TerrainNeighbor>* neighbors,
                         int x,int y,uint16_t id) {
    terrain::TerrainSurface flat;flat.height=0;flat.thickness=0;flat.kind=terrain::TerrainKind::Ground;
    const int dx[]={0,0,1,-1},dz[]={-1,1,0,0};
    terrain_plane(s,mesh,x,y,kGroundBase,id,true);
    for(int side=0;side<4;++side) {
        const auto neighbor=terrain_neighbor(s,resolved,neighbors,x+dx[side],y+dz[side]);
        auto exposed=terrain_exposed({{kGroundBase,0}},neighbor,true);
        for(auto [lo,hi]:exposed) terrain_side(s,mesh,x,y,flat,side,lo,hi,id);
    }
}
