#include "convert.h"

#include "io.h"

#include "gentileset.h"

namespace tsm {

std::string convert_tileset_to_atlas(const TilesetDoc& src, AtlasDoc& out) {
    Image src_im{};
    std::string err = tileset_to_image(src, src_im);
    if (!err.empty()) {
        image_free(&src_im);
        out.error = err;
        return err;
    }

    Cuts cuts{};
    gentileset_default_cuts(&cuts, src.tile_size, src.tile_size);

    Image dst{};
    if (!convert_5x3(&src_im, &dst, &cuts)) {
        image_free(&src_im);
        image_free(&dst);
        out.error = "gentileset convert_5x3 failed";
        return out.error;
    }
    image_free(&src_im);

    const auto old_bindings = out.bindings;
    std::vector<std::vector<uint8_t>> extra_tiles;
    if (out.tile_size == src.tile_size && !old_bindings.empty()) {
        extra_tiles.reserve(old_bindings.size());
        for (const auto& b : old_bindings) {
            extra_tiles.push_back(out.get_tile(b.x, b.y));
        }
    }

    err = atlas_from_image(out, dst);
    image_free(&dst);
    if (!err.empty()) {
        out.error = err;
        return err;
    }

    if (out.tile_size == src.tile_size && !old_bindings.empty()) {
        for (size_t i = 0; i < old_bindings.size(); ++i) {
            const Cell slot = out.add_variant({old_bindings[i].root_x, old_bindings[i].root_y},
                                              old_bindings[i].probability);
            if (slot.x >= 0 && i < extra_tiles.size()) {
                out.set_tile(slot.x, slot.y, extra_tiles[i]);
            }
        }
    }

    out.error.clear();
    return {};
}

std::string convert_atlas_to_tileset(const AtlasDoc& src, TilesetDoc& out) {
    if (src.cols < AtlasDoc::kBaseCols) {
        return "Atlas must have at least 12 columns";
    }
    const int ts = src.tile_size;
    if (ts != 8 && ts != 16) {
        return "Tile size must be 8 or 16";
    }

    out.reset(ts);
    out.apply_palette(src.palette);

    struct CellMap {
        int a_col;
        int a_row;
        int t_col;
        int t_row;
    };

    static const CellMap kMap[] = {
        // 3x3 autotile (outer corners, edges, center)
        { 8,  0, 0, 0 }, // Top-left corner
        { 10, 0, 1, 0 }, // Top edge
        { 11, 0, 2, 0 }, // Top-right corner
        { 8,  1, 0, 1 }, // Left edge
        { 9,  2, 1, 1 }, // Center fill
        { 11, 2, 2, 1 }, // Right edge
        { 8,  3, 0, 2 }, // Bottom-left corner
        { 9,  3, 1, 2 }, // Bottom edge
        { 11, 3, 2, 2 }, // Bottom-right corner

        // Specialty caps and extras
        { 0,  0, 3, 0 }, // Pillar top
        { 0,  2, 3, 1 }, // Pillar bottom
        { 1,  3, 3, 2 }, // Platform left
        { 2,  1, 4, 0 }, // Inner corner
        { 0,  3, 4, 1 }, // Isolated tile
        { 3,  3, 4, 2 }, // Platform right
    };

    for (const auto& m : kMap) {
        out.set_tile(m.t_col, m.t_row, src.get_tile(m.a_col, m.a_row));
    }

    out.painted = true;
    out.hflip_linked = out.mirrors_match();
    out.vflip_linked = out.vmirrors_match();
    out.sync_seed_to_center();

    return {};
}

} // namespace tsm
