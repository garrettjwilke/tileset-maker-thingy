#include "editor.h"
#include "settings.h"

#include "core/atlas_doc.h"
#include "core/convert.h"
#include "core/draw.h"
#include "core/io.h"
#include "core/md_color.h"
#include "core/palette_presets.h"
#include "core/project.h"
#include "core/tileset_doc.h"

#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_sdlrenderer3.h"
#include "nfd.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {

using tsm::AtlasDoc;
using tsm::Cell;
using tsm::MdColor;
using tsm::PalettePresets;
using tsm::ProjectData;
using tsm::ProjectStep;
using tsm::Rgb;
using tsm::TilesetDoc;

enum class Step { Center, Edges, Specialty, Variants };
enum class Tool { Pencil, Eraser, Fill, Line, Square, Circle, Eyedropper, Select };
enum class PendingAction { None, New, Open, Quit };

const char* tool_name(Tool t) {
    switch (t) {
    case Tool::Pencil: return "Pencil";
    case Tool::Eraser: return "Eraser";
    case Tool::Fill: return "Fill";
    case Tool::Line: return "Line";
    case Tool::Square: return "Square";
    case Tool::Circle: return "Circle";
    case Tool::Eyedropper: return "Eyedropper";
    case Tool::Select: return "Select";
    }
    return "?";
}

bool uses_brush(Tool t) {
    return t == Tool::Pencil || t == Tool::Eraser || t == Tool::Line || t == Tool::Square || t == Tool::Circle;
}

ImU32 im_color(Rgb c, int a = 255) {
    return IM_COL32(c.r, c.g, c.b, a);
}

std::string trim_copy(const char* s) {
    std::string t = s ? s : "";
    const auto a = t.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) {
        return {};
    }
    const auto b = t.find_last_not_of(" \t\r\n");
    return t.substr(a, b - a + 1);
}

std::string dirname_of(const std::string& path) {
    const auto slash = path.find_last_of("/\\");
    return (slash == std::string::npos) ? std::string() : path.substr(0, slash);
}

ProjectStep project_step_from(Step step) {
    switch (step) {
    case Step::Edges: return ProjectStep::Edges;
    case Step::Specialty: return ProjectStep::Specialty;
    case Step::Variants: return ProjectStep::Variants;
    case Step::Center:
    default: return ProjectStep::Center;
    }
}

Step step_from_project(ProjectStep step) {
    switch (step) {
    case ProjectStep::Edges: return Step::Edges;
    case ProjectStep::Specialty: return Step::Specialty;
    case ProjectStep::Variants: return Step::Variants;
    case ProjectStep::Center:
    default: return Step::Center;
    }
}

struct Clipboard {
    int w = 0;
    int h = 0;
    std::vector<uint8_t> pixels;
    bool valid() const { return w > 0 && h > 0 && static_cast<int>(pixels.size()) == w * h; }
};

struct Editor {
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

    bool is_palette_selected(int idx) const {
        return (idx >= 0 && idx < 16) && ((palette_selected_mask & (1 << idx)) != 0);
    }
    void select_single_palette(int idx) {
        paint_index = idx;
        palette_anchor = idx;
        palette_selected_mask = (idx >= 0 && idx < 16) ? static_cast<uint16_t>(1 << idx) : 0;
    }
    void select_range_palette(int dest) {
        if (dest < 0 || dest >= 16) return;
        paint_index = dest;
        palette_selected_mask = 0;
        const int lo = std::min(palette_anchor, dest);
        const int hi = std::max(palette_anchor, dest);
        for (int k = lo; k <= hi; ++k) {
            palette_selected_mask |= static_cast<uint16_t>(1 << k);
        }
    }
    void toggle_palette_selected(int idx) {
        if (idx < 0 || idx >= 16) return;
        palette_selected_mask ^= static_cast<uint16_t>(1 << idx);
        if (palette_selected_mask == 0) {
            palette_selected_mask = static_cast<uint16_t>(1 << idx);
        }
        paint_index = idx;
        palette_anchor = idx;
    }

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
    enum class FloatingType {
        None,
        Paste,
        MoveSelection
    };
    bool floating = false;
    FloatingType floating_type = FloatingType::None;
    Cell floating_pos{0, 0};
    int floating_w = 0;
    int floating_h = 0;
    std::vector<uint8_t> floating_pixels;
    bool floating_dragging = false;
    Cell floating_drag_start_pos{0, 0};
    ImVec2 floating_drag_start_mouse{0.0f, 0.0f};
    Cell move_orig_a{-1, -1};
    Cell move_orig_b{-1, -1};
    struct SelectionSnap {
        Cell a{-1, -1};
        Cell b{-1, -1};
    };
    std::vector<SelectionSnap> undo_selections;

    int edit_ox = 1, edit_oy = 1, edit_cols = 1, edit_rows = 1;
    int zoom = 16;

    void touch() { dirty = true; }

    void bump_art() {
        ++art_rev;
        dirty = true;
    }

    bool art_step() const { return step != Step::Variants; }

    bool has_selection() const {
        return sel_a.x >= 0 && sel_b.x >= 0;
    }

    void clear_selection() {
        sel_a = {-1, -1};
        sel_b = {-1, -1};
        selecting = false;
    }

    bool in_selection(Cell src) const {
        if (!has_selection()) {
            return false;
        }
        const int x0 = std::min(sel_a.x, sel_b.x);
        const int y0 = std::min(sel_a.y, sel_b.y);
        const int x1 = std::max(sel_a.x, sel_b.x);
        const int y1 = std::max(sel_a.y, sel_b.y);
        return src.x >= x0 && src.x <= x1 && src.y >= y0 && src.y <= y1;
    }

    bool in_floating_box(Cell c) const {
        if (!floating) return false;
        return c.x >= floating_pos.x && c.x < floating_pos.x + floating_w &&
               c.y >= floating_pos.y && c.y < floating_pos.y + floating_h;
    }

    void cancel_floating_internal() {
        if (!floating) return;
        if (floating_type == FloatingType::MoveSelection) {
            do_undo();
            sel_a = move_orig_a;
            sel_b = move_orig_b;
        }
        floating = false;
        floating_type = FloatingType::None;
        floating_pixels.clear();
        floating_dragging = false;
    }

    void configure_view() {
        cancel_floating_internal();
        clear_selection();
        switch (step) {
        case Step::Center:
            edit_ox = TilesetDoc::kCenter.x;
            edit_oy = TilesetDoc::kCenter.y;
            edit_cols = 1;
            edit_rows = 1;
            preview_sel = TilesetDoc::kCenter;
            break;
        case Step::Edges:
            edit_ox = 0;
            edit_oy = 0;
            edit_cols = 3;
            edit_rows = 3;
            if (preview_sel.x >= 3 || preview_sel.y >= 3) {
                preview_sel = {0, 1};
            }
            break;
        case Step::Specialty:
            edit_ox = specialty.x;
            edit_oy = specialty.y;
            edit_cols = 3;
            edit_rows = 3;
            preview_sel = specialty;
            break;
        case Step::Variants:
            edit_ox = atlas_cell.x;
            edit_oy = atlas_cell.y;
            edit_cols = 1;
            edit_rows = 1;
            break;
        }
    }

    int src_w() const { return edit_cols * (art_step() ? doc.tile_size : atlas.tile_size); }
    int src_h() const { return edit_rows * (art_step() ? doc.tile_size : atlas.tile_size); }
    int reps() const { return (step == Step::Center && tile_mode) ? 5 : 1; }
    int tile_size() const { return art_step() ? doc.tile_size : atlas.tile_size; }

    int get_px(int col, int row, int x, int y) const {
        return art_step() ? doc.get_pixel(col, row, x, y) : atlas.get_pixel(col, row, x, y);
    }
    void set_px(int col, int row, int x, int y, int idx) {
        if (art_step()) {
            doc.set_pixel(col, row, x, y, idx);
        } else {
            atlas.set_pixel(col, row, x, y, idx);
        }
    }
    Rgb color(int idx) const { return art_step() ? doc.color_at(idx) : atlas.color_at(idx); }
    int last_index() const { return art_step() ? doc.last_palette_index() : atlas.last_palette_index(); }
    bool in_doc(int col, int row) const {
        return art_step() ? TilesetDoc::in_sheet(col, row) : atlas.in_sheet(col, row);
    }

    void push_undo() {
        if (art_step()) {
            doc.push_undo();
        } else {
            atlas.push_undo();
        }
        undo_selections.push_back({sel_a, sel_b});
        if (undo_selections.size() > 30) {
            undo_selections.erase(undo_selections.begin());
        }
    }
    void do_undo() {
        bool undone = false;
        if (art_step()) {
            undone = doc.undo();
        } else {
            undone = atlas.undo();
        }
        if (undone && !undo_selections.empty()) {
            const auto sel = undo_selections.back();
            undo_selections.pop_back();
            sel_a = sel.a;
            sel_b = sel.b;
        }
        bump_art();
    }

    Cell src_to_cell(Cell src) const {
        const int ts = tile_size();
        if (ts <= 0) {
            return {-1, -1};
        }
        const int gx = src.x / ts;
        const int gy = src.y / ts;
        if (step == Step::Specialty) {
            return TilesetDoc::specialty_context_cell(specialty, gx, gy);
        }
        return {edit_ox + gx, edit_oy + gy};
    }
    Cell src_local(Cell src) const {
        const int ts = tile_size();
        if (ts <= 0) {
            return {0, 0};
        }
        return {src.x % ts, src.y % ts};
    }

    int sample_hover_pixel(Cell hover) {
        if (hover.x < 0 || hover.y < 0) return -1;
        const Cell loc = src_local(hover);
        if (step == Step::Specialty && specialty == TilesetDoc::kInnerCorner) {
            ensure_atlas();
            const int ts = tile_size();
            const int gx = hover.x / ts;
            const int gy = hover.y / ts;
            if (gx == 1 && gy == 1) {
                return doc.get_pixel(TilesetDoc::kInnerCorner.x, TilesetDoc::kInnerCorner.y, loc.x, loc.y);
            } else if ((gx == 1 && gy == 0) || (gx == 1 && gy == 2)) {
                return atlas.get_pixel(0, 1, loc.x, loc.y);
            } else if ((gx == 0 && gy == 1) || (gx == 2 && gy == 1)) {
                return atlas.get_pixel(2, 3, loc.x, loc.y);
            }
            return -1;
        }
        const Cell cell = src_to_cell(hover);
        if (in_doc(cell.x, cell.y)) {
            return get_px(cell.x, cell.y, loc.x, loc.y);
        }
        return -1;
    }

    bool plot_src(Cell src, int index) {
        if (src.x < 0 || src.y < 0 || src.x >= src_w() || src.y >= src_h()) {
            return false;
        }
        const Cell cell = src_to_cell(src);
        const Cell loc = src_local(src);
        if (!in_doc(cell.x, cell.y)) {
            return false;
        }
        set_px(cell.x, cell.y, loc.x, loc.y, index);
        return true;
    }

    bool stamp_src(Cell origin, int index) {
        bool wrote = false;
        for (int dy = 0; dy < brush; ++dy) {
            for (int dx = 0; dx < brush; ++dx) {
                const Cell p{origin.x + dx, origin.y + dy};
                if (has_selection() && !in_selection(p)) {
                    continue;
                }
                if (plot_src(p, index)) {
                    wrote = true;
                }
            }
        }
        return wrote;
    }

    bool can_stamp(Cell origin) const {
        for (int dy = 0; dy < brush; ++dy) {
            for (int dx = 0; dx < brush; ++dx) {
                const Cell p{origin.x + dx, origin.y + dy};
                if (has_selection() && !in_selection(p)) {
                    continue;
                }
                if (p.x < 0 || p.y < 0 || p.x >= src_w() || p.y >= src_h()) {
                    continue;
                }
                const Cell cell = src_to_cell(p);
                if (in_doc(cell.x, cell.y)) {
                    return true;
                }
            }
        }
        return false;
    }

    void go_next() {
        if (step == Step::Center) {
            const bool force = !seeded || doc.center_changed_since_seed();
            if (force && seeded) {
                push_undo();
            }
            doc.seed_from_center(force);
            seeded = true;
            bump_art();
            step = Step::Edges;
            status = force ? "Surrounding tiles reset from the new center fill."
                           : "Paint the 8 surrounding tiles.";
        } else if (step == Step::Edges) {
            if (!stamped) {
                doc.stamp_all_specialty();
                stamped = true;
                bump_art();
            }
            step = Step::Specialty;
            status = "Caps and extras stamped from the 3x3. Tweak them, or stamp again after edge edits.";
        } else if (step == Step::Specialty) {
            const std::string err = tsm::convert_tileset_to_atlas(doc, atlas);
            if (!err.empty()) {
                status = err;
                return;
            }
            has_atlas = true;
            atlas_rev = art_rev;
            atlas_cell = {9, 2};
            step = Step::Variants;
            status = "5x3 converted to a 12x4 atlas. Click a tile to edit it, or add a variant.";
        }
        touch();
        configure_view();
    }

    void go_back() {
        if (step == Step::Edges) {
            step = Step::Center;
        } else if (step == Step::Specialty) {
            step = Step::Edges;
        } else if (step == Step::Variants) {
            step = Step::Specialty;
        }
        touch();
        configure_view();
    }

