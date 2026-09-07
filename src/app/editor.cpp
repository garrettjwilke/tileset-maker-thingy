#include "editor.h"
#include "settings.h"

#include "core/atlas_doc.h"
#include "core/convert.h"
#include "core/draw.h"
#include "core/io.h"
#include "core/md_color.h"
#include "core/palette_presets.h"
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
using tsm::Rgb;
using tsm::TilesetDoc;
using tsm::VariantBinding;

enum class Step { Center, Edges, Specialty, Variants };
enum class Tool { Pencil, Eraser, Fill, Line, Square, Circle, Eyedropper, Select };

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
    std::string status = "Paint the center fill tile.";
    std::string last_dir;

    Tool tool = Tool::Pencil;
    int paint_index = 1;
    int brush = 1;
    bool tile_mode = true;
    bool export_header = true;
    bool export_terrain = true;
    bool export_5x3 = true;
    float variant_chance = 0.3f;

    bool dragging = false;
    bool selecting = false;
    bool stroke_pending = false;
    Cell hover{-1, -1};
    Cell stroke_from{-1, -1};
    Cell stroke_to{-1, -1};
    Cell sel_a{-1, -1};
    Cell sel_b{-1, -1};
    Clipboard clipboard;

    int edit_ox = 1, edit_oy = 1, edit_cols = 1, edit_rows = 1;
    int zoom = 16;

    void bump_art() { ++art_rev; }

    bool art_step() const { return step != Step::Variants; }

    void configure_view() {
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
            edit_cols = 1;
            edit_rows = 1;
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
    int reps() const { return (step == Step::Center && tile_mode) ? 3 : 1; }
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
    }
    void do_undo() {
        if (art_step()) {
            doc.undo();
        } else {
            atlas.undo();
        }
        bump_art();
    }

    Cell src_to_cell(Cell src) const {
        const int ts = tile_size();
        if (ts <= 0) {
            return {-1, -1};
        }
        return {edit_ox + src.x / ts, edit_oy + src.y / ts};
    }
    Cell src_local(Cell src) const {
        const int ts = tile_size();
        if (ts <= 0) {
            return {0, 0};
        }
        return {src.x % ts, src.y % ts};
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
                if (plot_src({origin.x + dx, origin.y + dy}, index)) {
                    wrote = true;
                }
            }
        }
        return wrote;
    }

    void go_next() {
        if (step == Step::Center) {
            const bool force = !seeded || doc.center_changed_since_seed();
            if (force && seeded) {
                doc.push_undo();
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
        configure_view();
    }

    std::string export_all() {
        if (!has_atlas || atlas_rev != art_rev) {
            const std::string err = tsm::convert_tileset_to_atlas(doc, atlas);
            if (!err.empty()) {
                return err;
            }
            has_atlas = true;
            atlas_rev = art_rev;
        }
        nfdu8filteritem_t filter = {"PNG", "png"};
        nfdu8char_t* path = nullptr;
        const nfdresult_t r = NFD_SaveDialogU8(&path, &filter, 1, last_dir.empty() ? nullptr : last_dir.c_str(),
                                               (std::string(project_name) + ".png").c_str());
        if (r != NFD_OKAY) {
            return r == NFD_CANCEL ? std::string() : "Save cancelled or failed";
        }
        std::string dest = path;
        NFD_FreePathU8(path);
        if (dest.size() < 4 || dest.substr(dest.size() - 4) != ".png") {
            dest += ".png";
        }
        last_dir = dest.substr(0, dest.find_last_of("/\\"));
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
};

struct UiState {
    bool show_settings = false;
};

UiState g_ui;
tsm::Settings g_settings;
SDL_Window* g_window = nullptr;
std::string g_imgui_ini;

ImU32 grid_color() {
    return g_settings.dark ? IM_COL32(255, 255, 255, 48) : IM_COL32(20, 24, 32, 55);
}

void draw_pixels(ImDrawList* dl, ImVec2 origin, int zoom, int w, int h, const Editor& ed, int ox, int oy,
                 int cols, int rows, bool atlas, ImU32 grid_col) {
    const int ts = ed.tile_size();
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const int col = ox + x / ts;
            const int row = oy + y / ts;
            const int lx = x % ts;
            const int ly = y % ts;
            const int idx = atlas ? ed.atlas.get_pixel(col, row, lx, ly) : ed.doc.get_pixel(col, row, lx, ly);
            const Rgb c = atlas ? ed.atlas.color_at(idx) : ed.doc.color_at(idx);
            const ImVec2 p0(origin.x + static_cast<float>(x * zoom), origin.y + static_cast<float>(y * zoom));
            const ImVec2 p1(p0.x + static_cast<float>(zoom), p0.y + static_cast<float>(zoom));
            dl->AddRectFilled(p0, p1, im_color(c));
        }
    }
    for (int row = 0; row <= rows; ++row) {
        const float y = origin.y + static_cast<float>(row * ts * zoom);
        dl->AddLine(ImVec2(origin.x, y), ImVec2(origin.x + static_cast<float>(w * zoom), y), grid_col);
    }
    for (int col = 0; col <= cols; ++col) {
        const float x = origin.x + static_cast<float>(col * ts * zoom);
        dl->AddLine(ImVec2(x, origin.y), ImVec2(x, origin.y + static_cast<float>(h * zoom)), grid_col);
    }
}

