#include "tileset_editor.h"

#include "../core/atlas_doc.h"
#include "../core/convert.h"
#include "../core/draw.h"
#include "../core/io.h"
#include "../core/md_color.h"
#include "../core/palette_presets.h"
#include "../core/project.h"
#include "../core/tileset_doc.h"

#include "imgui.h"
#include "nfd.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace tsm {
namespace {

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

std::string dirname_of(const std::string& path) {
    const auto slash = path.find_last_of("/\\");
    return (slash == std::string::npos) ? std::string() : path.substr(0, slash);
}

std::string basename_of(const std::string& path) {
    const auto slash = path.find_last_of("/\\");
    const std::string file = (slash == std::string::npos) ? path : path.substr(slash + 1);
    const auto dot = file.find_last_of('.');
    return (dot == std::string::npos) ? file : file.substr(0, dot);
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

ImU32 canvas_tile_grid_color(const Settings& s) {
    return im_color(s.tile_grid_color);
}

ImU32 canvas_pixel_grid_color(const Settings& s) {
    return im_color(s.pixel_grid_color);
}

void draw_pixels(ImDrawList* dl, ImVec2 origin, float zoom, int w, int h, const TilesetEditor& ed, int ox, int oy, int cols,
                 int rows, bool atlas, ImU32 grid_col, float grid_thickness = 1.0f, bool pixel_grid = false,
                 bool canvas_mode = false) {
    const int ts = atlas ? ed.atlas.tile_size : ed.doc.tile_size;
    if (ts <= 0 || zoom <= 0.0f) {
        return;
    }
    const ImU32 bg_color0 = im_color(atlas ? ed.atlas.color_at(0) : ed.doc.color_at(0));

    auto is_empty_cell = [&](int gx, int gy) -> bool {
        if (!canvas_mode) return false;
        if (atlas && ed.step == Step::Variants) {
            if (gx == 1 && gy == 1) return false;
            const Cell ctx = ed.atlas.context_cell(ed.atlas_cell, gx, gy);
            return (ctx.x < 0 || ctx.y < 0);
        }
        if (!atlas && ed.step == Step::Specialty) {
            if (ed.specialty == TilesetDoc::kInnerCorner) {
                return ((gx == 0 || gx == 2) && (gy == 0 || gy == 2));
            }
            const Cell cell = ed.src_to_cell(Cell{gx * ts, gy * ts});
            return (cell.x < 0 || cell.y < 0);
        }
        return false;
    };

    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const ImVec2 p0(origin.x + static_cast<float>(x) * zoom, origin.y + static_cast<float>(y) * zoom);
            const ImVec2 p1(origin.x + static_cast<float>(x + 1) * zoom, origin.y + static_cast<float>(y + 1) * zoom);

            if (canvas_mode && atlas && ed.step == Step::Variants) {
                const int gx = x / ts;
                const int gy = y / ts;
                const int lx = x % ts;
                const int ly = y % ts;
                const Cell ctx = (gx == 1 && gy == 1) ? ed.atlas_cell : ed.atlas.context_cell(ed.atlas_cell, gx, gy);
                if (ctx.x < 0 || ctx.y < 0) {
                    dl->AddRectFilled(p0, p1, bg_color0);
                } else {
                    const int idx = ed.atlas.get_pixel(ctx.x, ctx.y, lx, ly);
                    dl->AddRectFilled(p0, p1, im_color(ed.atlas.color_at(idx)));
                }
            } else if (canvas_mode && !atlas && ed.step == Step::Specialty) {
                const int gx = x / ts;
                const int gy = y / ts;
                const int lx = x % ts;
                const int ly = y % ts;

                if (ed.specialty == TilesetDoc::kInnerCorner) {
                    if (gx == 1 && gy == 1) {
                        const int idx = ed.doc.get_pixel(TilesetDoc::kInnerCorner.x, TilesetDoc::kInnerCorner.y, lx, ly);
                        dl->AddRectFilled(p0, p1, im_color(ed.doc.color_at(idx)));
                    } else if ((gx == 1 && gy == 0) || (gx == 1 && gy == 2)) {
                        const int idx = ed.atlas.get_pixel(0, 1, lx, ly);
                        dl->AddRectFilled(p0, p1, im_color(ed.atlas.color_at(idx)));
                    } else if ((gx == 0 && gy == 1) || (gx == 2 && gy == 1)) {
                        const int idx = ed.atlas.get_pixel(2, 3, lx, ly);
                        dl->AddRectFilled(p0, p1, im_color(ed.atlas.color_at(idx)));
                    } else {
                        dl->AddRectFilled(p0, p1, bg_color0);
                    }
                    continue;
                }

                const Cell cell = ed.src_to_cell(Cell{x, y});
                if (cell.x < 0 || cell.y < 0) {
                    dl->AddRectFilled(p0, p1, bg_color0);
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
        const ImU32 pixel_col = canvas_pixel_grid_color(ed.settings);
        for (int gy = 0; gy < rows; ++gy) {
            for (int gx = 0; gx < cols; ++gx) {
                if (is_empty_cell(gx, gy)) continue;
                const float cell_x = origin.x + static_cast<float>(gx * ts * zoom);
                const float cell_y = origin.y + static_cast<float>(gy * ts * zoom);
                for (int py = 1; py < ts; ++py) {
                    const float y = cell_y + static_cast<float>(py * zoom);
                    dl->AddLine(ImVec2(cell_x, y), ImVec2(cell_x + static_cast<float>(ts * zoom), y), pixel_col);
                }
                for (int px = 1; px < ts; ++px) {
                    const float x = cell_x + static_cast<float>(px * zoom);
                    dl->AddLine(ImVec2(x, cell_y), ImVec2(x, cell_y + static_cast<float>(ts * zoom)), pixel_col);
                }
            }
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

    if (canvas_mode && ((!atlas && ed.step == Step::Specialty) || (atlas && ed.step == Step::Variants))) {
        const ImVec2 c0(origin.x + static_cast<float>(ts * zoom), origin.y + static_cast<float>(ts * zoom));
        const ImVec2 c1(c0.x + static_cast<float>(ts * zoom), c0.y + static_cast<float>(ts * zoom));
        dl->AddRect(c0, c1, IM_COL32(255, 220, 60, 220), 0.0f, 0, 2.0f);

        if (!atlas && ed.step == Step::Specialty && ed.specialty == TilesetDoc::kInnerCorner) {
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

void draw_preview_grid(TilesetEditor& ed, const char* title, int cols, int rows, bool atlas, int ox = 0, int oy = 0) {
    ImGui::TextUnformatted(title);
    const int ts = ed.tile_size();
    const float avail_w = ImGui::GetContentRegionAvail().x;
    const float grid_px_w = static_cast<float>(std::max(1, cols * ts));
    const float z = std::max(0.1f, avail_w / grid_px_w);
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImVec2 size(avail_w, static_cast<float>(rows * ts) * z);
    ImGui::InvisibleButton("preview", size);
    draw_pixels(ImGui::GetWindowDrawList(), origin, z, cols * ts, rows * ts, ed, ox, oy, cols, rows, atlas, canvas_tile_grid_color(ed.settings));
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

void row_rule() {
    ImGui::Separator();
}

bool tool_button(Tool t, Tool current, bool dark) {
    const bool selected = (t == current);
    const ImVec4 normal = dark ? ImVec4(0.20f, 0.22f, 0.26f, 1.0f) : ImVec4(0.84f, 0.86f, 0.90f, 1.0f);
    const ImVec4 active = dark ? ImVec4(0.26f, 0.44f, 0.72f, 1.0f) : ImVec4(0.68f, 0.80f, 0.98f, 1.0f);
    const ImVec4 text = dark ? ImVec4(0.93f, 0.94f, 0.96f, 1.0f) : ImVec4(0.10f, 0.12f, 0.16f, 1.0f);
    ImGui::PushStyleColor(ImGuiCol_Button, selected ? active : normal);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, active);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, active);
    ImGui::PushStyleColor(ImGuiCol_Text, text);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, selected ? 2.0f : 1.0f);
    ImGui::PushStyleColor(ImGuiCol_Border, selected ? ImVec4(1, 1, 1, dark ? 0.95f : 0.85f)
                                                    : ImVec4(0, 0, 0, dark ? 0.45f : 0.25f));
    char label[32];
    std::snprintf(label, sizeof(label), "%s  %d", tool_name(t), static_cast<int>(t) + 1);
    const bool hit = ImGui::Button(label);
    ImGui::PopStyleColor(5);
    ImGui::PopStyleVar();
    return hit;
}

std::vector<std::pair<int, int>> selected_palette_runs(const TilesetEditor& ed) {
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

bool can_shift_palette(const TilesetEditor& ed, int delta) {
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

bool shift_selected_palette(TilesetEditor& ed, int delta) {
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

void draw_rectangular_color_picker(TilesetEditor& ed, int n) {
    if (ed.paint_index < 0 || ed.paint_index >= n) {
        return;
    }
    const Rgb current_c = ed.color(ed.paint_index);
    if (!ed.picker_init || current_c != ed.picker_last_rgb) {
        ed.picker_last_rgb = current_c;
        ed.picker_init = true;
        const float r = current_c.r / 255.0f;
        const float g = current_c.g / 255.0f;
        const float b = current_c.b / 255.0f;
        float h, s, v;
        ImGui::ColorConvertRGBtoHSV(r, g, b, h, s, v);
        if (s > 0.001f) {
            ed.picker_h = h;
        }
        ed.picker_s = s;
        ed.picker_v = v;
    }

    const float avail_w = std::max(60.0f, ImGui::GetContentRegionAvail().x);
    const float sv_h = 80.0f * ed.settings.scale;
    const float hue_h = 14.0f * ed.settings.scale;
    bool changed = false;
    ImDrawList* draw_list = ImGui::GetWindowDrawList();

    const ImVec2 sv_pos = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##sv_rect", ImVec2(avail_w, sv_h));
    if (ImGui::IsItemHovered()) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
    }
    if (ImGui::IsItemActivated()) {
        ed.push_undo();
    }
    if (ImGui::IsItemActive()) {
        const ImVec2 mpos = ImGui::GetIO().MousePos;
        ed.picker_s = std::clamp((mpos.x - sv_pos.x) / std::max(1.0f, avail_w - 1.0f), 0.0f, 1.0f);
        ed.picker_v = std::clamp(1.0f - (mpos.y - sv_pos.y) / std::max(1.0f, sv_h - 1.0f), 0.0f, 1.0f);
        changed = true;
    }

    float hr, hg, hb;
    ImGui::ColorConvertHSVtoRGB(ed.picker_h, 1.0f, 1.0f, hr, hg, hb);
    const ImU32 hue_col = ImGui::ColorConvertFloat4ToU32(ImVec4(hr, hg, hb, 1.0f));
    const ImU32 white_col = IM_COL32(255, 255, 255, 255);
    const ImU32 black_col = IM_COL32(0, 0, 0, 255);
    const ImVec2 sv_max(sv_pos.x + avail_w, sv_pos.y + sv_h);

    draw_list->AddRectFilledMultiColor(sv_pos, sv_max, white_col, hue_col, hue_col, white_col);
    draw_list->AddRectFilledMultiColor(sv_pos, sv_max, 0, 0, black_col, black_col);
    const ImU32 border_col = ed.settings.dark ? IM_COL32(70, 74, 82, 255) : IM_COL32(170, 176, 186, 255);
    draw_list->AddRect(sv_pos, sv_max, border_col, 0.0f, 0, 1.0f);

    const ImVec2 marker_pos(sv_pos.x + ed.picker_s * (avail_w - 1.0f),
                            sv_pos.y + (1.0f - ed.picker_v) * (sv_h - 1.0f));
    draw_list->AddCircle(marker_pos, 5.0f, IM_COL32(0, 0, 0, 230), 0, 2.0f);
    draw_list->AddCircle(marker_pos, 4.0f, IM_COL32(255, 255, 255, 255), 0, 1.5f);

    const ImVec2 hue_pos = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##hue_bar", ImVec2(avail_w, hue_h));
    if (ImGui::IsItemHovered()) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
    }
    if (ImGui::IsItemActivated()) {
        ed.push_undo();
    }
    if (ImGui::IsItemActive()) {
        const ImVec2 mpos = ImGui::GetIO().MousePos;
        ed.picker_h = std::clamp((mpos.x - hue_pos.x) / std::max(1.0f, avail_w - 1.0f), 0.0f, 1.0f);
        changed = true;
    }

    static const ImU32 col_hues[7] = {
        IM_COL32(255, 0, 0, 255),
        IM_COL32(255, 255, 0, 255),
        IM_COL32(0, 255, 0, 255),
        IM_COL32(0, 255, 255, 255),
        IM_COL32(0, 0, 255, 255),
        IM_COL32(255, 0, 255, 255),
        IM_COL32(255, 0, 0, 255)
    };
    const float step_w = avail_w / 6.0f;
    for (int i = 0; i < 6; ++i) {
        const ImVec2 p0(hue_pos.x + static_cast<float>(i) * step_w, hue_pos.y);
        const ImVec2 p1(hue_pos.x + static_cast<float>(i + 1) * step_w, hue_pos.y + hue_h);
        draw_list->AddRectFilledMultiColor(p0, p1, col_hues[i], col_hues[i + 1], col_hues[i + 1], col_hues[i]);
    }
    draw_list->AddRect(hue_pos, ImVec2(hue_pos.x + avail_w, hue_pos.y + hue_h), border_col, 0.0f, 0, 1.0f);

    const float hx = hue_pos.x + ed.picker_h * (avail_w - 1.0f);
    draw_list->AddRectFilled(ImVec2(hx - 2.5f, hue_pos.y - 1.0f), ImVec2(hx + 2.5f, hue_pos.y + hue_h + 1.0f), IM_COL32(0, 0, 0, 230), 1.0f);
    draw_list->AddRectFilled(ImVec2(hx - 1.0f, hue_pos.y), ImVec2(hx + 1.0f, hue_pos.y + hue_h), IM_COL32(255, 255, 255, 255), 1.0f);

    if (changed) {
        float nr, ng, nb;
        ImGui::ColorConvertHSVtoRGB(ed.picker_h, ed.picker_s, ed.picker_v, nr, ng, nb);
        Rgb next = MdColor::quantize(Rgb{static_cast<uint8_t>(nr * 255.0f + 0.5f),
                                         static_cast<uint8_t>(ng * 255.0f + 0.5f),
                                         static_cast<uint8_t>(nb * 255.0f + 0.5f)});
        ed.picker_last_rgb = next;
        for (int i = 0; i < n; ++i) {
            if (ed.is_palette_selected(i)) {
                if (ed.art_step()) {
                    ed.doc.set_palette_color(i, next);
                } else {
                    ed.atlas.set_palette_color(i, next);
                }
            }
        }
        ed.bump_art();
    }
    ImGui::Spacing();
}

void draw_palette(TilesetEditor& ed) {
    const int n = ed.art_step() ? ed.doc.palette_count() : ed.atlas.palette_count();
    ImGui::Text("Palette (%d)", n);
    draw_rectangular_color_picker(ed, n);
    const float swatch = 22.0f * ed.settings.scale;
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
            ImGui::GetWindowDrawList()->AddRect(p0, p1, canvas_tile_grid_color(ed.settings));
        }
        ImGui::PopID();
    }
    ImGui::Spacing();
    const bool can_left = can_shift_palette(ed, -1);
    const bool can_right = can_shift_palette(ed, 1);
    if (!can_left) ImGui::BeginDisabled();
    if (ImGui::Button("< Shift Left")) shift_selected_palette(ed, -1);
    if (!can_left) ImGui::EndDisabled();
    ImGui::SameLine();
    if (!can_right) ImGui::BeginDisabled();
    if (ImGui::Button("Shift Right >")) shift_selected_palette(ed, 1);
    if (!can_right) ImGui::EndDisabled();

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
    const bool can_add = (ed.art_step() ? ed.doc.palette_count() : ed.atlas.palette_count()) < TilesetDoc::kPaletteSize;
    if (!can_add) ImGui::BeginDisabled();
    if (ImGui::Button("Add Color")) {
        const int count = ed.art_step() ? ed.doc.palette_count() : ed.atlas.palette_count();
        if (count < TilesetDoc::kPaletteSize) {
            ed.push_undo();
            const int src_idx = std::clamp(ed.paint_index, 0, count - 1);
            const Rgb copy_c = ed.color(src_idx);
            const bool grown = ed.art_step() ? ed.doc.grow_palette(copy_c) : ed.atlas.grow_palette(copy_c);
            if (grown) {
                const int new_idx = (ed.art_step() ? ed.doc.palette_count() : ed.atlas.palette_count()) - 1;
                ed.select_single_palette(new_idx);
                ed.touch();
                ed.bump_art();
            }
        }
    }
    if (!can_add) ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Remove Unused Colors")) {
        ed.push_undo();
        if (ed.art_step()) ed.doc.compact_unused();
        else ed.atlas.compact_unused();
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
                if (ed.art_step()) ed.doc.apply_palette(loaded.colors);
                else ed.atlas.apply_palette(loaded.colors);
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
            if (ed.status.empty()) ed.status = "Saved palette.";
            NFD_FreePathU8(path);
        }
    }
}

void handle_canvas(TilesetEditor& ed) {
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
    const ImU32 bg_color0 = im_color(ed.color(0));
    dl->AddRectFilled(cursor, ImVec2(cursor.x + std::max(1.0f, avail.x), cursor.y + std::max(1.0f, avail.y)), bg_color0);
    for (int ry = 0; ry < reps; ++ry) {
        for (int rx = 0; rx < reps; ++rx) {
            const ImVec2 o(origin.x + static_cast<float>(rx * sw * ed.zoom),
                            origin.y + static_cast<float>(ry * sh * ed.zoom));
            draw_pixels(dl, o, static_cast<float>(ed.zoom), sw, sh, ed, ed.edit_ox, ed.edit_oy, ed.edit_cols, ed.edit_rows, !ed.art_step(),
                        canvas_tile_grid_color(ed.settings), 2.0f, ed.settings.pixel_grid, /*canvas_mode=*/true);
        }
    }

    auto pos_to_src = [&](ImVec2 p) -> Cell {
        const int lx = static_cast<int>(p.x - origin.x);
        const int ly = static_cast<int>(p.y - origin.y);
        if (lx < 0 || ly < 0 || ed.zoom <= 0) return {-1, -1};
        if (lx >= sw * reps * ed.zoom || ly >= sh * reps * ed.zoom) return {-1, -1};
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
            ed.cancel_floating();
        } else if (ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter)) {
            ed.commit_floating();
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
                    ed.floating_drag_start_mouse_x = mpos.x;
                    ed.floating_drag_start_mouse_y = mpos.y;
                } else {
                    ed.commit_floating();
                }
            }

            if (ed.floating && ed.floating_dragging && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                const float dmx = mpos.x - ed.floating_drag_start_mouse_x;
                const float dmy = mpos.y - ed.floating_drag_start_mouse_y;
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
                ed.start_selection_move();
                ed.floating_pos.x = std::clamp(ed.floating_pos.x + dx, 0, std::max(0, sw - ed.floating_w));
                ed.floating_pos.y = std::clamp(ed.floating_pos.y + dy, 0, std::max(0, sh - ed.floating_h));
            }
        }

