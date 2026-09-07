#include "atlas_doc.h"

#include "tileset_doc.h"

#include <algorithm>

namespace tsm {

AtlasDoc::AtlasDoc(int px) {
    reset(px, kBaseCols);
}

void AtlasDoc::reset(int px, int width_cols) {
    tile_size = (px == 16) ? 16 : 8;
    cols = std::max(width_cols, kBaseCols);
    palette = TilesetDoc::default_palette();
    resize_tiles();
    bindings.clear();
    painted = false;
    error.clear();
    undo_.clear();
}

bool AtlasDoc::in_sheet(int col, int row) const {
    return col >= 0 && row >= 0 && col < cols && row < kRows;
}

bool AtlasDoc::is_extra(int col, int row) const {
    return col >= kBaseCols || row >= kRows;
}

bool AtlasDoc::is_bound_extra(int col, int row) const {
    return is_extra(col, row) && binding_index({col, row}) >= 0;
}

int AtlasDoc::extra_count() const {
    return std::max(cols - kBaseCols, 0) * kRows;
}

int AtlasDoc::cell_index(int col, int row) const {
    return row * cols + col;
}

std::string AtlasDoc::cell_name(int col, int row) const {
    if (is_extra(col, row)) {
        const Cell root = binding_root({col, row});
        if (root.x >= 0) {
            return "Variant of (" + std::to_string(root.x) + "," + std::to_string(root.y) + ") at (" +
                   std::to_string(col) + "," + std::to_string(row) + ")";
        }
        return "Variant (" + std::to_string(col) + "," + std::to_string(row) + ")";
    }
    return "Tile (" + std::to_string(col) + "," + std::to_string(row) + ")";
}

std::vector<uint8_t> AtlasDoc::get_tile(int col, int row) const {
    if (!in_sheet(col, row)) {
        return {};
    }
    const int n = tile_size * tile_size;
    const size_t start = static_cast<size_t>(cell_index(col, row)) * static_cast<size_t>(n);
    if (start + static_cast<size_t>(n) > tiles.size()) {
        return empty_tile();
    }
    return std::vector<uint8_t>(tiles.begin() + static_cast<std::ptrdiff_t>(start),
                                tiles.begin() + static_cast<std::ptrdiff_t>(start + static_cast<size_t>(n)));
}

void AtlasDoc::set_tile(int col, int row, const std::vector<uint8_t>& data) {
    if (!in_sheet(col, row)) {
        return;
    }
    const auto copy = copy_tile_data(data);
    const int n = tile_size * tile_size;
    const size_t start = static_cast<size_t>(cell_index(col, row)) * static_cast<size_t>(n);
    if (start + static_cast<size_t>(n) > tiles.size()) {
        return;
    }
    std::copy(copy.begin(), copy.end(), tiles.begin() + static_cast<std::ptrdiff_t>(start));
}

void AtlasDoc::copy_tile(int from_col, int from_row, int to_col, int to_row) {
    set_tile(to_col, to_row, get_tile(from_col, from_row));
}

int AtlasDoc::get_pixel(int col, int row, int x, int y) const {
    if (!in_sheet(col, row) || x < 0 || y < 0 || x >= tile_size || y >= tile_size) {
        return 0;
    }
    const int n = tile_size * tile_size;
    const size_t i = static_cast<size_t>(cell_index(col, row)) * static_cast<size_t>(n) +
                     static_cast<size_t>(y * tile_size + x);
    if (i >= tiles.size()) {
        return 0;
    }
    return tiles[i];
}

void AtlasDoc::set_pixel(int col, int row, int x, int y, int index) {
    if (!in_sheet(col, row) || x < 0 || y < 0 || x >= tile_size || y >= tile_size) {
        return;
    }
    const int n = tile_size * tile_size;
    const size_t i = static_cast<size_t>(cell_index(col, row)) * static_cast<size_t>(n) +
                     static_cast<size_t>(y * tile_size + x);
    if (i >= tiles.size()) {
        return;
    }
    tiles[i] = static_cast<uint8_t>(clampi(index, 0, last_palette_index()));
    painted = true;
}

int AtlasDoc::palette_count() const {
    return clampi(static_cast<int>(palette.size()), kPaletteMin, kPaletteSize);
}

int AtlasDoc::last_palette_index() const {
    return palette_count() - 1;
}

Rgb AtlasDoc::color_at(int index) const {
    const int i = clampi(index, 0, last_palette_index());
    if (i >= static_cast<int>(palette.size())) {
        return Rgb{0, 0, 0};
    }
    return palette[static_cast<size_t>(i)];
}

void AtlasDoc::set_palette_color(int index, Rgb color) {
    const int i = clampi(index, 0, last_palette_index());
    if (i >= static_cast<int>(palette.size())) {
        return;
    }
    palette[static_cast<size_t>(i)] = color;
}

void AtlasDoc::apply_palette(const std::vector<Rgb>& colors) {
    const int n = clampi(static_cast<int>(colors.size()), kPaletteMin, kPaletteSize);
    const auto defaults = TilesetDoc::default_palette();
    palette.resize(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i) {
        palette[static_cast<size_t>(i)] =
            (i < static_cast<int>(colors.size())) ? colors[static_cast<size_t>(i)] : defaults[static_cast<size_t>(i)];
    }
}

bool AtlasDoc::grow_palette() {
    if (static_cast<int>(palette.size()) >= kPaletteSize) {
        return false;
    }
    const auto defaults = TilesetDoc::default_palette();
    const int i = static_cast<int>(palette.size());
    palette.push_back(i < static_cast<int>(defaults.size()) ? defaults[static_cast<size_t>(i)] : palette.back());
    return true;
}

void AtlasDoc::set_palette_size(int new_size) {
    new_size = clampi(new_size, kPaletteMin, kPaletteSize);
    if (new_size == static_cast<int>(palette.size())) {
        return;
    }
    if (new_size > static_cast<int>(palette.size())) {
        const auto defaults = TilesetDoc::default_palette();
        while (static_cast<int>(palette.size()) < new_size) {
            const int i = static_cast<int>(palette.size());
            palette.push_back(i < static_cast<int>(defaults.size()) ? defaults[static_cast<size_t>(i)] : palette.back());
        }
    } else {
        palette.resize(static_cast<size_t>(new_size));
    }
}

void AtlasDoc::apply_shrink_remap(int new_size, int replacement) {
    new_size = clampi(new_size, kPaletteMin, kPaletteSize);
    if (new_size >= static_cast<int>(palette.size())) {
        return;
    }
    replacement = clampi(replacement, 0, new_size - 1);
    replace_indices_from(new_size, replacement);
    palette.resize(static_cast<size_t>(new_size));
}

bool AtlasDoc::replace_index(int from_index, int to_index) {
    const int last = last_palette_index();
    if (from_index < 0 || to_index < 0 || from_index > last || to_index > last || from_index == to_index) {
        return false;
    }
    std::vector<int> map(static_cast<size_t>(last + 1));
    for (int i = 0; i <= last; ++i) {
        map[static_cast<size_t>(i)] = (i == from_index) ? to_index : i;
    }
    remap_pixels(map);
    return true;
}

bool AtlasDoc::reorder_palette(int from_index, int to_index) {
    const int n = palette_count();
    if (from_index < 1 || to_index < 1 || from_index >= n || to_index >= n || from_index == to_index) {
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

bool AtlasDoc::has_unused_colors() const {
    return static_cast<int>(keep_indices_for_compact().size()) < palette_count();
}

bool AtlasDoc::compact_unused() {
    const auto keep = keep_indices_for_compact();
    const int n = palette_count();
    if (static_cast<int>(keep.size()) >= n) {
        return false;
    }
    std::vector<int> map(static_cast<size_t>(n), 0);
    std::vector<Rgb> next(keep.size());
    for (size_t new_i = 0; new_i < keep.size(); ++new_i) {
        const int old_i = keep[new_i];
        next[new_i] = (old_i < static_cast<int>(palette.size())) ? palette[static_cast<size_t>(old_i)] : Rgb{};
        if (old_i < n) {
            map[static_cast<size_t>(old_i)] = static_cast<int>(new_i);
        }
    }
    remap_pixels(map);
    palette = std::move(next);
    return true;
}

void AtlasDoc::flood_fill(int col, int row, int x, int y, int new_index) {
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

Cell AtlasDoc::next_extra_slot() const {
    const int n = static_cast<int>(bindings.size());
    return {kBaseCols + n / kRows, n % kRows};
}

Cell AtlasDoc::add_variant(Cell source, float probability) {
    if (!in_sheet(source.x, source.y)) {
        return {-1, -1};
    }
    Cell root = source;
    if (is_extra(source.x, source.y)) {
        root = binding_root(source);
    }
    if (!in_sheet(root.x, root.y) || is_extra(root.x, root.y)) {
        return {-1, -1};
    }
    const Cell slot = next_extra_slot();
    if (slot.x >= cols) {
        grow_cols(slot.x + 1);
    }
    copy_tile(source.x, source.y, slot.x, slot.y);
    bindings.push_back({slot.x, slot.y, root.x, root.y, probability});
    painted = true;
    return slot;
}

bool AtlasDoc::remove_variant(Cell cell) {
    const int idx = binding_index(cell);
    if (idx < 0) {
        return false;
    }
    bindings.erase(bindings.begin() + idx);
    compact_extras();
    painted = true;
    return true;
}

Cell AtlasDoc::binding_root(Cell cell) const {
    const int idx = binding_index(cell);
    if (idx < 0) {
        return {-1, -1};
    }
    return {bindings[static_cast<size_t>(idx)].root_x, bindings[static_cast<size_t>(idx)].root_y};
}

float AtlasDoc::binding_probability(Cell cell) const {
    const int idx = binding_index(cell);
    if (idx < 0) {
        return 0.3f;
    }
    return bindings[static_cast<size_t>(idx)].probability;
}

void AtlasDoc::set_binding_probability(Cell cell, float probability) {
    const int idx = binding_index(cell);
    if (idx < 0) {
        return;
    }
    bindings[static_cast<size_t>(idx)].probability = clampf(probability, 0.01f, 1.0f);
}

std::vector<VariantBinding> AtlasDoc::collect_bindings() const {
    return bindings;
}

bool AtlasDoc::tiles_equal(const std::vector<uint8_t>& a, const std::vector<uint8_t>& b) {
    return a == b;
}

std::vector<uint8_t> AtlasDoc::empty_tile() const {
    return std::vector<uint8_t>(static_cast<size_t>(tile_size * tile_size), 0);
}

AtlasSnap AtlasDoc::snapshot() const {
    return {tile_size, cols, palette, tiles, bindings, painted};
}

void AtlasDoc::restore(const AtlasSnap& snap) {
    tile_size = snap.tile_size;
    cols = std::max(snap.cols, kBaseCols);
    palette = snap.palette;
    tiles = snap.tiles;
    bindings = snap.bindings;
    painted = snap.painted;
}

void AtlasDoc::push_undo() {
    undo_.push_back(snapshot());
    if (static_cast<int>(undo_.size()) > kUndoLimit) {
        undo_.erase(undo_.begin());
    }
}

bool AtlasDoc::undo() {
    if (undo_.empty()) {
        return false;
    }
    restore(undo_.back());
    undo_.pop_back();
    return true;
}

bool AtlasDoc::can_undo() const {
    return !undo_.empty();
}

int AtlasDoc::binding_index(Cell cell) const {
    for (int i = 0; i < static_cast<int>(bindings.size()); ++i) {
        if (bindings[static_cast<size_t>(i)].x == cell.x && bindings[static_cast<size_t>(i)].y == cell.y) {
            return i;
        }
    }
    return -1;
}

std::vector<uint8_t> AtlasDoc::copy_tile_data(const std::vector<uint8_t>& data) const {
    std::vector<uint8_t> copy(static_cast<size_t>(tile_size * tile_size), 0);
    const size_t n = std::min(data.size(), copy.size());
    std::copy(data.begin(), data.begin() + static_cast<std::ptrdiff_t>(n), copy.begin());
    return copy;
}

void AtlasDoc::resize_tiles() {
    tiles.assign(static_cast<size_t>(cols * kRows * tile_size * tile_size), 0);
}

void AtlasDoc::rebuild_tiles(int old_cols, const std::vector<uint8_t>& old_tiles) {
    const int n = tile_size * tile_size;
    std::vector<uint8_t> fresh(static_cast<size_t>(cols * kRows * n), 0);
    for (int row = 0; row < kRows; ++row) {
        for (int col = 0; col < cols; ++col) {
            if (col >= old_cols) {
                continue;
            }
            const size_t src = static_cast<size_t>((row * old_cols + col) * n);
            const size_t dest = static_cast<size_t>((row * cols + col) * n);
            if (src + static_cast<size_t>(n) <= old_tiles.size()) {
                std::copy(old_tiles.begin() + static_cast<std::ptrdiff_t>(src),
                          old_tiles.begin() + static_cast<std::ptrdiff_t>(src + static_cast<size_t>(n)),
                          fresh.begin() + static_cast<std::ptrdiff_t>(dest));
            }
        }
    }
    tiles = std::move(fresh);
}

void AtlasDoc::grow_cols(int width_cols) {
    if (width_cols <= cols) {
        return;
    }
    const int old_cols = cols;
    const auto old = tiles;
    cols = width_cols;
    rebuild_tiles(old_cols, old);
}

void AtlasDoc::compact_extras() {
    std::vector<std::vector<uint8_t>> extras;
    extras.reserve(bindings.size());
    for (const auto& entry : bindings) {
        extras.push_back(get_tile(entry.x, entry.y));
    }
    std::vector<std::vector<uint8_t>> kept;
    kept.reserve(static_cast<size_t>(kBaseCols * kRows));
    for (int row = 0; row < kRows; ++row) {
        for (int col = 0; col < kBaseCols; ++col) {
            kept.push_back(get_tile(col, row));
        }
    }
    int need = kBaseCols;
    if (!bindings.empty()) {
        need = kBaseCols + (static_cast<int>(bindings.size()) - 1) / kRows + 1;
    }
    cols = need;
    resize_tiles();
    size_t i = 0;
    for (int row = 0; row < kRows; ++row) {
        for (int col = 0; col < kBaseCols; ++col) {
            set_tile(col, row, kept[i++]);
        }
    }
    for (size_t j = 0; j < bindings.size(); ++j) {
        const Cell slot{kBaseCols + static_cast<int>(j) / kRows, static_cast<int>(j) % kRows};
        bindings[j].x = slot.x;
        bindings[j].y = slot.y;
        set_tile(slot.x, slot.y, extras[j]);
    }
}

std::vector<int> AtlasDoc::keep_indices_for_compact() const {
    std::vector<int> keep{0};
    std::vector<uint8_t> used(static_cast<size_t>(palette_count()), 0);
    for (uint8_t v : tiles) {
        if (v < used.size()) {
            used[v] = 1;
        }
    }
    for (int i = 1; i < palette_count(); ++i) {
        if (used[static_cast<size_t>(i)]) {
            keep.push_back(i);
        }
    }
    if (static_cast<int>(keep.size()) < kPaletteMin) {
        keep.push_back(1);
    }
    return keep;
}

void AtlasDoc::replace_indices_from(int min_index, int replacement) {
    for (uint8_t& v : tiles) {
        if (static_cast<int>(v) >= min_index) {
            v = static_cast<uint8_t>(replacement);
        }
    }
}

void AtlasDoc::remap_pixels(const std::vector<int>& map) {
    const int last = static_cast<int>(map.size()) - 1;
    for (uint8_t& v : tiles) {
        const int old = static_cast<int>(v);
        v = (old < 0 || old > last) ? 0 : static_cast<uint8_t>(map[static_cast<size_t>(old)]);
    }
}

} // namespace tsm