    std::string ensure_atlas() {
        if (!has_atlas || atlas_rev != art_rev) {
            const std::string err = tsm::convert_tileset_to_atlas(doc, atlas);
            if (!err.empty()) {
                return err;
            }
            has_atlas = true;
            atlas_rev = art_rev;
        }
        return {};
    }

    std::string export_all() {
        const std::string prep = ensure_atlas();
        if (!prep.empty()) {
            return prep;
        }
        nfdu8filteritem_t filter = {"PNG", "png"};
        nfdu8char_t* path = nullptr;
        const nfdresult_t r = NFD_SaveDialogU8(&path, &filter, 1, last_dir.empty() ? nullptr : last_dir.c_str(),
                                               (std::string(project_name) + ".png").c_str());
        if (r != NFD_OKAY) {
            return r == NFD_CANCEL ? std::string("#cancel") : "Save cancelled or failed";
        }
        std::string dest = path;
        NFD_FreePathU8(path);
        if (dest.size() < 4 || dest.substr(dest.size() - 4) != ".png") {
            dest += ".png";
        }
        last_dir = dirname_of(dest);
        std::string err = tsm::save_atlas_png(atlas, dest);
        if (!err.empty()) {
            return err;
        }
        if (export_5x3) {
            const auto dot = dest.find_last_of('.');
            const std::string side = dest.substr(0, dot) + "_5x3.png";
            err = tsm::save_tileset_png(doc, side);
            if (!err.empty()) {
                return err;
            }
        }
        if (export_header) {
            err = tsm::save_header(atlas, dest.substr(0, dest.find_last_of('.')) + ".h", dest);
            if (!err.empty()) {
                return err;
            }
        }
        if (export_terrain) {
            err = tsm::save_terrain(atlas, dest.substr(0, dest.find_last_of('.')) + ".terrain", dest);
            if (!err.empty()) {
                return err;
            }
        }
        return {};
    }

    ProjectData to_project() const {
        ProjectData data;
        data.name = project_name;
        data.step = project_step_from(step);
        data.seeded = seeded;
        data.stamped = stamped;
        data.specialty = specialty;
        data.atlas_cell = atlas_cell;
        data.preview_sel = preview_sel;
        data.tile_mode = tile_mode;
        data.export_header = export_header;
        data.export_terrain = export_terrain;
        data.export_5x3 = export_5x3;
        data.art_rev = art_rev;
        data.atlas_rev = atlas_rev;
        data.has_atlas = has_atlas;
        data.tileset = doc.snapshot();
        if (has_atlas) {
            data.atlas = atlas.snapshot();
        }
        return data;
    }

    void apply_project(const ProjectData& data, const std::string& path) {
        doc.restore(data.tileset);
        if (data.has_atlas) {
            atlas.restore(data.atlas);
        } else {
            atlas.reset(data.tileset.tile_size);
        }
        has_atlas = data.has_atlas;
        art_rev = data.art_rev;
        atlas_rev = data.atlas_rev;
        step = step_from_project(data.step);
        seeded = data.seeded;
        stamped = data.stamped;
        specialty = data.specialty;
        atlas_cell = data.atlas_cell;
        preview_sel = data.preview_sel;
        tile_mode = data.tile_mode;
        export_header = data.export_header;
        export_terrain = data.export_terrain;
        export_5x3 = data.export_5x3;
        std::snprintf(project_name, sizeof(project_name), "%s", data.name.c_str());
        project_path = path;
        if (!path.empty()) {
            last_dir = dirname_of(path);
        }
        dirty = false;
        undo_selections.clear();
        status = "Loaded project.";
        configure_view();
    }

    void reset_new(const std::string& name, int size) {
        const float keep_side = sidebar_w;
        *this = Editor();
        sidebar_w = keep_side;
        std::snprintf(project_name, sizeof(project_name), "%s", name.c_str());
        doc.reset(size);
        atlas.reset(size);
        dirty = false;
        status = "Paint the center fill tile.";
        configure_view();
    }

    std::string save_project(bool save_as) {
        if (!save_as && !project_path.empty()) {
            const std::string err = tsm::save_project(to_project(), project_path);
            if (err.empty()) {
                dirty = false;
            }
            return err;
        }
        nfdu8filteritem_t filter = {"Tileset project", "tilesetproj"};
        nfdu8char_t* path = nullptr;
        const std::string def = std::string(project_name) + ".tilesetproj";
        const nfdresult_t r = NFD_SaveDialogU8(&path, &filter, 1, last_dir.empty() ? nullptr : last_dir.c_str(),
                                               def.c_str());
        if (r != NFD_OKAY) {
            return r == NFD_CANCEL ? std::string("#cancel") : "Save cancelled or failed";
        }
        std::string dest = tsm::with_tilesetproj_ext(path);
        NFD_FreePathU8(path);
        last_dir = dirname_of(dest);
        const std::string err = tsm::save_project(to_project(), dest);
        if (err.empty()) {
            project_path = dest;
            dirty = false;
        }
        return err;
    }
};

struct UiState {
    bool show_settings = false;
    bool show_export = false;
    bool show_new = false;
    bool show_unsaved = false;
    bool project_open = false;
    bool new_from_welcome = true;
    PendingAction pending = PendingAction::None;
    int export_zoom = 4;
    char new_name[64] = "";
    int new_tile_size = 16;
    bool new_focus_name = false;
};

UiState g_ui;
tsm::Settings g_settings;
SDL_Window* g_window = nullptr;
std::string g_imgui_ini;

ImU32 canvas_tile_grid_color() {
    return im_color(g_settings.tile_grid_color);
}

ImU32 canvas_pixel_grid_color() {
    return im_color(g_settings.pixel_grid_color);
}

ImU32 grid_color() {
    return canvas_tile_grid_color();
}

void draw_pixels(ImDrawList* dl, ImVec2 origin, float zoom, int w, int h, const Editor& ed, int ox, int oy, int cols,
                 int rows, bool atlas, ImU32 grid_col, float grid_thickness = 1.0f, bool pixel_grid = false,
                 bool canvas_mode = false) {
    const int ts = atlas ? ed.atlas.tile_size : ed.doc.tile_size;
    if (ts <= 0 || zoom <= 0.0f) {
        return;
    }
    const ImU32 bg_a = g_settings.dark ? IM_COL32(31, 33, 41, 255) : IM_COL32(235, 237, 240, 255);
    const ImU32 bg_b = g_settings.dark ? IM_COL32(20, 23, 28, 255) : IM_COL32(215, 218, 222, 255);

    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const ImVec2 p0(origin.x + static_cast<float>(x) * zoom, origin.y + static_cast<float>(y) * zoom);
            const ImVec2 p1(origin.x + static_cast<float>(x + 1) * zoom, origin.y + static_cast<float>(y + 1) * zoom);

            if (canvas_mode && !atlas && ed.step == Step::Specialty) {
                const int gx = x / ts;
                const int gy = y / ts;
                const int lx = x % ts;
                const int ly = y % ts;

                if (ed.specialty == TilesetDoc::kInnerCorner) {
                    if (gx == 1 && gy == 1) {
                        const int idx = ed.doc.get_pixel(TilesetDoc::kInnerCorner.x, TilesetDoc::kInnerCorner.y, lx, ly);
                        dl->AddRectFilled(p0, p1, im_color(ed.doc.color_at(idx)));
                    } else if ((gx == 1 && gy == 0) || (gx == 1 && gy == 2)) {
                        // North and South tiles: tile 0,1 from the 12x4 atlas
                        const int idx = ed.atlas.get_pixel(0, 1, lx, ly);
                        dl->AddRectFilled(p0, p1, im_color(ed.atlas.color_at(idx)));
                    } else if ((gx == 0 && gy == 1) || (gx == 2 && gy == 1)) {
                        // West and East tiles: tile 2,3 from the 12x4 atlas
                        const int idx = ed.atlas.get_pixel(2, 3, lx, ly);
                        dl->AddRectFilled(p0, p1, im_color(ed.atlas.color_at(idx)));
                    } else {
                        const bool checker = (((x / 4) + (y / 4)) % 2 == 0);
                        dl->AddRectFilled(p0, p1, checker ? bg_a : bg_b);
                    }
                    continue;
                }

                const Cell cell = ed.src_to_cell(Cell{x, y});
                if (cell.x < 0 || cell.y < 0) {
                    const bool checker = (((x / 4) + (y / 4)) % 2 == 0);
                    dl->AddRectFilled(p0, p1, checker ? bg_a : bg_b);
                    continue;
                }
                const Cell loc = ed.src_local(Cell{x, y});
                const int idx = ed.doc.get_pixel(cell.x, cell.y, loc.x, loc.y);
                const Rgb c = ed.doc.color_at(idx);
                dl->AddRectFilled(p0, p1, im_color(c));
            } else {
                const int col = ox + x / ts;
                const int row = oy + y / ts;
                const int lx = x % ts;
                const int ly = y % ts;
                const int idx = atlas ? ed.atlas.get_pixel(col, row, lx, ly) : ed.doc.get_pixel(col, row, lx, ly);
                const Rgb c = atlas ? ed.atlas.color_at(idx) : ed.doc.color_at(idx);
                dl->AddRectFilled(p0, p1, im_color(c));
            }
        }
    }
    const float drawn_w = static_cast<float>(w * zoom);
    const float drawn_h = static_cast<float>(h * zoom);
    if (pixel_grid && zoom >= 3) {
        const ImU32 pixel_col = canvas_pixel_grid_color();
        for (int y = 1; y < h; ++y) {
            if (y % ts == 0) {
                continue;
            }
            const float py = origin.y + static_cast<float>(y * zoom);
            dl->AddLine(ImVec2(origin.x, py), ImVec2(origin.x + drawn_w, py), pixel_col);
        }
        for (int x = 1; x < w; ++x) {
            if (x % ts == 0) {
                continue;
            }
            const float px = origin.x + static_cast<float>(x * zoom);
            dl->AddLine(ImVec2(px, origin.y), ImVec2(px, origin.y + drawn_h), pixel_col);
        }
    }
    for (int row = 0; row <= rows; ++row) {
        const float y = origin.y + static_cast<float>(row * ts * zoom);
        dl->AddLine(ImVec2(origin.x, y), ImVec2(origin.x + drawn_w, y), grid_col, grid_thickness);
    }
    for (int col = 0; col <= cols; ++col) {
        const float x = origin.x + static_cast<float>(col * ts * zoom);
        dl->AddLine(ImVec2(x, origin.y), ImVec2(x, origin.y + drawn_h), grid_col, grid_thickness);
    }

    // In Step 3 canvas mode: highlight the center tile being edited
    if (canvas_mode && !atlas && ed.step == Step::Specialty) {
        const ImVec2 c0(origin.x + static_cast<float>(ts * zoom), origin.y + static_cast<float>(ts * zoom));
        const ImVec2 c1(c0.x + static_cast<float>(ts * zoom), c0.y + static_cast<float>(ts * zoom));
        dl->AddRect(c0, c1, IM_COL32(255, 220, 60, 220), 0.0f, 0, 2.0f);

        // If editing inner corner, show subtle quadrant cutouts inside center tile
        if (ed.specialty == TilesetDoc::kInnerCorner) {
            const float half_px = static_cast<float>((ts / 2) * zoom);
            const float tile_px = static_cast<float>(ts * zoom);
            const ImU32 cut_col = IM_COL32(90, 190, 255, 140);
            dl->AddRect(c0, ImVec2(c0.x + half_px, c0.y + half_px), cut_col, 0.0f, 0, 1.5f);
            dl->AddRect(ImVec2(c0.x + half_px, c0.y), ImVec2(c0.x + tile_px, c0.y + half_px), cut_col, 0.0f, 0, 1.5f);
            dl->AddRect(ImVec2(c0.x, c0.y + half_px), ImVec2(c0.x + half_px, c0.y + tile_px), cut_col, 0.0f, 0, 1.5f);
            dl->AddRect(ImVec2(c0.x + half_px, c0.y + half_px), ImVec2(c0.x + tile_px, c0.y + tile_px), cut_col, 0.0f, 0, 1.5f);
        }
    }
}

void draw_preview_grid(Editor& ed, const char* title, int cols, int rows, bool atlas, int ox = 0, int oy = 0) {
    ImGui::TextUnformatted(title);
    const int ts = ed.tile_size();
    const float avail_w = ImGui::GetContentRegionAvail().x;
    const float grid_px_w = static_cast<float>(std::max(1, cols * ts));
    const float z = std::max(0.1f, avail_w / grid_px_w);
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImVec2 size(avail_w, static_cast<float>(rows * ts) * z);
    ImGui::InvisibleButton("preview", size);
    draw_pixels(ImGui::GetWindowDrawList(), origin, z, cols * ts, rows * ts, ed, ox, oy, cols, rows, atlas, grid_color());
    if (ImGui::IsItemClicked()) {
        const ImVec2 mp = ImGui::GetIO().MousePos;
        const int col = ox + static_cast<int>((mp.x - origin.x) / (static_cast<float>(ts) * z));
        const int row = oy + static_cast<int>((mp.y - origin.y) / (static_cast<float>(ts) * z));
        if (col >= ox && row >= oy && col < ox + cols && row < oy + rows) {
            if (atlas) {
                ed.atlas_cell = {col, row};
            } else if (ed.step == Step::Specialty) {
                ed.specialty = {col, row};
                ed.preview_sel = {col, row};
            } else if (ed.step == Step::Edges && col < 3 && row < 3) {
                ed.preview_sel = {col, row};
            } else if (ed.step == Step::Center) {
                ed.preview_sel = {col, row};
            }
            ed.configure_view();
        }
    }
    Cell sel = atlas ? ed.atlas_cell : ed.preview_sel;
    if (sel.x >= ox && sel.x < ox + cols && sel.y >= oy && sel.y < oy + rows) {
        const ImVec2 s0(origin.x + static_cast<float>((sel.x - ox) * ts) * z,
                        origin.y + static_cast<float>((sel.y - oy) * ts) * z);
        ImGui::GetWindowDrawList()->AddRect(s0,
                                            ImVec2(s0.x + static_cast<float>(ts) * z, s0.y + static_cast<float>(ts) * z),
                                            IM_COL32(255, 220, 60, 255), 0, 0, 2.0f);
    }
}

