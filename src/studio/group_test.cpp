#include "group.h"
#include "pattern_io.h"
#include "diorama.h"
#include "cutout.h"
#include "room2d.h"
#include "part_geometry.h"
#include "mesh_audit.h"
#include <cmath>
#include <algorithm>
#include <limits>
#include <cstdio>
#include <set>

namespace studio {

#include "voxel_test.inl"

int document_selftest(const vr::world::Snapshot& s, const char* output) {
    using namespace vr::overrides;
    int count = 0;
    auto check = [&](bool ok, const char* reason) {
        if (ok) ++count;
        else std::fprintf(stderr, "[document-selftest] FAIL: %s\n", reason);
        return ok;
    };
    {
        using namespace vr::part_geometry;
        using Polygon = vr::part_geometry::Polygon;
        const Vec asymmetric{1,2,3};
        if (!check(rotate(asymmetric,{90,0,0})==Vec{1,-3,2} &&
                   rotate(asymmetric,{0,90,0})==Vec{3,2,-1} &&
                   rotate(asymmetric,{0,0,90})==Vec{-2,1,3},
                   "part quarter turns preserve asymmetric axes exactly")) return -1;
        Transform t{{5,-2,7},{2,3,4},{31,-47,63}};
        const Vec back=to_local(to_group(asymmetric,t),t);
        if (!check(std::abs(back.x-1)<1e-5f && std::abs(back.y-2)<1e-5f && std::abs(back.z-3)<1e-5f,
                   "part inverse reverses all three ordered rotations and translation") ||
            !check(rotate(asymmetric,{90,90,90})==Vec{3,2,-1}, "rotation order is X then Y then Z")) return -1;
        auto bad=t; bad.size.x=-1;
        if (!check(valid(t) && !valid(bad), "negative part size refused")) return -1;
        bad=t; bad.angles.y=std::numeric_limits<float>::infinity();
        if (!check(!valid(bad), "nonfinite part transform refused")) return -1;
        const auto cube=prism({0,0,0},{1,1,1},{});
        const Polygon front={{0,0,1},{1,0,1},{1,1,1},{0,1,1}};
        auto rear=front; std::reverse(rear.begin(),rear.end());
        if (!check(subtract(front,cube,true)==std::vector<Polygon>{front} &&
                   subtract(front,cube,false).empty(), "coincident outer face has exactly one material owner") ||
            !check(subtract(rear,cube,true).empty(), "opposite adjoining face is removed")) return -1;
        const Polygon covered={{.2f,.2f,.5f},{.8f,.2f,.5f},{.8f,.8f,.5f},{.2f,.8f,.5f}};
        if (!check(subtract(covered,cube,true).empty(), "buried face removed")) return -1;
        const Polygon wide={{-.5f,0,.5f},{1.5f,0,.5f},{1.5f,1,.5f},{-.5f,1,.5f}};
        const auto split=subtract(wide,cube,false);
        float area=0;
        for (const auto& poly:split) for(size_t i=1;i+1<poly.size();++i)
            area+=std::abs(cross(poly[i]-poly[0],poly[i+1]-poly[0]).z)*.5f;
        if (!check(split.size()==2 && std::abs(area-1)<1e-6f,
                   "partial solid overlap splits a face and preserves exact exposed area")) return -1;
    }
    vr::diorama::ObjectModel model;
    vr::diorama::segment(s, {}, &model);
    Pattern object, cells;
    if (!check(pattern_io::from_object(s, model, 21, 0, "house", &object), "derive house")) return -1;
    std::vector<pattern_io::Cell> membership;
    for (int y = 0; y < 4; ++y) for (int x = 21; x < 25; ++x) membership.push_back({x, y});
    membership.push_back({21, 0}); // a duplicate brush visit is not a second cell
    if (!check(pattern_io::from_cells(s, membership, "house", &cells), "derive arbitrary membership")) return -1;
    cells.apply = object.apply;
    if (!check(cells == object, "arbitrary membership equals the segmented definition") ||
        !check(!pattern_io::from_cells(s, {}, "empty", &cells), "empty applied group refused") ||
        !check(!pattern_io::from_cells(s, {{-1, 0}}, "outside", &cells), "out-of-map membership refused")) return -1;

    // A sparse group must not claim its bounding-box holes or a neighbouring object.
    Pattern sparse;
    if (!check(pattern_io::from_cells(s, {{21,0},{24,0},{21,3},{24,3}}, "sparse", &sparse),
               "derive disjoint membership")) return -1;
    sparse.apply.cls = ApplyClass::kStructure;
    vr::diorama::ObjectModel sparse_model;
    OverrideSet sparse_set; sparse_set.patterns.push_back(sparse);
    vr::diorama::segment(s, sparse_set, &sparse_model);
    const int owner = sparse_model.object_at(21, 0);
    if (!check(owner >= 0 && sparse_model.object_at(24,0) == owner &&
               sparse_model.object_at(21,3) == owner && sparse_model.object_at(24,3) == owner &&
               sparse_model.object_at(22,1) != owner && sparse_model.object_at(20,0) != owner,
               "production segmentation claims only member cells")) return -1;

    auto repeated = s;
    repeated.width = 5; repeated.height = 1; repeated.grid = {620,621,0,620,621};
    Pattern repeat;
    if (!check(pattern_io::from_cells(repeated, {{0,0},{1,0}}, "repeated", &repeat), "build repeat fixture")) return -1;
    OverrideSet repeated_set; repeated_set.patterns.push_back(repeat);
    if (!check(find(repeated, repeated_set).size() == 2, "two structurally verified matches")) return -1;
    repeated.grid[4] = 622;
    if (!check(find(repeated, repeated_set).size() == 1, "changed required cell removes one match")) return -1;

    repeated_set.patterns.push_back(repeat);
    const auto overlaps = resolve(repeated, repeated_set);
    if (!check(overlaps.accepted.size() == 1 && overlaps.rejected.size() == 1 &&
               overlaps.accepted[0].pattern == 0 && overlaps.rejected[0].pattern == 1 &&
               overlaps.owner == std::vector<int>({0,0,-1,-1,-1}),
               "shared overlap resolution reports accepted claims, rejected matches and ownership")) return -1;

    Document document;
    document.state.has_draft = true;
    document.state.draft = object;
    document.state.draft.id = document.new_id();
    document.state.draft.source = {"MAP_ROUTE101", 21, 0};
    document.state.draft_base = document.state.draft;
    const auto original = document.state;
    document.state.draft.name = "roof \"east\"\\wall\ncutout";
    document.record(original);
    const auto edited = document.state;
    document.state.working.patterns.push_back(document.state.draft);
    document.state.draft_slot = 0;
    document.state.draft_base = document.state.draft;
    document.record(edited);
    const auto applied = document.state;
    if (!check(document.undo() && document.state == edited && document.draft_dirty(), "undo Apply restores exact dirty draft") ||
        !check(document.undo() && document.state == original, "undo edit restores exact document") ||
        !check(document.redo() && document.state == edited && document.redo() && document.state == applied,
               "redo restores exact applied state") ||
        !check(document.unsaved() && !document.draft_dirty(), "saved and applied markers are distinct")) return -1;

    const std::string path = std::string(output) + ".document-test.json";
    OverrideSet reloaded;
    if (!check(pattern_io::write(path.c_str(), document.state.working), "atomic save") ||
        !check(load(path.c_str(), &reloaded) && reloaded == document.state.working,
               "round trip preserves stable ID, source and escaped name")) return -1;
    auto invalid = document.state.working;
    invalid.patterns[0].mask[0] = 2;
    if (!check(!pattern_io::write(path.c_str(), invalid), "invalid membership cannot replace saved file") ||
        !check(load(path.c_str(), &reloaded) && reloaded == document.state.working,
               "failed write preserves previous file")) return -1;
    invalid = document.state.working;
    invalid.patterns.push_back(invalid.patterns[0]);
    if (!check(!pattern_io::write(path.c_str(), invalid), "duplicate stable IDs refused")) return -1;

    const std::string broken_path = path + ".invalid";
    std::FILE* f = std::fopen(broken_path.c_str(), "wb");
    if (!check(f != nullptr, "create invalid-format fixture")) return -1;
    std::fputs("{\"version\":1,\"patterns\":[]}", f); std::fclose(f);
    const auto previous = reloaded;
    if (!check(!load(broken_path.c_str(), &reloaded) && reloaded == previous,
               "version refusal preserves destination document")) return -1;
    OverrideSet empty;
    if (!check(pattern_io::write(path.c_str(), empty) && load(path.c_str(), &reloaded) && reloaded.empty(),
               "an intentionally empty document round trips")) return -1;

    document.undo();
    const auto before_branch = document.state;
    document.state.draft.name = "new branch";
    document.record(before_branch);
    if (!check(!document.redo() && document.redo_count() == 0, "editing after undo clears redo")) return -1;
    for (int i = 0; i < 140; ++i) {
        const auto before = document.state;
        document.state.draft.name = std::to_string(i);
        document.record(before);
    }
    if (!check(document.undo_count() == 128, "history is bounded at 128 transactions")) return -1;

    vr::cutout::Art art;
    Room2D room;
    if (!check(vr::cutout::compose(s, object, &art) && room.build(s), "compose full house cutout art")) return -1;
    bool same_art = art.w == 64 && art.h == 64;
    for (int y = 0; y < art.h; ++y) for (int x = 0; x < art.w; ++x)
        same_art = same_art && art.pixels[size_t(y) * art.w + x].rgba == room.rgba[size_t(y) * room.w + 21*16 + x];
    if (!check(same_art, "cutout composite matches the existing two-pass room renderer pixel-for-pixel")) return -1;
    object.cutout = vr::cutout::original_opacity(art);
    object.cutout->opacity[17] = 0;
    object.cutout->opacity[18] = 0;
    Cutout decoded;
    const auto runs = vr::cutout::encode(*object.cutout);
    if (!check(vr::cutout::decode(64,64,object.cutout->opacity.front(),runs,&decoded) && decoded == *object.cutout,
               "binary RLE exactly preserves painted holes")) return -1;
    const auto unchanged = decoded;
    if (!check(!vr::cutout::decode(64,64,0,{0,4096},&decoded) &&
               !vr::cutout::decode(64,64,0,{4097},&decoded) &&
               !vr::cutout::decode(64,64,0,{4095},&decoded) &&
               !vr::cutout::decode(64,64,2,{4096},&decoded) &&
               !vr::cutout::decode(1025,64,0,{65600},&decoded) && decoded == unchanged,
               "malformed or oversized RLE fails before changing its destination")) return -1;
    if (!check(vr::cutout::compose(s,sparse,&art), "compose disjoint membership art")) return -1;
    sparse.cutout = vr::cutout::original_opacity(art);
    if (!check(vr::cutout::valid(sparse) && !sparse.cutout->opacity[17*64+17], "nonmember cells stay transparent")) return -1;
    object.source.room = "MAP_ROUTE101";
    OverrideSet masked; masked.patterns.push_back(object);
    const std::string mask_path = std::string(output) + ".mask-test.json";
    if (!check(pattern_io::write(mask_path.c_str(),masked) && load(mask_path.c_str(),&reloaded) && masked == reloaded,
               "JSON preserves cutout dimensions and exact opacity")) return -1;
    masked.patterns[0].cutout->opacity[0] = 2;
    if (!check(!pattern_io::write(mask_path.c_str(),masked), "nonbinary source opacity is refused before RLE encoding")) return -1;
    sparse.cutout->opacity[17*64+17] = 1;
    masked.patterns[0] = sparse;
    if (!check(!pattern_io::write(mask_path.c_str(),masked), "opaque nonmember pixels cannot be saved")) return -1;
    object.cutout->opacity.assign(4096,0); masked.patterns[0] = object;
    if (!check(pattern_io::write(mask_path.c_str(),masked) && load(mask_path.c_str(),&reloaded) &&
               reloaded.patterns[0].cutout && reloaded.patterns[0].cutout->opacity == std::vector<uint8_t>(4096,0),
               "a present all-transparent cutout survives persistence")) return -1;
    Part box; box.id="part-a"; box.name="Asymmetric box";
    box.transform={{.25f,1.5f,-2},{2,3,4},{31,-47,63}};
    Part billboard; billboard.id="part-b"; billboard.name="Cutout";billboard.kind=PartKind::Billboard;
    billboard.transform.size={4,4,.125f};
    masked.patterns[0].parts={box,billboard}; masked.patterns[0].model_seeded=true;
    if (!check(pattern_io::write(mask_path.c_str(),masked) && load(mask_path.c_str(),&reloaded) && reloaded==masked,
               "v4 round trip preserves ordered parts, stable IDs, transforms and seeding state")) return -1;
    const auto valid_parts=masked;
    masked.patterns[0].parts[1].id=box.id;
    if(!check(!pattern_io::write(mask_path.c_str(),masked),"duplicate part IDs refused")) return -1;
    masked=valid_parts;masked.patterns[0].parts[0].transform.size.y=0;
    if(!check(!pattern_io::write(mask_path.c_str(),masked),"zero part size refused")) return -1;
    masked=valid_parts;masked.patterns[0].parts[0].transform.position.x=129;
    if(!check(!pattern_io::write(mask_path.c_str(),masked),"out-of-range part position refused")) return -1;
    masked=valid_parts;masked.patterns[0].parts[0].kind=static_cast<PartKind>(99);
    if(!check(!pattern_io::write(mask_path.c_str(),masked),"unknown part kind refused")) return -1;
    masked=valid_parts;masked.patterns[0].cutout.reset();
    if(!check(!pattern_io::write(mask_path.c_str(),masked),"billboard without cutout refused") ||
       !check(load(mask_path.c_str(),&reloaded) && reloaded==valid_parts,"invalid parts never replace the valid saved document")) return -1;
    {
        // Four differently coloured quadrants, unequal physical dimensions and a
        // transparent upper pass. No game-art assumptions enter this material oracle.
        auto coloured=s;coloured.width=1;coloured.height=1;coloured.grid={0};
        for(int q=0;q<4;++q) {
            coloured.metatiles[size_t(q)]=uint16_t(900+q);
            std::fill_n(coloured.vram_tiles.begin()+(900+q)*32,32,uint8_t((q+1)*17));
            coloured.metatiles[size_t(q+4)]=904;
        }
        std::fill_n(coloured.vram_tiles.begin()+904*32,32,0);
        coloured.bg_palette[1]=31;coloured.bg_palette[2]=31<<5;
        coloured.bg_palette[3]=31<<10;coloured.bg_palette[4]=31|(31<<5);
        Pattern coloured_pattern;
        if(!check(pattern_io::from_cells(coloured,{{0,0}},"Four colours",&coloured_pattern),"construct independent coloured fixture")) return -1;
        const int voxel_checks=voxel_selftest(coloured,coloured_pattern,output);
        if(voxel_checks<0) return -1;count+=voxel_checks;
        Part part;part.id="coloured-box";part.name="Unequal axes";part.transform.size={2,3,4};
        coloured_pattern.parts={part};
        std::vector<vr::diorama::AuthoredVertex> mesh;
        if(!check(vr::diorama::inspect_authored_mesh(coloured,coloured_pattern,&mesh) && mesh.size()==6*16*16*6,
                  "box material fixture contains six subdivided faces")) return -1;
        bool material=true;
        for(size_t i=0;i<mesh.size();i+=3) {
            using namespace vr::part_geometry;
            const auto centre=(mesh[i].position+mesh[i+1].position+mesh[i+2].position)*(1.f/3);
            const auto n=cross(mesh[i+2].position-mesh[i].position,mesh[i+1].position-mesh[i].position);
            float x=0,y=0;
            if(n.z>0) { x=centre.x/2;y=1-centre.y/3; }
            else if(n.z<0) { x=1-centre.x/2;y=1-centre.y/3; }
            else if(n.x>0) { x=(2-centre.z)/4;y=1-centre.y/3; }
            else if(n.x<0) { x=(centre.z+2)/4;y=1-centre.y/3; }
            else if(n.y>0) { x=centre.x/2;y=(centre.z+2)/4; }
            else { x=centre.x/2;y=(2-centre.z)/4; }
            const int expected=900+(x>=.5f?1:0)+(y>=.5f?2:0);
            for(int k=0;k<3;++k) material &= mesh[i+k].tile==expected && mesh[i+k].palette==0;
        }
        if(!check(material,"all six box faces preserve independently specified coloured quadrant orientation")) return -1;
        coloured_pattern.parts[0].art_region={8,0,8,8};
        std::vector<vr::diorama::AuthoredVertex> cropped;
        bool crop_ok=vr::diorama::inspect_authored_mesh(coloured,coloured_pattern,&cropped) && cropped.size()==mesh.size();
        if(crop_ok) for(size_t i=0;i<cropped.size();++i)
            crop_ok &= cropped[i].tile==901 && cropped[i].position==mesh[i].position;
        if(!check(crop_ok,"art region samples only the chosen colour quadrant without altering geometry")) return -1;
        OverrideSet crop_set;crop_set.patterns={coloured_pattern};
        const auto crop_path=std::string(output)+".art-region-test.json";
        if(!check(pattern_io::write(crop_path.c_str(),crop_set) && load(crop_path.c_str(),&reloaded) && reloaded==crop_set,
                  "source art region round trips exactly")) return -1;
        crop_set.patterns[0].parts[0].art_region={8,0,9,8};
        if(!check(!pattern_io::write(crop_path.c_str(),crop_set),"out-of-bounds source art region is refused")) return -1;
        coloured_pattern.parts[0].art_region={};
        for(int axis=0;axis<3;++axis) {
            auto& t=coloured_pattern.parts[0].transform;t.position={5,6,7};
            t.angles=axis==0?vr::part_geometry::Vec{90,0,0}:axis==1?vr::part_geometry::Vec{0,90,0}:vr::part_geometry::Vec{0,0,90};
            std::vector<vr::diorama::AuthoredVertex> turned;
            bool correct=vr::diorama::inspect_authored_mesh(coloured,coloured_pattern,&turned) && turned.size()==mesh.size();
            if(correct) for(size_t i=0;i<mesh.size();++i) {
                const auto p=mesh[i].position;
                const vr::part_geometry::Vec expected=axis==0?vr::part_geometry::Vec{p.x+5,-p.z+6,p.y+7}:
                    axis==1?vr::part_geometry::Vec{p.z+5,p.y+6,-p.x+7}:vr::part_geometry::Vec{-p.y+5,p.x+6,p.z+7};
                correct &= turned[i].position==expected && turned[i].tile==mesh[i].tile && turned[i].u==mesh[i].u && turned[i].v==mesh[i].v;
            }
            if(!check(correct,"production vertices rotate and translate asymmetric art without changing material coordinates")) return -1;
        }
        {
            // A facade has fewer vertical texels than its backing wall. Their
            // adjoining edges must meet even though the sampling differs.
            auto joined=coloured_pattern;
            joined.cutout=Cutout{16,16,std::vector<uint8_t>(256,0)};
            std::fill(joined.cutout->opacity.begin()+16*10,joined.cutout->opacity.end(),1);
            Part wall;wall.id="wall";wall.transform.size={2,1.125f,4};
            Part facade;facade.id="facade";facade.kind=PartKind::Billboard;
            facade.transform.position={0,0,2};facade.transform.size={2,3,.0625f};
            joined.parts={wall,facade};
            std::vector<vr::diorama::AuthoredVertex> joined_mesh;
            if(!check(vr::diorama::inspect_authored_mesh(coloured,joined,&joined_mesh),"emit unequal wall and facade sampling through shared mesher")) return -1;
            const auto seam=audit_mesh(joined_mesh);
            if(!check(seam.closed_connected() && std::abs(seam.volume-9.0703125)<1e-6,
                "unequal wall and facade grids join without T junctions or volume change")) return -1;
            joined.parts[1].transform.position.z+=.125f;
            vr::diorama::inspect_authored_mesh(coloured,joined,&joined_mesh);
            if(!check(audit_mesh(joined_mesh).components==2,"edge conformity does not bridge a real gap between parts")) return -1;
        }
        coloured_pattern.w=64;coloured_pattern.extent=64;
        if(!check(!vr::overrides::valid_parts(coloured_pattern),"model work bound rejects oversized authored definitions before allocation")) return -1;
    }
    {
        Pattern roof=object;roof.cutout.reset();roof.parts.clear();
        Part left;left.id="roof-left";left.name="Left slope";left.kind=PartKind::Wedge;left.transform.size={2,1,4};
        Part right=left;right.id="roof-right";right.name="Right slope";right.transform.position.x=2;right.wedge_direction=-1;
        roof.parts={left,right};
        std::vector<vr::diorama::AuthoredVertex> mesh;
        if(!check(vr::diorama::inspect_authored_mesh(s,roof,&mesh),"build wedge roof through production emitter")) return -1;
        const auto audit=audit_mesh(mesh);
        std::fprintf(stdout,"[roof-audit] triangles=%zu boundary=%zu nonmanifold=%zu winding=%zu degenerate=%zu duplicate=%zu components=%zu volume=%.9g\n",
            audit.triangles,audit.boundary,audit.nonmanifold,audit.winding,audit.degenerate,audit.duplicate,audit.components,audit.volume);
        if(!check(audit.closed_connected() && std::abs(audit.volume-8)<1e-5,"wedge-built roof is one closed solid with analytic volume eight")) return -1;
        auto damaged=mesh;damaged.erase(damaged.begin(),damaged.begin()+3);
        const auto missing=audit_mesh(damaged);
        if(!check(!missing.closed_connected() && missing.boundary>0,"roof verifier rejects deliberately missing surface triangle")) return -1;
        damaged=mesh;damaged.insert(damaged.end(),mesh.begin(),mesh.begin()+3);
        if(!check(audit_mesh(damaged).duplicate>0,"roof verifier rejects duplicate faces")) return -1;
        damaged=mesh;damaged[0]=damaged[1];
        if(!check(audit_mesh(damaged).degenerate>0,"roof verifier rejects degenerate faces")) return -1;
        roof.parts[1].transform.position.x+=.0625f;
        vr::diorama::inspect_authored_mesh(s,roof,&mesh);
        const auto fins=audit_mesh(mesh);
        if(!check(!fins.closed_connected() && fins.components==2,"roof verifier rejects separated fins despite each fin being closed")) return -1;
        roof.parts[1].transform.position.x-=.0625f;
        OverrideSet roofs;roofs.patterns={roof};
        const auto roof_path=std::string(output)+".wedge-test.json";
        if(!check(pattern_io::write(roof_path.c_str(),roofs) && load(roof_path.c_str(),&reloaded) && reloaded==roofs,
                  "v5 round trip preserves wedge axes and directions")) return -1;
        roofs.patterns[0].parts[0].wedge_axis=1;
        if(!check(!pattern_io::write(roof_path.c_str(),roofs),"unsupported wedge axis is refused")) return -1;
        roof.parts={left};roof.parts[0].wedge_axis=2;
        for(int direction:{-1,1}) {
            roof.parts[0].wedge_direction=direction;
            vr::diorama::inspect_authored_mesh(s,roof,&mesh);
            const auto z_audit=audit_mesh(mesh);
            bool high_edge=false,below_plane=true;
            for(const auto& vertex:mesh) {
                const auto p=vertex.position;
                below_plane &= p.y<=.5f+direction*p.z/4+1e-5f;
                high_edge |= std::abs(p.y-1)<1e-5f && std::abs(p.z-direction*2)<1e-5f;
            }
            if(!check(z_audit.closed_connected() && std::abs(z_audit.volume-4)<1e-5 && high_edge && below_plane,
                      "Z-axis wedge direction produces the specified closed slope and volume")) return -1;
        }
    }
    {
        using vr::diorama::AuthoredVertex;using vr::diorama::DioramaStats;
        std::vector<AuthoredVertex> flat,paper;DioramaStats flat_stats,stats;
        if(!check(vr::diorama::inspect_diorama_mesh(s,{},&flat,&flat_stats),"build independent flat-only DIORAMA reference")) return -1;
        size_t defined=0;for(int y=0;y<s.height;++y) for(int x=0;x<s.width;++x) if(s.cell(x,y)!=vr::world::kGridUndefined) ++defined;
        if(!check(flat_stats.raised_instances==0 && flat_stats.accepted_instances==0 && flat_stats.part_instances==0 &&
                  flat_stats.authored_vertices==0 && flat.size()==defined*48 &&
                  flat_stats.flat_vertices==flat.size(),"flat reference has only two paper-art layers per defined cell")) return -1;
        std::fprintf(stdout,"[diorama-flat] geom=%016llx vertices=%zu\n",(unsigned long long)flat_stats.geometry_hash,flat.size());
        Pattern membership=object;membership.cutout.reset();membership.parts.clear();membership.apply.height=200;
        OverrideSet authored;authored.patterns={membership};
        if(!check(vr::diorama::inspect_diorama_mesh(s,authored,&paper,&stats) && stats.geometry_hash==flat_stats.geometry_hash &&
                  stats.raised_instances==0 && stats.accepted_instances==1 && stats.part_instances==0,
                  "membership and inference parameters alone leave the exact flat reference")) return -1;
        Pattern sparse_paper;
        if(!check(pattern_io::from_cells(s,{{21,0},{24,0},{21,3},{24,3}},"paper corners",&sparse_paper),"derive sparse DIORAMA fixture")) return -1;
        vr::cutout::Art sparse_art;vr::cutout::compose(s,sparse_paper,&sparse_art);
        sparse_paper.cutout=vr::cutout::original_opacity(sparse_art);sparse_paper.cutout->opacity[1]=0;
        authored.patterns={sparse_paper};
        if(!check(vr::diorama::inspect_diorama_mesh(s,authored,&paper,&stats) && stats.raised_instances==1 && stats.authored_vertices>0,
                  "sparse authored cutout raises exactly one accepted rendered instance")) return -1;
        size_t offset=0;bool art_correct=true;
        for(int y=0;y<s.height;++y) for(int x=0;x<s.width;++x) {
            if(s.cell(x,y)==vr::world::kGridUndefined) continue;
            const bool claimed=(x==21 || x==24) && (y==0 || y==3);
            const auto tile=claimed?stats.ground_tile:s.metatile_id(x,y);
            for(int k=0;k<8;++k) {
                const auto expected=vr::world::unpack_tile_entry(s.metatiles[size_t(tile)*8+k]);
                for(int v=0;v<6;++v) {
                    const auto& vertex=paper[offset+size_t(k)*6+v];
                    art_correct &= vertex.tile==expected.index && vertex.palette==expected.palette &&
                        vertex.position.y==(k/4)*.002f && vertex.position.x>=x && vertex.position.x<=x+1 &&
                        vertex.position.z>=y && vertex.position.z<=y+1;
                }
            }
            offset+=48;
        }
        if(!check(art_correct && offset==stats.flat_vertices,"claimed cells recover ground while holes and every unclaimed cell retain original flat art")) return -1;
        std::vector<AuthoredVertex> local;
        vr::diorama::inspect_authored_mesh(s,sparse_paper,&local);
        if(!check(local.size()==stats.authored_vertices && stats.geometry_hash!=flat_stats.geometry_hash,
                  "DIORAMA appends the shared cutout exactly once")) return -1;
        authored.patterns[0].cutout->opacity.assign(authored.patterns[0].cutout->opacity.size(),0);
        if(!check(vr::diorama::inspect_diorama_mesh(s,authored,&paper,&stats) && stats.raised_instances==0 &&
                  stats.authored_vertices==0 && stats.accepted_instances==1 && stats.part_instances==0 && stats.geometry_hash!=flat_stats.geometry_hash,
                  "empty cutout removes its flat source art and raises no instance")) return -1;
        auto repeated=s;repeated.width=3;repeated.height=1;
        repeated.grid={s.metatile_id(21,0),vr::world::kGridUndefined,s.metatile_id(21,0)};
        Pattern repeat;pattern_io::from_cells(repeated,{{0,0}},"repeat paper",&repeat);
        vr::cutout::Art art;vr::cutout::compose(repeated,repeat,&art);repeat.cutout=vr::cutout::original_opacity(art);
        repeat.parts={{"repeated-billboard","Repeated billboard",PartKind::Billboard,{{0,0,0},{1,1,.125f},{0,0,0}}}};
        authored.patterns={repeat};
        if(!check(vr::diorama::inspect_diorama_mesh(repeated,authored,&paper,&stats) && stats.raised_instances==2 &&
                  stats.accepted_instances==2 && stats.part_instances==2,
                  "one definition counts two rendered placements and two placed part entries")) return -1;
        authored.patterns.push_back(repeat);
        if(!check(vr::diorama::inspect_diorama_mesh(repeated,authored,&paper,&stats) && stats.raised_instances==2 &&
                  stats.accepted_instances==2 && stats.part_instances==2 && resolve(repeated,authored).rejected.size()==2,
                  "rejected overlapping candidates do not inflate instance or placed-part counts")) return -1;
    }
    return count;
}

} // namespace studio
