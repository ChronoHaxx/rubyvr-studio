// Included inside the production mesher. Partial 8-pixel strips repeat native
// tile UVs; cliff height never stretches a source pixel. Buried sides are cut
// against every neighboring solid interval, including decks over water.
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
// Clip a graded edge against the neighbour's solid intervals, then against
// native 8px material bands. Matching ramp edges emit no buried seam faces.
void terrain_graded_side(const world::Snapshot& s,std::vector<Vertex>& mesh,int x,int z,
                         const terrain::TerrainSurface& p,int side,uint16_t id,
                         const terrain::TerrainCell* neighbor) {
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
    const float bottom=p.kind==terrain::TerrainKind::Ground?0.f:float(p.height-p.thickness);
    const int anchor=p.height+p.side_offset;
    const int ceiling=anchor+int(std::ceil((std::max(a,a+b)-anchor)/8.f))*8;
    for(int hi=ceiling;hi>bottom;hi-=8) for(int half=0;half<2;++half) {
        const float left=half*.5f,right=left+.5f;
        Poly own={{left,float(hi)},{right,float(hi)},{right,float(hi-8)},{left,float(hi-8)}};
        own=clip(clip(own,a,b,false),bottom,0,true);
        std::vector<Poly> visible;if(own.size()>=3) visible.push_back(std::move(own));
        if(neighbor) for(const auto& q:neighbor->surfaces) {
            if(q.kind==terrain::TerrainKind::Water || terrain::maximum_height(q)==0) continue;
            const float qa=terrain::surface_height(q,float(ox[side]-dx[side]),float(oz[side]-dz[side]));
            const float qb=terrain::surface_height(q,float(ox[side]-dx[side]+rx[side]),float(oz[side]-dz[side]+rz[side]))-qa;
            const float low=q.kind==terrain::TerrainKind::Ground?0.f:float(q.height-q.thickness);
            std::vector<Poly> next;
            for(const auto& polygon:visible) {
                auto upper=clip(polygon,qa,qb,true),lower=clip(polygon,low,0,false);
                if(upper.size()>=3) next.push_back(std::move(upper));
                if(lower.size()>=3) next.push_back(std::move(lower));
            }
            visible=std::move(next);
        }
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
                       const terrain::Resolved& resolved,const terrain::TerrainCell& c,int x,int y,uint16_t floor) {
    const int dx[]={0,0,1,-1},dz[]={-1,1,0,0};
    for(const auto& p:c.surfaces) {
        const uint16_t top=p.top>=0?uint16_t(p.top):floor;
        terrain_plane(s,mesh,x,y,p.height,top,false,&p);
        if(p.kind==terrain::TerrainKind::Water || (!p.thickness && !terrain::maximum_height(p))) continue;
        terrain_plane(s,mesh,x,y,p.height-p.thickness,p.side>=0?uint16_t(p.side):floor,true);
        for(int side=0;side<4;++side) {
            const auto* neighbor=resolved.cell(x+dx[side],y+dz[side]);
            bool graded=p.rise_x || p.rise_z || p.corner_delta;
            if(neighbor) for(const auto& q:neighbor->surfaces) graded|=q.rise_x || q.rise_z || q.corner_delta;
            if(graded) {
                terrain_graded_side(s,mesh,x,y,p,side,p.side>=0?uint16_t(p.side):floor,neighbor);
                continue;
            }
            std::vector<std::pair<int,int>> exposed{{p.height-p.thickness,p.height}};
            if(neighbor) for(const auto& q:neighbor->surfaces) {
                if(!q.thickness) continue;
                std::vector<std::pair<int,int>> next;
                for(auto [lo,hi]:exposed) {
                    if(q.height<=lo || q.height-q.thickness>=hi) next.emplace_back(lo,hi);
                    else {
                        if(q.height-q.thickness>lo) next.emplace_back(lo,q.height-q.thickness);
                        if(q.height<hi) next.emplace_back(q.height,hi);
                    }
                }
                exposed=std::move(next);
            }
            for(auto [lo,hi]:exposed) terrain_side(s,mesh,x,y,p,side,lo,hi,p.side>=0?uint16_t(p.side):floor);
        }
    }
}
