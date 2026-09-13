#pragma once

#include "types.h"

#include <array>
#include <cstddef>
#include <set>
#include <string>
#include <vector>

namespace tsm {

struct TilesetSnap {
    int tile_size = 16;
    std::vector<Rgb> palette;
    std::array<std::vector<uint8_t>, 15> tiles;
    bool painted = false;
    bool hflip_linked = true;
    bool vflip_linked = false;
    std::vector<uint8_t> last_seed;
};

class TilesetDoc {
public:
    static constexpr int kCols = 5;
    static constexpr int kRows = 3;
    static constexpr int kCellCount = kCols * kRows;
    static constexpr int kPaletteSize = 16;
    static constexpr int kPaletteMin = 2;
    static constexpr int kUndoLimit = 64;

    static constexpr Cell kCenter{1, 1};
    static constexpr Cell kPillarTop{3, 0};
    static constexpr Cell kPillarBottom{3, 1};
    static constexpr Cell kPlatformLeft{3, 2};
    static constexpr Cell kInnerCorner{4, 0};
    static constexpr Cell kIsolated{4, 1};
    static constexpr Cell kPlatformRight{4, 2};

    explicit TilesetDoc(int px = 16);

    void reset(int px);

    static int cell_index(int col, int row);
    static bool in_sheet(int col, int row);
    static bool is_mirror_pair_right(int col, int row);
    static bool is_mirror_pair_left(int col, int row);
    static bool is_specialty(int col, int row);
    static Cell specialty_context_cell(Cell specialty, int gx, int gy);
    static std::vector<Rgb> default_palette();

    std::string cell_name(int col, int row) const;

    const std::vector<uint8_t>& get_tile(int col, int row) const;
    void set_tile(int col, int row, const std::vector<uint8_t>& data);
    void copy_tile(int from_col, int from_row, int to_col, int to_row);

    int get_pixel(int col, int row, int x, int y) const;
    void set_pixel(int col, int row, int x, int y, int index);
    int corner_context_pixel(int px, int py, bool* out_is_bg = nullptr, bool* out_is_cutout = nullptr) const;

    int palette_count() const;
    int last_palette_index() const;
    Rgb color_at(int index) const;
    void set_palette_color(int index, Rgb color);
    void apply_palette(const std::vector<Rgb>& colors);

    std::set<int> used_index_set() const;
    bool uses_index_at_or_above(int min_index) const;
    bool grow_palette();
    void set_palette_size(int new_size);
    void apply_shrink_remap(int new_size, int replacement);
    bool replace_index(int from_index, int to_index);
    bool reorder_palette(int from_index, int to_index);
    bool has_unused_colors() const;
    bool compact_unused();

    std::vector<uint8_t> hflip_copy(const std::vector<uint8_t>& src) const;
    std::vector<uint8_t> vflip_copy(const std::vector<uint8_t>& src) const;
    static bool tiles_equal(const std::vector<uint8_t>& a, const std::vector<uint8_t>& b);

    void sync_mirrors();
    void sync_mirrors_from_left();
    void sync_mirrors_from_right();
    bool mirrors_match() const;
    void sync_vmirrors();
    void sync_vmirrors_from_top();
    void sync_vmirrors_from_bottom();
    bool vmirrors_match() const;

    bool center_changed_since_seed() const;
    void seed_from_center(bool force = false);
    void sync_seed_to_center();

    void stamp_specialty(Cell cell);
    void stamp_all_specialty();
    void reset_tile_to_base(int col, int row);
    void reset_all_to_base();
    void reset_cell_to_center(int col, int row);
    void reset_specialty_to_center();

    void flood_fill(int col, int row, int x, int y, int new_index);

    TilesetSnap snapshot() const;
    void restore(const TilesetSnap& snap);
    void push_undo();
    bool undo();
    bool can_undo() const;

    int tile_size = 16;
    std::vector<Rgb> palette;
    std::array<std::vector<uint8_t>, kCellCount> tiles;
    bool painted = false;
    bool hflip_linked = true;
    bool vflip_linked = false;

private:
    std::vector<uint8_t> empty_tile() const;
    std::vector<int> keep_indices_for_compact() const;
    void replace_indices_from(int min_index, int replacement);
    void remap_pixels(const std::vector<int>& map);
    void stamp_platform_pair();
    void stamp_assembled(Cell dest, const int specs[][4], int spec_count);
    void stamp_halves_h(int left_col, int left_row, int right_col, int right_row, Cell dest);
    void stamp_from_quadrants(int tl_c, int tl_r, int tr_c, int tr_r, int bl_c, int bl_r, int br_c, int br_r, Cell dest);

    std::vector<TilesetSnap> undo_;
    std::vector<uint8_t> last_seed_;
};

} // namespace tsm
