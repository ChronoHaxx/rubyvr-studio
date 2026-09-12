#include "terrain.h"
#include <algorithm>
#include <cmath>
#include <set>

namespace vr::terrain {
namespace {
bool integer(const json::Value* v, int lo, int hi) {
    return v && v->type == json::Value::Type::Number && std::isfinite(v->number) &&
        v->number >= lo && v->number <= hi && std::floor(v->number) == v->number;
}
bool fields(const json::Value& v, std::initializer_list<const char*> names) {
    if(!v.is_object()) return false;
    std::set<std::string> seen;
    for(const auto& k:v.keys) {
        if(!seen.insert(k).second) return false;
        if(k=="_comment") continue;
        bool known=false;for(const auto* n:names) known |= k==n;
        if(!known) return false;
    }
    return true;
}
bool tile_matches(const world::Snapshot& s, int id, const overrides::TileDef& d) {
    if(id<0 || size_t(id)>=s.attributes.size() || size_t(id)*8+8>s.metatiles.size()) return false;
    return s.attributes[id]==d.attr && std::equal(std::begin(d.entries),std::end(d.entries),s.metatiles.begin()+id*8);
}
}

bool primary_cell(const world::Snapshot& s,int x,int y) {
    return x>=7 && y>=7 && x<s.width-8 && y<s.height-7 && s.cell(x,y)!=world::kGridUndefined;
}
bool guard_tile(const world::Snapshot& s,uint16_t id,TerrainMap* m) {
    if(!m || id>=s.attributes.size() || size_t(id)*8+8>s.metatiles.size()) return false;
    overrides::TileDef d;d.attr=s.attributes[id];
    std::copy_n(s.metatiles.begin()+id*8,8,d.entries);m->tiles[id]=d;return true;
}
float surface_height(const TerrainSurface& p,float u,float v) {
    // The NW-SE diagonal agrees with the two triangles used by each tile quad.
    return p.height+p.rise_x*u+p.rise_z*v+p.corner_delta*std::min(u,v);
}
int maximum_height(const TerrainSurface& p) {
    return p.height+std::max({0,p.rise_x,p.rise_z,p.rise_x+p.rise_z+p.corner_delta});
}
static size_t surface_budget(const TerrainSurface& p) {
    const bool grade=p.rise_x || p.rise_z || p.corner_delta;
    return (grade?512:96)+size_t(((grade?maximum_height(p):p.thickness)+7)/8)*(grade?768:96);
}
bool valid(const std::vector<TerrainMap>& maps) {
    if(maps.size()>1024) return false;
    std::set<std::pair<int,int>> identities;
    size_t total=0;
    for(const auto& m:maps) {
        size_t vertex_budget=0;
        if(m.group<0 || m.group>255 || m.number<0 || m.number>255 ||
           !identities.emplace(m.group,m.number).second || m.width<16 || m.height<15 ||
           m.width>1024 || m.height>1024 || int64_t(m.width)*m.height>10240 ||
           m.cells.empty() || m.cells.size()>10240 || (total+=m.cells.size())>262144 || m.tiles.size()>1024) return false;
        std::set<std::pair<int,int>> positions;
        for(const auto& c:m.cells) {
            if(c.x<7 || c.y<7 || c.x>=m.width-8 || c.y>=m.height-7 ||
               c.expected==world::kGridUndefined || !positions.emplace(c.x,c.y).second ||
               !m.tiles.count(c.expected&world::kMetatileIdMask) || c.underlay < -1 || c.underlay>1023 ||
               (c.underlay>=0 && !m.tiles.count(c.underlay)) || c.surfaces.empty() || c.surfaces.size()>4) return false;
            std::set<int> layers;
            for(const auto& p:c.surfaces) {
                if(p.layer<0 || p.layer>15 || !layers.insert(p.layer).second || p.height<0 || p.height>256 ||
                   p.thickness<0 || p.thickness>p.height || p.top < -1 || p.top>1023 || p.side < -1 || p.side>1023 ||
                   (p.top>=0 && !m.tiles.count(p.top)) || (p.side>=0 && !m.tiles.count(p.side)) ||
                   (p.kind!=TerrainKind::Ground && p.kind!=TerrainKind::Water && p.kind!=TerrainKind::Deck) ||
                   (p.kind==TerrainKind::Ground && p.thickness!=p.height) ||
                   (p.kind==TerrainKind::Water && p.thickness!=0) || (p.kind==TerrainKind::Deck && p.thickness<1) ||
                   p.rise_x < -256 || p.rise_x>256 || p.rise_z < -256 || p.rise_z>256 ||
                   p.corner_delta < -512 || p.corner_delta>512 ||
                   p.side_offset<0 || p.side_offset>15 || maximum_height(p)>256 ||
                   p.height+std::min({0,p.rise_x,p.rise_z,p.rise_x+p.rise_z+p.corner_delta})<0 ||
                   ((p.rise_x || p.rise_z || p.corner_delta) && (p.kind!=TerrainKind::Ground || c.surfaces.size()!=1)) ||
                   (c.surfaces.size()>1 && (p.layer==0 || p.layer==15))) return false;
                vertex_budget+=surface_budget(p);
                if(vertex_budget>2000000) return false;
            }
            for(size_t i=0;i<c.surfaces.size();++i) for(size_t j=i+1;j<c.surfaces.size();++j) {
                const auto& a=c.surfaces[i];const auto& b=c.surfaces[j];
                // Includes open water surfaces: no plane embedded in a solid.
                if(a.height==b.height || (a.height<b.height && a.height>b.height-b.thickness) ||
                   (b.height<a.height && b.height>a.height-a.thickness)) return false;
            }
        }
        for(const auto& [id,d]:m.tiles) if(id>1023) return false;
    }
    return true;
}

bool read(const json::Value& root,std::vector<TerrainMap>* out) {
    if(!out || !fields(root,{"version","maps"}) || !integer(root.find("version"),1,1)) return false;
    const auto* maps=root.find("maps");if(!maps || !maps->is_array() || maps->items.size()>1024) return false;
    std::vector<TerrainMap> result;size_t total=0;
    for(const auto& v:maps->items) {
        if(!fields(v,{"group","number","width","height","cells","tiles"})) return false;
        TerrainMap m;
        for(auto [name,target]:{std::pair{"group",&m.group},{"number",&m.number},{"width",&m.width},{"height",&m.height}}) {
            if(!integer(v.find(name),0,1024)) return false;*target=v.find(name)->as_int();
        }
        const auto* cells=v.find("cells");const auto* tiles=v.find("tiles");
        if(!cells || !cells->is_array() || cells->items.size()>10240 || !tiles || !tiles->is_array() || tiles->items.size()>1024) return false;
        if((total+=cells->items.size())>262144) return false;
        for(const auto& t:tiles->items) {
            if(!fields(t,{"id","entries","attr"}) || !integer(t.find("id"),0,1023) || !integer(t.find("attr"),0,65535)) return false;
            const auto* entries=t.find("entries");if(!entries || !entries->is_array() || entries->items.size()!=8) return false;
            overrides::TileDef d;d.attr=uint16_t(t.find("attr")->as_int());
            for(int i=0;i<8;++i) {if(!integer(&entries->items[i],0,65535)) return false;d.entries[i]=uint16_t(entries->items[i].as_int());}
            if(!m.tiles.emplace(uint16_t(t.find("id")->as_int()),d).second) return false;
        }
        for(const auto& c:cells->items) {
            if(!fields(c,{"x","y","expected","underlay","surfaces"}) || !integer(c.find("x"),0,1023) ||
               !integer(c.find("y"),0,1023) || !integer(c.find("expected"),0,65535) || !integer(c.find("underlay"),-1,1023)) return false;
            TerrainCell cell;cell.x=c.find("x")->as_int();cell.y=c.find("y")->as_int();
            cell.expected=uint16_t(c.find("expected")->as_int());cell.underlay=c.find("underlay")->as_int();
            const auto* surfaces=c.find("surfaces");if(!surfaces || !surfaces->is_array() || surfaces->items.size()>4) return false;
            for(const auto& p:surfaces->items) {
                if(!fields(p,{"layer","height","thickness","kind","top","side","rise_x","rise_z","corner_delta","side_offset"})) return false;
                TerrainSurface surface;
                for(auto [name,target]:{std::pair{"layer",&surface.layer},{"height",&surface.height},{"thickness",&surface.thickness},{"top",&surface.top},{"side",&surface.side}}) {
                    if(!integer(p.find(name),-1,1023)) return false;*target=p.find(name)->as_int();
                }
                for(auto [name,target]:{std::pair{"rise_x",&surface.rise_x},{"rise_z",&surface.rise_z},{"corner_delta",&surface.corner_delta},{"side_offset",&surface.side_offset}}) {
                    if(const auto* value=p.find(name)) {if(!integer(value,-512,512)) return false;*target=value->as_int();}
                }
                const auto* kind=p.find("kind");if(!kind || kind->type!=json::Value::Type::String) return false;
                if(kind->string=="ground") surface.kind=TerrainKind::Ground;
                else if(kind->string=="water") surface.kind=TerrainKind::Water;
                else if(kind->string=="deck") surface.kind=TerrainKind::Deck;
                else return false;
                cell.surfaces.push_back(surface);
            }
            m.cells.push_back(std::move(cell));
        }
        result.push_back(std::move(m));
    }
    if(!valid(result)) return false;*out=std::move(result);return true;
}
void write(std::FILE* f,const std::vector<TerrainMap>& maps) {
    std::fputs(",\n  \"terrain\": {\"version\": 1, \"maps\": [",f);
    for(size_t i=0;i<maps.size();++i) {
        const auto& m=maps[i];
        std::fprintf(f,"%s\n    {\"group\":%d,\"number\":%d,\"width\":%d,\"height\":%d,\"tiles\":[",i?",":"",m.group,m.number,m.width,m.height);
        size_t j=0;for(const auto& [id,d]:m.tiles) {
            std::fprintf(f,"%s{\"id\":%u,\"attr\":%u,\"entries\":[",j++?",":"",unsigned(id),unsigned(d.attr));
            for(int k=0;k<8;++k) std::fprintf(f,"%s%u",k?",":"",unsigned(d.entries[k]));std::fputs("]}",f);
        }
        std::fputs("],\"cells\":[",f);j=0;
        for(const auto& c:m.cells) {
            std::fprintf(f,"%s\n      {\"x\":%d,\"y\":%d,\"expected\":%u,\"underlay\":%d,\"surfaces\":[",j++?",":"",c.x,c.y,unsigned(c.expected),c.underlay);
            size_t k=0;for(const auto& p:c.surfaces) {
                std::fprintf(f,"%s{\"layer\":%d,\"height\":%d,\"thickness\":%d,\"kind\":\"%s\",\"top\":%d,\"side\":%d",
                    k++?",":"",p.layer,p.height,p.thickness,p.kind==TerrainKind::Ground?"ground":p.kind==TerrainKind::Water?"water":"deck",p.top,p.side);
                if(p.rise_x) std::fprintf(f,",\"rise_x\":%d",p.rise_x);
                if(p.rise_z) std::fprintf(f,",\"rise_z\":%d",p.rise_z);
                if(p.corner_delta) std::fprintf(f,",\"corner_delta\":%d",p.corner_delta);
                if(p.side_offset) std::fprintf(f,",\"side_offset\":%d",p.side_offset);
                std::fputc('}',f);
            }
            std::fputs("]}",f);
        }
        std::fputs("]}",f);
    }
    std::fputs("\n  ]}",f);
}

const TerrainCell* Resolved::cell(int x,int y) const {
    return x<0 || y<0 || x>=width || y>=height?nullptr:cells[size_t(y)*width+x];
}
Height Resolved::query(int x,int y,int layer,float u,float v) const {
    if(x<0 || y<0 || x>=width || y>=height || layer < -1 || layer>15 ||
       !std::isfinite(u) || !std::isfinite(v) || u<0 || v<0 || u>1 || v>1) return {Status::Unresolved};
    if(mismatched[size_t(y)*width+x]) return {Status::SourceMismatch};
    const auto* c=cell(x,y);if(!c) return {};
    if(layer>0 && layer<15) {
        for(const auto& p:c->surfaces) if(p.layer==layer) return {Status::Authored,surface_height(p,u,v),&p};
        // Ruby IsZCoordMismatchAt treats source elevation 0 as neutral. A
        // single authored ground surface on that neutral cell supports a
        // crossing actor (notably ledge jumps). Never pick an unrelated deck,
        // water layer, or an author-only zero over a non-neutral source cell.
        if(c->surfaces.size()==1 && (c->expected>>12)==0) {
            const auto& p=c->surfaces.front();
            if(p.layer==0 && p.kind==TerrainKind::Ground)
                return {Status::Authored,surface_height(p,u,v),&p};
        }
        return {Status::Unresolved};
    }
    if(c->surfaces.size()==1) return {Status::Authored,surface_height(c->surfaces.front(),u,v),&c->surfaces.front()};
    return {Status::Unresolved};
}
RegionHeight query_region(const std::vector<RegionMapView>& maps,
                          double world_x, double world_z, int gameplay_layer) {
    if(!std::isfinite(world_x) || !std::isfinite(world_z) ||
       gameplay_layer < -1 || gameplay_layer > 15 || maps.empty() || maps.size()>9)
        return {};

    // Validate the entire region before selecting an owner or indexing storage.
    // Dimension bounds precede valid_connections(), which subtracts slice sizes.
    for(const auto& m:maps) {
        if(!m.source || !m.resolved || m.x < -8192 || m.x > 8192 ||
           m.z < -8192 || m.z > 8192) return {};
        const auto& s=*m.source;
        const auto& r=*m.resolved;
        if(!s.valid || s.width<16 || s.width>1024 || s.height<15 || s.height>1024 ||
           int64_t(s.width)*s.height>10240 || !s.has_map_identity()) return {};
        using Identity = world::Snapshot::IdentitySource;
        if((s.identity_source!=Identity::SourceTable && s.identity_source!=Identity::LiveCapture) ||
           !s.valid_connections()) return {};
        const size_t count=size_t(s.width)*size_t(s.height);
        if(s.grid.size()!=count || r.width!=s.width || r.height!=s.height ||
           r.cells.size()!=count || r.mismatched.size()!=count) return {};
    }
    // All dimensions/offsets are now bounded: these integer endpoints cannot
    // overflow. Strict inequalities permit shared edges/corners and padding.
    for(size_t i=0;i<maps.size();++i) for(size_t j=0;j<i;++j) {
        const auto& a=maps[i];const auto& b=maps[j];
        const auto& s=*a.source;const auto& t=*b.source;
        if(s.map_group==t.map_group && s.map_number==t.map_number) return {};
        if(std::max(a.x+7,b.x+7)<std::min(a.x+s.width-8,b.x+t.width-8) &&
           std::max(a.z+7,b.z+7)<std::min(a.z+s.height-7,b.z+t.height-7)) return {};
    }
    for(const auto& m:maps) {
        const auto& s=*m.source;
        if(world_x<m.x+7 || world_x>=m.x+s.width-8 ||
           world_z<m.z+7 || world_z>=m.z+s.height-7) continue;

        // Containment proves that floor/casts are bounded. Floor in world space
        // first: subtracting an offset from a fractional double near an edge
        // could round it into the next cell. Integer translation is exact.
        const double x=std::floor(world_x),z=std::floor(world_z);
        RegionHeight result;
        result.map_group=s.map_group;result.map_number=s.map_number;
        result.cell_x=int(x)-m.x;result.cell_y=int(z)-m.z;
        result.u=float(world_x-x);result.v=float(world_z-z);
        // Ownership is rectangular, even for undefined cells. primary_cell()
        // excludes undefined cells and would lose this diagnostic ownership.
        if(s.cell(result.cell_x,result.cell_y)==world::kGridUndefined) return result;
        result.height=m.resolved->query(result.cell_x,result.cell_y,gameplay_layer,
                                        result.u,result.v);
        return result;
    }
    return {};
}

Resolved resolve(const world::Snapshot& s,const std::vector<TerrainMap>& maps) {
    Resolved r;r.width=s.width;r.height=s.height;
    if(s.width<=0 || s.height<=0 || int64_t(s.width)*s.height>10240) {r.width=r.height=0;return r;}
    r.cells.resize(size_t(s.width)*s.height);r.mismatched.resize(r.cells.size());
    if(!s.has_map_identity() || !s.valid_connections() || s.grid.size()!=r.cells.size()) return r;
    // The last copied connection owns overlaps, exactly like FillConnection.
    std::vector<const world::ConnectionSlice*> owners(r.cells.size());
    for(const auto& c:s.connections) for(int y=c.y;y<c.y+c.h;++y) for(int x=c.x;x<c.x+c.w;++x)
        owners[size_t(y)*s.width+x]=&c;
    for(const auto& m:maps) {
        const bool current=m.group==s.map_group && m.number==s.map_number;
        if(!current && std::none_of(s.connections.begin(),s.connections.end(),[&](const auto& c){return c.group==m.group && c.number==m.number;})) continue;
        std::set<int> bad;
        for(const auto& [id,d]:m.tiles) if(!tile_matches(s,id,d)) bad.insert(id);
        auto place=[&](const TerrainCell& c,int x,int y,bool shape_ok) {
            if(x<0 || y<0 || x>=s.width || y>=s.height) {++r.rejected;return;}
            bool ok=shape_ok && s.cell(x,y)==c.expected && !bad.count(c.expected&1023);
            if(c.underlay>=0) ok &= !bad.count(c.underlay);
            for(const auto& p:c.surfaces) ok &= !bad.count(p.top) && !bad.count(p.side);
            const size_t i=size_t(y)*s.width+x;r.cells[i]=ok?&c:nullptr;r.mismatched[i]=!ok;
        };
        if(current) for(const auto& c:m.cells)
            place(c,c.x,c.y,m.width==s.width && m.height==s.height && primary_cell(s,c.x,c.y));
        for(const auto& copy:s.connections) if(copy.group==m.group && copy.number==m.number) for(const auto& c:m.cells) {
            if(c.x<copy.source_x || c.y<copy.source_y || c.x>=copy.source_x+copy.w || c.y>=copy.source_y+copy.h) continue;
            const int x=c.x-copy.source_x+copy.x,y=c.y-copy.source_y+copy.y;
            if(owners[size_t(y)*s.width+x]!=&copy) continue;
            place(c,x,y,copy.width==m.width && copy.height==m.height && copy.compatible_art);
        }
    }
    size_t budget=0;
    for(const auto* c:r.cells) if(c) for(const auto& p:c->surfaces) budget+=surface_budget(p);
    // Combining otherwise valid maps must not multiply the visible allocation
    // bound. Refuse the complete visible terrain rather than clip a plateau.
    if(budget>2000000) for(size_t i=0;i<r.cells.size();++i) if(r.cells[i]) {r.cells[i]=nullptr;r.mismatched[i]=true;}
    for(size_t i=0;i<r.cells.size();++i) {r.matched+=r.cells[i]!=nullptr;r.rejected+=r.mismatched[i];}
    return r;
}
}