void draw_preview_grid(Editor& ed, const char* title, int cols, int rows, bool atlas) {
    ImGui::TextUnformatted(title);
    const int ts = ed.tile_size();
    const int z = std::max(4, 48 / ts);
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImVec2 size(static_cast<float>(cols * ts * z), static_cast<float>(rows * ts * z));
    ImGui::InvisibleButton("preview", size);
    draw_pixels(ImGui::GetWindowDrawList(), origin, z, cols * ts, rows * ts, ed, 0, 0, cols, rows, atlas, grid_color());
    if (ImGui::IsItemClicked()) {
        const ImVec2 mp = ImGui::GetIO().MousePos;
        const int col = static_cast<int>((mp.x - origin.x) / static_cast<float>(ts * z));
        const int row = static_cast<int>((mp.y - origin.y) / static_cast<float>(ts * z));
        if (col >= 0 && row >= 0 && col < cols && row < rows) {
            if (atlas) {
                ed.atlas_cell = {col, row};
            } else if (ed.step == Step::Specialty) {
                ed.specialty = {col, row};
            } else if (ed.step == Step::Edges && col < 3 && row < 3) {
                ed.preview_sel = {col, row};
            }
            ed.configure_view();
        }
    }
    Cell sel = atlas ? ed.atlas_cell : ed.preview_sel;
    const ImVec2 s0(origin.x + static_cast<float>(sel.x * ts * z), origin.y + static_cast<float>(sel.y * ts * z));
    ImGui::GetWindowDrawList()->AddRect(s0,
                                        ImVec2(s0.x + static_cast<float>(ts * z), s0.y + static_cast<float>(ts * z)),
                                        IM_COL32(255, 220, 60, 255), 0, 0, 2.0f);
}

