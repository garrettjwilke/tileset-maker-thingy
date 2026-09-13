#pragma once

#include "atlas_doc.h"
#include "tileset_doc.h"

#include "gentileset.h"

#include <string>
#include <vector>

namespace tsm {

struct HeaderSymbols {
    std::string ident;
    std::string macro;
    std::string guard;
};

struct PaletteLoad {
    std::string error;
    std::vector<Rgb> colors;
};

struct TerrainLoad {
    std::string error;
    std::string tileset;
    int tile_size = 0;
    std::vector<VariantBinding> variants;
};

std::string tileset_to_image(const TilesetDoc& doc, Image& im);
std::string atlas_to_image(const AtlasDoc& doc, Image& im);
std::string tileset_from_image(TilesetDoc& doc, const Image& im);
std::string atlas_from_image(AtlasDoc& doc, const Image& im);

std::string save_tileset_png(const TilesetDoc& doc, const std::string& path);
std::string load_tileset_png(TilesetDoc& doc, const std::string& path);
std::string save_atlas_png(const AtlasDoc& doc, const std::string& path);
std::string load_atlas_png(AtlasDoc& doc, const std::string& path);

HeaderSymbols symbols_from_path(const std::string& path);
std::string c_ident(const std::string& stem);
std::string render_header(const AtlasDoc& atlas, const std::string& png_path);
std::string save_header(const AtlasDoc& atlas, const std::string& dest_path, const std::string& png_path);

std::string palette_to_text(const std::vector<Rgb>& colors);
PaletteLoad palette_from_text(const std::string& text);
std::string save_palette_file(const std::string& path, const std::vector<Rgb>& colors);
PaletteLoad load_palette_file(const std::string& path);

std::string render_terrain(const AtlasDoc& atlas, const std::string& tileset_name);
std::string save_terrain(const AtlasDoc& atlas, const std::string& dest_path, const std::string& tileset_path);
TerrainLoad load_terrain(const std::string& path);
std::string import_12x4_tileset(AtlasDoc& atlas, const std::string& path, std::string& loaded_png_path, bool& had_terrain);

std::string write_text_file(const std::string& path, const std::string& text);
std::string read_text_file(const std::string& path, std::string& out);

} // namespace tsm
