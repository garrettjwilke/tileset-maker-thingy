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

    err = atlas_from_image(out, dst);
    image_free(&dst);
    if (!err.empty()) {
        out.error = err;
        return err;
    }
    out.error.clear();
    return {};
}

} // namespace tsm