        if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
            const int px = (ed.hover.x >= 0) ? ed.sample_hover_pixel(ed.hover) : 0;
            if (px >= 0) ed.select_single_palette(px);
        }

        const bool stroke_tool = ed.tool == Tool::Line || ed.tool == Tool::Square || ed.tool == Tool::Circle;
        if (ImGui::IsItemClicked(ImGuiMouseButton_Left) && ed.hover.x >= 0) {
            if (ed.tool == Tool::Select) {
                if (ed.has_selection() && ed.in_selection(ed.hover)) {
                    ed.start_selection_move();
                    ed.floating_dragging = true;
                    ed.floating_drag_start_pos = ed.floating_pos;
                    ed.floating_drag_start_mouse_x = ImGui::GetIO().MousePos.x;
                    ed.floating_drag_start_mouse_y = ImGui::GetIO().MousePos.y;
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
                const int px = (ed.hover.x >= 0) ? ed.sample_hover_pixel(ed.hover) : 0;
                if (px >= 0) ed.select_single_palette(px);
            } else if (ed.tool == Tool::Fill) {
                if (!ed.has_selection()) {
                    if (ed.can_stamp(ed.hover)) {
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
                    }
                } else if (ed.in_selection(ed.hover)) {
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
                            if (ed.has_selection() && !ed.in_selection(pt)) continue;
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

void draw_split_layout(TilesetEditor& ed, float avail_h) {
    const float avail_x = ImGui::GetContentRegionAvail().x;
    const float avail_y = (avail_h > 0.0f) ? avail_h : ImGui::GetContentRegionAvail().y;
    const float splitter = 8.0f;
    const float min_side = 290.0f * ed.settings.scale;
    const float min_canvas = 160.0f * ed.settings.scale;
    float side = ed.sidebar_w;
    if (avail_x < min_side + min_canvas + splitter) {
        side = std::max(170.0f * ed.settings.scale, avail_x - min_canvas * 0.5f - splitter);
    } else {
        side = std::clamp(side, min_side, avail_x - min_canvas - splitter);
    }
    ed.sidebar_w = side;
    const float canvas_w = std::max(1.0f, avail_x - side - splitter);

    const ImVec4 canvas_bg = ImGui::ColorConvertU32ToFloat4(im_color(ed.color(0)));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, canvas_bg);
    ImGui::BeginChild("canvas_panel", ImVec2(canvas_w, avail_y), ImGuiChildFlags_Borders,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    handle_canvas(ed);
    ImGui::EndChild();
    ImGui::PopStyleColor();
    ImGui::SameLine(0, 0);
    ImGui::InvisibleButton("vsplit", ImVec2(splitter, avail_y));
    if (ImGui::IsItemActive()) {
        ed.sidebar_w -= ImGui::GetIO().MouseDelta.x;
    }
    if (ImGui::IsItemHovered() || ImGui::IsItemActive()) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
    }
    ImGui::GetWindowDrawList()->AddRectFilled(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(),
                                              ed.settings.dark ? IM_COL32(70, 74, 82, 255) : IM_COL32(170, 176, 186, 255));
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

} // namespace

TilesetEditor::TilesetEditor() {
    configure_view();
}

void TilesetEditor::reset_new(const std::string& name, int size) {
    const float keep_side = sidebar_w;
    const bool keep_embedded = embedded;
    *this = TilesetEditor();
    sidebar_w = keep_side;
    embedded = keep_embedded;
    std::snprintf(project_name, sizeof(project_name), "%s", name.c_str());
    doc.reset(size);
    atlas.reset(size);
    dirty = false;
    status = "Paint the center fill tile.";
    ui.project_open = true;
    configure_view();
}

bool TilesetEditor::import_12x4(const std::string& file_path) {
    std::string loaded_png;
    bool had_terrain = false;
    const std::string err = tsm::import_12x4_tileset(atlas, file_path, loaded_png, had_terrain);
    if (!err.empty()) {
        status = err;
        return false;
    }

    tsm::convert_atlas_to_tileset(atlas, doc);

    last_dir = dirname_of(file_path);
    std::string stem = basename_of(file_path);
    if (stem.size() >= 8 && stem.substr(stem.size() - 8) == ".terrain") {
        stem = stem.substr(0, stem.size() - 8);
    }
    std::snprintf(project_name, sizeof(project_name), "%s", stem.c_str());
    project_path.clear();

    seeded = true;
    stamped = true;
    has_atlas = true;
    art_rev = 0;
    atlas_rev = 0;
    step = Step::Variants;
    atlas_cell = {9, 2};
    preview_sel = {1, 1};
    dirty = false;
    undo_selections.clear();
    ui.project_open = true;
    configure_view();

    if (had_terrain) {
        status = "Imported 12x4 tileset with terrain (" + std::to_string(atlas.bindings.size()) + " variants).";
    } else {
        status = "Imported 12x4 tileset.";
    }
    return true;
}

bool TilesetEditor::import_5x3(const std::string& file_path) {
    const std::string err = tsm::load_tileset_png(doc, file_path);
    if (!err.empty()) {
        status = err;
        return false;
    }

    last_dir = dirname_of(file_path);
    std::string stem = basename_of(file_path);
    if (stem.size() >= 4 && stem.substr(stem.size() - 4) == "_5x3") {
        stem = stem.substr(0, stem.size() - 4);
    }
    std::snprintf(project_name, sizeof(project_name), "%s", stem.c_str());
    project_path.clear();

    seeded = true;
    stamped = true;
    has_atlas = false;
    art_rev = 0;
    atlas_rev = -1;
    step = Step::Specialty;
    atlas_cell = {9, 2};
    preview_sel = {1, 1};
    dirty = false;
    undo_selections.clear();
    ui.project_open = true;
    configure_view();
    bump_art();
    status = "Imported 5x3 tileset sheet.";
    return true;
}

bool TilesetEditor::try_import_12x4_dialog() {
    nfdu8filteritem_t filters[] = {
        {"12x4 Tileset (*.png, *.terrain)", "png,terrain"},
        {"PNG Image (*.png)", "png"},
        {"Terrain File (*.terrain)", "terrain"}
    };
    nfdu8char_t* path = nullptr;
    if (NFD_OpenDialogU8(&path, filters, 3, last_dir.empty() ? nullptr : last_dir.c_str()) != NFD_OKAY) {
        return false;
    }
    const std::string file_path = path;
    NFD_FreePathU8(path);
    return import_12x4(file_path);
}

bool TilesetEditor::try_import_5x3_dialog() {
    nfdu8filteritem_t filter = {"PNG Image (*.png)", "png"};
    nfdu8char_t* path = nullptr;
    if (NFD_OpenDialogU8(&path, &filter, 1, last_dir.empty() ? nullptr : last_dir.c_str()) != NFD_OKAY) {
        return false;
    }
    const std::string file_path = path;
    NFD_FreePathU8(path);
    return import_5x3(file_path);
}

bool TilesetEditor::try_open_project_dialog() {
    nfdu8filteritem_t filter = {"Tileset project", "tilesetproj"};
    nfdu8char_t* path = nullptr;
    if (NFD_OpenDialogU8(&path, &filter, 1, last_dir.empty() ? nullptr : last_dir.c_str()) != NFD_OKAY) {
        return false;
    }
    const std::string file_path = path;
    NFD_FreePathU8(path);
    ProjectData data;
    const std::string err = tsm::load_project(data, file_path);
    if (!err.empty()) {
        status = err;
        return false;
    }

    doc.restore(data.tileset);
    if (data.has_atlas) {
        atlas.restore(data.atlas);
    } else {
        atlas.reset(data.tileset.tile_size);
    }
    has_atlas = data.has_atlas;
    art_rev = data.art_rev;
    atlas_rev = data.has_atlas ? art_rev : data.atlas_rev;
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
    project_path = file_path;
    last_dir = dirname_of(file_path);
    dirty = false;
    undo_selections.clear();
    status = "Loaded project.";
    ui.project_open = true;
    configure_view();
    return true;
}

std::string TilesetEditor::save_project(bool save_as) {
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

    if (!save_as && !project_path.empty()) {
        const std::string err = tsm::save_project(data, project_path);
        if (err.empty()) dirty = false;
        return err;
    }
    nfdu8filteritem_t filter = {"Tileset project", "tilesetproj"};
    nfdu8char_t* path = nullptr;
    const std::string def = std::string(project_name) + ".tilesetproj";
    const nfdresult_t r = NFD_SaveDialogU8(&path, &filter, 1, last_dir.empty() ? nullptr : last_dir.c_str(), def.c_str());
    if (r != NFD_OKAY) {
        return r == NFD_CANCEL ? std::string("#cancel") : "Save cancelled or failed";
    }
    std::string dest = tsm::with_tilesetproj_ext(path);
    NFD_FreePathU8(path);
    last_dir = dirname_of(dest);
    const std::string err = tsm::save_project(data, dest);
    if (err.empty()) {
        project_path = dest;
        dirty = false;
    }
    return err;
}

std::string TilesetEditor::ensure_atlas() {
    if (atlas_rev == art_rev && has_atlas) {
        return {};
    }
    const std::string err = tsm::convert_tileset_to_atlas(doc, atlas);
    if (err.empty()) {
        has_atlas = true;
        atlas_rev = art_rev;
    }
    return err;
}

std::string TilesetEditor::export_all() {
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
    last_exported_png_path = dest;
    const auto dot = dest.find_last_of('.');
    if (export_5x3) {
        const std::string side = dest.substr(0, dot) + "_5x3.png";
        err = tsm::save_tileset_png(doc, side);
        if (!err.empty()) return err;
    }
    if (export_header) {
        err = tsm::save_header(atlas, dest.substr(0, dot) + ".h", dest);
        if (!err.empty()) return err;
    }
    if (export_terrain) {
        const std::string terr_path = dest.substr(0, dot) + ".terrain";
        err = tsm::save_terrain(atlas, terr_path, dest);
        if (!err.empty()) return err;
        last_exported_terrain_path = terr_path;
    }
    return {};
}

void TilesetEditor::bump_art() {
    if (art_step()) {
        ++art_rev;
    }
    dirty = true;
}

void TilesetEditor::configure_view() {
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
        edit_cols = 3;
        edit_rows = 3;
        break;
    }
}

void TilesetEditor::go_back() {
    if (step == Step::Edges) {
        step = Step::Center;
    } else if (step == Step::Specialty) {
        step = Step::Edges;
    } else if (step == Step::Variants) {
        step = Step::Specialty;
    }
    configure_view();
}

void TilesetEditor::go_next() {
    if (step == Step::Center) {
        if (!seeded) {
            push_undo();
            doc.seed_from_center();
            seeded = true;
            bump_art();
        }
        step = Step::Edges;
    } else if (step == Step::Edges) {
        if (!stamped) {
            push_undo();
            doc.stamp_all_specialty();
            stamped = true;
            bump_art();
        }
        step = Step::Specialty;
    } else if (step == Step::Specialty) {
        const std::string err = ensure_atlas();
        if (!err.empty()) {
            status = err;
            return;
        }
        step = Step::Variants;
        atlas_cell = {9, 2};
    }
    configure_view();
}

int TilesetEditor::src_w() const { return edit_cols * (art_step() ? doc.tile_size : atlas.tile_size); }
int TilesetEditor::src_h() const { return edit_rows * (art_step() ? doc.tile_size : atlas.tile_size); }
int TilesetEditor::reps() const { return (step == Step::Center && tile_mode) ? 5 : 1; }
int TilesetEditor::tile_size() const { return art_step() ? doc.tile_size : atlas.tile_size; }
uint8_t TilesetEditor::pixel(int col, int row, int x, int y) const {
    return art_step() ? doc.get_pixel(col, row, x, y) : atlas.get_pixel(col, row, x, y);
}
Rgb TilesetEditor::color(int idx) const { return art_step() ? doc.color_at(idx) : atlas.color_at(idx); }
int TilesetEditor::last_index() const { return art_step() ? doc.last_palette_index() : atlas.last_palette_index(); }
bool TilesetEditor::in_sheet(int col, int row) const {
    return art_step() ? TilesetDoc::in_sheet(col, row) : atlas.in_sheet(col, row);
}

bool TilesetEditor::is_palette_selected(int idx) const {
    return (idx >= 0 && idx < 16) && ((palette_selected_mask & (1 << idx)) != 0);
}
void TilesetEditor::select_single_palette(int idx) {
    paint_index = idx;
    palette_anchor = idx;
    palette_selected_mask = (idx >= 0 && idx < 16) ? static_cast<uint16_t>(1 << idx) : 0;
}
void TilesetEditor::select_range_palette(int dest) {
    if (dest < 0 || dest >= 16) return;
    paint_index = dest;
    palette_selected_mask = 0;
    const int lo = std::min(palette_anchor, dest);
    const int hi = std::max(palette_anchor, dest);
    for (int k = lo; k <= hi; ++k) {
        palette_selected_mask |= static_cast<uint16_t>(1 << k);
    }
}
void TilesetEditor::toggle_palette_selected(int idx) {
    if (idx < 0 || idx >= 16) return;
    palette_selected_mask ^= static_cast<uint16_t>(1 << idx);
    if (palette_selected_mask == 0) {
        palette_selected_mask = static_cast<uint16_t>(1 << idx);
    }
    paint_index = idx;
    palette_anchor = idx;
}

bool TilesetEditor::has_selection() const { return sel_a.x >= 0 && sel_b.x >= 0; }
void TilesetEditor::clear_selection() { sel_a = {-1, -1}; sel_b = {-1, -1}; selecting = false; }
bool TilesetEditor::in_selection(Cell src) const {
    if (!has_selection()) return false;
    const int x0 = std::min(sel_a.x, sel_b.x);
    const int y0 = std::min(sel_a.y, sel_b.y);
    const int x1 = std::max(sel_a.x, sel_b.x);
    const int y1 = std::max(sel_a.y, sel_b.y);
    return src.x >= x0 && src.x <= x1 && src.y >= y0 && src.y <= y1;
}
bool TilesetEditor::in_floating_box(Cell c) const {
    if (!floating) return false;
    return c.x >= floating_pos.x && c.x < floating_pos.x + floating_w &&
           c.y >= floating_pos.y && c.y < floating_pos.y + floating_h;
}
void TilesetEditor::cancel_floating_internal() {
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
void TilesetEditor::cancel_floating() {
    cancel_floating_internal();
    status = "Cancelled.";
}
void TilesetEditor::commit_floating() {
    if (!floating) return;
    if (floating_type == FloatingType::MoveSelection) {
        if (floating_pos.x == move_orig_a.x && floating_pos.y == move_orig_a.y) {
            do_undo();
            sel_a = move_orig_a;
            sel_b = move_orig_b;
            floating = false;
            floating_type = FloatingType::None;
            floating_pixels.clear();
            floating_dragging = false;
            status = "Placed.";
            return;
        }
    }
    if (floating_type == FloatingType::Paste) {
        push_undo();
    }
    for (int y = 0; y < floating_h; ++y) {
        for (int x = 0; x < floating_w; ++x) {
            const int px = floating_pos.x + x;
            const int py = floating_pos.y + y;
            plot_src({px, py}, floating_pixels[static_cast<size_t>(y * floating_w + x)]);
        }
    }
    bump_art();
    sel_a = floating_pos;
    sel_b = {floating_pos.x + floating_w - 1, floating_pos.y + floating_h - 1};
    floating = false;
    floating_type = FloatingType::None;
    floating_pixels.clear();
    floating_dragging = false;
    status = "Placed.";
}
void TilesetEditor::start_selection_move() {
    if (!has_selection()) return;
    if (floating) commit_floating();
    const int x0 = std::min(sel_a.x, sel_b.x);
    const int y0 = std::min(sel_a.y, sel_b.y);
    const int x1 = std::max(sel_a.x, sel_b.x);
    const int y1 = std::max(sel_a.y, sel_b.y);
    const int w = x1 - x0 + 1;
    const int h = y1 - y0 + 1;

    push_undo();

    floating = true;
    floating_type = FloatingType::MoveSelection;
    floating_pos = {x0, y0};
    floating_w = w;
    floating_h = h;
    move_orig_a = {x0, y0};
    move_orig_b = {x1, y1};
    sel_a = {x0, y0};
    sel_b = {x1, y1};
    floating_pixels.resize(static_cast<size_t>(w * h));

    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const Cell src{x0 + x, y0 + y};
            const Cell cell = src_to_cell(src);
            const Cell loc = src_local(src);
            if (in_doc(cell.x, cell.y)) {
                floating_pixels[static_cast<size_t>(y * w + x)] = static_cast<uint8_t>(get_px(cell.x, cell.y, loc.x, loc.y));
                set_px(cell.x, cell.y, loc.x, loc.y, 0);
            } else {
                floating_pixels[static_cast<size_t>(y * w + x)] = 0;
            }
        }
    }
    bump_art();
    status = "Moving selection: Arrow keys or click and drag. Enter or click outside to place, Esc to cancel.";
}
void TilesetEditor::start_paste() {
    if (!clipboard.valid()) {
        status = "Clipboard is empty.";
        return;
    }
    if (floating) commit_floating();
    tool = Tool::Select;
    floating = true;
    floating_type = FloatingType::Paste;
    floating_w = clipboard.w;
    floating_h = clipboard.h;
    floating_pixels = clipboard.pixels;
    const int sw = src_w();
    const int sh = src_h();
    if (has_selection()) {
        floating_pos = {std::min(sel_a.x, sel_b.x), std::min(sel_a.y, sel_b.y)};
    } else if (hover.x >= 0 && hover.y >= 0) {
        floating_pos.x = std::clamp(hover.x - floating_w / 2, 0, std::max(0, sw - floating_w));
        floating_pos.y = std::clamp(hover.y - floating_h / 2, 0, std::max(0, sh - floating_h));
    } else {
        floating_pos.x = std::clamp((sw - floating_w) / 2, 0, std::max(0, sw - floating_w));
        floating_pos.y = std::clamp((sh - floating_h) / 2, 0, std::max(0, sh - floating_h));
    }
    status = "Paste: Arrow keys or click and drag. Enter or click outside to place, Esc to cancel.";
}
void TilesetEditor::copy_selection() {
    if (floating && !floating_pixels.empty()) {
        clipboard.w = floating_w;
        clipboard.h = floating_h;
        clipboard.pixels = floating_pixels;
        status = "Copied " + std::to_string(clipboard.w) + "x" + std::to_string(clipboard.h) + " selection.";
        return;
    }
    if (sel_a.x < 0 || sel_b.x < 0) return;
    const int x0 = std::min(sel_a.x, sel_b.x);
    const int y0 = std::min(sel_a.y, sel_b.y);
    const int x1 = std::max(sel_a.x, sel_b.x);
    const int y1 = std::max(sel_a.y, sel_b.y);
    clipboard.w = x1 - x0 + 1;
    clipboard.h = y1 - y0 + 1;
    clipboard.pixels.resize(static_cast<size_t>(clipboard.w * clipboard.h));
    for (int y = 0; y < clipboard.h; ++y) {
        for (int x = 0; x < clipboard.w; ++x) {
            const Cell src{x0 + x, y0 + y};
            const Cell loc = src_local(src);
            if (step == Step::Variants) {
                const int ts = tile_size();
                const int gx = src.x / ts;
                const int gy = src.y / ts;
                const Cell ctx = (gx == 1 && gy == 1) ? atlas_cell : atlas.context_cell(atlas_cell, gx, gy);
                if (ctx.x >= 0 && ctx.y >= 0) {
                    clipboard.pixels[static_cast<size_t>(y * clipboard.w + x)] = static_cast<uint8_t>(atlas.get_pixel(ctx.x, ctx.y, loc.x, loc.y));
                } else {
                    clipboard.pixels[static_cast<size_t>(y * clipboard.w + x)] = 0;
                }
            } else {
                const Cell cell = src_to_cell(src);
                if (in_doc(cell.x, cell.y)) {
                    clipboard.pixels[static_cast<size_t>(y * clipboard.w + x)] = static_cast<uint8_t>(get_px(cell.x, cell.y, loc.x, loc.y));
                } else {
                    clipboard.pixels[static_cast<size_t>(y * clipboard.w + x)] = 0;
                }
            }
        }
    }
    status = "Copied " + std::to_string(clipboard.w) + "x" + std::to_string(clipboard.h) + " selection.";
}

bool TilesetEditor::in_doc(int col, int row) const {
    if (art_step()) return TilesetDoc::in_sheet(col, row);
    return atlas.in_sheet(col, row);
}
int TilesetEditor::get_px(int col, int row, int x, int y) const {
    if (art_step()) return doc.get_pixel(col, row, x, y);
    return atlas.get_pixel(col, row, x, y);
}
void TilesetEditor::set_px(int col, int row, int x, int y, int idx) {
    if (art_step()) doc.set_pixel(col, row, x, y, idx);
    else atlas.set_pixel(col, row, x, y, idx);
}
Cell TilesetEditor::src_to_cell(Cell src) const {
    const int ts = tile_size();
    if (step == Step::Specialty) {
        const int gx = src.x / ts;
        const int gy = src.y / ts;
        return doc.specialty_context_cell(specialty, gx, gy);
    }
    if (step == Step::Variants) {
        const int gx = src.x / ts;
        const int gy = src.y / ts;
        return atlas.context_cell(atlas_cell, gx, gy);
    }
    return {edit_ox + src.x / ts, edit_oy + src.y / ts};
}
Cell TilesetEditor::src_local(Cell src) const {
    const int ts = tile_size();
    return {src.x % ts, src.y % ts};
}
bool TilesetEditor::can_stamp(Cell src) const {
    if (step == Step::Specialty && specialty == TilesetDoc::kInnerCorner) {
        const int ts = tile_size();
        const int gx = src.x / ts;
        const int gy = src.y / ts;
        return gx == 1 && gy == 1;
    }
    if (step == Step::Variants) {
        const int ts = tile_size();
        const int gx = src.x / ts;
        const int gy = src.y / ts;
        return gx == 1 && gy == 1;
    }
    const Cell cell = src_to_cell(src);
    return in_doc(cell.x, cell.y);
}
bool TilesetEditor::stamp_src(Cell src, int idx) {
    bool drawn = false;
    for (int dy = 0; dy < brush; ++dy) {
        for (int dx = 0; dx < brush; ++dx) {
            const Cell p{src.x + dx, src.y + dy};
            if (has_selection() && !in_selection(p)) continue;
            if (can_stamp(p)) {
                plot_src(p, idx);
                drawn = true;
            }
        }
    }
    return drawn;
}
int TilesetEditor::sample_hover_pixel(Cell src) const {
    if (src.x < 0 || src.y < 0) return 0;
    const Cell loc = src_local(src);
    if (step == Step::Variants) {
        const int ts = tile_size();
        const int gx = src.x / ts;
        const int gy = src.y / ts;
        const Cell ctx = (gx == 1 && gy == 1) ? atlas_cell : atlas.context_cell(atlas_cell, gx, gy);
        if (ctx.x >= 0 && ctx.y >= 0) return atlas.get_pixel(ctx.x, ctx.y, loc.x, loc.y);
        return 0;
    }
    if (step == Step::Specialty && specialty == TilesetDoc::kInnerCorner) {
        const int ts = tile_size();
        const int gx = src.x / ts;
        const int gy = src.y / ts;
        if (gx == 1 && gy == 1) {
            return doc.get_pixel(TilesetDoc::kInnerCorner.x, TilesetDoc::kInnerCorner.y, loc.x, loc.y);
        } else if ((gx == 1 && gy == 0) || (gx == 1 && gy == 2)) {
            return atlas.get_pixel(0, 1, loc.x, loc.y);
        } else if ((gx == 0 && gy == 1) || (gx == 2 && gy == 1)) {
            return atlas.get_pixel(2, 3, loc.x, loc.y);
        } else {
            return 0;
        }
    }
    const Cell cell = src_to_cell(src);
    if (!in_doc(cell.x, cell.y)) return 0;
    return get_px(cell.x, cell.y, loc.x, loc.y);
}
bool TilesetEditor::plot_src(Cell src, int idx) {
    if (src.x < 0 || src.y < 0 || src.x >= src_w() || src.y >= src_h()) {
        return false;
    }
    const Cell cell = src_to_cell(src);
    const Cell loc = src_local(src);
    if (!in_doc(cell.x, cell.y)) {
        return false;
    }
    set_px(cell.x, cell.y, loc.x, loc.y, idx);
    return true;
}
void TilesetEditor::do_undo() {
    if (art_step()) doc.undo();
    else atlas.undo();
    if (!undo_selections.empty()) {
        sel_a = undo_selections.back().a;
        sel_b = undo_selections.back().b;
        undo_selections.pop_back();
    } else {
        clear_selection();
    }
    if (paint_index > last_index()) {
        select_single_palette(std::max(0, last_index()));
    }
    bump_art();
}
void TilesetEditor::push_undo() {
    undo_selections.push_back({sel_a, sel_b});
    if (art_step()) doc.push_undo();
    else atlas.push_undo();
}

void TilesetEditor::handle_shortcuts(const ImGuiIO& io) {
    const bool cmd = io.KeyCtrl || io.KeySuper;
    if (floating) {
        if (ImGui::IsKeyPressed(ImGuiKey_Escape)) cancel_floating();
        if (ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter)) commit_floating();
    }
    if (cmd && ImGui::IsKeyPressed(ImGuiKey_Z)) {
        if (floating) cancel_floating();
        else do_undo();
    }
    if (cmd && ImGui::IsKeyPressed(ImGuiKey_C) && (tool == Tool::Select || sel_a.x >= 0 || floating)) {
        copy_selection();
    }
    if (cmd && ImGui::IsKeyPressed(ImGuiKey_V)) {
        start_paste();
    }
    if (cmd && ImGui::IsKeyPressed(ImGuiKey_D)) {
        if (floating) commit_floating();
        clear_selection();
    }
    if (!io.WantTextInput) {
        if (ImGui::IsKeyPressed(ImGuiKey_LeftBracket)) shift_selected_palette(*this, -1);
        if (ImGui::IsKeyPressed(ImGuiKey_RightBracket)) shift_selected_palette(*this, 1);
        Tool new_tool = tool;
        if (ImGui::IsKeyPressed(ImGuiKey_1)) new_tool = Tool::Pencil;
        if (ImGui::IsKeyPressed(ImGuiKey_2)) new_tool = Tool::Eraser;
        if (ImGui::IsKeyPressed(ImGuiKey_3)) new_tool = Tool::Fill;
        if (ImGui::IsKeyPressed(ImGuiKey_4)) new_tool = Tool::Line;
        if (ImGui::IsKeyPressed(ImGuiKey_5)) new_tool = Tool::Square;
        if (ImGui::IsKeyPressed(ImGuiKey_6)) new_tool = Tool::Circle;
        if (ImGui::IsKeyPressed(ImGuiKey_7)) new_tool = Tool::Eyedropper;
        if (ImGui::IsKeyPressed(ImGuiKey_8)) new_tool = Tool::Select;
        if (new_tool != tool) {
            if (floating && new_tool != Tool::Select) commit_floating();
            tool = new_tool;
        }
    }
}

