#pragma once

#include "../core/atlas_doc.h"
#include "../core/tileset_doc.h"
#include "../core/types.h"
#include "settings.h"

#include <string>
#include <vector>

struct SDL_Renderer;
struct SDL_Window;
struct ImGuiIO;

namespace tsm {

enum class Step { Center, Edges, Specialty, Variants };
enum class Tool { Pencil, Eraser, Fill, Line, Square, Circle, Eyedropper, Select };
enum class PendingAction { None, New, Open, Quit, Import12x4, Import5x3 };

struct Clipboard {
    int w = 0;
    int h = 0;
    std::vector<uint8_t> pixels;
    bool valid() const { return w > 0 && h > 0 && static_cast<int>(pixels.size()) == w * h; }
};

struct UiState {
    bool show_settings = false;
    bool show_export = false;
    bool show_new = false;
    bool show_unsaved = false;
    PendingAction pending = PendingAction::None;
    bool project_open = false;
    int new_tile_size = 16;
    char new_name[64] = "untitled";
    bool new_focus_name = false;
    int export_zoom = 2;
};

class TilesetEditor {
public:
    TilesetDoc doc{16};
    AtlasDoc atlas{16};
    bool has_atlas = false;
    int art_rev = 0;
    int atlas_rev = -1;
    Step step = Step::Center;
    bool seeded = false;
    bool stamped = false;
    Cell specialty = TilesetDoc::kInnerCorner;
    Cell atlas_cell{9, 2};
    Cell preview_sel{1, 1};

    char project_name[64] = "untitled";
    std::string project_path;
    std::string status = "Paint the center fill tile.";
    std::string last_dir;
    bool dirty = false;

    Tool tool = Tool::Pencil;
    int paint_index = 1;
    uint16_t palette_selected_mask = (1 << 1);
    int palette_anchor = 1;

    float picker_h = 0.0f;
    float picker_s = 1.0f;
    float picker_v = 1.0f;
    Rgb picker_last_rgb{255, 255, 255};
    bool picker_init = false;

    int brush = 1;
    bool tile_mode = true;
    bool export_header = true;
    bool export_terrain = true;
    bool export_5x3 = true;
    float variant_chance = 0.3f;
    float sidebar_w = 320.0f;

    bool dragging = false;
    bool stroke_has_drawn = false;
    bool selecting = false;
    bool stroke_pending = false;
    Cell hover{-1, -1};
    Cell stroke_from{-1, -1};
    Cell stroke_to{-1, -1};
    Cell sel_tile{0, 0};
    Cell sel_a{-1, -1};
    Cell sel_b{-1, -1};
    Clipboard clipboard;

    enum class FloatingType { None, Paste, MoveSelection };
    bool floating = false;
    FloatingType floating_type = FloatingType::None;
    Cell floating_pos{0, 0};
    int floating_w = 0;
    int floating_h = 0;
    std::vector<uint8_t> floating_pixels;
    bool floating_dragging = false;
    Cell floating_drag_start_pos{0, 0};
    float floating_drag_start_mouse_x = 0.0f;
    float floating_drag_start_mouse_y = 0.0f;
    Cell move_orig_a{-1, -1};
    Cell move_orig_b{-1, -1};

    struct SelectionSnap {
        Cell a{-1, -1};
        Cell b{-1, -1};
    };
    std::vector<SelectionSnap> undo_selections;

    int edit_ox = 1, edit_oy = 1, edit_cols = 1, edit_rows = 1;
    int zoom = 16;

    UiState ui;
    Settings settings;

    // View control for embedded usage inside Tilemap Maker
    bool embedded = false;
    bool request_return_to_map = false;
    bool request_sync_to_map = false;
    std::string last_exported_png_path;
    std::string last_exported_terrain_path;

    TilesetEditor();

    void reset_new(const std::string& name = "untitled", int size = 16);
    bool import_12x4(const std::string& path);
    bool import_5x3(const std::string& path);
    bool try_import_12x4_dialog();
    bool try_import_5x3_dialog();
    bool try_open_project_dialog();
    std::string save_project(bool save_as = false);
    std::string export_all();

    void configure_view();
    void touch() { dirty = true; }
    void bump_art();
    bool art_step() const { return step != Step::Variants; }
    std::string ensure_atlas();

    void go_back();
    void go_next();

    void handle_shortcuts(const ImGuiIO& io);
    void draw_step_header(bool show_return_button = false);
    void draw_tools_and_options();
    void draw_content(SDL_Renderer* renderer, SDL_Window* window, float avail_height = -1.0f);
    void draw_modals(bool& running);

    // Helpers
    int src_w() const;
    int src_h() const;
    int reps() const;
    int tile_size() const;
    uint8_t pixel(int col, int row, int x, int y) const;
    Rgb color(int idx) const;
    int last_index() const;
    bool in_sheet(int col, int row) const;

    bool is_palette_selected(int idx) const;
    void select_single_palette(int idx);
    void select_range_palette(int dest);
    void toggle_palette_selected(int idx);

    bool has_selection() const;
    void clear_selection();
    bool in_selection(Cell src) const;
    bool in_floating_box(Cell c) const;
    void cancel_floating_internal();
    void cancel_floating();
    void commit_floating();
    void start_selection_move();
    void start_paste();
    void copy_selection();

    bool in_doc(int col, int row) const;
    int get_px(int col, int row, int x, int y) const;
    void set_px(int col, int row, int x, int y, int idx);
    Cell src_to_cell(Cell src) const;
    Cell src_local(Cell src) const;
    bool can_stamp(Cell src) const;
    bool stamp_src(Cell src, int idx);
    int sample_hover_pixel(Cell src) const;
    bool plot_src(Cell src, int idx);

    void do_undo();
    void push_undo();
};

} // namespace tsm
