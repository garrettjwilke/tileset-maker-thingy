#pragma once

#include "types.h"

#include <string>
#include <vector>

namespace tsm {

struct PalettePresets {
    static std::vector<std::string> names();
    static std::vector<Rgb> colors_for(const std::string& name);
    static std::string match_name(const std::vector<Rgb>& colors);
};

} // namespace tsm
