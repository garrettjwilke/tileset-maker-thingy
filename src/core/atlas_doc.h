#pragma once

#include "types.h"

#include <string>
#include <vector>

namespace tsm {

struct VariantBinding {
    int x = 0;
    int y = 0;
    int root_x = 0;
    int root_y = 0;
    float probability = 0.3f;
};

struct AtlasSnap {
    int tile_size = 16;
    int cols = 12;
    std::vector<Rgb> palette;
    std::vector<uint8_t> tiles;
    std::vector<VariantBinding> bindings;
    bool painted = false;
};

class AtlasDoc {
public:
    static constexpr int kBaseCols = 12;
    static constexpr int kRows = 4;
    static constexpr int kPaletteSize = 16;
    static constexpr int kPaletteMin = 2;
    static constexpr int kUndoLimit = 64;

    explicit AtlasDoc(int px = 16);

    void reset(int px = 16, int width_cols = kBaseCols);

    bool in_sheet(int col, int row) const;
    bool is_extra(int col, int row) const;
    bool is_bound_extra(int col, int row) const;
    int extra_count() const;
    int cell_index(int col, int row) const;
    std::string cell_name(int col, int row) const;
    Cell context_cell(Cell cell, int gx, int gy) const;

    std::vector<uint8_t> get_tile(int col, int row) const;
    void set_tile(int col, int row, const std::vector<uint8_t>& data);
    void copy_tile(int from_col, int from_row, int to_col, int to_row);
    int get_pixel(int col, int row, int x, int y) const;
    void set_pixel(int col, int row, int x, int y, int index);

    int palette_count() const;
    int last_palette_index() const;
    Rgb color_at(int index) const;
    void set_palette_color(int index, Rgb color);
    void apply_palette(const std::vector<Rgb>& colors);

    bool grow_palette();
    void set_palette_size(int new_size);
    void apply_shrink_remap(int new_size, int replacement);
    bool replace_index(int from_index, int to_index);
    bool reorder_palette(int from_index, int to_index);
    bool has_unused_colors() const;
    bool compact_unused();

    void flood_fill(int col, int row, int x, int y, int new_index);

    Cell next_extra_slot() const;
    Cell add_variant(Cell source, float probability = 0.3f);
    bool remove_variant(Cell cell);
    Cell binding_root(Cell cell) const;
    float binding_probability(Cell cell) const;
    void set_binding_probability(Cell cell, float probability);
    std::vector<VariantBinding> collect_bindings() const;

    static bool tiles_equal(const std::vector<uint8_t>& a, const std::vector<uint8_t>& b);
    std::vector<uint8_t> empty_tile() const;

    AtlasSnap snapshot() const;
    void restore(const AtlasSnap& snap);
    void push_undo();
    bool undo();
    bool can_undo() const;

    int tile_size = 16;
    int cols = kBaseCols;
    std::vector<Rgb> palette;
    std::vector<uint8_t> tiles;
    std::vector<VariantBinding> bindings;
    bool painted = false;
    std::string error;

private:
    int binding_index(Cell cell) const;
    std::vector<uint8_t> copy_tile_data(const std::vector<uint8_t>& data) const;
    void resize_tiles();
    void rebuild_tiles(int old_cols, const std::vector<uint8_t>& old_tiles);
    void grow_cols(int width_cols);
    void compact_extras();
    std::vector<int> keep_indices_for_compact() const;
    void replace_indices_from(int min_index, int replacement);
    void remap_pixels(const std::vector<int>& map);

    std::vector<AtlasSnap> undo_;
};

} // namespace tsm