void copy_selection(Editor& ed) {
    if (ed.floating && !ed.floating_pixels.empty()) {
        ed.clipboard.w = ed.floating_w;
        ed.clipboard.h = ed.floating_h;
        ed.clipboard.pixels = ed.floating_pixels;
        ed.status = "Copied " + std::to_string(ed.clipboard.w) + "x" + std::to_string(ed.clipboard.h) + " selection.";
        return;
    }
    if (ed.sel_a.x < 0 || ed.sel_b.x < 0) {
        return;
    }
    const int x0 = std::min(ed.sel_a.x, ed.sel_b.x);
    const int y0 = std::min(ed.sel_a.y, ed.sel_b.y);
    const int x1 = std::max(ed.sel_a.x, ed.sel_b.x);
    const int y1 = std::max(ed.sel_a.y, ed.sel_b.y);
    ed.clipboard.w = x1 - x0 + 1;
    ed.clipboard.h = y1 - y0 + 1;
    ed.clipboard.pixels.resize(static_cast<size_t>(ed.clipboard.w * ed.clipboard.h));
    for (int y = 0; y < ed.clipboard.h; ++y) {
        for (int x = 0; x < ed.clipboard.w; ++x) {
            const Cell src{x0 + x, y0 + y};
            const Cell cell = ed.src_to_cell(src);
            const Cell loc = ed.src_local(src);
            if (ed.in_doc(cell.x, cell.y)) {
                ed.clipboard.pixels[static_cast<size_t>(y * ed.clipboard.w + x)] =
                    static_cast<uint8_t>(ed.get_px(cell.x, cell.y, loc.x, loc.y));
            } else {
                ed.clipboard.pixels[static_cast<size_t>(y * ed.clipboard.w + x)] = 0;
            }
        }
    }
    ed.status = "Copied " + std::to_string(ed.clipboard.w) + "x" + std::to_string(ed.clipboard.h) + " selection.";
}

void cancel_floating(Editor& ed) {
    if (!ed.floating) {
        return;
    }
    if (ed.floating_type == Editor::FloatingType::MoveSelection) {
        ed.do_undo();
        ed.sel_a = ed.move_orig_a;
        ed.sel_b = ed.move_orig_b;
    }
    ed.floating = false;
    ed.floating_type = Editor::FloatingType::None;
    ed.floating_pixels.clear();
    ed.floating_dragging = false;
    ed.status = "Cancelled.";
}

void commit_floating(Editor& ed) {
    if (!ed.floating) {
        return;
    }
    if (ed.floating_type == Editor::FloatingType::MoveSelection) {
        if (ed.floating_pos.x == ed.move_orig_a.x && ed.floating_pos.y == ed.move_orig_a.y) {
            ed.do_undo();
            ed.sel_a = ed.move_orig_a;
            ed.sel_b = ed.move_orig_b;
            ed.floating = false;
            ed.floating_type = Editor::FloatingType::None;
            ed.floating_pixels.clear();
            ed.floating_dragging = false;
            ed.status = "Placed.";
            return;
        }
    }
    if (ed.floating_type == Editor::FloatingType::Paste) {
        ed.push_undo();
    }
    for (int y = 0; y < ed.floating_h; ++y) {
        for (int x = 0; x < ed.floating_w; ++x) {
            const int px = ed.floating_pos.x + x;
            const int py = ed.floating_pos.y + y;
            ed.plot_src({px, py}, ed.floating_pixels[static_cast<size_t>(y * ed.floating_w + x)]);
        }
    }
    ed.bump_art();
    ed.sel_a = ed.floating_pos;
    ed.sel_b = {ed.floating_pos.x + ed.floating_w - 1, ed.floating_pos.y + ed.floating_h - 1};
    ed.floating = false;
    ed.floating_type = Editor::FloatingType::None;
    ed.floating_pixels.clear();
    ed.floating_dragging = false;
    ed.status = "Placed.";
}

void start_selection_move(Editor& ed) {
    if (!ed.has_selection()) {
        return;
    }
    if (ed.floating) {
        commit_floating(ed);
    }
    const int x0 = std::min(ed.sel_a.x, ed.sel_b.x);
    const int y0 = std::min(ed.sel_a.y, ed.sel_b.y);
    const int x1 = std::max(ed.sel_a.x, ed.sel_b.x);
    const int y1 = std::max(ed.sel_a.y, ed.sel_b.y);
    const int w = x1 - x0 + 1;
    const int h = y1 - y0 + 1;

    ed.push_undo();

    ed.floating = true;
    ed.floating_type = Editor::FloatingType::MoveSelection;
    ed.floating_pos = {x0, y0};
    ed.floating_w = w;
    ed.floating_h = h;
    ed.move_orig_a = {x0, y0};
    ed.move_orig_b = {x1, y1};
    ed.sel_a = {x0, y0};
    ed.sel_b = {x1, y1};
    ed.floating_pixels.resize(static_cast<size_t>(w * h));

    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const Cell src{x0 + x, y0 + y};
            const Cell cell = ed.src_to_cell(src);
            const Cell loc = ed.src_local(src);
            if (ed.in_doc(cell.x, cell.y)) {
                ed.floating_pixels[static_cast<size_t>(y * w + x)] =
                    static_cast<uint8_t>(ed.get_px(cell.x, cell.y, loc.x, loc.y));
                ed.set_px(cell.x, cell.y, loc.x, loc.y, 0);
            } else {
                ed.floating_pixels[static_cast<size_t>(y * w + x)] = 0;
            }
        }
    }
    ed.bump_art();
    ed.status = "Moving selection: Arrow keys or click and drag. Enter or click outside to place, Esc to cancel.";
}

void start_paste(Editor& ed) {
    if (!ed.clipboard.valid()) {
        ed.status = "Clipboard is empty.";
        return;
    }
    if (ed.floating) {
        commit_floating(ed);
    }
    ed.tool = Tool::Select;
    ed.floating = true;
    ed.floating_type = Editor::FloatingType::Paste;
    ed.floating_w = ed.clipboard.w;
    ed.floating_h = ed.clipboard.h;
    ed.floating_pixels = ed.clipboard.pixels;
    const int sw = ed.src_w();
    const int sh = ed.src_h();
    if (ed.has_selection()) {
        ed.floating_pos = {std::min(ed.sel_a.x, ed.sel_b.x), std::min(ed.sel_a.y, ed.sel_b.y)};
    } else if (ed.hover.x >= 0 && ed.hover.y >= 0) {
        ed.floating_pos.x = std::clamp(ed.hover.x - ed.floating_w / 2, 0, std::max(0, sw - ed.floating_w));
        ed.floating_pos.y = std::clamp(ed.hover.y - ed.floating_h / 2, 0, std::max(0, sh - ed.floating_h));
    } else {
        ed.floating_pos.x = std::clamp((sw - ed.floating_w) / 2, 0, std::max(0, sw - ed.floating_w));
        ed.floating_pos.y = std::clamp((sh - ed.floating_h) / 2, 0, std::max(0, sh - ed.floating_h));
    }
    ed.status = "Paste: Arrow keys or click and drag. Enter or click outside to place, Esc to cancel.";
}

void paste_clipboard(Editor& ed) {
    start_paste(ed);
}

void commit_paste(Editor& ed) {
    commit_floating(ed);
}

void cancel_paste(Editor& ed) {
    cancel_floating(ed);
}

