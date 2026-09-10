// Storage for the region path of the shared authored mesher. Rigid models are
// stored once per pattern per source atlas. Draw ranges retain claim order:
// grouping non-adjacent objects could change coplanar pixel/depth ownership.
struct MeshOffset { float x,y,z; };
struct MeshDraw { size_t first=0,count=0,instance_first=0,instances=0; };
struct PlacedMesh {
    std::vector<MeshDraw> draws;
    std::vector<MeshOffset> offsets;
    size_t models=0,deformed_instances=0;
    size_t vertex_limit=8000000,expanded_limit=16000000;

    void direct(size_t first,size_t count) {
        if(!count)return;
        if(!draws.empty() && !draws.back().instances && draws.back().first+draws.back().count==first)
            draws.back().count+=count;
        else draws.push_back({first,count,0,0});
    }
    void instance(const MeshDraw& model,MeshOffset offset) {
        if(!draws.empty() && draws.back().instances && draws.back().first==model.first &&
           draws.back().instance_first+draws.back().instances==offsets.size())++draws.back().instances;
        else draws.push_back({model.first,model.count,offsets.size(),1});
        offsets.push_back(offset);
    }
};

// Enumerate the same ordered world vertices as the original expanded mesh,
// without retaining a full copy. Hashes, bounds and native audits use this.
// Keep placement then region additions in the same order as the vertex shader.
template<class Visitor>
void visit_placed(const std::vector<Vertex>& mesh,const PlacedMesh& placed,int x,int z,Visitor visit) {
    for(const auto& draw:placed.draws)for(size_t i=0;i<std::max(size_t(1),draw.instances);++i)
        for(size_t k=draw.first;k<draw.first+draw.count;++k) {
            auto v=mesh[k];
            if(draw.instances) {const auto& p=placed.offsets[draw.instance_first+i];v.x+=p.x;v.y+=p.y;v.z+=p.z;}
            v.x+=x;v.z+=z;visit(v);
        }
}
