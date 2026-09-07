#include "tileset_doc.h"

#include "md_color.h"

#include <algorithm>

namespace tsm {
namespace {

const std::array<Rgb, 16> kDefaultSource = {{
    {0, 0, 0},     {73, 73, 73},   {182, 182, 182}, {255, 255, 255},
    {146, 0, 0},   {255, 0, 0},    {0, 109, 0},     {0, 255, 0},
    {0, 0, 146},   {0, 73, 255},   {146, 109, 0},   {255, 255, 0},
    {146, 0, 73},  {255, 109, 182},{73, 0, 146},    {182, 0, 255},
}};

} // namespace

TilesetDoc::TilesetDoc(int px) {
    reset(px);
}

void TilesetDoc::reset(int px) {
    tile_size = (px == 16) ? 16 : 8;
    palette = default_palette();
    const auto empty = empty_tile();
    for (auto& tile : tiles) {
        tile = empty;
    }
    painted = false;
    hflip_linked = true;
    vflip_linked = false;
    undo_.clear();
    last_seed_.clear();
}

int TilesetDoc::cell_index(int col, int row) {
    return row * kCols + col;
}

bool TilesetDoc::in_sheet(int col, int row) {
    return col >= 0 && row >= 0 && col < kCols && row < kRows;
}

bool TilesetDoc::is_mirror_pair_right(int col, int row) {
    if (col == 2 && row >= 0 && row <= 2) {
        return true;
    }
    return col == 4 && row == 2;
}

bool TilesetDoc::is_mirror_pair_left(int col, int row) {
    if (col == 0 && row >= 0 && row <= 2) {
        return true;
    }
    return col == 3 && row == 2;
}

bool TilesetDoc::is_specialty(int col, int row) {
    (void)row;
    return col >= 3;
}

std::vector<Rgb> TilesetDoc::default_palette() {
    std::vector<Rgb> colors(kPaletteSize);
    for (int i = 0; i < kPaletteSize; ++i) {
        colors[i] = MdColor::quantize(kDefaultSource[static_cast<size_t>(i)]);
    }
    if (!colors.empty()) {
        colors[0] = Rgb{0, 0, 0};
    }
    return colors;
}

std::string TilesetDoc::cell_name(int col, int row) const {
    const std::string linked = hflip_linked ? " (h-flip)" : "";
    const std::string vlinked = vflip_linked ? " (v-flip)" : "";
    if (col == kCenter.x && row == kCenter.y) return "Center fill";
    if (col == 1 && row == 0) return "Top edge";
    if (col == 1 && row == 2) return "Bottom edge" + vlinked;
    if (col == 0 && row == 1) return "Left edge";
    if (col == 2 && row == 1) return "Right edge" + linked;
    if (col == 0 && row == 0) return "Top-left corner";
    if (col == 2 && row == 0) return "Top-right corner" + linked;
    if (col == 0 && row == 2) return "Bottom-left corner";
    if (col == 2 && row == 2) return "Bottom-right corner" + linked;
    if (col == kPillarTop.x && row == kPillarTop.y) return "Pillar top";
    if (col == kPillarBottom.x && row == kPillarBottom.y) return "Pillar bottom";
    if (col == kPlatformLeft.x && row == kPlatformLeft.y) return "Platform left";
    if (col == kPlatformRight.x && row == kPlatformRight.y) return "Platform right" + linked;
    if (col == kInnerCorner.x && row == kInnerCorner.y) return "Inner corner";
    if (col == kIsolated.x && row == kIsolated.y) return "Isolated block";
    return "(" + std::to_string(col) + "," + std::to_string(row) + ")";
}

const std::vector<uint8_t>& TilesetDoc::get_tile(int col, int row) const {
    static const std::vector<uint8_t> empty;
    if (!in_sheet(col, row)) {
        return empty;
    }
    return tiles[static_cast<size_t>(cell_index(col, row))];
}

void TilesetDoc::set_tile(int col, int row, const std::vector<uint8_t>& data) {
    if (!in_sheet(col, row)) {
        return;
    }
    auto& dest = tiles[static_cast<size_t>(cell_index(col, row))];
    dest = empty_tile();
    const size_t n = std::min(data.size(), dest.size());
    for (size_t i = 0; i < n; ++i) {
        dest[i] = data[i];
    }
}

void TilesetDoc::copy_tile(int from_col, int from_row, int to_col, int to_row) {
    set_tile(to_col, to_row, get_tile(from_col, from_row));
}

int TilesetDoc::get_pixel(int col, int row, int x, int y) const {
    if (!in_sheet(col, row) || x < 0 || y < 0 || x >= tile_size || y >= tile_size) {
        return 0;
    }
    return tiles[static_cast<size_t>(cell_index(col, row))][static_cast<size_t>(y * tile_size + x)];
}

void TilesetDoc::set_pixel(int col, int row, int x, int y, int index) {
    if (!in_sheet(col, row) || x < 0 || y < 0 || x >= tile_size || y >= tile_size) {
        return;
    }
    const uint8_t idx = static_cast<uint8_t>(clampi(index, 0, last_palette_index()));
    tiles[static_cast<size_t>(cell_index(col, row))][static_cast<size_t>(y * tile_size + x)] = idx;
    painted = true;
    if (col == kCenter.x && row == kCenter.y) {
        for (int r = 0; r < kRows; ++r) {
            for (int c = 0; c < kCols; ++c) {
                if (c == kCenter.x && r == kCenter.y) {
                    continue;
                }
                tiles[static_cast<size_t>(cell_index(c, r))][static_cast<size_t>(y * tile_size + x)] = idx;
            }
        }
        const size_t si = static_cast<size_t>(y * tile_size + x);
        if (!last_seed_.empty() && si < last_seed_.size()) {
            last_seed_[si] = idx;
        }
        return;
    }
    if (hflip_linked) {
        const int mx = tile_size - 1 - x;
        if (col == 0 && row >= 0 && row <= 2) {
            tiles[static_cast<size_t>(cell_index(2, row))][static_cast<size_t>(y * tile_size + mx)] = idx;
        } else if (col == 2 && row >= 0 && row <= 2) {
            tiles[static_cast<size_t>(cell_index(0, row))][static_cast<size_t>(y * tile_size + mx)] = idx;
        } else if (col == 3 && row == 2) {
            tiles[static_cast<size_t>(cell_index(4, 2))][static_cast<size_t>(y * tile_size + mx)] = idx;
        } else if (col == 4 && row == 2) {
            tiles[static_cast<size_t>(cell_index(3, 2))][static_cast<size_t>(y * tile_size + mx)] = idx;
        }
    }
    if (vflip_linked) {
        const int my = tile_size - 1 - y;
        if (col == 1 && row == 0) {
            tiles[static_cast<size_t>(cell_index(1, 2))][static_cast<size_t>(my * tile_size + x)] = idx;
        } else if (col == 1 && row == 2) {
            tiles[static_cast<size_t>(cell_index(1, 0))][static_cast<size_t>(my * tile_size + x)] = idx;
        }
    }
}

int TilesetDoc::palette_count() const {
    return clampi(static_cast<int>(palette.size()), kPaletteMin, kPaletteSize);
}

int TilesetDoc::last_palette_index() const {
    return palette_count() - 1;
}

Rgb TilesetDoc::color_at(int index) const {
    const int i = clampi(index, 0, last_palette_index());
    if (i >= static_cast<int>(palette.size())) {
        return Rgb{0, 0, 0};
    }
    return palette[static_cast<size_t>(i)];
}

void TilesetDoc::set_palette_color(int index, Rgb color) {
    const int i = clampi(index, 0, last_palette_index());
    if (i >= static_cast<int>(palette.size())) {
        return;
    }
    palette[static_cast<size_t>(i)] = color;
}

void TilesetDoc::apply_palette(const std::vector<Rgb>& colors) {
    const int n = clampi(static_cast<int>(colors.size()), kPaletteMin, kPaletteSize);
    const auto defaults = default_palette();
    palette.resize(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i) {
        palette[static_cast<size_t>(i)] =
            (i < static_cast<int>(colors.size())) ? colors[static_cast<size_t>(i)] : defaults[static_cast<size_t>(i)];
    }
}

std::set<int> TilesetDoc::used_index_set() const {
    std::set<int> used;
    for (const auto& tile : tiles) {
        for (uint8_t v : tile) {
            used.insert(static_cast<int>(v));
        }
    }
    return used;
}

bool TilesetDoc::uses_index_at_or_above(int min_index) const {
    for (const auto& tile : tiles) {
        for (uint8_t v : tile) {
            if (static_cast<int>(v) >= min_index) {
                return true;
            }
        }
    }
    return false;
}

bool TilesetDoc::grow_palette() {
    if (static_cast<int>(palette.size()) >= kPaletteSize) {
        return false;
    }
    const auto defaults = default_palette();
    const int i = static_cast<int>(palette.size());
    if (i < static_cast<int>(defaults.size())) {
        palette.push_back(defaults[static_cast<size_t>(i)]);
    } else {
        palette.push_back(palette.back());
    }
    return true;
}

void TilesetDoc::set_palette_size(int new_size) {
    new_size = clampi(new_size, kPaletteMin, kPaletteSize);
    if (new_size == static_cast<int>(palette.size())) {
        return;
    }
    if (new_size > static_cast<int>(palette.size())) {
        const auto defaults = default_palette();
        while (static_cast<int>(palette.size()) < new_size) {
            const int i = static_cast<int>(palette.size());
            palette.push_back(i < static_cast<int>(defaults.size()) ? defaults[static_cast<size_t>(i)] : palette.back());
        }
    } else {
        palette.resize(static_cast<size_t>(new_size));
    }
}

void TilesetDoc::apply_shrink_remap(int new_size, int replacement) {
    new_size = clampi(new_size, kPaletteMin, kPaletteSize);
    if (new_size >= static_cast<int>(palette.size())) {
        return;
    }
    replacement = clampi(replacement, 0, new_size - 1);
    replace_indices_from(new_size, replacement);
    palette.resize(static_cast<size_t>(new_size));
}

bool TilesetDoc::replace_index(int from_index, int to_index) {
    const int last = last_palette_index();
    if (from_index < 0 || to_index < 0 || from_index > last || to_index > last) {
        return false;
    }
    if (from_index == to_index) {
        return false;
    }
    std::vector<int> map(static_cast<size_t>(last + 1));
    for (int i = 0; i <= last; ++i) {
        map[static_cast<size_t>(i)] = (i == from_index) ? to_index : i;
    }
    remap_pixels(map);
    return true;
}

bool TilesetDoc::reorder_palette(int from_index, int to_index) {
    const int n = palette_count();
    if (from_index < 1 || to_index < 1 || from_index >= n || to_index >= n) {
        return false;
    }
    if (from_index == to_index) {
        return false;
    }
    std::vector<Rgb> colors = palette;
    colors.resize(static_cast<size_t>(n));
    const Rgb moved = colors[static_cast<size_t>(from_index)];
    colors.erase(colors.begin() + from_index);
    colors.insert(colors.begin() + to_index, moved);
    std::vector<int> map(static_cast<size_t>(n));
    for (int old = 0; old < n; ++old) {
        if (old == from_index) {
            map[static_cast<size_t>(old)] = to_index;
        } else if (from_index < to_index) {
            map[static_cast<size_t>(old)] = (old > from_index && old <= to_index) ? old - 1 : old;
        } else {
            map[static_cast<size_t>(old)] = (old >= to_index && old < from_index) ? old + 1 : old;
        }
    }
    remap_pixels(map);
    palette = std::move(colors);
    return true;
}

bool TilesetDoc::has_unused_colors() const {
    return static_cast<int>(keep_indices_for_compact().size()) < palette_count();
}

bool TilesetDoc::compact_unused() {
    const auto keep = keep_indices_for_compact();
    const int n = palette_count();
    if (static_cast<int>(keep.size()) >= n) {
        return false;
    }
    std::vector<int> map(static_cast<size_t>(n), 0);
    std::vector<Rgb> next(keep.size());
    for (size_t new_i = 0; new_i < keep.size(); ++new_i) {
        const int old_i = keep[new_i];
        next[new_i] = (old_i < static_cast<int>(palette.size())) ? palette[static_cast<size_t>(old_i)] : Rgb{0, 0, 0};
        if (old_i < n) {
            map[static_cast<size_t>(old_i)] = static_cast<int>(new_i);
        }
    }
    remap_pixels(map);
    palette = std::move(next);
    return true;
}

std::vector<uint8_t> TilesetDoc::hflip_copy(const std::vector<uint8_t>& src) const {
    std::vector<uint8_t> out(static_cast<size_t>(tile_size * tile_size), 0);
    for (int y = 0; y < tile_size; ++y) {
        for (int x = 0; x < tile_size; ++x) {
            const int si = y * tile_size + (tile_size - 1 - x);
            out[static_cast<size_t>(y * tile_size + x)] =
                (si >= 0 && si < static_cast<int>(src.size())) ? src[static_cast<size_t>(si)] : 0;
        }
    }
    return out;
}

std::vector<uint8_t> TilesetDoc::vflip_copy(const std::vector<uint8_t>& src) const {
    std::vector<uint8_t> out(static_cast<size_t>(tile_size * tile_size), 0);
    for (int y = 0; y < tile_size; ++y) {
        for (int x = 0; x < tile_size; ++x) {
            const int si = (tile_size - 1 - y) * tile_size + x;
            out[static_cast<size_t>(y * tile_size + x)] =
                (si >= 0 && si < static_cast<int>(src.size())) ? src[static_cast<size_t>(si)] : 0;
        }
    }
    return out;
}

bool TilesetDoc::tiles_equal(const std::vector<uint8_t>& a, const std::vector<uint8_t>& b) {
    return a == b;
}

void TilesetDoc::sync_mirrors() {
    if (hflip_linked) {
        sync_mirrors_from_left();
    }
}

void TilesetDoc::sync_mirrors_from_left() {
    for (int row = 0; row < 3; ++row) {
        set_tile(2, row, hflip_copy(get_tile(0, row)));
    }
    set_tile(4, 2, hflip_copy(get_tile(3, 2)));
}

void TilesetDoc::sync_mirrors_from_right() {
    for (int row = 0; row < 3; ++row) {
        set_tile(0, row, hflip_copy(get_tile(2, row)));
    }
    set_tile(3, 2, hflip_copy(get_tile(4, 2)));
}

bool TilesetDoc::mirrors_match() const {
    for (int row = 0; row < 3; ++row) {
        if (!tiles_equal(get_tile(2, row), hflip_copy(get_tile(0, row)))) {
            return false;
        }
    }
    return tiles_equal(get_tile(4, 2), hflip_copy(get_tile(3, 2)));
}

void TilesetDoc::sync_vmirrors() {
    if (vflip_linked) {
        sync_vmirrors_from_top();
    }
}

void TilesetDoc::sync_vmirrors_from_top() {
    set_tile(1, 2, vflip_copy(get_tile(1, 0)));
}

void TilesetDoc::sync_vmirrors_from_bottom() {
    set_tile(1, 0, vflip_copy(get_tile(1, 2)));
}

bool TilesetDoc::vmirrors_match() const {
    return tiles_equal(get_tile(1, 2), vflip_copy(get_tile(1, 0)));
}

bool TilesetDoc::center_changed_since_seed() const {
    if (last_seed_.empty()) {
        return true;
    }
    return !tiles_equal(get_tile(kCenter.x, kCenter.y), last_seed_);
}

void TilesetDoc::seed_from_center(bool force) {
    const auto center = get_tile(kCenter.x, kCenter.y);
    for (int row = 0; row < kRows; ++row) {
        for (int col = 0; col < kCols; ++col) {
            if (col == kCenter.x && row == kCenter.y) {
                continue;
            }
            if (force || last_seed_.empty() || tiles_equal(get_tile(col, row), last_seed_)) {
                set_tile(col, row, center);
            }
        }
    }
    last_seed_ = center;
}

void TilesetDoc::stamp_specialty(Cell cell) {
    if (cell == kPillarTop) {
        stamp_halves_h(0, 0, 2, 0, kPillarTop);
    } else if (cell == kPillarBottom) {
        stamp_halves_h(0, 2, 2, 2, kPillarBottom);
    } else if (cell == kPlatformLeft || cell == kPlatformRight) {
        stamp_platform_pair();
    } else if (cell == kIsolated) {
        stamp_from_quadrants(0, 0, 2, 0, 0, 2, 2, 2, kIsolated);
    } else if (cell == kInnerCorner) {
        copy_tile(kCenter.x, kCenter.y, kInnerCorner.x, kInnerCorner.y);
    }
}

void TilesetDoc::stamp_all_specialty() {
    stamp_specialty(kPillarTop);
    stamp_specialty(kPillarBottom);
    stamp_platform_pair();
    stamp_specialty(kIsolated);
    stamp_specialty(kInnerCorner);
}

void TilesetDoc::reset_tile_to_base(int col, int row) {
    if (!in_sheet(col, row) || (col == kCenter.x && row == kCenter.y)) {
        return;
    }
    copy_tile(kCenter.x, kCenter.y, col, row);
}

void TilesetDoc::reset_all_to_base() {
    const auto center = get_tile(kCenter.x, kCenter.y);
    for (int row = 0; row < 3; ++row) {
        for (int col = 0; col < 3; ++col) {
            if (col == kCenter.x && row == kCenter.y) {
                continue;
            }
            set_tile(col, row, center);
        }
    }
}

void TilesetDoc::reset_cell_to_center(int col, int row) {
    reset_tile_to_base(col, row);
}

void TilesetDoc::reset_specialty_to_center() {
    const Cell extras[] = {kPillarTop, kPillarBottom, kPlatformLeft, kInnerCorner, kIsolated, kPlatformRight};
    for (Cell cell : extras) {
        copy_tile(kCenter.x, kCenter.y, cell.x, cell.y);
    }
}

void TilesetDoc::flood_fill(int col, int row, int x, int y, int new_index) {
    const int old = get_pixel(col, row, x, y);
    const int idx = clampi(new_index, 0, last_palette_index());
    if (old == idx) {
        return;
    }
    std::vector<Cell> stack{{x, y}};
    std::vector<uint8_t> seen(static_cast<size_t>(tile_size * tile_size), 0);
    while (!stack.empty()) {
        const Cell p = stack.back();
        stack.pop_back();
        if (p.x < 0 || p.y < 0 || p.x >= tile_size || p.y >= tile_size) {
            continue;
        }
        const size_t key = static_cast<size_t>(p.y * tile_size + p.x);
        if (seen[key]) {
            continue;
        }
        seen[key] = 1;
        if (get_pixel(col, row, p.x, p.y) != old) {
            continue;
        }
        set_pixel(col, row, p.x, p.y, idx);
        stack.push_back({p.x + 1, p.y});
        stack.push_back({p.x - 1, p.y});
        stack.push_back({p.x, p.y + 1});
        stack.push_back({p.x, p.y - 1});
    }
}

TilesetSnap TilesetDoc::snapshot() const {
    TilesetSnap snap;
    snap.tile_size = tile_size;
    snap.palette = palette;
    snap.tiles = tiles;
    snap.painted = painted;
    snap.hflip_linked = hflip_linked;
    snap.vflip_linked = vflip_linked;
    snap.last_seed = last_seed_;
    return snap;
}

void TilesetDoc::restore(const TilesetSnap& snap) {
    tile_size = snap.tile_size;
    palette = snap.palette;
    tiles = snap.tiles;
    painted = snap.painted;
    hflip_linked = snap.hflip_linked;
    vflip_linked = snap.vflip_linked;
    last_seed_ = snap.last_seed;
}

void TilesetDoc::push_undo() {
    undo_.push_back(snapshot());
    if (static_cast<int>(undo_.size()) > kUndoLimit) {
        undo_.erase(undo_.begin());
    }
}

bool TilesetDoc::undo() {
    if (undo_.empty()) {
        return false;
    }
    restore(undo_.back());
    undo_.pop_back();
    return true;
}

bool TilesetDoc::can_undo() const {
    return !undo_.empty();
}

std::vector<uint8_t> TilesetDoc::empty_tile() const {
    return std::vector<uint8_t>(static_cast<size_t>(tile_size * tile_size), 0);
}

std::vector<int> TilesetDoc::keep_indices_for_compact() const {
    const auto used = used_index_set();
    std::vector<int> keep{0};
    for (int i = 1; i < palette_count(); ++i) {
        if (used.count(i)) {
            keep.push_back(i);
        }
    }
    if (static_cast<int>(keep.size()) < kPaletteMin) {
        keep.push_back(1);
    }
    return keep;
}

void TilesetDoc::replace_indices_from(int min_index, int replacement) {
    for (auto& tile : tiles) {
        for (uint8_t& v : tile) {
            if (static_cast<int>(v) >= min_index) {
                v = static_cast<uint8_t>(replacement);
            }
        }
    }
}

void TilesetDoc::remap_pixels(const std::vector<int>& map) {
    const int last = static_cast<int>(map.size()) - 1;
    for (auto& tile : tiles) {
        for (uint8_t& v : tile) {
            const int old = static_cast<int>(v);
            v = (old < 0 || old > last) ? 0 : static_cast<uint8_t>(map[static_cast<size_t>(old)]);
        }
    }
}

void TilesetDoc::stamp_platform_pair() {
    const int half = tile_size / 2;
    const int left_specs[4][4] = {
        {0, 0, 0, 0},
        {1, 0, 0, 0},
        {0, 2, 0, half},
        {1, 2, 0, half},
    };
    const int right_specs[4][4] = {
        {1, 0, half, 0},
        {2, 0, half, 0},
        {1, 2, half, half},
        {2, 2, half, half},
    };
    stamp_assembled(kPlatformLeft, left_specs, 4);
    stamp_assembled(kPlatformRight, right_specs, 4);
}

void TilesetDoc::stamp_assembled(Cell dest, const int specs[][4], int spec_count) {
    const int half = tile_size / 2;
    const Cell dest_at[4] = {{0, 0}, {half, 0}, {0, half}, {half, half}};
    std::vector<uint8_t> out(static_cast<size_t>(tile_size * tile_size), 0);
    const int n = std::min(spec_count, 4);
    for (int i = 0; i < n; ++i) {
        const auto& src = get_tile(specs[i][0], specs[i][1]);
        const int sx = specs[i][2];
        const int sy = specs[i][3];
        const int dx = dest_at[i].x;
        const int dy = dest_at[i].y;
        for (int y = 0; y < half; ++y) {
            for (int x = 0; x < half; ++x) {
                const int si = (sy + y) * tile_size + (sx + x);
                const int di = (dy + y) * tile_size + (dx + x);
                if (di >= 0 && di < static_cast<int>(out.size())) {
                    out[static_cast<size_t>(di)] =
                        (si >= 0 && si < static_cast<int>(src.size())) ? src[static_cast<size_t>(si)] : 0;
                }
            }
        }
    }
    set_tile(dest.x, dest.y, out);
}

void TilesetDoc::stamp_halves_h(int left_col, int left_row, int right_col, int right_row, Cell dest) {
    const int half = tile_size / 2;
    const auto& left = get_tile(left_col, left_row);
    const auto& right = get_tile(right_col, right_row);
    std::vector<uint8_t> out(static_cast<size_t>(tile_size * tile_size), 0);
    for (int y = 0; y < tile_size; ++y) {
        for (int x = 0; x < tile_size; ++x) {
            const auto& src = (x < half) ? left : right;
            const int i = y * tile_size + x;
            out[static_cast<size_t>(i)] = (i < static_cast<int>(src.size())) ? src[static_cast<size_t>(i)] : 0;
        }
    }
    set_tile(dest.x, dest.y, out);
}

void TilesetDoc::stamp_from_quadrants(
    int tl_c, int tl_r, int tr_c, int tr_r, int bl_c, int bl_r, int br_c, int br_r, Cell dest) {
    const int half = tile_size / 2;
    const auto& tl = get_tile(tl_c, tl_r);
    const auto& tr = get_tile(tr_c, tr_r);
    const auto& bl = get_tile(bl_c, bl_r);
    const auto& br = get_tile(br_c, br_r);
    std::vector<uint8_t> out(static_cast<size_t>(tile_size * tile_size), 0);
    for (int y = 0; y < tile_size; ++y) {
        for (int x = 0; x < tile_size; ++x) {
            const std::vector<uint8_t>* src = &br;
            if (y < half && x < half) {
                src = &tl;
            } else if (y < half) {
                src = &tr;
            } else if (x < half) {
                src = &bl;
            }
            const int i = y * tile_size + x;
            out[static_cast<size_t>(i)] = (i < static_cast<int>(src->size())) ? (*src)[static_cast<size_t>(i)] : 0;
        }
    }
    set_tile(dest.x, dest.y, out);
}

} // namespace tsm
