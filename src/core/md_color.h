#pragma once

#include "types.h"

#include <array>

namespace tsm {

struct MdColor {
    static constexpr std::array<int, 8> kLevels = {0, 36, 73, 109, 146, 182, 219, 255};

    static int quantize_channel_byte(int byte);
    static Rgb quantize(Rgb color);
    static Rgb quantize_rgb8(int r, int g, int b);
    static bool is_legal(Rgb color);
};

} // namespace tsm
