#pragma once

#include "../core/types.h"

#include <string>
#include <utility>
#include <vector>

namespace tsm {

// Add a setting by appending one line. It is loaded, saved, and defaulted from here.
#define TSM_SETTINGS_FIELDS(X)                      \
    X(bool, dark, true, Theme, "theme")             \
    X(float, scale, 1.0f, Float, "scale")           \
    X(bool, pixel_grid, true, Bool, "pixel_grid")   \
    X(Rgb, tile_grid_color, (Rgb{128, 128, 128}), Color, "tile_grid_color") \
    X(Rgb, pixel_grid_color, (Rgb{104, 104, 104}), Color, "pixel_grid_color") \
    X(int, window_x, 0, Int, "window_x")            \
    X(int, window_y, 0, Int, "window_y")            \
    X(int, window_w, 1280, Int, "window_w")         \
    X(int, window_h, 800, Int, "window_h")          \
    X(bool, window_maximized, false, Bool, "window_maximized") \
    X(bool, window_placed, false, Bool, "window_placed")

struct Settings {
#define X(type, name, def, kind, key) type name = def;
    TSM_SETTINGS_FIELDS(X)
#undef X
    std::vector<std::pair<std::string, std::string>> extra;
};

std::string config_dir();
std::string settings_path();
std::string imgui_ini_path();
bool ensure_config_dir();

std::string format_settings(const Settings& s);
bool parse_settings_text(Settings& s, const std::string& text);
bool load_settings_file(Settings& s, const std::string& path);
bool save_settings_file(const Settings& s, const std::string& path);

} // namespace tsm