void handle_canvas(Editor& ed) {
    if (ed.step == Step::Specialty) {
        ed.ensure_atlas();
    }
    const int ts = ed.tile_size();
    const int sw = ed.src_w();
    const int sh = ed.src_h();
    const int reps = ed.reps();
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const int src_px_w = std::max(1, sw * reps);
    const int src_px_h = std::max(1, sh * reps);
    const float pad = 8.0f;
    const int zx = static_cast<int>((avail.x - pad) / static_cast<float>(src_px_w));
    const int zy = static_cast<int>((avail.y - pad) / static_cast<float>(src_px_h));
    ed.zoom = std::max(1, std::min(zx, zy));
    const float drawn_w = static_cast<float>(src_px_w * ed.zoom);
    const float drawn_h = static_cast<float>(src_px_h * ed.zoom);
    const ImVec2 cursor = ImGui::GetCursorScreenPos();
    const ImVec2 origin(cursor.x + std::max(0.0f, (avail.x - drawn_w) * 0.5f),
                        cursor.y + std::max(0.0f, (avail.y - drawn_h) * 0.5f));
    ImGui::InvisibleButton("canvas", ImVec2(std::max(1.0f, avail.x), std::max(1.0f, avail.y)));
    ImDrawList* dl = ImGui::GetWindowDrawList();
    for (int ry = 0; ry < reps; ++ry) {
        for (int rx = 0; rx < reps; ++rx) {
            const ImVec2 o(origin.x + static_cast<float>(rx * sw * ed.zoom),
                            origin.y + static_cast<float>(ry * sh * ed.zoom));
            draw_pixels(dl, o, ed.zoom, sw, sh, ed, ed.edit_ox, ed.edit_oy, ed.edit_cols, ed.edit_rows, !ed.art_step(),
                        canvas_tile_grid_color(), 2.0f, g_settings.pixel_grid, /*canvas_mode=*/true);
        }
    }

    auto pos_to_src = [&](ImVec2 p) -> Cell {
        const int lx = static_cast<int>(p.x - origin.x);
        const int ly = static_cast<int>(p.y - origin.y);
        if (lx < 0 || ly < 0 || ed.zoom <= 0) {
            return {-1, -1};
        }
        if (lx >= sw * reps * ed.zoom || ly >= sh * reps * ed.zoom) {
            return {-1, -1};
        }
        int px = (lx / ed.zoom) % sw;
        int py = (ly / ed.zoom) % sh;
        if (px < 0) px += sw;
        if (py < 0) py += sh;
        return {px, py};
    };

    if (ImGui::IsItemHovered()) {
        ed.hover = pos_to_src(ImGui::GetIO().MousePos);
    } else {
        ed.hover = {-1, -1};
    }

    if (ed.floating) {
        if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            cancel_floating(ed);
        } else if (ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter)) {
            commit_floating(ed);
        } else if (!ed.floating_dragging) {
            int dx = 0, dy = 0;
            if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow, true))  dx -= 1;
            if (ImGui::IsKeyPressed(ImGuiKey_RightArrow, true)) dx += 1;
            if (ImGui::IsKeyPressed(ImGuiKey_UpArrow, true))    dy -= 1;
            if (ImGui::IsKeyPressed(ImGuiKey_DownArrow, true))  dy += 1;
            if (dx != 0 || dy != 0) {
                ed.floating_pos.x = std::clamp(ed.floating_pos.x + dx, 0, std::max(0, sw - ed.floating_w));
                ed.floating_pos.y = std::clamp(ed.floating_pos.y + dy, 0, std::max(0, sh - ed.floating_h));
            }
        }

        if (ed.floating) {
            const ImVec2 mpos = ImGui::GetIO().MousePos;
            const bool inside = ed.in_floating_box(ed.hover);
            if (inside) {
                ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
            }

            if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
                if (inside) {
                    ed.floating_dragging = true;
                    ed.floating_drag_start_pos = ed.floating_pos;
                    ed.floating_drag_start_mouse = mpos;
                } else {
                    commit_floating(ed);
                }
            }

            if (ed.floating && ed.floating_dragging && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                const float dmx = mpos.x - ed.floating_drag_start_mouse.x;
                const float dmy = mpos.y - ed.floating_drag_start_mouse.y;
                if (ed.zoom > 0) {
                    const int dpx = static_cast<int>(std::round(dmx / static_cast<float>(ed.zoom)));
                    const int dpy = static_cast<int>(std::round(dmy / static_cast<float>(ed.zoom)));
                    ed.floating_pos.x = std::clamp(ed.floating_drag_start_pos.x + dpx, 0, std::max(0, sw - ed.floating_w));
                    ed.floating_pos.y = std::clamp(ed.floating_drag_start_pos.y + dpy, 0, std::max(0, sh - ed.floating_h));
                }
            }

            if (ed.floating && ed.floating_dragging && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
                ed.floating_dragging = false;
            }
        }
    } else {
        if (ed.tool == Tool::Select && ed.has_selection()) {
            if (ed.in_selection(ed.hover)) {
                ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
            }
            int dx = 0, dy = 0;
            if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow, true))  dx -= 1;
            if (ImGui::IsKeyPressed(ImGuiKey_RightArrow, true)) dx += 1;
            if (ImGui::IsKeyPressed(ImGuiKey_UpArrow, true))    dy -= 1;
            if (ImGui::IsKeyPressed(ImGuiKey_DownArrow, true))  dy += 1;
            if (dx != 0 || dy != 0) {
                start_selection_move(ed);
                ed.floating_pos.x = std::clamp(ed.floating_pos.x + dx, 0, std::max(0, sw - ed.floating_w));
                ed.floating_pos.y = std::clamp(ed.floating_pos.y + dy, 0, std::max(0, sh - ed.floating_h));
            }
        }

        if (ImGui::IsItemClicked(ImGuiMouseButton_Right) && ed.hover.x >= 0) {
            const int px = ed.sample_hover_pixel(ed.hover);
            if (px >= 0) {
                ed.select_single_palette(px);
            }
        }

        const bool stroke_tool = ed.tool == Tool::Line || ed.tool == Tool::Square || ed.tool == Tool::Circle;
        if (ImGui::IsItemClicked(ImGuiMouseButton_Left) && ed.hover.x >= 0) {
            if (ed.tool == Tool::Select) {
                if (ed.has_selection() && ed.in_selection(ed.hover)) {
                    start_selection_move(ed);
                    ed.floating_dragging = true;
                    ed.floating_drag_start_pos = ed.floating_pos;
                    ed.floating_drag_start_mouse = ImGui::GetIO().MousePos;
                } else {
                    const ImVec2 mp = ImGui::GetIO().MousePos;
                    const float lx = mp.x - origin.x;
                    const float ly = mp.y - origin.y;
                    const int rx = std::clamp(static_cast<int>(lx / (sw * ed.zoom)), 0, reps - 1);
                    const int ry = std::clamp(static_cast<int>(ly / (sh * ed.zoom)), 0, reps - 1);
                    ed.sel_tile = {rx, ry};
                    const int px = std::clamp(static_cast<int>(std::floor(lx / static_cast<float>(ed.zoom))) - rx * sw, 0, sw - 1);
                    const int py = std::clamp(static_cast<int>(std::floor(ly / static_cast<float>(ed.zoom))) - ry * sh, 0, sh - 1);
                    ed.sel_a = {px, py};
                    ed.sel_b = {px, py};
                    ed.selecting = true;
                }
            } else if (stroke_tool) {
                ed.stroke_from = ed.hover;
                ed.stroke_to = ed.hover;
                ed.stroke_pending = true;
            } else if (ed.tool == Tool::Eyedropper) {
                const int px = ed.sample_hover_pixel(ed.hover);
                if (px >= 0) {
                    ed.select_single_palette(px);
                }
            } else if (ed.tool == Tool::Fill) {
                if (ed.has_selection() && !ed.in_selection(ed.hover)) {
                    // Clicked outside selection: do nothing
                } else if (!ed.has_selection()) {
                    const Cell cell = ed.src_to_cell(ed.hover);
                    if (ed.in_doc(cell.x, cell.y)) {
                        const Cell loc = ed.src_local(ed.hover);
                        ed.push_undo();
                        if (ed.art_step()) {
                            ed.doc.flood_fill(cell.x, cell.y, loc.x, loc.y, ed.paint_index);
                        } else {
                            ed.atlas.flood_fill(cell.x, cell.y, loc.x, loc.y, ed.paint_index);
                        }
                        ed.bump_art();
                    }
                } else {
                    const Cell start_cell = ed.src_to_cell(ed.hover);
                    if (ed.in_doc(start_cell.x, start_cell.y)) {
                        const Cell start_loc = ed.src_local(ed.hover);
                        const int old = ed.get_px(start_cell.x, start_cell.y, start_loc.x, start_loc.y);
                        if (old != ed.paint_index) {
                            ed.push_undo();
                            std::vector<Cell> stack{ed.hover};
                            std::vector<uint8_t> seen(static_cast<size_t>(sw * sh), 0);
                            while (!stack.empty()) {
                                const Cell p = stack.back();
                                stack.pop_back();
                                if (p.x < 0 || p.y < 0 || p.x >= sw || p.y >= sh) continue;
                                if (!ed.in_selection(p)) continue;
                                const size_t key = static_cast<size_t>(p.y * sw + p.x);
                                if (seen[key]) continue;
                                seen[key] = 1;
                                const Cell c = ed.src_to_cell(p);
                                const Cell loc = ed.src_local(p);
                                if (!ed.in_doc(c.x, c.y)) continue;
                                if (ed.get_px(c.x, c.y, loc.x, loc.y) != old) continue;
                                ed.set_px(c.x, c.y, loc.x, loc.y, ed.paint_index);
                                stack.push_back({p.x + 1, p.y});
                                stack.push_back({p.x - 1, p.y});
                                stack.push_back({p.x, p.y + 1});
                                stack.push_back({p.x, p.y - 1});
                            }
                            ed.bump_art();
                        }
                    }
                }
            } else {
                ed.stroke_has_drawn = false;
                ed.dragging = true;
                if (!ed.has_selection() || ed.in_selection(ed.hover)) {
                    if (ed.can_stamp(ed.hover)) {
                        ed.push_undo();
                        ed.stroke_has_drawn = true;
                        if (ed.stamp_src(ed.hover, ed.tool == Tool::Eraser ? 0 : ed.paint_index)) {
                            ed.bump_art();
                        }
                    }
                }
            }
        }
        if (ed.dragging && ImGui::IsMouseDown(ImGuiMouseButton_Left) && ed.hover.x >= 0) {
            if (!ed.stroke_has_drawn && ed.can_stamp(ed.hover)) {
                ed.push_undo();
                ed.stroke_has_drawn = true;
            }
            if (ed.stamp_src(ed.hover, ed.tool == Tool::Eraser ? 0 : ed.paint_index)) {
                ed.bump_art();
            }
        }
        if (ed.selecting && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            const ImVec2 mp = ImGui::GetIO().MousePos;
            const float lx = mp.x - origin.x;
            const float ly = mp.y - origin.y;
            const int raw_x = static_cast<int>(std::floor(lx / static_cast<float>(ed.zoom))) - ed.sel_tile.x * sw;
            const int raw_y = static_cast<int>(std::floor(ly / static_cast<float>(ed.zoom))) - ed.sel_tile.y * sh;
            ed.sel_b.x = std::clamp(raw_x, 0, sw - 1);
            ed.sel_b.y = std::clamp(raw_y, 0, sh - 1);
        }
        if (ed.stroke_pending && ImGui::IsMouseDown(ImGuiMouseButton_Left) && ed.hover.x >= 0) {
            ed.stroke_to = ed.hover;
        }
        if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
            if (ed.selecting) {
                ed.selecting = false;
                const bool dragged = ImGui::GetIO().MouseDragMaxDistanceSqr[0] >= 9.0f;
                if (!dragged && ed.sel_a.x == ed.sel_b.x && ed.sel_a.y == ed.sel_b.y) {
                    ed.clear_selection();
                } else {
                    const int x0 = std::min(ed.sel_a.x, ed.sel_b.x);
                    const int y0 = std::min(ed.sel_a.y, ed.sel_b.y);
                    const int x1 = std::max(ed.sel_a.x, ed.sel_b.x);
                    const int y1 = std::max(ed.sel_a.y, ed.sel_b.y);
                    ed.sel_a = {x0, y0};
                    ed.sel_b = {x1, y1};
                    ed.status = "Selected " + std::to_string(x1 - x0 + 1) + "x" + std::to_string(y1 - y0 + 1) +
                                ". Arrow keys or click and drag to move.";
                }
            }
            if (ed.stroke_pending && ed.stroke_from.x >= 0) {
                std::vector<Cell> pts;
                if (ed.tool == Tool::Square) {
                    pts = tsm::rect_outline(ed.stroke_from, ed.stroke_to);
                } else if (ed.tool == Tool::Circle) {
                    pts = tsm::ellipse_outline(ed.stroke_from, ed.stroke_to);
                } else {
                    pts = tsm::bresenham(ed.stroke_from, ed.stroke_to);
                }
                bool can_draw = false;
                for (Cell p : pts) {
                    if (ed.can_stamp(p)) {
                        can_draw = true;
                        break;
                    }
                }
                if (can_draw) {
                    ed.push_undo();
                    for (Cell p : pts) {
                        ed.stamp_src(p, ed.paint_index);
                    }
                    ed.bump_art();
                }
            }
            ed.dragging = false;
            ed.stroke_has_drawn = false;
            ed.stroke_pending = false;
            ed.stroke_from = {-1, -1};
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            ed.clear_selection();
        }
    }

    if (ed.floating && !ed.floating_pixels.empty()) {
        for (int ry = 0; ry < reps; ++ry) {
            for (int rx = 0; rx < reps; ++rx) {
                const ImVec2 to(origin.x + static_cast<float>(rx * sw * ed.zoom),
                                origin.y + static_cast<float>(ry * sh * ed.zoom));
                for (int y = 0; y < ed.floating_h; ++y) {
                    for (int x = 0; x < ed.floating_w; ++x) {
                        const int px = ed.floating_pos.x + x;
                        const int py = ed.floating_pos.y + y;
                        if (px < 0 || px >= sw || py < 0 || py >= sh) continue;
                        const uint8_t c_idx = ed.floating_pixels[static_cast<size_t>(y * ed.floating_w + x)];
                        const Rgb c = ed.color(c_idx);
                        const ImVec2 p0(to.x + static_cast<float>(px * ed.zoom),
                                        to.y + static_cast<float>(py * ed.zoom));
                        const ImVec2 p1(p0.x + static_cast<float>(ed.zoom),
                                        p0.y + static_cast<float>(ed.zoom));
                        dl->AddRectFilled(p0, p1, im_color(c, 240));
                    }
                }
                const ImVec2 b0(to.x + static_cast<float>(ed.floating_pos.x * ed.zoom),
                                to.y + static_cast<float>(ed.floating_pos.y * ed.zoom));
                const ImVec2 b1(to.x + static_cast<float>((ed.floating_pos.x + ed.floating_w) * ed.zoom),
                                to.y + static_cast<float>((ed.floating_pos.y + ed.floating_h) * ed.zoom));
                dl->AddRect(ImVec2(b0.x - 1.0f, b0.y - 1.0f), ImVec2(b1.x + 1.0f, b1.y + 1.0f),
                            IM_COL32(0, 0, 0, 180), 0.0f, 0, 1.0f);
                dl->AddRect(b0, b1, IM_COL32(255, 220, 60, 255), 0.0f, 0, 2.0f);
            }
        }
    } else if (ed.has_selection()) {
        const int x0 = std::min(ed.sel_a.x, ed.sel_b.x);
        const int y0 = std::min(ed.sel_a.y, ed.sel_b.y);
        const int x1 = std::max(ed.sel_a.x, ed.sel_b.x) + 1;
        const int y1 = std::max(ed.sel_a.y, ed.sel_b.y) + 1;
        for (int ry = 0; ry < reps; ++ry) {
            for (int rx = 0; rx < reps; ++rx) {
                const ImVec2 to(origin.x + static_cast<float>(rx * sw * ed.zoom),
                                origin.y + static_cast<float>(ry * sh * ed.zoom));
                dl->AddRect(ImVec2(to.x + static_cast<float>(x0 * ed.zoom), to.y + static_cast<float>(y0 * ed.zoom)),
                            ImVec2(to.x + static_cast<float>(x1 * ed.zoom), to.y + static_cast<float>(y1 * ed.zoom)),
                            IM_COL32(255, 255, 255, 220), 0, 0, 2.0f);
            }
        }
    }
    if (ed.stroke_pending && ed.stroke_from.x >= 0) {
        const Rgb c = ed.color(ed.paint_index);
        std::vector<Cell> pts = (ed.tool == Tool::Square)   ? tsm::rect_outline(ed.stroke_from, ed.stroke_to)
                                : (ed.tool == Tool::Circle) ? tsm::ellipse_outline(ed.stroke_from, ed.stroke_to)
                                                            : tsm::bresenham(ed.stroke_from, ed.stroke_to);
        for (int ry = 0; ry < reps; ++ry) {
            for (int rx = 0; rx < reps; ++rx) {
                const ImVec2 to(origin.x + static_cast<float>(rx * sw * ed.zoom),
                                origin.y + static_cast<float>(ry * sh * ed.zoom));
                for (Cell p : pts) {
                    for (int dy = 0; dy < ed.brush; ++dy) {
                        for (int dx = 0; dx < ed.brush; ++dx) {
                            const Cell pt{p.x + dx, p.y + dy};
                            if (ed.has_selection() && !ed.in_selection(pt)) {
                                continue;
                            }
                            const ImVec2 p0(to.x + static_cast<float>(pt.x * ed.zoom),
                                            to.y + static_cast<float>(pt.y * ed.zoom));
                            dl->AddRectFilled(p0, ImVec2(p0.x + static_cast<float>(ed.zoom), p0.y + static_cast<float>(ed.zoom)),
                                              im_color(c, 180));
                        }
                    }
                }
            }
        }
    }
    (void)ts;
}

