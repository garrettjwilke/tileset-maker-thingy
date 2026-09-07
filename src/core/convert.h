#pragma once

#include "atlas_doc.h"
#include "tileset_doc.h"

#include <string>

namespace tsm {

std::string convert_tileset_to_atlas(const TilesetDoc& src, AtlasDoc& out);

} // namespace tsm
