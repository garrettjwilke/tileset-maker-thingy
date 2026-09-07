#pragma once

#include "types.h"

#include <functional>
#include <vector>

namespace tsm {

std::vector<Cell> bresenham(Cell a, Cell b);
std::vector<Cell> rect_outline(Cell a, Cell b);
std::vector<Cell> ellipse_outline(Cell a, Cell b);

} // namespace tsm