std::vector<std::pair<int, int>> selected_palette_runs(const Editor& ed) {
    std::vector<std::pair<int, int>> runs;
    std::vector<int> indices;
    const int n = ed.art_step() ? ed.doc.palette_count() : ed.atlas.palette_count();
    for (int i = 1; i < n; ++i) {
        if (ed.is_palette_selected(i)) {
            indices.push_back(i);
        }
    }
    if (indices.empty()) {
        return runs;
    }
    int lo = indices[0];
    int prev = lo;
    for (size_t i = 1; i < indices.size(); ++i) {
        int idx = indices[i];
        if (idx == prev + 1) {
            prev = idx;
            continue;
        }
        runs.push_back({lo, prev});
        lo = idx;
        prev = idx;
    }
    runs.push_back({lo, prev});
    return runs;
}

bool can_shift_palette(const Editor& ed, int delta) {
    if (delta != 1 && delta != -1) return false;
    const int n = ed.art_step() ? ed.doc.palette_count() : ed.atlas.palette_count();
    auto runs = selected_palette_runs(ed);
    for (const auto& run : runs) {
        if (run.first + delta >= 1 && run.second + delta < n) {
            return true;
        }
    }
    return false;
}

bool shift_selected_palette(Editor& ed, int delta) {
    if (delta != 1 && delta != -1) return false;
    const int n = ed.art_step() ? ed.doc.palette_count() : ed.atlas.palette_count();
    auto runs = selected_palette_runs(ed);
    if (runs.empty()) return false;

    if (delta > 0) {
        std::reverse(runs.begin(), runs.end());
    }

    std::vector<std::pair<int, int>> moving;
    for (const auto& run : runs) {
        if (run.first + delta >= 1 && run.second + delta < n) {
            moving.push_back(run);
        }
    }
    if (moving.empty()) return false;

    ed.push_undo();
    std::vector<bool> moved(16, false);
    for (const auto& run : moving) {
        bool ok = false;
        if (delta > 0) {
            ok = ed.art_step() ? ed.doc.reorder_palette(run.second + 1, run.first)
                               : ed.atlas.reorder_palette(run.second + 1, run.first);
        } else {
            ok = ed.art_step() ? ed.doc.reorder_palette(run.first - 1, run.second)
                               : ed.atlas.reorder_palette(run.first - 1, run.second);
        }
        if (!ok) continue;
        for (int i = run.first; i <= run.second; ++i) {
            moved[i] = true;
        }
    }

    uint16_t next_mask = 0;
    for (int i = 0; i < 16; ++i) {
        if (ed.palette_selected_mask & (1 << i)) {
            if (moved[i]) {
                next_mask |= static_cast<uint16_t>(1 << (i + delta));
            } else {
                next_mask |= static_cast<uint16_t>(1 << i);
            }
        }
    }
    ed.palette_selected_mask = next_mask;
    if (ed.paint_index >= 1 && ed.paint_index < 16 && moved[ed.paint_index]) {
        ed.paint_index += delta;
    }
    if (ed.palette_anchor >= 1 && ed.palette_anchor < 16 && moved[ed.palette_anchor]) {
        ed.palette_anchor += delta;
    }
    ed.bump_art();
    ed.touch();
    return true;
}

void draw_palette(Editor& ed) {
    const int n = ed.art_step() ? ed.doc.palette_count() : ed.atlas.palette_count();
    ImGui::Text("Palette (%d)", n);
    if (ed.paint_index >= 0 && ed.paint_index < n) {
        Rgb c = ed.color(ed.paint_index);
        float col[3] = {c.r / 255.0f, c.g / 255.0f, c.b / 255.0f};
        const float picker_w = std::min(ImGui::GetContentRegionAvail().x, 120.0f * g_settings.scale);
        ImGui::SetNextItemWidth(picker_w);
        if (ImGui::ColorPicker3("##picker", col,
                                ImGuiColorEditFlags_PickerHueBar |
                                ImGuiColorEditFlags_NoSidePreview |
                                ImGuiColorEditFlags_NoAlpha |
                                ImGuiColorEditFlags_NoInputs |
                                ImGuiColorEditFlags_NoLabel |
                                ImGuiColorEditFlags_NoOptions)) {
            Rgb next = MdColor::quantize(Rgb{static_cast<uint8_t>(col[0] * 255.0f + 0.5f),
                                             static_cast<uint8_t>(col[1] * 255.0f + 0.5f),
                                             static_cast<uint8_t>(col[2] * 255.0f + 0.5f)});
            if (ed.paint_index == 0) {
                next = Rgb{0, 0, 0};
            }
            if (ImGui::IsItemActivated()) {
                ed.push_undo();
            }
            for (int i = 1; i < n; ++i) {
                if (ed.is_palette_selected(i)) {
                    if (ed.art_step()) {
                        ed.doc.set_palette_color(i, next);
                    } else {
                        ed.atlas.set_palette_color(i, next);
                    }
                }
            }
            if (ed.paint_index == 0) {
                if (ed.art_step()) {
                    ed.doc.set_palette_color(0, Rgb{0, 0, 0});
                } else {
                    ed.atlas.set_palette_color(0, Rgb{0, 0, 0});
                }
            }
            ed.bump_art();
        }
        ImGui::Spacing();
    }
    const float swatch = 22.0f * g_settings.scale;
    for (int i = 0; i < TilesetDoc::kPaletteSize; ++i) {
        if (i % 8 != 0) {
            ImGui::SameLine();
        }
        ImGui::PushID(i);
        if (i < n) {
            const Rgb c = ed.color(i);
            float col[3] = {c.r / 255.0f, c.g / 255.0f, c.b / 255.0f};
            if (ImGui::ColorButton("##sw", ImVec4(col[0], col[1], col[2], 1), ImGuiColorEditFlags_NoTooltip,
                                   ImVec2(swatch, swatch))) {
                if (ImGui::GetIO().KeyShift) {
                    ed.select_range_palette(i);
                } else if (ImGui::GetIO().KeyCtrl || ImGui::GetIO().KeySuper) {
                    ed.toggle_palette_selected(i);
                } else {
                    ed.select_single_palette(i);
                }
            }
            if (ed.paint_index == i) {
                ImGui::GetWindowDrawList()->AddRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(),
                                                    IM_COL32(255, 220, 60, 255), 0, 0, 2.0f);
            } else if (ed.is_palette_selected(i)) {
                ImGui::GetWindowDrawList()->AddRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(),
                                                    IM_COL32(100, 200, 255, 255), 0, 0, 2.0f);
            }
        } else {
            const ImVec2 p0 = ImGui::GetCursorScreenPos();
            ImGui::Dummy(ImVec2(swatch, swatch));
            const ImVec2 p1(p0.x + swatch, p0.y + swatch);
            ImGui::GetWindowDrawList()->AddRectFilled(p0, p1, IM_COL32(0, 0, 0, 40));
            ImGui::GetWindowDrawList()->AddRect(p0, p1, grid_color());
        }
        ImGui::PopID();
    }
    ImGui::Spacing();
    const bool can_left = can_shift_palette(ed, -1);
    const bool can_right = can_shift_palette(ed, 1);
    if (!can_left) {
        ImGui::BeginDisabled();
    }
    if (ImGui::Button("< Shift Left")) {
        shift_selected_palette(ed, -1);
    }
    if (!can_left) {
        ImGui::EndDisabled();
    }
    ImGui::SameLine();
    if (!can_right) {
        ImGui::BeginDisabled();
    }
    if (ImGui::Button("Shift Right >")) {
        shift_selected_palette(ed, 1);
    }
    if (!can_right) {
        ImGui::EndDisabled();
    }
    if (ImGui::BeginCombo("Preset", "Apply preset")) {
        for (const auto& name : PalettePresets::names()) {
            if (ImGui::Selectable(name.c_str())) {
                ed.push_undo();
                const auto colors = PalettePresets::colors_for(name);
                if (ed.art_step()) {
                    ed.doc.apply_palette(colors);
                } else {
                    ed.atlas.apply_palette(colors);
                }
                ed.paint_index = std::min(ed.paint_index, ed.last_index());
                ed.select_single_palette(ed.paint_index);
                ed.bump_art();
            }
        }
        ImGui::EndCombo();
    }
    if (ImGui::Button("Add Color")) {
        if (ed.art_step()) {
            ed.doc.grow_palette();
        } else {
            ed.atlas.grow_palette();
        }
        ed.touch();
    }
    ImGui::SameLine();
    if (ImGui::Button("Remove Unused Colors")) {
        ed.push_undo();
        if (ed.art_step()) {
            ed.doc.compact_unused();
        } else {
            ed.atlas.compact_unused();
        }
        const int count = ed.art_step() ? ed.doc.palette_count() : ed.atlas.palette_count();
        ed.paint_index = std::clamp(ed.paint_index, 0, count - 1);
        ed.select_single_palette(ed.paint_index);
        ed.bump_art();
    }
    if (ImGui::Button("Load .palette")) {
        nfdu8filteritem_t filter = {"Palette", "palette"};
        nfdu8char_t* path = nullptr;
        if (NFD_OpenDialogU8(&path, &filter, 1, nullptr) == NFD_OKAY) {
            const auto loaded = tsm::load_palette_file(path);
            NFD_FreePathU8(path);
            if (loaded.error.empty()) {
                ed.push_undo();
                if (ed.art_step()) {
                    ed.doc.apply_palette(loaded.colors);
                } else {
                    ed.atlas.apply_palette(loaded.colors);
                }
                ed.paint_index = std::clamp(ed.paint_index, 0, ed.last_index());
                ed.select_single_palette(ed.paint_index);
                ed.bump_art();
            } else {
                ed.status = loaded.error;
            }
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Save .palette")) {
        nfdu8filteritem_t filter = {"Palette", "palette"};
        nfdu8char_t* path = nullptr;
        if (NFD_SaveDialogU8(&path, &filter, 1, nullptr, "palette.palette") == NFD_OKAY) {
            const auto colors = ed.art_step() ? ed.doc.palette : ed.atlas.palette;
            ed.status = tsm::save_palette_file(path, colors);
            if (ed.status.empty()) {
                ed.status = "Saved palette.";
            }
            NFD_FreePathU8(path);
        }
    }
}


void row_rule() {
    ImGui::Separator();
}

void apply_unscaled_style(bool dark) {
    ImGuiStyle& s = ImGui::GetStyle();
    s = ImGuiStyle();
    s.WindowPadding = ImVec2(10, 8);
    s.FramePadding = ImVec2(8, 4);
    s.ItemSpacing = ImVec2(8, 5);
    s.ItemInnerSpacing = ImVec2(6, 4);
    s.CellPadding = ImVec2(6, 4);
    s.IndentSpacing = 18.0f;
    s.ScrollbarSize = 16.0f;
    s.GrabMinSize = 14.0f;
    s.FrameRounding = 5.0f;
    s.GrabRounding = 4.0f;
    s.ChildRounding = 6.0f;
    s.PopupRounding = 6.0f;
    s.TabRounding = 5.0f;
    s.WindowRounding = 6.0f;
    s.WindowBorderSize = 1.0f;
    s.ChildBorderSize = 1.0f;
    s.FrameBorderSize = 1.0f;
    s.PopupBorderSize = 1.0f;
    s.SeparatorTextBorderSize = 1.0f;
    if (dark) {
        ImGui::StyleColorsDark(&s);
        s.Colors[ImGuiCol_WindowBg] = ImVec4(0.11f, 0.12f, 0.14f, 1.0f);
        s.Colors[ImGuiCol_ChildBg] = ImVec4(0.14f, 0.15f, 0.17f, 1.0f);
        s.Colors[ImGuiCol_PopupBg] = ImVec4(0.13f, 0.14f, 0.16f, 1.0f);
        s.Colors[ImGuiCol_Border] = ImVec4(0.32f, 0.34f, 0.38f, 1.0f);
        s.Colors[ImGuiCol_FrameBg] = ImVec4(0.18f, 0.19f, 0.22f, 1.0f);
        s.Colors[ImGuiCol_FrameBgHovered] = ImVec4(0.24f, 0.26f, 0.30f, 1.0f);
        s.Colors[ImGuiCol_FrameBgActive] = ImVec4(0.28f, 0.31f, 0.36f, 1.0f);
        s.Colors[ImGuiCol_TitleBg] = ImVec4(0.09f, 0.10f, 0.12f, 1.0f);
        s.Colors[ImGuiCol_TitleBgActive] = ImVec4(0.12f, 0.14f, 0.18f, 1.0f);
        s.Colors[ImGuiCol_MenuBarBg] = ImVec4(0.14f, 0.15f, 0.18f, 1.0f);
        s.Colors[ImGuiCol_Button] = ImVec4(0.26f, 0.32f, 0.42f, 1.0f);
        s.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.34f, 0.42f, 0.55f, 1.0f);
        s.Colors[ImGuiCol_ButtonActive] = ImVec4(0.42f, 0.52f, 0.68f, 1.0f);
        s.Colors[ImGuiCol_Header] = ImVec4(0.24f, 0.32f, 0.44f, 1.0f);
        s.Colors[ImGuiCol_HeaderHovered] = ImVec4(0.32f, 0.42f, 0.56f, 1.0f);
        s.Colors[ImGuiCol_Separator] = ImVec4(0.40f, 0.43f, 0.48f, 1.0f);
        s.Colors[ImGuiCol_Text] = ImVec4(0.93f, 0.94f, 0.96f, 1.0f);
        s.Colors[ImGuiCol_CheckMark] = ImVec4(0.55f, 0.82f, 1.0f, 1.0f);
        s.Colors[ImGuiCol_SliderGrab] = ImVec4(0.45f, 0.70f, 0.95f, 1.0f);
        s.Colors[ImGuiCol_SliderGrabActive] = ImVec4(0.60f, 0.82f, 1.0f, 1.0f);
    } else {
        ImGui::StyleColorsLight(&s);
        s.Colors[ImGuiCol_WindowBg] = ImVec4(0.94f, 0.95f, 0.97f, 1.0f);
        s.Colors[ImGuiCol_ChildBg] = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
        s.Colors[ImGuiCol_PopupBg] = ImVec4(0.98f, 0.98f, 0.99f, 1.0f);
        s.Colors[ImGuiCol_Border] = ImVec4(0.55f, 0.58f, 0.64f, 1.0f);
        s.Colors[ImGuiCol_FrameBg] = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
        s.Colors[ImGuiCol_FrameBgHovered] = ImVec4(0.90f, 0.93f, 0.98f, 1.0f);
        s.Colors[ImGuiCol_FrameBgActive] = ImVec4(0.82f, 0.88f, 0.96f, 1.0f);
        s.Colors[ImGuiCol_TitleBg] = ImVec4(0.82f, 0.85f, 0.90f, 1.0f);
        s.Colors[ImGuiCol_TitleBgActive] = ImVec4(0.72f, 0.78f, 0.88f, 1.0f);
        s.Colors[ImGuiCol_MenuBarBg] = ImVec4(0.88f, 0.90f, 0.93f, 1.0f);
        s.Colors[ImGuiCol_Button] = ImVec4(0.78f, 0.84f, 0.93f, 1.0f);
        s.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.68f, 0.77f, 0.92f, 1.0f);
        s.Colors[ImGuiCol_ButtonActive] = ImVec4(0.55f, 0.68f, 0.88f, 1.0f);
        s.Colors[ImGuiCol_Header] = ImVec4(0.75f, 0.83f, 0.94f, 1.0f);
        s.Colors[ImGuiCol_Separator] = ImVec4(0.58f, 0.62f, 0.68f, 1.0f);
        s.Colors[ImGuiCol_Text] = ImVec4(0.10f, 0.12f, 0.16f, 1.0f);
        s.Colors[ImGuiCol_CheckMark] = ImVec4(0.10f, 0.35f, 0.72f, 1.0f);
        s.Colors[ImGuiCol_SliderGrab] = ImVec4(0.22f, 0.45f, 0.78f, 1.0f);
        s.Colors[ImGuiCol_SliderGrabActive] = ImVec4(0.14f, 0.34f, 0.66f, 1.0f);
    }
}

