#include "project.h"

#include "io.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <utility>
#include <vector>

namespace tsm {
namespace {

struct Json {
    enum Type { Null, Bool, Number, String, Array, Object };
    Type type = Null;
    bool b = false;
    double n = 0;
    std::string s;
    std::vector<Json> a;
    std::vector<std::pair<std::string, Json>> o;

    const Json* get(const char* key) const {
        if (type != Object) {
            return nullptr;
        }
        for (const auto& kv : o) {
            if (kv.first == key) {
                return &kv.second;
            }
        }
        return nullptr;
    }

    std::string as_string(const char* def = "") const {
        if (type == String) {
            return s;
        }
        return def;
    }

    int as_int(int def = 0) const {
        if (type == Number) {
            return static_cast<int>(n);
        }
        if (type == Bool) {
            return b ? 1 : 0;
        }
        return def;
    }

    bool as_bool(bool def = false) const {
        if (type == Bool) {
            return b;
        }
        if (type == Number) {
            return n != 0;
        }
        return def;
    }

    double as_number(double def = 0) const {
        if (type == Number) {
            return n;
        }
        return def;
    }
};

struct Parser {
    const std::string& t;
    size_t i = 0;
    std::string err;

    explicit Parser(const std::string& text) : t(text) {}

    void skip() {
        while (i < t.size() && std::isspace(static_cast<unsigned char>(t[i]))) {
            ++i;
        }
    }

    bool fail(const char* msg) {
        if (err.empty()) {
            err = msg;
        }
        return false;
    }

    bool expect(char ch) {
        skip();
        if (i >= t.size() || t[i] != ch) {
            return fail("Unexpected token");
        }
        ++i;
        return true;
    }

    bool parse_string(std::string& out) {
        skip();
        if (i >= t.size() || t[i] != '"') {
            return fail("Expected string");
        }
        ++i;
        out.clear();
        while (i < t.size()) {
            const char ch = t[i++];
            if (ch == '"') {
                return true;
            }
            if (ch == '\\') {
                if (i >= t.size()) {
                    return fail("Unterminated string");
                }
                const char esc = t[i++];
                if (esc == '"' || esc == '\\' || esc == '/') {
                    out.push_back(esc);
                } else if (esc == 'n') {
                    out.push_back('\n');
                } else if (esc == 't') {
                    out.push_back('\t');
                } else {
                    out.push_back(esc);
                }
            } else {
                out.push_back(ch);
            }
        }
        return fail("Unterminated string");
    }

    bool parse_number(double& out) {
        skip();
        const size_t start = i;
        if (i < t.size() && (t[i] == '-' || t[i] == '+')) {
            ++i;
        }
        while (i < t.size() && std::isdigit(static_cast<unsigned char>(t[i]))) {
            ++i;
        }
        if (i < t.size() && t[i] == '.') {
            ++i;
            while (i < t.size() && std::isdigit(static_cast<unsigned char>(t[i]))) {
                ++i;
            }
        }
        if (start == i) {
            return fail("Expected number");
        }
        out = std::strtod(t.c_str() + start, nullptr);
        return true;
    }

    bool parse_value(Json& out) {
        skip();
        if (i >= t.size()) {
            return fail("Unexpected end of JSON");
        }
        const char ch = t[i];
        if (ch == '"') {
            out.type = Json::String;
            return parse_string(out.s);
        }
        if (ch == '{') {
            return parse_object(out);
        }
        if (ch == '[') {
            return parse_array(out);
        }
        if (ch == 't' || ch == 'f') {
            if (t.compare(i, 4, "true") == 0) {
                i += 4;
                out.type = Json::Bool;
                out.b = true;
                return true;
            }
            if (t.compare(i, 5, "false") == 0) {
                i += 5;
                out.type = Json::Bool;
                out.b = false;
                return true;
            }
            return fail("Invalid boolean");
        }
        if (ch == 'n' && t.compare(i, 4, "null") == 0) {
            i += 4;
            out.type = Json::Null;
            return true;
        }
        out.type = Json::Number;
        return parse_number(out.n);
    }

