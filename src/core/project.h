#pragma once

#include "atlas_doc.h"
#include "tileset_doc.h"

#include <string>

namespace tsm {

enum class ProjectStep { Center, Edges, Specialty, Variants };

struct ProjectData {
    std::string name = "untitled";
    ProjectStep step = ProjectStep::Center;
    bool seeded = false;
    bool stamped = false;
    Cell specialty = TilesetDoc::kInnerCorner;
    Cell atlas_cell{9, 2};
    Cell preview_sel{1, 1};
    bool tile_mode = true;
    bool export_header = true;
    bool export_terrain = true;
    bool export_5x3 = true;
    int art_rev = 0;
    int atlas_rev = -1;
    bool has_atlas = false;
    TilesetSnap tileset;
    AtlasSnap atlas;
};

std::string project_to_text(const ProjectData& data);
std::string project_from_text(ProjectData& data, const std::string& text);
std::string save_project(const ProjectData& data, const std::string& path);
std::string load_project(ProjectData& data, const std::string& path);
std::string with_tilesetproj_ext(const std::string& path);

} // namespace tsm