void apply_appearance() {
    apply_unscaled_style(g_settings.dark);
    ImGuiStyle& s = ImGui::GetStyle();
    s.ScaleAllSizes(g_settings.scale);
    s.FontScaleMain = g_settings.scale;
}

bool window_rect_visible(int x, int y, int w, int h) {
    int n = 0;
    SDL_DisplayID* displays = SDL_GetDisplays(&n);
    if (!displays || n <= 0) {
        return false;
    }
    bool ok = false;
    for (int i = 0; i < n; ++i) {
        SDL_Rect b{};
        if (!SDL_GetDisplayUsableBounds(displays[i], &b)) {
            continue;
        }
        const int probe_x = x + w / 2;
        const int probe_y = y + 16;
        if (probe_x >= b.x && probe_x < b.x + b.w && probe_y >= b.y && probe_y < b.y + b.h) {
            ok = true;
            break;
        }
    }
    SDL_free(displays);
    return ok;
}

void capture_window() {
    if (!g_window) {
        return;
    }
    const SDL_WindowFlags flags = SDL_GetWindowFlags(g_window);
    if (flags & SDL_WINDOW_MINIMIZED) {
        return;
    }
    g_settings.window_placed = true;
    g_settings.window_maximized = (flags & SDL_WINDOW_MAXIMIZED) != 0;
    if (!g_settings.window_maximized) {
        SDL_GetWindowPosition(g_window, &g_settings.window_x, &g_settings.window_y);
        SDL_GetWindowSize(g_window, &g_settings.window_w, &g_settings.window_h);
    }
}

void apply_window_geometry() {
    if (!g_window) {
        return;
    }
    SDL_SetWindowSize(g_window, g_settings.window_w, g_settings.window_h);
    if (g_settings.window_placed &&
        window_rect_visible(g_settings.window_x, g_settings.window_y, g_settings.window_w, g_settings.window_h)) {
        SDL_SetWindowPosition(g_window, g_settings.window_x, g_settings.window_y);
    }
    if (g_settings.window_maximized) {
        SDL_MaximizeWindow(g_window);
    }
}

void persist_settings(bool capture = true) {
    if (capture) {
        capture_window();
    }
    if (!tsm::save_settings_file(g_settings, tsm::settings_path())) {
        SDL_Log("failed to write settings: %s", tsm::settings_path().c_str());
    }
}

void load_settings() {
    if (tsm::load_settings_file(g_settings, tsm::settings_path())) {
        return;
    }
    char* pref = SDL_GetPrefPath("chuchu-soldier", "tileset-maker-thingy");
    if (pref) {
        const std::string old = std::string(pref) + "settings.cfg";
        SDL_free(pref);
        if (tsm::load_settings_file(g_settings, old)) {
            persist_settings();
            return;
        }
    }
    tsm::load_settings_file(g_settings, "tileset-maker-settings.cfg");
}

ImVec4 tool_fill(Tool t, bool selected) {
    ImVec4 c;
    switch (t) {
    case Tool::Pencil:     c = ImVec4(0.22f, 0.48f, 0.86f, 1.0f); break;
    case Tool::Eraser:     c = ImVec4(0.82f, 0.28f, 0.32f, 1.0f); break;
    case Tool::Fill:       c = ImVec4(0.12f, 0.62f, 0.58f, 1.0f); break;
    case Tool::Line:       c = ImVec4(0.90f, 0.52f, 0.12f, 1.0f); break;
    case Tool::Square:     c = ImVec4(0.52f, 0.32f, 0.78f, 1.0f); break;
    case Tool::Circle:     c = ImVec4(0.28f, 0.62f, 0.28f, 1.0f); break;
    case Tool::Eyedropper: c = ImVec4(0.78f, 0.62f, 0.12f, 1.0f); break;
    case Tool::Select:     c = ImVec4(0.38f, 0.46f, 0.58f, 1.0f); break;
    }
    if (!selected) {
        const float mix = g_settings.dark ? 0.42f : 0.28f;
        ImVec4 bg = g_settings.dark ? ImVec4(0.16f, 0.17f, 0.20f, 1.0f) : ImVec4(0.92f, 0.93f, 0.95f, 1.0f);
        c.x = c.x * (1.0f - mix) + bg.x * mix;
        c.y = c.y * (1.0f - mix) + bg.y * mix;
        c.z = c.z * (1.0f - mix) + bg.z * mix;
        if (g_settings.dark) {
            c.x *= 0.72f;
            c.y *= 0.72f;
            c.z *= 0.72f;
        } else {
            c.x = c.x * 0.45f + 0.55f;
            c.y = c.y * 0.45f + 0.55f;
            c.z = c.z * 0.45f + 0.55f;
        }
    }
    return c;
}

bool tool_button(Tool t, Tool current) {
    const bool selected = (t == current);
    const ImVec4 fill = tool_fill(t, selected);
    ImVec4 hover = fill;
    hover.x = std::min(1.0f, fill.x + 0.10f);
    hover.y = std::min(1.0f, fill.y + 0.10f);
    hover.z = std::min(1.0f, fill.z + 0.10f);
    ImVec4 active = fill;
    active.x *= 0.85f;
    active.y *= 0.85f;
    active.z *= 0.85f;
    const float lum = 0.2126f * fill.x + 0.7152f * fill.y + 0.0722f * fill.z;
    const ImVec4 text = (selected || lum < 0.55f) ? ImVec4(1, 1, 1, 1) : ImVec4(0.08f, 0.10f, 0.14f, 1);
    ImGui::PushStyleColor(ImGuiCol_Button, fill);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, hover);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, active);
    ImGui::PushStyleColor(ImGuiCol_Text, text);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, selected ? 2.0f : 1.0f);
    ImGui::PushStyleColor(ImGuiCol_Border, selected ? ImVec4(1, 1, 1, g_settings.dark ? 0.95f : 0.85f)
                                                    : ImVec4(0, 0, 0, g_settings.dark ? 0.45f : 0.25f));
    char label[32];
    std::snprintf(label, sizeof(label), "%s  %d", tool_name(t), static_cast<int>(t) + 1);
    const bool hit = ImGui::Button(label);
    ImGui::PopStyleColor(5);
    ImGui::PopStyleVar();
    return hit;
}

void draw_settings_controls() {
    ImGui::TextUnformatted("Theme");
    ImGui::Spacing();
    if (ImGui::RadioButton("Dark", g_settings.dark)) {
        g_settings.dark = true;
        persist_settings();
    }
    ImGui::SameLine();
    if (ImGui::RadioButton("Light", !g_settings.dark)) {
        g_settings.dark = false;
        persist_settings();
    }
    row_rule();
    ImGui::TextUnformatted("UI scale");
    ImGui::Spacing();
    float percent = g_settings.scale * 100.0f;
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
    if (ImGui::SliderFloat("##uiscale", &percent, 75.0f, 200.0f, "%.0f%%")) {
        g_settings.scale = percent / 100.0f;
        persist_settings();
    }
    ImGui::TextDisabled("Drag to preview. Applies to the whole interface.");
    row_rule();
    ImGui::TextUnformatted("Grid");
    ImGui::Spacing();
    if (ImGui::Checkbox("Pixel grid", &g_settings.pixel_grid)) {
        persist_settings();
    }
    ImGui::TextDisabled("Shows a line between every pixel in the drawing area.");
    ImGui::Spacing();
    float tile_col[3] = {g_settings.tile_grid_color.r / 255.0f,
                         g_settings.tile_grid_color.g / 255.0f,
                         g_settings.tile_grid_color.b / 255.0f};
    if (ImGui::ColorEdit3("Tile grid color", tile_col, ImGuiColorEditFlags_Uint8)) {
        g_settings.tile_grid_color = tsm::Rgb{
            static_cast<uint8_t>(tsm::clampi(static_cast<int>(tile_col[0] * 255.0f + 0.5f), 0, 255)),
            static_cast<uint8_t>(tsm::clampi(static_cast<int>(tile_col[1] * 255.0f + 0.5f), 0, 255)),
            static_cast<uint8_t>(tsm::clampi(static_cast<int>(tile_col[2] * 255.0f + 0.5f), 0, 255)),
        };
        persist_settings();
    }
    float pixel_col[3] = {g_settings.pixel_grid_color.r / 255.0f,
                          g_settings.pixel_grid_color.g / 255.0f,
                          g_settings.pixel_grid_color.b / 255.0f};
    if (ImGui::ColorEdit3("Pixel grid color", pixel_col, ImGuiColorEditFlags_Uint8)) {
        g_settings.pixel_grid_color = tsm::Rgb{
            static_cast<uint8_t>(tsm::clampi(static_cast<int>(pixel_col[0] * 255.0f + 0.5f), 0, 255)),
            static_cast<uint8_t>(tsm::clampi(static_cast<int>(pixel_col[1] * 255.0f + 0.5f), 0, 255)),
            static_cast<uint8_t>(tsm::clampi(static_cast<int>(pixel_col[2] * 255.0f + 0.5f), 0, 255)),
        };
        persist_settings();
    }
    ImGui::Spacing();
    if (ImGui::Button("Reset grid colors")) {
        g_settings.tile_grid_color = tsm::Rgb{128, 128, 128};
        g_settings.pixel_grid_color = tsm::Rgb{104, 104, 104};
        persist_settings();
    }
}

void draw_settings_window() {
    if (!g_ui.show_settings) {
        return;
    }
    ImGui::SetNextWindowSize(ImVec2(420, 390), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Settings", &g_ui.show_settings)) {
        ImGui::End();
        return;
    }
    draw_settings_controls();
    ImGui::End();
}

void open_new_project_dialog(bool from_welcome) {
    g_ui.show_new = true;
    g_ui.new_from_welcome = from_welcome;
    g_ui.new_name[0] = 0;
    g_ui.new_tile_size = 16;
    g_ui.new_focus_name = true;
}

bool try_open_project(Editor& ed) {
    nfdu8filteritem_t filter = {"Tileset project", "tilesetproj"};
    nfdu8char_t* path = nullptr;
    if (NFD_OpenDialogU8(&path, &filter, 1, ed.last_dir.empty() ? nullptr : ed.last_dir.c_str()) != NFD_OKAY) {
        return false;
    }
    const std::string dest = path;
    NFD_FreePathU8(path);
    ProjectData data;
    const std::string err = tsm::load_project(data, dest);
    if (!err.empty()) {
        ed.status = err;
        return false;
    }
    ed.apply_project(data, dest);
    g_ui.project_open = true;
    return true;
}

