#include "draw.h"

#include <cmath>
#include <set>
#include <utility>

namespace tsm {

std::vector<Cell> bresenham(Cell a, Cell b) {
    std::vector<Cell> points;
    int x0 = a.x, y0 = a.y, x1 = b.x, y1 = b.y;
    const int dx = std::abs(x1 - x0);
    const int dy = -std::abs(y1 - y0);
    const int sx = (x0 < x1) ? 1 : -1;
    const int sy = (y0 < y1) ? 1 : -1;
    int err = dx + dy;
    while (true) {
        points.push_back({x0, y0});
        if (x0 == x1 && y0 == y1) {
            break;
        }
        const int e2 = err * 2;
        if (e2 >= dy) {
            err += dy;
            x0 += sx;
        }
        if (e2 <= dx) {
            err += dx;
            y0 += sy;
        }
    }
    return points;
}

std::vector<Cell> rect_outline(Cell a, Cell b) {
    const int x0 = std::min(a.x, b.x);
    const int y0 = std::min(a.y, b.y);
    const int x1 = std::max(a.x, b.x);
    const int y1 = std::max(a.y, b.y);
    std::vector<Cell> points;
    std::set<std::pair<int, int>> seen;
    auto add = [&](int x, int y) {
        if (seen.insert({x, y}).second) {
            points.push_back({x, y});
        }
    };
    for (int x = x0; x <= x1; ++x) {
        add(x, y0);
        add(x, y1);
    }
    for (int y = y0; y <= y1; ++y) {
        add(x0, y);
        add(x1, y);
    }
    return points;
}

std::vector<Cell> ellipse_outline(Cell a, Cell b) {
    const int x0 = std::min(a.x, b.x);
    const int y0 = std::min(a.y, b.y);
    const int x1 = std::max(a.x, b.x);
    const int y1 = std::max(a.y, b.y);
    const int w = x1 - x0;
    const int h = y1 - y0;
    if (w <= 0 || h <= 0) {
        return bresenham(a, b);
    }
    std::vector<Cell> points;
    std::set<std::pair<int, int>> seen;
    auto add = [&](int x, int y) {
        if (seen.insert({x, y}).second) {
            points.push_back({x, y});
        }
    };
    const float rx = static_cast<float>(w) * 0.5f;
    const float ry = static_cast<float>(h) * 0.5f;
    const float cx = static_cast<float>(x0) + rx;
    const float cy = static_cast<float>(y0) + ry;
    for (int x = x0; x <= x1; ++x) {
        const float nx = (static_cast<float>(x) - cx) / rx;
        const float inside = 1.0f - nx * nx;
        if (inside < 0.0f) {
            continue;
        }
        const float dy = ry * std::sqrt(inside);
        add(x, static_cast<int>(std::lround(cy - dy)));
        add(x, static_cast<int>(std::lround(cy + dy)));
    }
    for (int y = y0; y <= y1; ++y) {
        const float ny = (static_cast<float>(y) - cy) / ry;
        const float inside = 1.0f - ny * ny;
        if (inside < 0.0f) {
            continue;
        }
        const float dx = rx * std::sqrt(inside);
        add(static_cast<int>(std::lround(cx - dx)), y);
        add(static_cast<int>(std::lround(cx + dx)), y);
    }
    return points;
}

} // namespace tsm
