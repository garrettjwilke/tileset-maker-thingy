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

} // namespace tsm