void begin_new_project(Editor& ed) {
    open_new_project_dialog(!g_ui.project_open);
}

void request_leave(Editor& ed, PendingAction action) {
    if (action == PendingAction::None) {
        return;
    }
    if (!g_ui.project_open || !ed.dirty) {
        g_ui.pending = action;
        return;
    }
    g_ui.pending = action;
    g_ui.show_unsaved = true;
}

void perform_pending(Editor& ed, bool& running) {
    const PendingAction action = g_ui.pending;
    g_ui.pending = PendingAction::None;
    g_ui.show_unsaved = false;
    if (action == PendingAction::New) {
        begin_new_project(ed);
    } else if (action == PendingAction::Open) {
        try_open_project(ed);
    } else if (action == PendingAction::Quit) {
        running = false;
    }
}

void draw_split_layout(Editor& ed) {
    const float avail_x = ImGui::GetContentRegionAvail().x;
    const float avail_y = ImGui::GetContentRegionAvail().y;
    const float splitter = 8.0f;
    const float min_side = 290.0f * g_settings.scale;
    const float min_canvas = 160.0f * g_settings.scale;
    float side = ed.sidebar_w;
    if (avail_x < min_side + min_canvas + splitter) {
        side = std::max(170.0f * g_settings.scale, avail_x - min_canvas * 0.5f - splitter);
    } else {
        side = std::clamp(side, min_side, avail_x - min_canvas - splitter);
    }
    ed.sidebar_w = side;
    const float canvas_w = std::max(1.0f, avail_x - side - splitter);

    ImGui::BeginChild("canvas_panel", ImVec2(canvas_w, avail_y), ImGuiChildFlags_Borders);
    handle_canvas(ed);
    ImGui::EndChild();
    ImGui::SameLine(0, 0);
    ImGui::InvisibleButton("vsplit", ImVec2(splitter, avail_y));
    if (ImGui::IsItemActive()) {
        ed.sidebar_w -= ImGui::GetIO().MouseDelta.x;
    }
    if (ImGui::IsItemHovered() || ImGui::IsItemActive()) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
    }
    ImGui::GetWindowDrawList()->AddRectFilled(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(),
                                              g_settings.dark ? IM_COL32(70, 74, 82, 255) : IM_COL32(170, 176, 186, 255));
    ImGui::SameLine(0, 0);
    ImGui::BeginChild("side_panel", ImVec2(side, avail_y), ImGuiChildFlags_Borders, ImGuiWindowFlags_AlwaysVerticalScrollbar);
    if (ed.art_step()) {
        if (ed.step == Step::Center) {
            draw_preview_grid(ed, "Center tile", 1, 1, false, TilesetDoc::kCenter.x, TilesetDoc::kCenter.y);
        } else if (ed.step == Step::Edges) {
            draw_preview_grid(ed, "3x3 preview", 3, 3, false, 0, 0);
        } else {
            draw_preview_grid(ed, "5x3 sheet", TilesetDoc::kCols, TilesetDoc::kRows, false, 0, 0);
        }
    } else if (ed.has_atlas) {
        draw_preview_grid(ed, "12x4 atlas", ed.atlas.cols, AtlasDoc::kRows, true, 0, 0);
    }
    row_rule();
    draw_palette(ed);
    ImGui::EndChild();
}

bool is_cancel(const std::string& err) {
    return err == "#cancel";
}

void apply_status(Editor& ed, const std::string& err, const char* ok) {
    if (is_cancel(err)) {
        return;
    }
    ed.status = err.empty() ? ok : err;
}