void TilesetEditor::draw_step_header(bool show_return_button) {
    ImGui::BeginGroup();
    ImGui::BeginDisabled(step == Step::Center);
    if (ImGui::Button("Back", ImVec2(88, 0))) {
        if (floating) commit_floating();
        go_back();
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    const char* step_label = "Step 1 of 4  —  Center tile";
    if (step == Step::Edges) step_label = "Step 2 of 4  —  Edges (3x3)";
    if (step == Step::Specialty) step_label = "Step 3 of 4  —  Caps & extras";
    if (step == Step::Variants) step_label = "Step 4 of 4  —  Atlas & header";
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(step_label);
    ImGui::SameLine();
    ImGui::BeginDisabled(step == Step::Variants);
    if (ImGui::Button("Next", ImVec2(88, 0))) {
        if (floating) commit_floating();
        go_next();
    }
    ImGui::EndDisabled();
    ImGui::SameLine(0, 24);
    ImGui::SetNextItemWidth(180);
    if (ImGui::InputText("Project", project_name, sizeof(project_name))) {
        touch();
    }
    ImGui::SameLine(0, 24);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Tile size");
    ImGui::SameLine();
    if (ImGui::RadioButton("8x8", doc.tile_size == 8)) {
        undo_selections.clear();
        doc.reset(8);
        atlas.reset(8);
        has_atlas = false;
        seeded = false;
        stamped = false;
        step = Step::Center;
        configure_view();
        bump_art();
    }
    ImGui::SameLine();
    if (ImGui::RadioButton("16x16", doc.tile_size == 16)) {
        undo_selections.clear();
        doc.reset(16);
        atlas.reset(16);
        has_atlas = false;
        seeded = false;
        stamped = false;
        step = Step::Center;
        configure_view();
        bump_art();
    }

    if (show_return_button) {
        ImGui::SameLine(0, 30);
        if (ImGui::Button("Apply & Return to Map", ImVec2(180, 0))) {
            request_sync_to_map = true;
            request_return_to_map = true;
        }
    }
    ImGui::EndGroup();
}

void TilesetEditor::draw_tools_and_options() {
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Tools");
    ImGui::SameLine(0, 16);
    for (int i = 0; i < 8; ++i) {
        const Tool t = static_cast<Tool>(i);
        if (i) ImGui::SameLine();
        if (tool_button(t, tool, settings.dark)) {
            if (floating && t != Tool::Select) commit_floating();
            tool = t;
        }
    }

    row_rule();

    if (ImGui::Button("Undo")) {
        if (floating) cancel_floating();
        else do_undo();
    }
    if (has_selection() && tool != Tool::Select) {
        ImGui::SameLine(0, 16);
        if (ImGui::Button("Deselect (Ctrl+D)")) clear_selection();
    }
    if (uses_brush(tool)) {
        ImGui::SameLine(0, 16);
        ImGui::SetNextItemWidth(120);
        ImGui::SliderInt("Brush", &brush, 1, 4);
    }
    if (step == Step::Center) {
        ImGui::SameLine(0, 16);
        if (ImGui::Checkbox("Tile mode", &tile_mode)) touch();
    }
    if (step == Step::Edges || step == Step::Specialty) {
        ImGui::SameLine(0, 16);
        if (ImGui::Checkbox("H-flip", &doc.hflip_linked)) touch();
        ImGui::SameLine();
        if (ImGui::Checkbox("V-flip", &doc.vflip_linked)) touch();
    }
    if (tool == Tool::Select) {
        if (floating) {
            ImGui::SameLine(0, 16);
            if (ImGui::Button("Apply (Enter)")) commit_floating();
            ImGui::SameLine();
            if (ImGui::Button("Cancel (Esc)")) cancel_floating();
        } else {
            ImGui::SameLine(0, 16);
            if (ImGui::Button("Copy")) copy_selection();
            ImGui::SameLine();
            if (ImGui::Button("Paste")) start_paste();
            if (has_selection()) {
                ImGui::SameLine();
                if (ImGui::Button("Deselect (Ctrl+D)")) clear_selection();
            }
        }
    }
    if (step == Step::Specialty) {
        ImGui::SameLine(0, 16);
        if (ImGui::Button("Stamp this")) {
            push_undo();
            doc.stamp_specialty(specialty);
            bump_art();
        }
        ImGui::SameLine();
        if (ImGui::Button("Stamp all extras")) {
            push_undo();
            doc.stamp_all_specialty();
            bump_art();
        }
        ImGui::SameLine();
        if (ImGui::Button("Reset extras")) {
            push_undo();
            doc.reset_specialty_to_center();
            bump_art();
        }
    }
    if (step == Step::Variants && has_atlas) {
        ImGui::SameLine(0, 16);
        if (ImGui::Button("Add variant")) {
            push_undo();
            const Cell slot = atlas.add_variant(atlas_cell, variant_chance);
            if (slot.x >= 0) {
                atlas_cell = slot;
                configure_view();
            }
            bump_art();
        }
        ImGui::SameLine();
        ImGui::BeginDisabled(!atlas.is_extra(atlas_cell.x, atlas_cell.y));
        if (ImGui::Button("Remove variant")) {
            push_undo();
            atlas.remove_variant(atlas_cell);
            atlas_cell = {9, 2};
            configure_view();
            bump_art();
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::SetNextItemWidth(150);
        const char* chances[] = {"Rare 0.08", "Uncommon 0.15", "0.30", "0.50", "Equal 1.0"};
        const float chance_vals[] = {0.08f, 0.15f, 0.30f, 0.50f, 1.0f};
        int ci = 2;
        for (int i = 0; i < 5; ++i) {
            if (std::fabs(variant_chance - chance_vals[i]) < 0.001f) ci = i;
        }
        if (ImGui::Combo("Chance", &ci, chances, 5)) {
            variant_chance = chance_vals[ci];
            if (atlas.is_extra(atlas_cell.x, atlas_cell.y)) {
                atlas.set_binding_probability(atlas_cell, variant_chance);
            }
            touch();
        }
        if (atlas.is_extra(atlas_cell.x, atlas_cell.y) && !atlas.is_bound_extra(atlas_cell.x, atlas_cell.y)) {
            ImGui::SameLine();
            if (ImGui::Button("Bind to center")) {
                push_undo();
                atlas.bind_variant(atlas_cell, {9, 2}, variant_chance);
                touch();
            }
        }
        if (atlas.cols > tsm::AtlasDoc::kBaseCols) {
            ImGui::SameLine();
            if (ImGui::Button("Auto-bind all")) {
                push_undo();
                atlas.auto_bind_extras({9, 2}, variant_chance);
                touch();
            }
        }
    }
}

void TilesetEditor::draw_content(SDL_Renderer* renderer, SDL_Window* window, float avail_height) {
    (void)renderer;
    (void)window;
    const float y_start = ImGui::GetCursorPosY();
    draw_step_header(embedded);
    row_rule();
    draw_tools_and_options();
    row_rule();
    const float header_h = ImGui::GetCursorPosY() - y_start;
    const float spacing_y = ImGui::GetStyle().ItemSpacing.y;
    const float split_avail = (avail_height > 0.0f) ? std::max(60.0f, avail_height - header_h - spacing_y) : -1.0f;
    draw_split_layout(*this, split_avail);
}

void TilesetEditor::draw_modals(bool& running) {
    if (!ui.project_open && !ui.show_new && !ui.show_unsaved && !embedded) {
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
                ui.show_new = true;
                ui.new_focus_name = true;
            }
            if (ImGui::Button("Load project", ImVec2(240, 0))) {
                ImGui::CloseCurrentPopup();
                try_open_project_dialog();
            }
            if (ImGui::Button("Import 12x4 Tileset", ImVec2(240, 0))) {
                ImGui::CloseCurrentPopup();
                try_import_12x4_dialog();
            }
            if (ImGui::Button("Import 5x3 Tileset", ImVec2(240, 0))) {
                ImGui::CloseCurrentPopup();
                try_import_5x3_dialog();
            }
            ImGui::EndPopup();
        }
    }

    if (ui.show_new) {
        ImGui::OpenPopup("New project");
    }
    const ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("New project", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove)) {
        ImGui::TextUnformatted("Name the tileset and choose a tile size.");
        ImGui::SetNextItemWidth(280);
        if (ui.new_focus_name || ImGui::IsWindowAppearing()) {
            ImGui::SetKeyboardFocusHere();
            ui.new_focus_name = false;
        }
        const bool enter_pressed = ImGui::InputText("Name", ui.new_name, sizeof(ui.new_name),
                                                   ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::TextUnformatted("Tile size");
        ImGui::RadioButton("8x8", &ui.new_tile_size, 8);
        ImGui::SameLine();
        ImGui::RadioButton("16x16", &ui.new_tile_size, 16);
        ImGui::Spacing();
        if (ImGui::Button("Create", ImVec2(120, 0)) || enter_pressed) {
            reset_new(ui.new_name, ui.new_tile_size);
            ui.show_new = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 0))) {
            ui.show_new = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    if (ui.show_unsaved) {
        ImGui::OpenPopup("Unsaved changes");
    }
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Unsaved changes", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove)) {
        ImGui::TextUnformatted("Save changes before continuing?");
        ImGui::Spacing();
        if (ImGui::Button("Save", ImVec2(110, 0))) {
            const std::string err = save_project(false);
            if (err.empty()) {
                if (ui.pending == PendingAction::Quit || ui.pending == PendingAction::None) {
                    running = false;
                }
                ui.pending = PendingAction::None;
                ui.show_unsaved = false;
                ImGui::CloseCurrentPopup();
            } else if (err != "#cancel") {
                status = err;
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Don't save", ImVec2(110, 0))) {
            if (ui.pending == PendingAction::Quit || ui.pending == PendingAction::None) {
                running = false;
            }
            ui.pending = PendingAction::None;
            ui.show_unsaved = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(110, 0))) {
            ui.pending = PendingAction::None;
            ui.show_unsaved = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    if (ui.show_export) {
        ImGui::OpenPopup("Export tileset");
    }
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowSize(ImVec2(vp->WorkSize.x * 0.82f, vp->WorkSize.y * 0.82f), ImGuiCond_Appearing);
    ImGui::SetNextWindowPos(vp->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Export tileset", &ui.show_export, ImGuiWindowFlags_NoSavedSettings)) {
        ImGui::Text("Atlas preview — %s", project_name);
        ImGui::SetNextItemWidth(220);
        ImGui::SliderInt("Zoom", &ui.export_zoom, 1, 24);
        ImGui::SameLine();
        if (ImGui::Button("-")) ui.export_zoom = std::max(1, ui.export_zoom - 1);
        ImGui::SameLine();
        if (ImGui::Button("+")) ui.export_zoom = std::min(24, ui.export_zoom + 1);

        const float footer = ImGui::GetFrameHeightWithSpacing() * 3.5f;
        ImGui::BeginChild("export_view", ImVec2(0, -footer), ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar);
        if (ImGui::IsWindowHovered() && ImGui::GetIO().MouseWheel != 0.0f) {
            ui.export_zoom = std::clamp(ui.export_zoom + (ImGui::GetIO().MouseWheel > 0.0f ? 1 : -1), 1, 24);
        }
        const int ts = atlas.tile_size;
        const int cols = atlas.cols;
        const int rows = AtlasDoc::kRows;
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        const ImVec2 size(static_cast<float>(cols * ts * ui.export_zoom), static_cast<float>(rows * ts * ui.export_zoom));
        ImGui::InvisibleButton("export_canvas", size);
        draw_pixels(ImGui::GetWindowDrawList(), origin, static_cast<float>(ui.export_zoom), cols * ts, rows * ts, *this, 0, 0, cols, rows, true,
                    canvas_tile_grid_color(settings));
        ImGui::EndChild();

        if (ImGui::Checkbox("C header (.h)", &export_header)) touch();
        ImGui::SameLine();
        if (ImGui::Checkbox("Terrain (.terrain)", &export_terrain)) touch();
        ImGui::SameLine();
        if (ImGui::Checkbox("5x3 PNG", &export_5x3)) touch();
        ImGui::TextDisabled("The atlas PNG is always exported.");
        if (ImGui::Button("Export…")) {
            const std::string err = export_all();
            if (err.empty()) {
                status = "Exported.";
                ui.show_export = false;
                ImGui::CloseCurrentPopup();
            } else if (err != "#cancel") {
                status = err;
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Close")) {
            ui.show_export = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

} // namespace tsm
