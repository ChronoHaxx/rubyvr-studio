#pragma once

#include <string>
#include <vector>

namespace studio::coverage {

struct Map { std::string id; int rows=0, failed=0, unmodeled=0; };
struct Index {
    std::string path, directory, created_at, pack_fingerprint, source_version;
    std::vector<Map> maps;
};
struct Row {
    std::string id, label, kind, category, boundary, model;
    std::string implementation, disposition, visual, live, headset, notes;
    int x=0,y=0,w=0,h=0,anchor_x=0,anchor_y=0,flags=0;
};
struct Room {
    std::string map, notes;
    int width=0,height=0;
    std::vector<Row> rows;
};

// Transactional readers: failure leaves the last successfully loaded view intact.
bool load_index(const std::string& path, Index* out, std::string* error);
bool load_room(const Index& index, const std::string& map, Room* out, std::string* error);
std::string file_fingerprint(const std::string& path);
// filter: All / No model / Unresolved / Unreviewed / Failed.
bool matches(const Row& row, int filter, bool include_flat, bool include_padding,
             const std::string& search);

} // namespace studio::coverage