void handle_canvas(Editor& ed) {
    const int ts = ed.tile_size();
    const int sw = ed.src_w();
    const int sh = ed.src_h();
    const int reps = ed.reps();
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImVec2 size(static_cast<float>(sw * reps * ed.zoom), static_cast<float>(sh * reps * ed.zoom));
    ImGui::InvisibleButton("canvas", size);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    for (int ry = 0; ry < reps; ++ry) {
        for (int rx = 0; rx < reps; ++rx) {
            const ImVec2 o(origin.x + static_cast<float>(rx * sw * ed.zoom),
                           origin.y + static_cast<float>(ry * sh * ed.zoom));
            draw_pixels(dl, o, ed.zoom, sw, sh, ed, ed.edit_ox, ed.edit_oy, ed.edit_cols, ed.edit_rows, !ed.art_step(),
                        grid_color());
        }
    }

    auto pos_to_src = [&](ImVec2 p) -> Cell {
        const int lx = static_cast<int>(p.x - origin.x);
        const int ly = static_cast<int>(p.y - origin.y);
        if (lx < 0 || ly < 0 || ed.zoom <= 0) {
            return {-1, -1};
        }
        int px = (lx / ed.zoom) % sw;
        int py = (ly / ed.zoom) % sh;
        if (px < 0) px += sw;
        if (py < 0) py += sh;
        if (lx >= sw * reps * ed.zoom || ly >= sh * reps * ed.zoom) {
            return {-1, -1};
        }
        return {px, py};
    };

    if (ImGui::IsItemHovered()) {
        ed.hover = pos_to_src(ImGui::GetIO().MousePos);
    }

    if (ImGui::IsItemClicked(ImGuiMouseButton_Right) && ed.hover.x >= 0) {
        const Cell cell = ed.src_to_cell(ed.hover);
        const Cell loc = ed.src_local(ed.hover);
        ed.paint_index = ed.get_px(cell.x, cell.y, loc.x, loc.y);
    }

    const bool stroke_tool = ed.tool == Tool::Line || ed.tool == Tool::Square || ed.tool == Tool::Circle;
    if (ImGui::IsItemClicked(ImGuiMouseButton_Left) && ed.hover.x >= 0) {
        if (ed.tool == Tool::Select) {
            ed.sel_a = ed.hover;
            ed.sel_b = ed.hover;
            ed.selecting = true;
        } else if (stroke_tool) {
            ed.stroke_from = ed.hover;
            ed.stroke_to = ed.hover;
            ed.stroke_pending = true;
        } else if (ed.tool == Tool::Eyedropper) {
            const Cell cell = ed.src_to_cell(ed.hover);
            const Cell loc = ed.src_local(ed.hover);
            ed.paint_index = ed.get_px(cell.x, cell.y, loc.x, loc.y);
        } else if (ed.tool == Tool::Fill) {
            const Cell cell = ed.src_to_cell(ed.hover);
            const Cell loc = ed.src_local(ed.hover);
            ed.push_undo();
            if (ed.art_step()) {
                ed.doc.flood_fill(cell.x, cell.y, loc.x, loc.y, ed.paint_index);
            } else {
                ed.atlas.flood_fill(cell.x, cell.y, loc.x, loc.y, ed.paint_index);
            }
            ed.bump_art();
        } else {
            ed.push_undo();
            ed.dragging = true;
            ed.stamp_src(ed.hover, ed.tool == Tool::Eraser ? 0 : ed.paint_index);
            ed.bump_art();
        }
    }
    if (ed.dragging && ImGui::IsMouseDown(ImGuiMouseButton_Left) && ed.hover.x >= 0) {
        ed.stamp_src(ed.hover, ed.tool == Tool::Eraser ? 0 : ed.paint_index);
        ed.bump_art();
    }
    if (ed.selecting && ImGui::IsMouseDown(ImGuiMouseButton_Left) && ed.hover.x >= 0) {
        ed.sel_b = ed.hover;
    }
    if (ed.stroke_pending && ImGui::IsMouseDown(ImGuiMouseButton_Left) && ed.hover.x >= 0) {
        ed.stroke_to = ed.hover;
    }
    if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
        if (ed.stroke_pending && ed.stroke_from.x >= 0) {
            std::vector<Cell> pts;
            if (ed.tool == Tool::Square) {
                pts = tsm::rect_outline(ed.stroke_from, ed.stroke_to);
            } else if (ed.tool == Tool::Circle) {
                pts = tsm::ellipse_outline(ed.stroke_from, ed.stroke_to);
            } else {
                pts = tsm::bresenham(ed.stroke_from, ed.stroke_to);
            }
            ed.push_undo();
            for (Cell p : pts) {
                ed.stamp_src(p, ed.paint_index);
            }
            ed.bump_art();
        }
        ed.dragging = false;
        ed.selecting = false;
        ed.stroke_pending = false;
        ed.stroke_from = {-1, -1};
    }

    if (ed.sel_a.x >= 0 && ed.sel_b.x >= 0) {
        const int x0 = std::min(ed.sel_a.x, ed.sel_b.x);
        const int y0 = std::min(ed.sel_a.y, ed.sel_b.y);
        const int x1 = std::max(ed.sel_a.x, ed.sel_b.x) + 1;
        const int y1 = std::max(ed.sel_a.y, ed.sel_b.y) + 1;
        dl->AddRect(ImVec2(origin.x + static_cast<float>(x0 * ed.zoom), origin.y + static_cast<float>(y0 * ed.zoom)),
                    ImVec2(origin.x + static_cast<float>(x1 * ed.zoom), origin.y + static_cast<float>(y1 * ed.zoom)),
                    IM_COL32(255, 255, 255, 220), 0, 0, 2.0f);
    }
    if (ed.stroke_pending && ed.stroke_from.x >= 0) {
        const Rgb c = ed.color(ed.paint_index);
        std::vector<Cell> pts = (ed.tool == Tool::Square) ? tsm::rect_outline(ed.stroke_from, ed.stroke_to)
                               : (ed.tool == Tool::Circle) ? tsm::ellipse_outline(ed.stroke_from, ed.stroke_to)
                                                           : tsm::bresenham(ed.stroke_from, ed.stroke_to);
        for (Cell p : pts) {
            for (int dy = 0; dy < ed.brush; ++dy) {
                for (int dx = 0; dx < ed.brush; ++dx) {
                    const ImVec2 p0(origin.x + static_cast<float>((p.x + dx) * ed.zoom),
                                    origin.y + static_cast<float>((p.y + dy) * ed.zoom));
                    dl->AddRectFilled(p0, ImVec2(p0.x + static_cast<float>(ed.zoom), p0.y + static_cast<float>(ed.zoom)),
                                      im_color(c, 180));
                }
            }
        }
    }
    (void)ts;
}