void draw_export_modal(Editor& ed) {
    if (g_ui.show_export) {
        ImGui::OpenPopup("Export tileset");
    }
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowSize(ImVec2(vp->WorkSize.x * 0.82f, vp->WorkSize.y * 0.82f), ImGuiCond_Appearing);
    ImGui::SetNextWindowPos(vp->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (!ImGui::BeginPopupModal("Export tileset", &g_ui.show_export, ImGuiWindowFlags_NoSavedSettings)) {
        return;
    }
    ImGui::Text("Atlas preview — %s", ed.project_name);
    ImGui::SetNextItemWidth(220);
    ImGui::SliderInt("Zoom", &g_ui.export_zoom, 1, 24);
    ImGui::SameLine();
    if (ImGui::Button("-")) {
        g_ui.export_zoom = std::max(1, g_ui.export_zoom - 1);
    }
    ImGui::SameLine();
    if (ImGui::Button("+")) {
        g_ui.export_zoom = std::min(24, g_ui.export_zoom + 1);
    }
    const float footer = ImGui::GetFrameHeightWithSpacing() * 3.5f;
    ImGui::BeginChild("export_view", ImVec2(0, -footer), ImGuiChildFlags_Borders,
                      ImGuiWindowFlags_HorizontalScrollbar);
    if (ImGui::IsWindowHovered() && ImGui::GetIO().MouseWheel != 0.0f) {
        g_ui.export_zoom = std::clamp(g_ui.export_zoom + (ImGui::GetIO().MouseWheel > 0.0f ? 1 : -1), 1, 24);
    }
    const int ts = ed.atlas.tile_size;
    const int cols = ed.atlas.cols;
    const int rows = AtlasDoc::kRows;
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImVec2 size(static_cast<float>(cols * ts * g_ui.export_zoom),
                      static_cast<float>(rows * ts * g_ui.export_zoom));
    ImGui::InvisibleButton("export_canvas", size);
    draw_pixels(ImGui::GetWindowDrawList(), origin, g_ui.export_zoom, cols * ts, rows * ts, ed, 0, 0, cols, rows, true,
                grid_color());
    ImGui::EndChild();

    if (ImGui::Checkbox("C header (.h)", &ed.export_header)) {
        ed.touch();
    }
    ImGui::SameLine();
    if (ImGui::Checkbox("Terrain (.terrain)", &ed.export_terrain)) {
        ed.touch();
    }
    ImGui::SameLine();
    if (ImGui::Checkbox("5x3 PNG", &ed.export_5x3)) {
        ed.touch();
    }
    ImGui::TextDisabled("The atlas PNG is always exported.");
    if (ImGui::Button("Export…")) {
        const std::string err = ed.export_all();
        apply_status(ed, err, "Exported.");
        if (err.empty()) {
            g_ui.show_export = false;
            ImGui::CloseCurrentPopup();
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Close")) {
        g_ui.show_export = false;
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

void draw_new_project_modal(Editor& ed) {
    if (g_ui.show_new) {
        ImGui::OpenPopup("New project");
    }
    const ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (!ImGui::BeginPopupModal("New project", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove)) {
        return;
    }
    ImGui::TextUnformatted("Name the tileset and choose a tile size.");
    ImGui::SetNextItemWidth(280);
    if (g_ui.new_focus_name || ImGui::IsWindowAppearing()) {
        ImGui::SetKeyboardFocusHere();
        g_ui.new_focus_name = false;
    }
    const bool enter_pressed = ImGui::InputText("Name", g_ui.new_name, sizeof(g_ui.new_name),
                                               ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::TextUnformatted("Tile size");
    ImGui::RadioButton("8x8", &g_ui.new_tile_size, 8);
    ImGui::SameLine();
    ImGui::RadioButton("16x16", &g_ui.new_tile_size, 16);
    const std::string name = trim_copy(g_ui.new_name);
    const bool can_create = !name.empty();
    bool should_create = false;
    if (can_create && (enter_pressed || ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter))) {
        should_create = true;
    }
    ImGui::BeginDisabled(!can_create);
    if (ImGui::Button("Create", ImVec2(120, 0))) {
        should_create = true;
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(120, 0))) {
        g_ui.show_new = false;
        ImGui::CloseCurrentPopup();
    }
    if (should_create && can_create) {
        ed.reset_new(name, g_ui.new_tile_size);
        g_ui.project_open = true;
        g_ui.show_new = false;
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

void draw_unsaved_modal(Editor& ed, bool& running) {
    if (g_ui.show_unsaved) {
        ImGui::OpenPopup("Unsaved changes");
    }
    const ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (!ImGui::BeginPopupModal("Unsaved changes", nullptr,
                                ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove)) {
        return;
    }
    ImGui::Text("Save changes to \"%s\"?", ed.project_name);
    if (ImGui::Button("Save", ImVec2(110, 0))) {
        const std::string err = ed.save_project(ed.project_path.empty());
        apply_status(ed, err, "Project saved.");
        if (err.empty() && !ed.dirty) {
            ImGui::CloseCurrentPopup();
            perform_pending(ed, running);
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Don't save", ImVec2(110, 0))) {
        ed.dirty = false;
        ImGui::CloseCurrentPopup();
        perform_pending(ed, running);
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(110, 0))) {
        g_ui.pending = PendingAction::None;
        g_ui.show_unsaved = false;
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

void draw_modals(Editor& ed, bool& running) {
    if (!g_ui.project_open && !g_ui.show_new && !g_ui.show_unsaved) {
        ImGui::OpenPopup("Welcome");
        const ImVec2 center = ImGui::GetMainViewport()->GetCenter();
        ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
        if (ImGui::BeginPopupModal("Welcome", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove)) {
            ImGui::TextUnformatted("tileset maker thingy");
            ImGui::Spacing();
            ImGui::TextUnformatted("Start a named project, or load a saved one.");
            ImGui::Spacing();
            if (ImGui::Button("New project", ImVec2(240, 0))) {
                ImGui::CloseCurrentPopup();
                open_new_project_dialog(true);
            }
            if (ImGui::Button("Load project", ImVec2(240, 0))) {
                ImGui::CloseCurrentPopup();
                try_open_project(ed);
            }
            ImGui::EndPopup();
        }
    }
    draw_new_project_modal(ed);
    draw_unsaved_modal(ed, running);
    if (g_ui.project_open) {
        draw_export_modal(ed);
    }
}

} // namespace

int run_editor() {
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) {
        SDL_Log("SDL_Init failed: %s", SDL_GetError());
        return 1;
    }
    NFD_Init();
    load_settings();

    SDL_Window* window = SDL_CreateWindow("tileset maker thingy", g_settings.window_w, g_settings.window_h,
                                         SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    g_window = window;
    SDL_Renderer* renderer = SDL_CreateRenderer(window, nullptr);
    if (!window || !renderer) {
        SDL_Log("SDL window/renderer failed: %s", SDL_GetError());
        return 1;
    }
    apply_window_geometry();
    persist_settings(false);
    SDL_SetRenderVSync(renderer, 1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    tsm::ensure_config_dir();
    g_imgui_ini = tsm::imgui_ini_path();
    io.IniFilename = g_imgui_ini.c_str();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
#ifdef IMGUI_HAS_DOCK
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
#endif
    apply_appearance();
    ImGui_ImplSDL3_InitForSDLRenderer(window, renderer);
    ImGui_ImplSDLRenderer3_Init(renderer);

    Editor ed;
    ed.configure_view();

    bool running = true;
    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL3_ProcessEvent(&event);
            if (event.type == SDL_EVENT_QUIT) {
                request_leave(ed, PendingAction::Quit);
                if (g_ui.pending == PendingAction::Quit && !g_ui.show_unsaved) {
                    perform_pending(ed, running);
                }
            }
        }

        if (g_ui.pending != PendingAction::None && !g_ui.show_unsaved) {
            perform_pending(ed, running);
        }

        apply_appearance();
        ImGui_ImplSDLRenderer3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();

        if (ImGui::BeginMainMenuBar()) {
            if (ImGui::BeginMenu("File")) {
                if (ImGui::MenuItem("New project", "Ctrl+N")) {
                    request_leave(ed, PendingAction::New);
                }
                if (ImGui::MenuItem("Open project...", "Ctrl+O")) {
                    request_leave(ed, PendingAction::Open);
                }
                ImGui::BeginDisabled(!g_ui.project_open);
                if (ImGui::MenuItem("Save project", "Ctrl+S")) {
                    apply_status(ed, ed.save_project(false), "Project saved.");
                }
                if (ImGui::MenuItem("Save project as...", "Ctrl+Shift+S")) {
                    apply_status(ed, ed.save_project(true), "Project saved.");
                }
                ImGui::EndDisabled();
                ImGui::Separator();
                ImGui::BeginDisabled(!g_ui.project_open);
                if (ImGui::MenuItem("Import 5x3 PNG")) {
                    nfdu8filteritem_t filter = {"PNG", "png"};
                    nfdu8char_t* path = nullptr;
                    if (NFD_OpenDialogU8(&path, &filter, 1, nullptr) == NFD_OKAY) {
                        const std::string err = tsm::load_tileset_png(ed.doc, path);
                        NFD_FreePathU8(path);
                        if (err.empty()) {
                            ed.seeded = true;
                            ed.stamped = true;
                            ed.has_atlas = false;
                            ed.step = Step::Specialty;
                            ed.configure_view();
                            ed.bump_art();
                            ed.status = "Imported 5x3 sheet.";
                        } else {
                            ed.status = err;
                        }
                    }
                }
                if (ImGui::MenuItem("Export...", "Ctrl+E")) {
                    const std::string err = ed.ensure_atlas();
                    if (err.empty()) {
                        g_ui.export_zoom = std::max(2, 192 / std::max(1, ed.atlas.tile_size * ed.atlas.cols));
                        g_ui.show_export = true;
                    } else {
                        ed.status = err;
                    }
                }
                ImGui::EndDisabled();
                ImGui::Separator();
                if (ImGui::MenuItem("Quit")) {
                    request_leave(ed, PendingAction::Quit);
                }
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Settings")) {
                ImGui::TextUnformatted("Theme");
                if (ImGui::MenuItem("Dark", nullptr, g_settings.dark)) {
                    g_settings.dark = true;
                    persist_settings();
                }
                if (ImGui::MenuItem("Light", nullptr, !g_settings.dark)) {
                    g_settings.dark = false;
                    persist_settings();
                }
                ImGui::Separator();
                ImGui::TextUnformatted("UI scale");
                float percent = g_settings.scale * 100.0f;
                ImGui::SetNextItemWidth(220.0f);
                if (ImGui::SliderFloat("##menuscale", &percent, 75.0f, 200.0f, "%.0f%%")) {
                    g_settings.scale = percent / 100.0f;
                    persist_settings();
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Pixel grid", nullptr, g_settings.pixel_grid)) {
                    g_settings.pixel_grid = !g_settings.pixel_grid;
                    persist_settings();
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Settings window...", nullptr, g_ui.show_settings)) {
                    g_ui.show_settings = !g_ui.show_settings;
                }
                ImGui::EndMenu();
            }
            ImGui::EndMainMenuBar();
        }

        const bool cmd = io.KeyCtrl || io.KeySuper;
        if (!io.WantTextInput) {
            if (cmd && ImGui::IsKeyPressed(ImGuiKey_N)) {
                request_leave(ed, PendingAction::New);
            }
            if (cmd && ImGui::IsKeyPressed(ImGuiKey_O)) {
                request_leave(ed, PendingAction::Open);
            }
            if (cmd && ImGui::IsKeyPressed(ImGuiKey_S) && g_ui.project_open) {
                apply_status(ed, ed.save_project(io.KeyShift), "Project saved.");
            }
            if (cmd && ImGui::IsKeyPressed(ImGuiKey_E) && g_ui.project_open) {
                const std::string err = ed.ensure_atlas();
                if (err.empty()) {
                    g_ui.export_zoom = std::max(2, 192 / std::max(1, ed.atlas.tile_size * ed.atlas.cols));
                    g_ui.show_export = true;
                } else {
                    ed.status = err;
                }
            }
        }
        if (g_ui.project_open) {
            if (ed.floating) {
                if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
                    cancel_floating(ed);
                }
                if (ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter)) {
                    commit_floating(ed);
                }
            }
            if (cmd && ImGui::IsKeyPressed(ImGuiKey_Z)) {
                if (ed.floating) {
                    cancel_floating(ed);
                } else {
                    ed.do_undo();
                }
            }
            if (cmd && ImGui::IsKeyPressed(ImGuiKey_C) && (ed.tool == Tool::Select || ed.sel_a.x >= 0 || ed.floating)) {
                copy_selection(ed);
            }
            if (cmd && ImGui::IsKeyPressed(ImGuiKey_V)) {
                start_paste(ed);
            }
            if (cmd && ImGui::IsKeyPressed(ImGuiKey_D)) {
                if (ed.floating) {
                    commit_floating(ed);
                }
                ed.clear_selection();
            }
            if (cmd && ImGui::IsKeyPressed(ImGuiKey_Comma)) {
                g_ui.show_settings = true;
            }
            if (!io.WantTextInput) {
                if (ImGui::IsKeyPressed(ImGuiKey_LeftBracket)) {
                    shift_selected_palette(ed, -1);
                }
                if (ImGui::IsKeyPressed(ImGuiKey_RightBracket)) {
                    shift_selected_palette(ed, 1);
                }
                Tool new_tool = ed.tool;
                if (ImGui::IsKeyPressed(ImGuiKey_1)) new_tool = Tool::Pencil;
                if (ImGui::IsKeyPressed(ImGuiKey_2)) new_tool = Tool::Eraser;
                if (ImGui::IsKeyPressed(ImGuiKey_3)) new_tool = Tool::Fill;
                if (ImGui::IsKeyPressed(ImGuiKey_4)) new_tool = Tool::Line;
                if (ImGui::IsKeyPressed(ImGuiKey_5)) new_tool = Tool::Square;
                if (ImGui::IsKeyPressed(ImGuiKey_6)) new_tool = Tool::Circle;
                if (ImGui::IsKeyPressed(ImGuiKey_7)) new_tool = Tool::Eyedropper;
                if (ImGui::IsKeyPressed(ImGuiKey_8)) new_tool = Tool::Select;
                if (new_tool != ed.tool) {
                    if (ed.floating && new_tool != Tool::Select) {
                        commit_floating(ed);
                    }
                    ed.tool = new_tool;
                }
            }
        }

        if (g_ui.project_open) {
            const ImGuiViewport* vp = ImGui::GetMainViewport();
            ImGui::SetNextWindowPos(vp->WorkPos);
            ImGui::SetNextWindowSize(vp->WorkSize);
            ImGui::Begin("Tileset Maker", nullptr,
                         ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
                             ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus |
                             ImGuiWindowFlags_NoSavedSettings);

            ImGui::BeginGroup();
            ImGui::BeginDisabled(ed.step == Step::Center);
            if (ImGui::Button("Back", ImVec2(88, 0))) {
                if (ed.floating) {
                    commit_floating(ed);
                }
                ed.go_back();
            }
            ImGui::EndDisabled();
            ImGui::SameLine();
            const char* step_label = "Step 1 of 4  —  Center tile";
            if (ed.step == Step::Edges) step_label = "Step 2 of 4  —  Edges (3x3)";
            if (ed.step == Step::Specialty) step_label = "Step 3 of 4  —  Caps & extras";
            if (ed.step == Step::Variants) step_label = "Step 4 of 4  —  Atlas & header";
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(step_label);
            ImGui::SameLine();
            ImGui::BeginDisabled(ed.step == Step::Variants);
            if (ImGui::Button("Next", ImVec2(88, 0))) {
                if (ed.floating) {
                    commit_floating(ed);
                }
                ed.go_next();
            }
            ImGui::EndDisabled();
            ImGui::SameLine(0, 24);
            ImGui::SetNextItemWidth(180);
            if (ImGui::InputText("Project", ed.project_name, sizeof(ed.project_name))) {
                ed.touch();
            }
            ImGui::SameLine(0, 24);
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted("Tile size");
            ImGui::SameLine();
            if (ImGui::RadioButton("8x8", ed.doc.tile_size == 8)) {
                ed.undo_selections.clear();
                ed.doc.reset(8);
                ed.atlas.reset(8);
                ed.has_atlas = false;
                ed.seeded = false;
                ed.stamped = false;
                ed.step = Step::Center;
                ed.configure_view();
                ed.bump_art();
            }
            ImGui::SameLine();
            if (ImGui::RadioButton("16x16", ed.doc.tile_size == 16)) {
                ed.undo_selections.clear();
                ed.doc.reset(16);
                ed.atlas.reset(16);
                ed.has_atlas = false;
                ed.seeded = false;
                ed.stamped = false;
                ed.step = Step::Center;
                ed.configure_view();
                ed.bump_art();
            }
            ImGui::EndGroup();

            row_rule();

            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted("Tools");
            ImGui::SameLine(0, 16);
            for (int i = 0; i < 8; ++i) {
                const Tool t = static_cast<Tool>(i);
                if (i) {
                    ImGui::SameLine();
                }
                if (tool_button(t, ed.tool)) {
                    if (ed.floating && t != Tool::Select) {
                        commit_floating(ed);
                    }
                    ed.tool = t;
                }
            }

            row_rule();

            if (ImGui::Button("Undo")) {
                if (ed.floating) {
                    cancel_floating(ed);
                } else {
                    ed.do_undo();
                }
            }
            if (ed.has_selection() && ed.tool != Tool::Select) {
                ImGui::SameLine(0, 16);
                if (ImGui::Button("Deselect (Ctrl+D)")) {
                    ed.clear_selection();
                }
            }
            if (uses_brush(ed.tool)) {
                ImGui::SameLine(0, 16);
                ImGui::SetNextItemWidth(120);
                ImGui::SliderInt("Brush", &ed.brush, 1, 4);
            }
            if (ed.step == Step::Center) {
                ImGui::SameLine(0, 16);
                if (ImGui::Checkbox("Tile mode", &ed.tile_mode)) {
                    ed.touch();
                }
            }
            if (ed.step == Step::Edges || ed.step == Step::Specialty) {
                ImGui::SameLine(0, 16);
                if (ImGui::Checkbox("H-flip", &ed.doc.hflip_linked)) {
                    ed.touch();
                }
                ImGui::SameLine();
                if (ImGui::Checkbox("V-flip", &ed.doc.vflip_linked)) {
                    ed.touch();
                }
            }
            if (ed.tool == Tool::Select) {
                if (ed.floating) {
                    ImGui::SameLine(0, 16);
                    if (ImGui::Button("Apply (Enter)")) {
                        commit_floating(ed);
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Cancel (Esc)")) {
                        cancel_floating(ed);
                    }
                } else {
                    ImGui::SameLine(0, 16);
                    if (ImGui::Button("Copy")) {
                        copy_selection(ed);
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Paste")) {
                        start_paste(ed);
                    }
                    if (ed.has_selection()) {
                        ImGui::SameLine();
                        if (ImGui::Button("Deselect (Ctrl+D)")) {
                            ed.clear_selection();
                        }
                    }
                }
            }
            if (ed.step == Step::Specialty) {
                ImGui::SameLine(0, 16);
                if (ImGui::Button("Stamp this")) {
                    ed.push_undo();
                    ed.doc.stamp_specialty(ed.specialty);
                    ed.bump_art();
                }
                ImGui::SameLine();
                if (ImGui::Button("Stamp all extras")) {
                    ed.push_undo();
                    ed.doc.stamp_all_specialty();
                    ed.bump_art();
                }
                ImGui::SameLine();
                if (ImGui::Button("Reset extras")) {
                    ed.push_undo();
                    ed.doc.reset_specialty_to_center();
                    ed.bump_art();
                }
            }
            if (ed.step == Step::Variants && ed.has_atlas) {
                ImGui::SameLine(0, 16);
                if (ImGui::Button("Add variant")) {
                    ed.push_undo();
                    const Cell slot = ed.atlas.add_variant(ed.atlas_cell, ed.variant_chance);
                    if (slot.x >= 0) {
                        ed.atlas_cell = slot;
                        ed.configure_view();
                    }
                    ed.bump_art();
                }
                ImGui::SameLine();
                ImGui::BeginDisabled(!ed.atlas.is_extra(ed.atlas_cell.x, ed.atlas_cell.y));
                if (ImGui::Button("Remove variant")) {
                    ed.push_undo();
                    ed.atlas.remove_variant(ed.atlas_cell);
                    ed.atlas_cell = {9, 2};
                    ed.configure_view();
                    ed.bump_art();
                }
                ImGui::EndDisabled();
                ImGui::SameLine();
                ImGui::SetNextItemWidth(150);
                const char* chances[] = {"Rare 0.08", "Uncommon 0.15", "0.30", "0.50", "Equal 1.0"};
                const float chance_vals[] = {0.08f, 0.15f, 0.30f, 0.50f, 1.0f};
                int ci = 2;
                for (int i = 0; i < 5; ++i) {
                    if (std::fabs(ed.variant_chance - chance_vals[i]) < 0.001f) {
                        ci = i;
                    }
                }
                if (ImGui::Combo("Chance", &ci, chances, 5)) {
                    ed.variant_chance = chance_vals[ci];
                    if (ed.atlas.is_extra(ed.atlas_cell.x, ed.atlas_cell.y)) {
                        ed.atlas.set_binding_probability(ed.atlas_cell, ed.variant_chance);
                    }
                    ed.touch();
                }
            }

            row_rule();

            ImGui::TextWrapped("%s", ed.status.c_str());
            if (ed.art_step()) {
                if (ed.step == Step::Specialty && ed.specialty == TilesetDoc::kInnerCorner) {
                    ImGui::TextDisabled("Inner corners (top-right: 4,0) — center tile in 3x3 context. Draw in center and fine-tune edges.");
                } else {
                    ImGui::TextDisabled("%s", ed.doc.cell_name(ed.preview_sel.x, ed.preview_sel.y).c_str());
                }
            } else {
                ImGui::TextDisabled("%s", ed.atlas.cell_name(ed.atlas_cell.x, ed.atlas_cell.y).c_str());
            }

            draw_split_layout(ed);
            ImGui::End();
        }

        draw_modals(ed, running);
        draw_settings_window();

        ImGui::Render();
        const ImVec4 bg = g_settings.dark ? ImVec4(0.10f, 0.11f, 0.13f, 1.0f) : ImVec4(0.90f, 0.91f, 0.93f, 1.0f);
        SDL_SetRenderDrawColor(renderer, static_cast<Uint8>(bg.x * 255), static_cast<Uint8>(bg.y * 255),
                               static_cast<Uint8>(bg.z * 255), 255);
        SDL_RenderClear(renderer);
        ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer);
        SDL_RenderPresent(renderer);
    }

    persist_settings();
    ImGui_ImplSDLRenderer3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    g_window = nullptr;
    NFD_Quit();
    SDL_Quit();
    return 0;
}
