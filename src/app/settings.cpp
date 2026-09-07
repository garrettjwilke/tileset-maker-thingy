#include "settings.h"

#include "core/types.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace tsm {
namespace {

namespace fs = std::filesystem;

std::string home_dir() {
    const char* home = std::getenv("HOME");
    if (home && home[0]) {
        return home;
    }
#ifdef _WIN32
    const char* userprofile = std::getenv("USERPROFILE");
    if (userprofile && userprofile[0]) {
        return userprofile;
    }
#endif
    return {};
}

std::string trim(std::string s) {
    size_t a = 0;
    while (a < s.size() && std::isspace(static_cast<unsigned char>(s[a]))) {
        ++a;
    }
    size_t b = s.size();
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) {
        --b;
    }
    return s.substr(a, b - a);
}

void assign_Theme(bool& dst, const std::string& v) {
    dst = (v != "light" && v != "false" && v != "0" && v != "no");
}

void assign_Bool(bool& dst, const std::string& v) {
    dst = (v == "true" || v == "1" || v == "yes" || v == "on");
}

void assign_Int(int& dst, const std::string& v) {
    dst = std::atoi(v.c_str());
}

void assign_Float(float& dst, const std::string& v) {
    dst = std::strtof(v.c_str(), nullptr);
}

std::string format_Theme(bool v) {
    return v ? "dark" : "light";
}

std::string format_Bool(bool v) {
    return v ? "true" : "false";
}

std::string format_Int(int v) {
    return std::to_string(v);
}

std::string format_Float(float v) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.4g", static_cast<double>(v));
    return buf;
}

bool is_known_key(const std::string& key) {
#define X(type, name, def, kind, k) \
    if (key == k) {                 \
        return true;                \
    }
    TSM_SETTINGS_FIELDS(X)
#undef X
    return false;
}

void apply_key(Settings& s, const std::string& key, const std::string& value) {
#define X(type, name, def, kind, k) \
    if (key == k) {                 \
        assign_##kind(s.name, value); \
        return;                     \
    }
    TSM_SETTINGS_FIELDS(X)
#undef X
    s.extra.emplace_back(key, value);
}

void sanitize(Settings& s) {
    s.scale = clampf(s.scale, 0.75f, 2.0f);
    s.window_w = clampi(s.window_w, 640, 16384);
    s.window_h = clampi(s.window_h, 480, 16384);
}

} // namespace

std::string config_dir() {
#if defined(_WIN32)
    if (const char* appdata = std::getenv("APPDATA"); appdata && appdata[0]) {
        return (fs::path(appdata) / "tileset-maker-thingy").string();
    }
    const std::string home = home_dir();
    if (!home.empty()) {
        return (fs::path(home) / "AppData" / "Roaming" / "tileset-maker-thingy").string();
    }
#elif defined(__APPLE__)
    const std::string home = home_dir();
    if (!home.empty()) {
        return (fs::path(home) / "Library" / "Application Support" / "tileset-maker-thingy").string();
    }
#else
    if (const char* xdg = std::getenv("XDG_CONFIG_HOME"); xdg && xdg[0]) {
        return (fs::path(xdg) / "tileset-maker-thingy").string();
    }
    const std::string home = home_dir();
    if (!home.empty()) {
        return (fs::path(home) / ".config" / "tileset-maker-thingy").string();
    }
#endif
    return "tileset-maker-thingy";
}

std::string settings_path() {
    return (fs::path(config_dir()) / "settings.cfg").string();
}

std::string imgui_ini_path() {
    return (fs::path(config_dir()) / "imgui.ini").string();
}

bool ensure_config_dir() {
    std::error_code ec;
    fs::create_directories(config_dir(), ec);
    return !ec;
}

std::string format_settings(const Settings& s) {
    std::ostringstream out;
    out << "# tileset-maker-thingy settings\n";
#define X(type, name, def, kind, k) out << k << "=" << format_##kind(s.name) << "\n";
    TSM_SETTINGS_FIELDS(X)
#undef X
    for (const auto& kv : s.extra) {
        if (!is_known_key(kv.first)) {
            out << kv.first << "=" << kv.second << "\n";
        }
    }
    return out.str();
}

bool parse_settings_text(Settings& s, const std::string& text) {
    std::istringstream in(text);
    std::string line;
    if (text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xef &&
        static_cast<unsigned char>(text[1]) == 0xbb && static_cast<unsigned char>(text[2]) == 0xbf) {
        in.ignore(3);
    }
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        line = trim(line);
        if (line.empty() || line[0] == '#' || line[0] == ';') {
            continue;
        }
        const size_t eq = line.find('=');
        if (eq == std::string::npos) {
            continue;
        }
        const std::string key = trim(line.substr(0, eq));
        const std::string value = trim(line.substr(eq + 1));
        if (key.empty()) {
            continue;
        }
        apply_key(s, key, value);
    }
    sanitize(s);
    return true;
}

bool load_settings_file(Settings& s, const std::string& path) {
    std::ifstream in(path.c_str());
    if (!in) {
        return false;
    }
    std::ostringstream buf;
    buf << in.rdbuf();
    Settings loaded;
    parse_settings_text(loaded, buf.str());
    s = std::move(loaded);
    return true;
}

bool save_settings_file(const Settings& s, const std::string& path) {
    std::error_code ec;
    const fs::path parent = fs::path(path).parent_path();
    if (!parent.empty()) {
        fs::create_directories(parent, ec);
        if (ec) {
            return false;
        }
    }
    std::ofstream out(path.c_str(), std::ios::binary);
    if (!out) {
        return false;
    }
    out << format_settings(s);
    return static_cast<bool>(out);
}

} // namespace tsm
