#include "io.h"

#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>

namespace tsm {
namespace {

void fill_image_from_palette(Image& im, const std::vector<Rgb>& palette) {
    im.indexed = 1;
    im.bpp = 1;
    im.palettesize = static_cast<unsigned>(std::min(static_cast<int>(palette.size()), 256));
    std::memset(im.palette, 0, sizeof(im.palette));
    for (unsigned i = 0; i < im.palettesize; ++i) {
        im.palette[i][0] = palette[i].r;
        im.palette[i][1] = palette[i].g;
        im.palette[i][2] = palette[i].b;
        im.palette[i][3] = 255;
    }
    im.clear[0] = 0;
    im.clear[1] = im.clear[2] = im.clear[3] = 0;
}

std::vector<Rgb> palette_from_image(const Image& im) {
    std::vector<Rgb> colors;
    if (im.indexed && im.palettesize > 0) {
        const unsigned n = std::min(im.palettesize, 16u);
        colors.resize(n);
        for (unsigned i = 0; i < n; ++i) {
            colors[i] = Rgb{im.palette[i][0], im.palette[i][1], im.palette[i][2]};
        }
    } else {
        colors = TilesetDoc::default_palette();
    }
    if (static_cast<int>(colors.size()) < TilesetDoc::kPaletteMin) {
        const auto defaults = TilesetDoc::default_palette();
        while (static_cast<int>(colors.size()) < TilesetDoc::kPaletteMin) {
            colors.push_back(colors.empty() ? defaults[0] : colors.back());
        }
    }
    if (static_cast<int>(colors.size()) > TilesetDoc::kPaletteSize) {
        colors.resize(TilesetDoc::kPaletteSize);
    }
    return colors;
}

uint8_t sample_index(const Image& im, int x, int y, int last) {
    if (x < 0 || y < 0 || x >= static_cast<int>(im.w) || y >= static_cast<int>(im.h) || !im.px) {
        return 0;
    }
    if (im.indexed && im.bpp == 1) {
        return static_cast<uint8_t>(clampi(im.px[static_cast<size_t>(y) * im.w + static_cast<size_t>(x)], 0, last));
    }
    return 0;
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

std::string basename_of(const std::string& path) {
    const auto slash = path.find_last_of("/\\");
    const std::string file = (slash == std::string::npos) ? path : path.substr(slash + 1);
    const auto dot = file.find_last_of('.');
    return (dot == std::string::npos) ? file : file.substr(0, dot);
}

std::string filename_of(const std::string& path) {
    const auto slash = path.find_last_of("/\\");
    return (slash == std::string::npos) ? path : path.substr(slash + 1);
}

std::string dirname_of(const std::string& path) {
    const auto slash = path.find_last_of("/\\");
    return (slash == std::string::npos) ? std::string() : path.substr(0, slash);
}

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

} // namespace

std::string tileset_to_image(const TilesetDoc& doc, Image& im) {
    Image like{};
    fill_image_from_palette(like, doc.palette);
    like.bpp = 1;
    like.indexed = 1;
    if (!image_alloc(&im, static_cast<unsigned>(TilesetDoc::kCols * doc.tile_size),
                     static_cast<unsigned>(TilesetDoc::kRows * doc.tile_size), &like)) {
        return "Out of memory";
    }
    for (int row = 0; row < TilesetDoc::kRows; ++row) {
        for (int col = 0; col < TilesetDoc::kCols; ++col) {
            const auto& tile = doc.get_tile(col, row);
            for (int y = 0; y < doc.tile_size; ++y) {
                for (int x = 0; x < doc.tile_size; ++x) {
                    const int dest = (row * doc.tile_size + y) * static_cast<int>(im.w) + (col * doc.tile_size + x);
                    const int src = y * doc.tile_size + x;
                    im.px[static_cast<size_t>(dest)] =
                        (src < static_cast<int>(tile.size())) ? tile[static_cast<size_t>(src)] : 0;
                }
            }
        }
    }
    return {};
}

std::string atlas_to_image(const AtlasDoc& doc, Image& im) {
    Image like{};
    fill_image_from_palette(like, doc.palette);
    like.bpp = 1;
    like.indexed = 1;
    if (!image_alloc(&im, static_cast<unsigned>(doc.cols * doc.tile_size),
                     static_cast<unsigned>(AtlasDoc::kRows * doc.tile_size), &like)) {
        return "Out of memory";
    }
    for (int row = 0; row < AtlasDoc::kRows; ++row) {
        for (int col = 0; col < doc.cols; ++col) {
            const auto tile = doc.get_tile(col, row);
            for (int y = 0; y < doc.tile_size; ++y) {
                for (int x = 0; x < doc.tile_size; ++x) {
                    const int dest = (row * doc.tile_size + y) * static_cast<int>(im.w) + (col * doc.tile_size + x);
                    const int src = y * doc.tile_size + x;
                    im.px[static_cast<size_t>(dest)] =
                        (src < static_cast<int>(tile.size())) ? tile[static_cast<size_t>(src)] : 0;
                }
            }
        }
    }
    return {};
}

std::string tileset_from_image(TilesetDoc& doc, const Image& im) {
    if (!im.px || im.w == 0 || im.h == 0) {
        return "Invalid PNG size";
    }
    if (im.w % TilesetDoc::kCols != 0 || im.h % TilesetDoc::kRows != 0) {
        return "PNG must be a 5x3 tile sheet";
    }
    const int tw = static_cast<int>(im.w / TilesetDoc::kCols);
    const int th = static_cast<int>(im.h / TilesetDoc::kRows);
    if (tw != th) {
        return "Tiles must be square";
    }
    if (tw != 8 && tw != 16) {
        return "Tile size must be 8 or 16";
    }
    if (!im.indexed) {
        return "PNG must be indexed-color";
    }
    doc.reset(tw);
    doc.apply_palette(palette_from_image(im));
    const int last = doc.last_palette_index();
    for (int row = 0; row < TilesetDoc::kRows; ++row) {
        for (int col = 0; col < TilesetDoc::kCols; ++col) {
            std::vector<uint8_t> buf(static_cast<size_t>(doc.tile_size * doc.tile_size), 0);
            for (int y = 0; y < doc.tile_size; ++y) {
                for (int x = 0; x < doc.tile_size; ++x) {
                    buf[static_cast<size_t>(y * doc.tile_size + x)] =
                        sample_index(im, col * doc.tile_size + x, row * doc.tile_size + y, last);
                }
            }
            doc.set_tile(col, row, buf);
        }
    }
    doc.painted = true;
    doc.hflip_linked = doc.mirrors_match();
    doc.vflip_linked = false;
    return {};
}

std::string atlas_from_image(AtlasDoc& doc, const Image& im) {
    if (!im.px || im.w == 0 || im.h == 0) {
        return "Invalid PNG size";
    }
    if (im.h % AtlasDoc::kRows != 0) {
        return "PNG must be a 12x4 Godot tileset (or wider)";
    }
    const int tw = static_cast<int>(im.h / AtlasDoc::kRows);
    if (tw <= 0 || im.w % static_cast<unsigned>(tw) != 0) {
        return "Tiles must be square";
    }
    if (tw != 8 && tw != 16) {
        return "Tile size must be 8 or 16";
    }
    const int width_cols = static_cast<int>(im.w / static_cast<unsigned>(tw));
    if (width_cols < AtlasDoc::kBaseCols) {
        return "PNG must be at least 12 tiles wide";
    }
    if (!im.indexed) {
        return "PNG must be indexed-color";
    }
    doc.reset(tw, width_cols);
    doc.apply_palette(palette_from_image(im));
    const int last = doc.last_palette_index();
    for (int row = 0; row < AtlasDoc::kRows; ++row) {
        for (int col = 0; col < doc.cols; ++col) {
            std::vector<uint8_t> buf(static_cast<size_t>(doc.tile_size * doc.tile_size), 0);
            for (int y = 0; y < doc.tile_size; ++y) {
                for (int x = 0; x < doc.tile_size; ++x) {
                    buf[static_cast<size_t>(y * doc.tile_size + x)] =
                        sample_index(im, col * doc.tile_size + x, row * doc.tile_size + y, last);
                }
            }
            doc.set_tile(col, row, buf);
        }
    }
    doc.painted = true;
    return {};
}

std::string save_tileset_png(const TilesetDoc& doc, const std::string& path) {
    Image im{};
    const std::string err = tileset_to_image(doc, im);
    if (!err.empty()) {
        image_free(&im);
        return err;
    }
    const int ok = save_png(path.c_str(), &im);
    image_free(&im);
    return ok ? std::string() : ("Could not write " + path);
}

std::string load_tileset_png(TilesetDoc& doc, const std::string& path) {
    Image im{};
    if (!load_png(path.c_str(), &im)) {
        return "Could not read " + path;
    }
    const std::string err = tileset_from_image(doc, im);
    image_free(&im);
    return err;
}

std::string save_atlas_png(const AtlasDoc& doc, const std::string& path) {
    Image im{};
    const std::string err = atlas_to_image(doc, im);
    if (!err.empty()) {
        image_free(&im);
        return err;
    }
    const int ok = save_png(path.c_str(), &im);
    image_free(&im);
    return ok ? std::string() : ("Could not write " + path);
}

std::string load_atlas_png(AtlasDoc& doc, const std::string& path) {
    Image im{};
    if (!load_png(path.c_str(), &im)) {
        return "Could not read " + path;
    }
    const std::string err = atlas_from_image(doc, im);
    image_free(&im);
    return err;
}

std::string c_ident(const std::string& stem) {
    std::string out;
    for (size_t i = 0; i < stem.size(); ++i) {
        const unsigned char ch = static_cast<unsigned char>(stem[i]);
        if (std::isalpha(ch) || ch == '_' || (i > 0 && std::isdigit(ch))) {
            out.push_back(static_cast<char>(std::tolower(ch)));
        } else {
            out.push_back('_');
        }
    }
    while (out.find("__") != std::string::npos) {
        const auto pos = out.find("__");
        out.replace(pos, 2, "_");
    }
    while (!out.empty() && out.front() == '_') {
        out.erase(out.begin());
    }
    while (!out.empty() && out.back() == '_') {
        out.pop_back();
    }
    if (out.empty()) {
        return {};
    }
    if (std::isdigit(static_cast<unsigned char>(out[0]))) {
        out = "tileset_" + out;
    }
    return out;
}

HeaderSymbols symbols_from_path(const std::string& path) {
    const std::string ident = c_ident(basename_of(path));
    std::string macro = ident;
    for (char& ch : macro) {
        ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
    }
    return {ident, macro, macro + "_H"};
}

std::string render_header(const AtlasDoc& atlas, const std::string& png_path) {
    const auto symbols = symbols_from_path(png_path.empty() ? "untitled.png" : png_path);
    if (symbols.ident.empty()) {
        return {};
    }
    const int tex_w = atlas.cols * atlas.tile_size;
    std::ostringstream out;
    out << "#ifndef " << symbols.guard << "\n";
    out << "#define " << symbols.guard << "\n\n";
    out << "/* PNG width in " << atlas.tile_size << "x" << atlas.tile_size << " tiles ("
        << tex_w << "px → " << atlas.cols << ").";
    if (atlas.cols > AtlasDoc::kBaseCols) {
        out << " Extra columns start at x = " << AtlasDoc::kBaseCols << ".";
    }
    out << " */\n";
    out << "#define " << symbols.macro << "_ATLAS_COLS  " << atlas.cols << "\n\n";
    if (atlas.bindings.empty()) {
        out << "/* " << symbols.ident << " has no extra tiles — nothing to bind. */\n";
    } else {
        out << "/* { extra_x, extra_y, root_x, root_y, weight }\n";
        out << " * weight 1 = rare, 100 = as often as the original. */\n";
    }
    out << "static const AtmVariantDef " << symbols.ident << "_variants[] =\n{\n";
    if (atlas.bindings.empty()) {
        out << "    { 0, 0, 0, 0, 0 }\n";
    } else {
        for (size_t i = 0; i < atlas.bindings.size(); ++i) {
            const auto& b = atlas.bindings[i];
            const int weight = clampi(static_cast<int>(std::lround(b.probability * 100.0f)), 0, 100);
            out << "    { " << b.x << ", " << b.y << ", " << b.root_x << ", " << b.root_y << ", " << weight << " }";
            if (i + 1 < atlas.bindings.size()) {
                out << ",";
            }
            out << "\n";
        }
    }
    out << "};\n\n#endif\n";
    return out.str();
}

std::string save_header(const AtlasDoc& atlas, const std::string& dest_path, const std::string& png_path) {
    std::string dest = dest_path;
    if (dest.size() < 2 || dest.substr(dest.size() - 2) != ".h") {
        dest += ".h";
    }
    const std::string text = render_header(atlas, png_path.empty() ? dest : png_path);
    if (text.empty()) {
        return "Could not derive a C symbol from the file name";
    }
    return write_text_file(dest, text);
}

std::string palette_to_text(const std::vector<Rgb>& colors) {
    std::ostringstream out;
    out << "{\n\t\"format\": \"md-tileset-palette\",\n\t\"version\": 1,\n\t\"colors\": [\n";
    for (size_t i = 0; i < colors.size(); ++i) {
        out << "\t\t\"" << hex_color(colors[i]) << "\"";
        if (i + 1 < colors.size()) {
            out << ",";
        }
        out << "\n";
    }
    out << "\t]\n}\n";
    return out.str();
}

PaletteLoad palette_from_text(const std::string& text) {
    PaletteLoad result;
    if (text.find("md-tileset-palette") == std::string::npos) {
        result.error = "Not an md-tileset palette file";
        return result;
    }
    size_t pos = 0;
    while (true) {
        const auto hash = text.find('#', pos);
        if (hash == std::string::npos) {
            break;
        }
        if (hash + 7 <= text.size()) {
            Rgb color{};
            if (parse_hex_color(text.substr(hash, 7), color)) {
                result.colors.push_back(color);
                if (result.colors.size() >= static_cast<size_t>(TilesetDoc::kPaletteSize)) {
                    break;
                }
            }
        }
        pos = hash + 1;
    }
    if (static_cast<int>(result.colors.size()) < TilesetDoc::kPaletteMin) {
        const auto defaults = TilesetDoc::default_palette();
        while (static_cast<int>(result.colors.size()) < TilesetDoc::kPaletteMin) {
            result.colors.push_back(defaults[result.colors.size()]);
        }
    }
    return result;
}

std::string save_palette_file(const std::string& path, const std::vector<Rgb>& colors) {
    std::string dest = path;
    if (dest.size() < 8 || dest.substr(dest.size() - 8) != ".palette") {
        dest += ".palette";
    }
    return write_text_file(dest, palette_to_text(colors));
}

PaletteLoad load_palette_file(const std::string& path) {
    std::string text;
    const std::string err = read_text_file(path, text);
    if (!err.empty()) {
        return {err, {}};
    }
    return palette_from_text(text);
}

std::string render_terrain(const AtlasDoc& atlas, const std::string& tileset_name) {
    std::ostringstream out;
    out << "{\n\t\"version\": 1,\n\t\"tile_size\": " << atlas.tile_size << ",\n";
    out << "\t\"tileset\": \"" << json_escape(tileset_name) << "\",\n\t\"variants\": [\n";
    for (size_t i = 0; i < atlas.bindings.size(); ++i) {
        const auto& b = atlas.bindings[i];
        out << "\t\t{\n";
        out << "\t\t\t\"x\": " << b.x << ",\n";
        out << "\t\t\t\"y\": " << b.y << ",\n";
        out << "\t\t\t\"root_x\": " << b.root_x << ",\n";
        out << "\t\t\t\"root_y\": " << b.root_y << ",\n";
        out << "\t\t\t\"probability\": " << b.probability << "\n";
        out << "\t\t}";
        if (i + 1 < atlas.bindings.size()) {
            out << ",";
        }
        out << "\n";
    }
    out << "\t]\n}\n";
    return out.str();
}

std::string save_terrain(const AtlasDoc& atlas, const std::string& dest_path, const std::string& tileset_path) {
    std::string dest = dest_path;
    if (dest.size() < 8 || dest.substr(dest.size() - 8) != ".terrain") {
        dest += ".terrain";
    }
    std::string tileset_name = filename_of(tileset_path);
    if (tileset_name.empty()) {
        tileset_name = filename_of(dest);
        const auto dot = tileset_name.find_last_of('.');
        if (dot != std::string::npos) {
            tileset_name = tileset_name.substr(0, dot) + ".png";
        }
    } else if (dirname_of(tileset_path) == dirname_of(dest)) {
        tileset_name = filename_of(tileset_path);
    }
    return write_text_file(dest, render_terrain(atlas, tileset_name));
}

TerrainLoad load_terrain(const std::string& path) {
    TerrainLoad result;
    std::string text;
    result.error = read_text_file(path, text);
    if (!result.error.empty()) {
        return result;
    }
    if (text.find("\"version\"") == std::string::npos) {
        result.error = "Terrain file is not valid JSON";
        return result;
    }
    auto find_int = [&](const char* key, int fallback) {
        const std::string needle = std::string("\"") + key + "\"";
        auto pos = text.find(needle);
        if (pos == std::string::npos) {
            return fallback;
        }
        pos = text.find(':', pos);
        if (pos == std::string::npos) {
            return fallback;
        }
        return std::atoi(text.c_str() + pos + 1);
    };
    auto find_float = [&](const char* key, float fallback, size_t from) {
        const std::string needle = std::string("\"") + key + "\"";
        auto pos = text.find(needle, from);
        if (pos == std::string::npos) {
            return fallback;
        }
        pos = text.find(':', pos);
        if (pos == std::string::npos) {
            return fallback;
        }
        return std::strtof(text.c_str() + pos + 1, nullptr);
    };
    result.tile_size = find_int("tile_size", 0);
    const auto ts = text.find("\"tileset\"");
    if (ts != std::string::npos) {
        auto q1 = text.find('"', ts + 9);
        if (q1 != std::string::npos) {
            q1 = text.find('"', q1 + 1);
        }
        const auto colon = text.find(':', ts);
        if (colon != std::string::npos) {
            auto start = text.find('"', colon + 1);
            if (start != std::string::npos) {
                const auto end = text.find('"', start + 1);
                if (end != std::string::npos) {
                    result.tileset = text.substr(start + 1, end - start - 1);
                }
            }
        }
    }
    if (result.tileset.empty()) {
        result.error = "Terrain file is missing tileset";
        return result;
    }
    size_t pos = 0;
    while (true) {
        const auto xkey = text.find("\"x\"", pos);
        if (xkey == std::string::npos) {
            break;
        }
        VariantBinding b;
        b.x = find_int("x", -1);
        // scoped from this object: parse from xkey
        auto read_near = [&](const char* key) {
            const auto p = text.find(std::string("\"") + key + "\"", xkey);
            if (p == std::string::npos || p > xkey + 200) {
                return -1;
            }
            const auto c = text.find(':', p);
            return (c == std::string::npos) ? -1 : std::atoi(text.c_str() + c + 1);
        };
        b.x = read_near("x");
        b.y = read_near("y");
        b.root_x = read_near("root_x");
        b.root_y = read_near("root_y");
        b.probability = find_float("probability", 0.3f, xkey);
        if (b.x >= 0 && b.y >= 0 && b.root_x >= 0 && b.root_y >= 0) {
            result.variants.push_back(b);
        }
        pos = xkey + 3;
        // skip to next object after this x to avoid re-reading y as x? y also has no "x"
        const auto next_obj = text.find('{', xkey);
        pos = (next_obj == std::string::npos) ? text.size() : next_obj + 1;
    }
    return result;
}

std::string write_text_file(const std::string& path, const std::string& text) {
    std::ofstream f(path.c_str(), std::ios::binary);
    if (!f) {
        return "Could not write " + path;
    }
    f << text;
    return {};
}

std::string read_text_file(const std::string& path, std::string& out) {
    std::ifstream f(path.c_str(), std::ios::binary);
    if (!f) {
        return "Could not read " + path;
    }
    std::ostringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    return {};
}

} // namespace tsm
