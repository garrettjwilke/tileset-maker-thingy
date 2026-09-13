#pragma once

#include "atlas_doc.h"
#include "tileset_doc.h"

#include <string>

namespace tsm {

std::string convert_tileset_to_atlas(const TilesetDoc& src, AtlasDoc& out);
std::string convert_atlas_to_tileset(const AtlasDoc& src, TilesetDoc& out);

} // namespace tsm
