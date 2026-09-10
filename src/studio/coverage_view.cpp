#include "coverage_view.h"
#include "json_scan.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <set>
#include <stdexcept>

namespace studio::coverage {
namespace {
using vr::json::Value;

std::string text(const Value& object, const char* key, size_t limit=4096) {
    const auto* v=object.find(key);
    if(!v || v->type!=Value::Type::String || v->string.size()>limit)
        throw std::runtime_error(std::string("Invalid review field: ")+key);
    return v->string;
}
int number(const Value& object,const char* key,int max=1000000) {
    const auto* v=object.find(key);
    if(!v || v->type!=Value::Type::Number || !std::isfinite(v->number) ||
       v->number<0 || v->number>max || std::floor(v->number)!=v->number)
        throw std::runtime_error(std::string("Invalid review number: ")+key);
    return int(v->number);
}
const std::vector<Value>& array(const Value& object,const char* key,size_t max) {
    const auto* v=object.find(key);
    if(!v || !v->is_array() || v->items.size()>max)
        throw std::runtime_error(std::string("Invalid review list: ")+key);
    return v->items;
}
bool map_id(const std::string& s) {
    return s.size()>4 && s.size()<160 && s.rfind("MAP_",0)==0 &&
        std::all_of(s.begin(),s.end(),[](char c){return (c>='A'&&c<='Z') || (c>='0'&&c<='9') || c=='_';});
}
Value parse(const std::filesystem::path& path) {
    std::error_code error;
    const auto bytes=std::filesystem::file_size(path,error);
    if(error || bytes>64*1024*1024) throw std::runtime_error("Review file missing or too large. Export the ledger again.");
    Value v;
    if(!vr::json::parse_file(path.string().c_str(),&v) || !v.is_object() || number(v,"version")!=1)
        throw std::runtime_error("Unsupported or malformed review snapshot.");
    return v;
}
std::string lower(std::string s) {
    for(char& c:s) if(c>='A'&&c<='Z') c=char(c-'A'+'a');
    return s;
}
} // namespace

bool load_index(const std::string& path,Index* out,std::string* error) {
    try {
        const auto v=parse(path);
        Index next;next.path=path;
        next.directory=text(v,"directory",64);
        if(next.directory.rfind("snapshot-",0)!=0 || next.directory.size()!=33 ||
           !std::all_of(next.directory.begin()+9,next.directory.end(),[](char c){return (c>='a'&&c<='f')||(c>='0'&&c<='9');}))
            throw std::runtime_error("Invalid review snapshot directory.");
        next.created_at=text(v,"created_at",128);
        next.pack_fingerprint=text(v,"pack_fingerprint",16);
        next.source_version=text(v,"source_version",128);
        if(next.pack_fingerprint.size()!=16) throw std::runtime_error("Missing review pack identity.");
        std::set<std::string> seen;
        for(const auto& m:array(v,"maps",4096)) {
            Map map;map.id=text(m,"id",160);
            map.rows=number(m,"rows");map.failed=number(m,"failed");map.unmodeled=number(m,"unmodeled");
            if(!map_id(map.id) || !seen.insert(map.id).second || map.failed>map.rows || map.unmodeled>map.rows)
                throw std::runtime_error("Invalid or duplicate review map.");
            next.maps.push_back(std::move(map));
        }
        if(next.maps.empty()) throw std::runtime_error("Review snapshot has no maps.");
        *out=std::move(next);error->clear();return true;
    } catch(const std::exception& e) { *error=e.what();return false; }
}

bool load_room(const Index& index,const std::string& map,Room* out,std::string* error) {
    try {
        const auto found=std::find_if(index.maps.begin(),index.maps.end(),[&](const auto& m){return m.id==map;});
        if(!map_id(map) || found==index.maps.end()) throw std::runtime_error("Map is not in this review snapshot.");
        const auto base=std::filesystem::path(index.path).parent_path()/index.directory;
        const auto v=parse(base/(map+".json"));
        Room next;next.map=text(v,"map",160);next.notes=text(v,"notes",65536);
        next.width=number(v,"width",8192);next.height=number(v,"height",8192);
        if(next.map!=map || !next.width || !next.height) throw std::runtime_error("Review map identity or bounds changed.");
        const auto& rows=array(v,"rows",100000);
        if(rows.size()!=size_t(found->rows)) throw std::runtime_error("Incomplete review snapshot. Reload after exporting.");
        std::set<std::string> seen;
        for(const auto& r:rows) {
            Row row;
            row.id=text(r,"id");row.label=text(r,"label");row.kind=text(r,"kind",32);
            row.category=text(r,"category",32);row.boundary=text(r,"boundary",32);row.model=text(r,"model",160);
            row.implementation=text(r,"implementation",32);row.disposition=text(r,"disposition",32);
            row.visual=text(r,"visual",32);row.live=text(r,"live",32);row.headset=text(r,"headset",32);
            row.notes=text(r,"notes",65536);
            row.x=number(r,"x",8192);row.y=number(r,"y",8192);
            row.w=number(r,"w",8192);row.h=number(r,"h",8192);
            row.anchor_x=number(r,"anchor_x",8192);row.anchor_y=number(r,"anchor_y",8192);
            row.flags=number(r,"flags",15);
            if(!row.w || !row.h || row.anchor_x<row.x || row.anchor_y<row.y ||
               row.anchor_x>=row.x+row.w || row.anchor_y>=row.y+row.h ||
               row.anchor_x>=next.width || row.anchor_y>=next.height || !seen.insert(row.id).second ||
               (row.kind!="placement" && row.kind!="instance" && row.kind!="export_gap"))
                throw std::runtime_error("Invalid review placement or duplicate identity.");
            next.rows.push_back(std::move(row));
        }
        *out=std::move(next);error->clear();return true;
    } catch(const std::exception& e) { *error=e.what();return false; }
}

std::string file_fingerprint(const std::string& path) {
    std::ifstream in(path,std::ios::binary);if(!in) return {};
    uint64_t hash=14695981039346656037ull;
    char bytes[8192];
    while(in) {in.read(bytes,sizeof(bytes));for(std::streamsize i=0;i<in.gcount();++i) {hash^=uint8_t(bytes[i]);hash*=1099511628211ull;}}
    if(!in.eof()) return {};
    char value[17];std::snprintf(value,sizeof(value),"%016llx",static_cast<unsigned long long>(hash));return value;
}

bool matches(const Row& row,int filter,bool include_flat,bool include_padding,const std::string& search) {
    const int flags[]={0,1,2,4,8};
    if(filter<0 || filter>4 || (filter && !(row.flags&flags[filter]))) return false;
    if(!include_flat && (row.category=="flat" || row.category=="ground_mask")) return false;
    if(!include_padding && row.boundary!="map_body") return false;
    return search.empty() || lower(row.label+" "+row.id+" "+row.category+" "+row.notes).find(lower(search))!=std::string::npos;
}
} // namespace studio::coverage