void draw_palette(Editor& ed) {
    const int n = ed.art_step() ? ed.doc.palette_count() : ed.atlas.palette_count();
    ImGui::Text("Palette (%d)", n);
    for (int i = 0; i < n; ++i) {
        const Rgb c = ed.color(i);
        float col[3] = {c.r / 255.0f, c.g / 255.0f, c.b / 255.0f};
        ImGui::PushID(i);
        if (ImGui::ColorButton("##sw", ImVec4(col[0], col[1], col[2], 1), ImGuiColorEditFlags_NoTooltip,
                               ImVec2(22, 22))) {
            ed.paint_index = i;
        }
        if (ed.paint_index == i) {
            ImGui::GetWindowDrawList()->AddRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(),
                                                IM_COL32(255, 220, 60, 255), 0, 0, 2.0f);
        }
        if (i + 1 < n) {
            ImGui::SameLine();
        }
        ImGui::PopID();
    }
    if (ed.paint_index >= 0 && ed.paint_index < n) {
        Rgb c = ed.color(ed.paint_index);
        float col[3] = {c.r / 255.0f, c.g / 255.0f, c.b / 255.0f};
        if (ImGui::ColorEdit3("Color", col, ImGuiColorEditFlags_Uint8)) {
            Rgb next = MdColor::quantize(Rgb{static_cast<uint8_t>(col[0] * 255.0f + 0.5f),
                                             static_cast<uint8_t>(col[1] * 255.0f + 0.5f),
                                             static_cast<uint8_t>(col[2] * 255.0f + 0.5f)});
            if (ed.paint_index == 0) {
                next = Rgb{0, 0, 0};
            }
            ed.push_undo();
            if (ed.art_step()) {
                ed.doc.set_palette_color(ed.paint_index, next);
            } else {
                ed.atlas.set_palette_color(ed.paint_index, next);
            }
        }
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
            }
        }
        ImGui::EndCombo();
    }
    if (ImGui::Button("Grow")) {
        if (ed.art_step()) {
            ed.doc.grow_palette();
        } else {
            ed.atlas.grow_palette();
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Compact unused")) {
        ed.push_undo();
        if (ed.art_step()) {
            ed.doc.compact_unused();
        } else {
            ed.atlas.compact_unused();
        }
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

void copy_selection(Editor& ed) {
    if (ed.sel_a.x < 0) {
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
            ed.clipboard.pixels[static_cast<size_t>(y * ed.clipboard.w + x)] =
                static_cast<uint8_t>(ed.get_px(cell.x, cell.y, loc.x, loc.y));
        }
    }
}

void paste_clipboard(Editor& ed) {
    if (!ed.clipboard.valid()) {
        return;
    }
    const Cell at = (ed.hover.x >= 0) ? ed.hover : Cell{0, 0};
    ed.push_undo();
    for (int y = 0; y < ed.clipboard.h; ++y) {
        for (int x = 0; x < ed.clipboard.w; ++x) {
            ed.plot_src({at.x + x, at.y + y}, ed.clipboard.pixels[static_cast<size_t>(y * ed.clipboard.w + x)]);
        }
    }
    ed.bump_art();
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
}

void draw_settings_window() {
    if (!g_ui.show_settings) {
        return;
    }
    ImGui::SetNextWindowSize(ImVec2(420, 240), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Settings", &g_ui.show_settings)) {
        ImGui::End();
        return;
    }
    draw_settings_controls();
    ImGui::End();
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
                running = false;
            }
        }

        apply_appearance();
        ImGui_ImplSDLRenderer3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();

        if (ImGui::BeginMainMenuBar()) {
            if (ImGui::BeginMenu("File")) {
                if (ImGui::MenuItem("Import 5x3 PNG", "Ctrl+O")) {
                    nfdu8filteritem_t filter = {"PNG", "png"};
                    nfdu8char_t* path = nullptr;
                    if (NFD_OpenDialogU8(&path, &filter, 1, nullptr) == NFD_OKAY) {
                        const std::string err = tsm::load_tileset_png(ed.doc, path);
                        NFD_FreePathU8(path);
                        if (err.empty()) {
                            ed.seeded = true;
                            ed.stamped = true;
                            ed.step = Step::Specialty;
                            ed.configure_view();
                            ed.status = "Imported 5x3 sheet.";
                        } else {
                            ed.status = err;
                        }
                    }
                }
                if (ImGui::MenuItem("Export", "Ctrl+S")) {
                    const std::string err = ed.export_all();
                    ed.status = err.empty() ? "Exported." : err;
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Quit")) {
                    running = false;
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
                if (ImGui::MenuItem("Settings window...", nullptr, g_ui.show_settings)) {
                    g_ui.show_settings = !g_ui.show_settings;
                }
                ImGui::EndMenu();
            }
            ImGui::EndMainMenuBar();
        }

        const bool cmd = io.KeyCtrl || io.KeySuper;
        if (cmd && ImGui::IsKeyPressed(ImGuiKey_Z)) {
            ed.do_undo();
        }
        if (cmd && ImGui::IsKeyPressed(ImGuiKey_S)) {
            const std::string err = ed.export_all();
            ed.status = err.empty() ? "Exported." : err;
        }
        if (cmd && ImGui::IsKeyPressed(ImGuiKey_C) && ed.tool == Tool::Select) {
            copy_selection(ed);
        }
        if (cmd && ImGui::IsKeyPressed(ImGuiKey_V)) {
            paste_clipboard(ed);
        }
        if (cmd && ImGui::IsKeyPressed(ImGuiKey_Comma)) {
            g_ui.show_settings = true;
        }
        if (!io.WantTextInput) {
            if (ImGui::IsKeyPressed(ImGuiKey_1)) ed.tool = Tool::Pencil;
            if (ImGui::IsKeyPressed(ImGuiKey_2)) ed.tool = Tool::Eraser;
            if (ImGui::IsKeyPressed(ImGuiKey_3)) ed.tool = Tool::Fill;
            if (ImGui::IsKeyPressed(ImGuiKey_4)) ed.tool = Tool::Line;
            if (ImGui::IsKeyPressed(ImGuiKey_5)) ed.tool = Tool::Square;
            if (ImGui::IsKeyPressed(ImGuiKey_6)) ed.tool = Tool::Circle;
            if (ImGui::IsKeyPressed(ImGuiKey_7)) ed.tool = Tool::Eyedropper;
            if (ImGui::IsKeyPressed(ImGuiKey_8)) ed.tool = Tool::Select;
        }

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
            ed.go_next();
        }
        ImGui::EndDisabled();
        ImGui::SameLine(0, 24);
        ImGui::SetNextItemWidth(180);
        ImGui::InputText("Project", ed.project_name, sizeof(ed.project_name));
        ImGui::SameLine(0, 24);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Tile size");
        ImGui::SameLine();
        if (ImGui::RadioButton("8x8", ed.doc.tile_size == 8)) {
            ed.doc.reset(8);
            ed.has_atlas = false;
            ed.seeded = false;
            ed.stamped = false;
            ed.step = Step::Center;
            ed.configure_view();
        }
        ImGui::SameLine();
        if (ImGui::RadioButton("16x16", ed.doc.tile_size == 16)) {
            ed.doc.reset(16);
            ed.has_atlas = false;
            ed.seeded = false;
            ed.stamped = false;
            ed.step = Step::Center;
            ed.configure_view();
        }
        ImGui::EndGroup();

        row_rule();

        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Export");
        ImGui::SameLine(0, 16);
        ImGui::Checkbox("Header", &ed.export_header);
        ImGui::SameLine();
        ImGui::Checkbox("Terrain", &ed.export_terrain);
        ImGui::SameLine();
        ImGui::Checkbox("5x3 sheet", &ed.export_5x3);
        ImGui::SameLine(0, 16);
        if (ImGui::Button("Export…")) {
            const std::string err = ed.export_all();
            ed.status = err.empty() ? "Exported." : err;
        }

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
                ed.tool = t;
            }
        }

        row_rule();

        if (ImGui::Button("Undo")) {
            ed.do_undo();
        }
        ImGui::SameLine(0, 16);
        ImGui::SetNextItemWidth(140);
        ImGui::SliderInt("Zoom", &ed.zoom, 4, 32);
        if (uses_brush(ed.tool)) {
            ImGui::SameLine(0, 16);
            ImGui::SetNextItemWidth(120);
            ImGui::SliderInt("Brush", &ed.brush, 1, 4);
        }
        if (ed.step == Step::Center) {
            ImGui::SameLine(0, 16);
            ImGui::Checkbox("Tile mode", &ed.tile_mode);
        }
        if (ed.step == Step::Edges || ed.step == Step::Specialty) {
            ImGui::SameLine(0, 16);
            ImGui::Checkbox("H-flip", &ed.doc.hflip_linked);
            ImGui::SameLine();
            ImGui::Checkbox("V-flip", &ed.doc.vflip_linked);
        }
        if (ed.tool == Tool::Select) {
            ImGui::SameLine(0, 16);
            if (ImGui::Button("Copy")) {
                copy_selection(ed);
            }
            ImGui::SameLine();
            if (ImGui::Button("Paste")) {
                paste_clipboard(ed);
            }
        }
        if (ed.step == Step::Specialty) {
            ImGui::SameLine(0, 16);
            if (ImGui::Button("Stamp this")) {
                ed.doc.push_undo();
                ed.doc.stamp_specialty(ed.specialty);
                ed.bump_art();
            }
            ImGui::SameLine();
            if (ImGui::Button("Stamp all extras")) {
                ed.doc.push_undo();
                ed.doc.stamp_all_specialty();
                ed.bump_art();
            }
            ImGui::SameLine();
            if (ImGui::Button("Reset extras")) {
                ed.doc.push_undo();
                ed.doc.reset_specialty_to_center();
                ed.bump_art();
            }
        }
        if (ed.step == Step::Variants && ed.has_atlas) {
            ImGui::SameLine(0, 16);
            if (ImGui::Button("Add variant")) {
                ed.atlas.push_undo();
                const Cell slot = ed.atlas.add_variant(ed.atlas_cell, ed.variant_chance);
                if (slot.x >= 0) {
                    ed.atlas_cell = slot;
                    ed.configure_view();
                }
            }
            ImGui::SameLine();
            ImGui::BeginDisabled(!ed.atlas.is_extra(ed.atlas_cell.x, ed.atlas_cell.y));
            if (ImGui::Button("Remove variant")) {
                ed.atlas.push_undo();
                ed.atlas.remove_variant(ed.atlas_cell);
                ed.atlas_cell = {9, 2};
                ed.configure_view();
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
            }
        }

        row_rule();

        ImGui::TextWrapped("%s", ed.status.c_str());
        if (ed.art_step()) {
            ImGui::TextDisabled("%s", ed.doc.cell_name(ed.preview_sel.x, ed.preview_sel.y).c_str());
        } else {
            ImGui::TextDisabled("%s", ed.atlas.cell_name(ed.atlas_cell.x, ed.atlas_cell.y).c_str());
        }

        if (ImGui::BeginTable("layout", 2, ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV)) {
            ImGui::TableSetupColumn("canvas", ImGuiTableColumnFlags_WidthStretch, 0.68f);
            ImGui::TableSetupColumn("side", ImGuiTableColumnFlags_WidthStretch, 0.32f);
            ImGui::TableNextColumn();
            ImGui::BeginChild("canvas_panel", ImVec2(0, 0), ImGuiChildFlags_Borders);
            handle_canvas(ed);
            ImGui::EndChild();
            ImGui::TableNextColumn();
            ImGui::BeginChild("side_panel", ImVec2(0, 0), ImGuiChildFlags_Borders);
            if (ed.art_step()) {
                const int pc = (ed.step == Step::Edges) ? 3 : TilesetDoc::kCols;
                const int pr = (ed.step == Step::Edges) ? 3 : TilesetDoc::kRows;
                draw_preview_grid(ed, ed.step == Step::Edges ? "3x3 preview" : "5x3 sheet", pc, pr, false);
            } else if (ed.has_atlas) {
                draw_preview_grid(ed, "12x4 atlas", ed.atlas.cols, AtlasDoc::kRows, true);
            }
            row_rule();
            draw_palette(ed);
            ImGui::EndChild();
            ImGui::EndTable();
        }
        ImGui::End();

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