    bool parse_object(Json& out) {
        if (!expect('{')) {
            return false;
        }
        out.type = Json::Object;
        skip();
        if (i < t.size() && t[i] == '}') {
            ++i;
            return true;
        }
        while (true) {
            std::string key;
            if (!parse_string(key) || !expect(':')) {
                return false;
            }
            Json value;
            if (!parse_value(value)) {
                return false;
            }
            out.o.emplace_back(std::move(key), std::move(value));
            skip();
            if (i < t.size() && t[i] == ',') {
                ++i;
                continue;
            }
            return expect('}');
        }
    }

    bool parse_array(Json& out) {
        if (!expect('[')) {
            return false;
        }
        out.type = Json::Array;
        skip();
        if (i < t.size() && t[i] == ']') {
            ++i;
            return true;
        }
        while (true) {
            Json value;
            if (!parse_value(value)) {
                return false;
            }
            out.a.push_back(std::move(value));
            skip();
            if (i < t.size() && t[i] == ',') {
                ++i;
                continue;
            }
            return expect(']');
        }
    }
};

std::string json_escape(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char ch : s) {
        if (ch == '"' || ch == '\\') {
            out.push_back('\\');
        }
        out.push_back(ch);
    }
    return out;
}

std::string hex_color(Rgb c) {
    char buf[8];
    std::snprintf(buf, sizeof(buf), "#%02X%02X%02X", c.r, c.g, c.b);
    return buf;
}

bool parse_hex_color(std::string hex, Rgb& out) {
    if (!hex.empty() && hex[0] != '#') {
        hex = "#" + hex;
    }
    if (hex.size() != 7) {
        return false;
    }
    auto nyb = [](char ch) -> int {
        if (ch >= '0' && ch <= '9') return ch - '0';
        if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
        if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
        return -1;
    };
    const int r1 = nyb(hex[1]), r2 = nyb(hex[2]);
    const int g1 = nyb(hex[3]), g2 = nyb(hex[4]);
    const int b1 = nyb(hex[5]), b2 = nyb(hex[6]);
    if (r1 < 0 || r2 < 0 || g1 < 0 || g2 < 0 || b1 < 0 || b2 < 0) {
        return false;
    }
    out = Rgb{
        static_cast<uint8_t>(r1 * 16 + r2),
        static_cast<uint8_t>(g1 * 16 + g2),
        static_cast<uint8_t>(b1 * 16 + b2),
    };
    return true;
}

int hex_nibble(char ch) {
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
    if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
    return -1;
}

std::string to_hex(const std::vector<uint8_t>& bytes) {
    static const char* kDigits = "0123456789abcdef";
    std::string out(bytes.size() * 2, '0');
    for (size_t i = 0; i < bytes.size(); ++i) {
        out[i * 2] = kDigits[bytes[i] >> 4];
        out[i * 2 + 1] = kDigits[bytes[i] & 0x0f];
    }
    return out;
}

bool from_hex(const std::string& hex, std::vector<uint8_t>& out) {
    if (hex.size() % 2 != 0) {
        return false;
    }
    out.resize(hex.size() / 2);
    for (size_t i = 0; i < out.size(); ++i) {
        const int hi = hex_nibble(hex[i * 2]);
        const int lo = hex_nibble(hex[i * 2 + 1]);
        if (hi < 0 || lo < 0) {
            return false;
        }
        out[i] = static_cast<uint8_t>((hi << 4) | lo);
    }
    return true;
}

const char* step_name(ProjectStep step) {
    switch (step) {
    case ProjectStep::Edges: return "edges";
    case ProjectStep::Specialty: return "specialty";
    case ProjectStep::Variants: return "variants";
    case ProjectStep::Center:
    default: return "center";
    }
}

ProjectStep step_from_name(const std::string& name) {
    if (name == "edges") return ProjectStep::Edges;
    if (name == "specialty") return ProjectStep::Specialty;
    if (name == "variants") return ProjectStep::Variants;
    return ProjectStep::Center;
}

void write_palette(std::ostringstream& out, const std::vector<Rgb>& colors, const char* indent) {
    out << indent << "[\n";
    for (size_t i = 0; i < colors.size(); ++i) {
        out << indent << "\t\"" << hex_color(colors[i]) << "\"";
        if (i + 1 < colors.size()) {
            out << ",";
        }
        out << "\n";
    }
    out << indent << "]";
}

bool read_palette(const Json* node, std::vector<Rgb>& colors) {
    if (!node || node->type != Json::Array) {
        return false;
    }
    colors.clear();
    for (const Json& item : node->a) {
        Rgb c{};
        if (item.type != Json::String || !parse_hex_color(item.s, c)) {
            return false;
        }
        colors.push_back(c);
        if (static_cast<int>(colors.size()) >= TilesetDoc::kPaletteSize) {
            break;
        }
    }
    if (static_cast<int>(colors.size()) < TilesetDoc::kPaletteMin) {
        return false;
    }
    return true;
}

Cell read_cell(const Json* node, Cell fallback) {
    if (!node || node->type != Json::Object) {
        return fallback;
    }
    const Json* x = node->get("x");
    const Json* y = node->get("y");
    return {x ? x->as_int(fallback.x) : fallback.x, y ? y->as_int(fallback.y) : fallback.y};
}

int json_int(const Json& root, const char* key, int def) {
    const Json* n = root.get(key);
    return n ? n->as_int(def) : def;
}

bool json_bool(const Json& root, const char* key, bool def) {
    const Json* n = root.get(key);
    return n ? n->as_bool(def) : def;
}

std::string json_string(const Json& root, const char* key, const char* def) {
    const Json* n = root.get(key);
    return n ? n->as_string(def) : std::string(def);
}

} // namespace

