#include "md_color.h"

#include <cmath>

namespace tsm {

int MdColor::quantize_channel_byte(int byte) {
    byte = clampi(byte, 0, 255);
    int best = kLevels[0];
    int best_d = std::abs(byte - best);
    for (size_t i = 1; i < kLevels.size(); ++i) {
        const int d = std::abs(byte - kLevels[i]);
        if (d < best_d) {
            best = kLevels[i];
            best_d = d;
        }
    }
    return best;
}

Rgb MdColor::quantize(Rgb color) {
    return Rgb{
        static_cast<uint8_t>(quantize_channel_byte(color.r)),
        static_cast<uint8_t>(quantize_channel_byte(color.g)),
        static_cast<uint8_t>(quantize_channel_byte(color.b)),
    };
}

Rgb MdColor::quantize_rgb8(int r, int g, int b) {
    return quantize(Rgb{
        static_cast<uint8_t>(clampi(r, 0, 255)),
        static_cast<uint8_t>(clampi(g, 0, 255)),
        static_cast<uint8_t>(clampi(b, 0, 255)),
    });
}

bool MdColor::is_legal(Rgb color) {
    return quantize_channel_byte(color.r) == color.r &&
           quantize_channel_byte(color.g) == color.g &&
           quantize_channel_byte(color.b) == color.b;
}

} // namespace tsm