std::string with_tilesetproj_ext(const std::string& path) {
    const std::string ext = ".tilesetproj";
    if (path.size() >= ext.size() && path.compare(path.size() - ext.size(), ext.size(), ext) == 0) {
        return path;
    }
    return path + ext;
}

std::string project_to_text(const ProjectData& data) {
    std::ostringstream out;
    out << "{\n";
    out << "\t\"format\": \"tilesetproj\",\n";
    out << "\t\"version\": 1,\n";
    out << "\t\"name\": \"" << json_escape(data.name) << "\",\n";
    out << "\t\"tile_size\": " << data.tileset.tile_size << ",\n";
    out << "\t\"step\": \"" << step_name(data.step) << "\",\n";
    out << "\t\"seeded\": " << (data.seeded ? "true" : "false") << ",\n";
    out << "\t\"stamped\": " << (data.stamped ? "true" : "false") << ",\n";
    out << "\t\"hflip_linked\": " << (data.tileset.hflip_linked ? "true" : "false") << ",\n";
    out << "\t\"vflip_linked\": " << (data.tileset.vflip_linked ? "true" : "false") << ",\n";
    out << "\t\"painted\": " << (data.tileset.painted ? "true" : "false") << ",\n";
    out << "\t\"specialty\": {\"x\": " << data.specialty.x << ", \"y\": " << data.specialty.y << "},\n";
    out << "\t\"atlas_cell\": {\"x\": " << data.atlas_cell.x << ", \"y\": " << data.atlas_cell.y << "},\n";
    out << "\t\"preview_sel\": {\"x\": " << data.preview_sel.x << ", \"y\": " << data.preview_sel.y << "},\n";
    out << "\t\"tile_mode\": " << (data.tile_mode ? "true" : "false") << ",\n";
    out << "\t\"export_header\": " << (data.export_header ? "true" : "false") << ",\n";
    out << "\t\"export_terrain\": " << (data.export_terrain ? "true" : "false") << ",\n";
    out << "\t\"export_5x3\": " << (data.export_5x3 ? "true" : "false") << ",\n";
    out << "\t\"art_rev\": " << data.art_rev << ",\n";
    out << "\t\"atlas_rev\": " << data.atlas_rev << ",\n";
    out << "\t\"has_atlas\": " << (data.has_atlas ? "true" : "false") << ",\n";
    out << "\t\"last_seed\": \"" << to_hex(data.tileset.last_seed) << "\",\n";
    out << "\t\"palette\": ";
    write_palette(out, data.tileset.palette, "\t");
    out << ",\n";
    out << "\t\"tiles\": [\n";
    for (size_t i = 0; i < data.tileset.tiles.size(); ++i) {
        out << "\t\t\"" << to_hex(data.tileset.tiles[i]) << "\"";
        if (i + 1 < data.tileset.tiles.size()) {
            out << ",";
        }
        out << "\n";
    }
    out << "\t]";
    if (data.has_atlas) {
        out << ",\n";
        out << "\t\"atlas\": {\n";
        out << "\t\t\"tile_size\": " << data.atlas.tile_size << ",\n";
        out << "\t\t\"cols\": " << data.atlas.cols << ",\n";
        out << "\t\t\"painted\": " << (data.atlas.painted ? "true" : "false") << ",\n";
        out << "\t\t\"palette\": ";
        write_palette(out, data.atlas.palette, "\t\t");
        out << ",\n";
        out << "\t\t\"pixels\": \"" << to_hex(data.atlas.tiles) << "\",\n";
        out << "\t\t\"bindings\": [\n";
        for (size_t i = 0; i < data.atlas.bindings.size(); ++i) {
            const auto& b = data.atlas.bindings[i];
            out << "\t\t\t{\"x\": " << b.x << ", \"y\": " << b.y << ", \"root_x\": " << b.root_x
                << ", \"root_y\": " << b.root_y << ", \"probability\": " << b.probability << "}";
            if (i + 1 < data.atlas.bindings.size()) {
                out << ",";
            }
            out << "\n";
        }
        out << "\t\t]\n";
        out << "\t}\n";
    } else {
        out << "\n";
    }
    out << "}\n";
    return out.str();
}

std::string project_from_text(ProjectData& data, const std::string& text) {
    Parser parser(text);
    Json root;
    if (!parser.parse_value(root) || root.type != Json::Object) {
        return parser.err.empty() ? "Invalid project JSON" : parser.err;
    }
    if (json_string(root, "format", "") != "tilesetproj") {
        return "Not a tilesetproj file";
    }
    const int version = json_int(root, "version", 0);
    if (version < 1) {
        return "Unsupported tilesetproj version";
    }

    ProjectData next;
    next.name = json_string(root, "name", "untitled");
    if (next.name.empty()) {
        next.name = "untitled";
    }
    next.step = step_from_name(json_string(root, "step", "center"));
    next.seeded = json_bool(root, "seeded", false);
    next.stamped = json_bool(root, "stamped", false);
    next.specialty = read_cell(root.get("specialty"), TilesetDoc::kInnerCorner);
    next.atlas_cell = read_cell(root.get("atlas_cell"), Cell{9, 2});
    next.preview_sel = read_cell(root.get("preview_sel"), TilesetDoc::kCenter);
    next.tile_mode = json_bool(root, "tile_mode", true);
    next.export_header = json_bool(root, "export_header", true);
    next.export_terrain = json_bool(root, "export_terrain", true);
    next.export_5x3 = json_bool(root, "export_5x3", true);
    next.art_rev = json_int(root, "art_rev", 0);
    next.atlas_rev = json_int(root, "atlas_rev", -1);
    next.has_atlas = json_bool(root, "has_atlas", false);

    const int tile_size = json_int(root, "tile_size", 16);
    if (tile_size != 8 && tile_size != 16) {
        return "Tile size must be 8 or 16";
    }
    next.tileset.tile_size = tile_size;
    next.tileset.hflip_linked = json_bool(root, "hflip_linked", true);
    next.tileset.vflip_linked = json_bool(root, "vflip_linked", false);
    next.tileset.painted = json_bool(root, "painted", false);
    if (!read_palette(root.get("palette"), next.tileset.palette)) {
        return "Project palette is missing or invalid";
    }
    const Json* last_seed = root.get("last_seed");
    if (last_seed && last_seed->type == Json::String && !last_seed->s.empty()) {
        if (!from_hex(last_seed->s, next.tileset.last_seed)) {
            return "Invalid last_seed data";
        }
    }
    const Json* tiles = root.get("tiles");
    if (!tiles || tiles->type != Json::Array || tiles->a.size() != static_cast<size_t>(TilesetDoc::kCellCount)) {
        return "Project must contain 15 tiles";
    }
    const size_t tile_bytes = static_cast<size_t>(tile_size * tile_size);
    for (int i = 0; i < TilesetDoc::kCellCount; ++i) {
        if (tiles->a[static_cast<size_t>(i)].type != Json::String ||
            !from_hex(tiles->a[static_cast<size_t>(i)].s, next.tileset.tiles[static_cast<size_t>(i)]) ||
            next.tileset.tiles[static_cast<size_t>(i)].size() != tile_bytes) {
            return "Invalid tile pixel data";
        }
    }

    if (next.has_atlas) {
        const Json* atlas = root.get("atlas");
        if (!atlas || atlas->type != Json::Object) {
            return "Project is missing atlas data";
        }
        next.atlas.tile_size = json_int(*atlas, "tile_size", tile_size);
        if (next.atlas.tile_size != 8 && next.atlas.tile_size != 16) {
            return "Atlas tile size must be 8 or 16";
        }
        next.atlas.cols = std::max(json_int(*atlas, "cols", AtlasDoc::kBaseCols), AtlasDoc::kBaseCols);
        next.atlas.painted = json_bool(*atlas, "painted", false);
        if (!read_palette(atlas->get("palette"), next.atlas.palette)) {
            return "Atlas palette is missing or invalid";
        }
        const Json* pixels = atlas->get("pixels");
        const size_t expect_px =
            static_cast<size_t>(next.atlas.cols * AtlasDoc::kRows * next.atlas.tile_size * next.atlas.tile_size);
        if (!pixels || pixels->type != Json::String || !from_hex(pixels->s, next.atlas.tiles) ||
            next.atlas.tiles.size() != expect_px) {
            return "Invalid atlas pixel data";
        }
        const Json* bindings = atlas->get("bindings");
        if (bindings && bindings->type == Json::Array) {
            for (const Json& item : bindings->a) {
                if (item.type != Json::Object) {
                    continue;
                }
                VariantBinding b;
                b.x = json_int(item, "x", -1);
                b.y = json_int(item, "y", -1);
                b.root_x = json_int(item, "root_x", -1);
                b.root_y = json_int(item, "root_y", -1);
                const Json* p = item.get("probability");
                b.probability = p ? static_cast<float>(p->as_number(0.3)) : 0.3f;
                if (b.x >= 0 && b.y >= 0 && b.root_x >= 0 && b.root_y >= 0) {
                    next.atlas.bindings.push_back(b);
                }
            }
        }
    }

    data = std::move(next);
    return {};
}

std::string save_project(const ProjectData& data, const std::string& path) {
    return write_text_file(with_tilesetproj_ext(path), project_to_text(data));
}

std::string load_project(ProjectData& data, const std::string& path) {
    std::string text;
    const std::string err = read_text_file(path, text);
    if (!err.empty()) {
        return err;
    }
    return project_from_text(data, text);
}

} // namespace tsm
