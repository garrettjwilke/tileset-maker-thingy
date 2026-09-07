#include "core/atlas_doc.h"
#include "core/convert.h"
#include "core/io.h"
#include "core/md_color.h"
#include "core/palette_presets.h"
#include "core/project.h"
#include "core/tileset_doc.h"
#include "app/settings.h"

#include "gentileset.h"

#include <cstdlib>
#include <cstdio>
#include <iostream>
#include <string>

#ifdef GENTILESET_CLI
#define GENTILESET_CLI_PATH GENTILESET_CLI
#else
#define GENTILESET_CLI_PATH "gentileset"
#endif

namespace {

int g_fails = 0;

void expect(bool cond, const char* msg) {
    if (!cond) {
        std::cerr << "FAIL: " << msg << "\n";
        ++g_fails;
    }
}

std::string temp_path(const char* name) {
    const char* dir = std::getenv("TMPDIR");
    if (!dir || !*dir) {
        dir = "/tmp";
    }
    return std::string(dir) + "/" + name;
}

void test_mirrors_and_stamps() {
    using namespace tsm;
    TilesetDoc doc(8);
    expect(doc.hflip_linked, "expected hflip_linked to be true by default");
    doc.set_pixel(0, 1, 0, 0, 3);
    expect(doc.get_pixel(2, 1, 7, 0) == 3, "live h-flip left→right failed");
    doc.set_pixel(2, 0, 0, 1, 4);
    expect(doc.get_pixel(0, 0, 7, 1) == 4, "live h-flip right→left failed");

    TilesetDoc seed_test(8);
    seed_test.set_pixel(TilesetDoc::kCenter.x, TilesetDoc::kCenter.y, 1, 1, 5);
    seed_test.seed_from_center(true);
    expect(seed_test.get_pixel(2, 1, 1, 1) == 5 && seed_test.get_pixel(2, 1, 6, 1) == 0,
           "seed_from_center should not mirror right side");

    doc.hflip_linked = false;
    doc.set_pixel(2, 1, 0, 0, 5);
    expect(doc.get_pixel(0, 1, 0, 0) == 3, "unlinked right edit wrote through to left");
    expect(!doc.mirrors_match(), "expected mismatch after unlinked edit");
    doc.sync_mirrors_from_left();
    expect(doc.mirrors_match(), "sync from left did not match");
    doc.set_pixel(2, 2, 7, 0, 6);
    doc.sync_mirrors_from_right();
    expect(doc.get_pixel(0, 2, 0, 0) == 6, "sync from right did not match");
    doc.hflip_linked = true;

    expect(!doc.vflip_linked, "expected vflip_linked to be false by default");
    doc.vflip_linked = true;
    doc.set_pixel(1, 0, 3, 1, 6);
    expect(doc.get_pixel(1, 2, 3, 6) == 6, "live v-flip top→bottom failed");
    doc.set_pixel(1, 2, 4, 7, 8);
    expect(doc.get_pixel(1, 0, 4, 0) == 8, "live v-flip bottom→top failed");
    doc.vflip_linked = false;
    doc.set_pixel(1, 2, 3, 6, 9);
    expect(doc.get_pixel(1, 0, 3, 1) == 6, "unlinked bottom edit wrote through to top");
    expect(!doc.vmirrors_match(), "expected vmirror mismatch after unlinked edit");
    doc.sync_vmirrors_from_top();
    expect(doc.vmirrors_match(), "sync vmirrors from top did not match");
    doc.set_pixel(1, 2, 2, 2, 7);
    doc.sync_vmirrors_from_bottom();
    expect(doc.get_pixel(1, 0, 2, 5) == 7, "sync vmirrors from bottom did not match");

    doc.vflip_linked = true;
    doc.set_pixel(TilesetDoc::kPillarTop.x, TilesetDoc::kPillarTop.y, 1, 1, 7);
    doc.reset_cell_to_center(TilesetDoc::kPillarTop.x, TilesetDoc::kPillarTop.y);
    expect(TilesetDoc::tiles_equal(doc.get_tile(TilesetDoc::kPillarTop.x, TilesetDoc::kPillarTop.y),
                                  doc.get_tile(TilesetDoc::kCenter.x, TilesetDoc::kCenter.y)),
           "reset to center failed");
    doc.set_pixel(TilesetDoc::kPillarTop.x, TilesetDoc::kPillarTop.y, 2, 2, 9);
    doc.set_pixel(1, 0, 1, 0, 12);
    doc.set_pixel(1, 0, 6, 0, 11);
    doc.stamp_all_specialty();
    expect(doc.get_pixel(TilesetDoc::kPillarTop.x, TilesetDoc::kPillarTop.y, 2, 2) != 9,
           "stamp all left pillar top unchanged");
    expect(doc.get_pixel(TilesetDoc::kPlatformLeft.x, TilesetDoc::kPlatformLeft.y, 5, 0) == 12,
           "platform left missing top-edge grass");
    expect(doc.get_pixel(TilesetDoc::kPlatformRight.x, TilesetDoc::kPlatformRight.y, 2, 0) == 11,
           "platform right missing top-edge grass");
    doc.reset_specialty_to_center();
    expect(TilesetDoc::tiles_equal(doc.get_tile(TilesetDoc::kPillarTop.x, TilesetDoc::kPillarTop.y),
                                  doc.get_tile(TilesetDoc::kCenter.x, TilesetDoc::kCenter.y)),
           "reset all extras failed");
}

void test_palette() {
    using namespace tsm;
    TilesetDoc swap(8);
    swap.set_pixel(TilesetDoc::kCenter.x, TilesetDoc::kCenter.y, 0, 0, 4);
    swap.set_pixel(TilesetDoc::kCenter.x, TilesetDoc::kCenter.y, 1, 0, 4);
    swap.set_pixel(TilesetDoc::kCenter.x, TilesetDoc::kCenter.y, 2, 0, 7);
    expect(swap.replace_index(4, 2), "replace_index failed");
    expect(swap.get_pixel(TilesetDoc::kCenter.x, TilesetDoc::kCenter.y, 0, 0) == 2, "replace 4→2 missed first pixel");
    expect(swap.get_pixel(TilesetDoc::kCenter.x, TilesetDoc::kCenter.y, 1, 0) == 2, "replace 4→2 missed second pixel");
    expect(swap.get_pixel(TilesetDoc::kCenter.x, TilesetDoc::kCenter.y, 2, 0) == 7, "replace 4→2 changed an unrelated pixel");
    expect(!swap.replace_index(2, 2), "replace same index should be a no-op");

    TilesetDoc pal(8);
    pal.set_pixel(TilesetDoc::kCenter.x, TilesetDoc::kCenter.y, 0, 0, 5);
    pal.set_pixel(TilesetDoc::kCenter.x, TilesetDoc::kCenter.y, 1, 0, 9);
    expect(pal.has_unused_colors(), "expected unused palette slots");
    expect(pal.compact_unused(), "compact unused failed");
    expect(pal.palette_count() == 3, "compact size");
    expect(pal.get_pixel(TilesetDoc::kCenter.x, TilesetDoc::kCenter.y, 0, 0) == 1, "compact remap 5→1 failed");
    expect(pal.get_pixel(TilesetDoc::kCenter.x, TilesetDoc::kCenter.y, 1, 0) == 2, "compact remap 9→2 failed");
    const Rgb old1 = pal.color_at(1);
    expect(pal.reorder_palette(1, 2), "reorder failed");
    expect(pal.color_at(2) == old1, "reorder did not move color");
    expect(pal.get_pixel(TilesetDoc::kCenter.x, TilesetDoc::kCenter.y, 0, 0) == 2, "reorder pixel follow failed");
    pal.apply_shrink_remap(2, 0);
    expect(pal.palette_count() == 2, "shrink size");
    expect(pal.get_pixel(TilesetDoc::kCenter.x, TilesetDoc::kCenter.y, 0, 0) == 0, "shrink remap failed");

    const auto q = MdColor::quantize(Rgb{20, 16, 28});
    expect(MdColor::is_legal(q), "quantized color is not Mega Drive legal");
    const auto def = TilesetDoc::default_palette();
    for (size_t i = 0; i < def.size(); ++i) {
        expect(MdColor::is_legal(def[i]), "default palette slot is not legal");
    }
    expect(PalettePresets::names().size() == 16, "expected 16 presets");
    const auto meadow = PalettePresets::colors_for("8 - Meadow");
    expect(meadow.size() == 8, "8 - Meadow size");
    for (Rgb c : meadow) {
        expect(MdColor::is_legal(c), "8 - Meadow slot is not legal");
    }
    TilesetDoc fresh(8);
    fresh.apply_palette(meadow);
    expect(fresh.palette_count() == 8, "apply 8 - Meadow size");
    const auto text = palette_to_text(meadow);
    const auto loaded = palette_from_text(text);
    expect(loaded.error.empty(), "palette io error");
    expect(loaded.colors.size() == meadow.size() && loaded.colors[1] == meadow[1], "palette io round-trip failed");
}

void test_pipeline_and_export() {
    using namespace tsm;
    TilesetDoc src(8);
    src.set_pixel(TilesetDoc::kCenter.x, TilesetDoc::kCenter.y, 0, 0, 7);
    src.seed_from_center(true);
    AtlasDoc atlas;
    const std::string err = convert_tileset_to_atlas(src, atlas);
    expect(err.empty(), "convert failed");
    expect(atlas.cols == 12, "convert cols");
    expect(atlas.tile_size == 8, "convert tile size");
    expect(atlas.color_at(7) == src.color_at(7), "palette not preserved");
    const Cell extra = atlas.add_variant({9, 2}, 0.3f);
    expect(extra.x == 12 && extra.y == 0, "extra slot");
    expect(atlas.bindings.size() == 1, "expected 1 binding");
    expect(AtlasDoc::tiles_equal(atlas.get_tile(12, 0), atlas.get_tile(9, 2)), "variant tile was not copied from root");
    for (int junk_row = 1; junk_row < 4; ++junk_row) {
        expect(AtlasDoc::tiles_equal(atlas.get_tile(12, junk_row), atlas.empty_tile()), "leftover junk variant");
    }
    expect(atlas.cols == 13, "atlas cols");

    const std::string header = render_header(atlas, "tileset.png");
    expect(header.find("#define TILESET_ATLAS_COLS  13") != std::string::npos, "missing ATLAS_COLS 13");
    expect(header.find("{ 12, 0, 9, 2, 30 }") != std::string::npos, "missing variant row");

    const std::string png = temp_path("tsm-self-test.png");
    const std::string hpath = temp_path("tsm-self-test.h");
    const std::string tpath = temp_path("tsm-self-test.terrain");
    const std::string side = temp_path("tsm-self-test_5x3.png");
    expect(save_atlas_png(atlas, png).empty(), "save atlas png");
    expect(save_header(atlas, hpath, "tileset.png").empty(), "save header");
    expect(save_terrain(atlas, tpath, png).empty(), "save terrain");
    expect(save_tileset_png(src, side).empty(), "save 5x3 sidecar");

    TilesetDoc loaded(16);
    expect(load_tileset_png(loaded, side).empty(), "load 5x3");
    expect(loaded.tile_size == 8 && loaded.get_pixel(TilesetDoc::kCenter.x, TilesetDoc::kCenter.y, 0, 0) == 7,
           "5x3 round-trip");

    const auto terrain = load_terrain(tpath);
    expect(terrain.error.empty(), "load terrain");
    expect(terrain.variants.size() == 1, "terrain variant count");
    expect(terrain.variants[0].x == 12 && terrain.variants[0].root_x == 9, "terrain variant coords");
}

void test_golden_vs_cli() {
    using namespace tsm;
    TilesetDoc src(8);
    src.set_pixel(TilesetDoc::kCenter.x, TilesetDoc::kCenter.y, 0, 0, 7);
    src.set_pixel(TilesetDoc::kCenter.x, TilesetDoc::kCenter.y, 3, 3, 4);
    src.seed_from_center(true);
    src.set_pixel(0, 0, 1, 1, 9);
    src.stamp_all_specialty();

    const std::string in = temp_path("tsm-golden-in.png");
    const std::string lib_out = temp_path("tsm-golden-lib.png");
    const std::string cli_out = temp_path("tsm-golden-cli.png");
    expect(save_tileset_png(src, in).empty(), "write golden input");

    AtlasDoc atlas;
    expect(convert_tileset_to_atlas(src, atlas).empty(), "lib convert");
    expect(save_atlas_png(atlas, lib_out).empty(), "write lib atlas");

    const std::string cmd = std::string("\"") + GENTILESET_CLI_PATH + "\" -i \"" + in + "\" -o \"" + cli_out +
                            "\" -w 8 -h 8";
    const int rc = std::system(cmd.c_str());
    expect(rc == 0, "gentileset CLI failed");

    Image a{};
    Image b{};
    expect(load_png(lib_out.c_str(), &a) != 0, "load lib png");
    expect(load_png(cli_out.c_str(), &b) != 0, "load cli png");
    expect(a.w == b.w && a.h == b.h && a.indexed && b.indexed, "golden size/type mismatch");
    if (a.px && b.px && a.w == b.w && a.h == b.h) {
        bool same = true;
        const size_t n = static_cast<size_t>(a.w) * a.h * static_cast<size_t>(a.bpp);
        for (size_t i = 0; i < n; ++i) {
            if (a.px[i] != b.px[i]) {
                same = false;
                break;
            }
        }
        expect(same, "library convert_5x3 output differs from gentileset CLI");
    }
    image_free(&a);
    image_free(&b);
}

void test_settings_file() {
    using namespace tsm;
    Settings s;
    expect(s.pixel_grid_color == (Rgb{104, 104, 104}), "default pixel_grid_color");
    s.dark = false;
    s.scale = 1.25f;
    s.window_x = 40;
    s.window_y = 80;
    s.window_w = 1600;
    s.window_h = 900;
    s.window_maximized = true;
    s.window_placed = true;
    s.pixel_grid = false;
    s.tile_grid_color = Rgb{0x12, 0x34, 0x56};
    s.pixel_grid_color = Rgb{0xAB, 0xCD, 0xEF};
    s.extra.emplace_back("future_flag", "ok");

    const std::string text = format_settings(s);
    expect(text.find("theme=light") != std::string::npos, "settings should write theme");
    expect(text.find("scale=1.25") != std::string::npos, "settings should write scale");
    expect(text.find("window_x=40") != std::string::npos, "settings should write window_x");
    expect(text.find("window_maximized=true") != std::string::npos, "settings should write maximized");
    expect(text.find("pixel_grid=false") != std::string::npos, "settings should write pixel_grid");
    expect(text.find("tile_grid_color=#123456") != std::string::npos, "settings should write tile_grid_color");
    expect(text.find("pixel_grid_color=#ABCDEF") != std::string::npos, "settings should write pixel_grid_color");
    expect(text.find("future_flag=ok") != std::string::npos, "settings should keep unknown keys");
#define X(type, name, def, kind, key) \
    expect(text.find(std::string(key) + "=") != std::string::npos, "settings missing " key);
    TSM_SETTINGS_FIELDS(X)
#undef X

    Settings loaded;
    expect(parse_settings_text(loaded, text), "parse settings text");
    expect(!loaded.dark, "roundtrip theme");
    expect(loaded.scale > 1.24f && loaded.scale < 1.26f, "roundtrip scale");
    expect(loaded.window_x == 40 && loaded.window_y == 80, "roundtrip window pos");
    expect(loaded.window_w == 1600 && loaded.window_h == 900, "roundtrip window size");
    expect(loaded.window_maximized && loaded.window_placed, "roundtrip window flags");
    expect(!loaded.pixel_grid, "roundtrip pixel_grid");
    expect(loaded.tile_grid_color == (Rgb{0x12, 0x34, 0x56}), "roundtrip tile_grid_color");
    expect(loaded.pixel_grid_color == (Rgb{0xAB, 0xCD, 0xEF}), "roundtrip pixel_grid_color");
    expect(loaded.extra.size() == 1 && loaded.extra[0].first == "future_flag" && loaded.extra[0].second == "ok",
           "roundtrip extra key");

    Settings alias_loaded;
    parse_settings_text(alias_loaded, "grid_color=#8899AA\n");
    expect(alias_loaded.tile_grid_color == (Rgb{0x88, 0x99, 0xAA}), "parse grid_color alias");

    Settings clamped;
    parse_settings_text(clamped, "scale=9\nwindow_w=10\nwindow_h=10\n");
    expect(clamped.scale == 2.0f, "scale should clamp to max");
    expect(clamped.window_w == 640 && clamped.window_h == 480, "window size should clamp to min");

    const std::string dir = config_dir();
    expect(dir.find("tileset-maker-thingy") != std::string::npos, "config dir should use app name");
#ifdef __APPLE__
    expect(dir.find("Application Support") != std::string::npos, "mac config should be Application Support");
#elif defined(_WIN32)
    expect(true, "windows config dir");
#else
    if (const char* xdg = std::getenv("XDG_CONFIG_HOME"); xdg && xdg[0]) {
        expect(dir.find(xdg) != std::string::npos, "linux config should honor XDG_CONFIG_HOME");
    } else {
        expect(dir.find(".config") != std::string::npos, "linux config should be ~/.config");
    }
#endif

    const std::string path = temp_path("tileset-maker-settings-test.cfg");
    expect(save_settings_file(s, path), "save settings file");
    Settings from_disk;
    expect(load_settings_file(from_disk, path), "load settings file");
    expect(!from_disk.dark && from_disk.window_placed && !from_disk.pixel_grid &&
           from_disk.tile_grid_color == (Rgb{0x12, 0x34, 0x56}) &&
           from_disk.pixel_grid_color == (Rgb{0xAB, 0xCD, 0xEF}) &&
           from_disk.extra.size() == 1,
           "disk roundtrip");
    std::remove(path.c_str());
}

void test_project_file() {
    using namespace tsm;
    TilesetDoc src(8);
    src.set_pixel(TilesetDoc::kCenter.x, TilesetDoc::kCenter.y, 0, 0, 7);
    src.set_pixel(TilesetDoc::kCenter.x, TilesetDoc::kCenter.y, 3, 3, 4);
    src.seed_from_center(true);
    src.hflip_linked = false;
    src.set_pixel(2, 1, 1, 1, 9);
    src.stamp_all_specialty();
    AtlasDoc atlas;
    expect(convert_tileset_to_atlas(src, atlas).empty(), "project convert");
    const Cell extra = atlas.add_variant({9, 2}, 0.3f);
    expect(extra.x == 12, "project variant slot");

    ProjectData data;
    data.name = "roundtrip";
    data.step = ProjectStep::Variants;
    data.seeded = true;
    data.stamped = true;
    data.specialty = TilesetDoc::kPillarTop;
    data.atlas_cell = extra;
    data.preview_sel = {2, 1};
    data.tile_mode = false;
    data.export_header = false;
    data.export_terrain = true;
    data.export_5x3 = false;
    data.art_rev = 11;
    data.atlas_rev = 11;
    data.has_atlas = true;
    data.tileset = src.snapshot();
    data.atlas = atlas.snapshot();

    const std::string path = temp_path("tsm-roundtrip.tilesetproj");
    expect(save_project(data, path).empty(), "save project");

    ProjectData loaded;
    expect(load_project(loaded, path).empty(), "load project");
    expect(loaded.name == "roundtrip", "project name");
    expect(loaded.tileset.tile_size == 8, "project tile size");
    expect(loaded.step == ProjectStep::Variants, "project step");
    expect(loaded.seeded && loaded.stamped, "project seeded/stamped");
    expect(!loaded.tileset.hflip_linked, "project hflip flag");
    expect(loaded.tileset.tiles[static_cast<size_t>(TilesetDoc::cell_index(TilesetDoc::kCenter.x, TilesetDoc::kCenter.y))][0] ==
               7,
           "project center pixel");
    expect(loaded.tileset.tiles[static_cast<size_t>(TilesetDoc::cell_index(2, 1))][static_cast<size_t>(1 * 8 + 1)] == 9,
           "project edge pixel");
    expect(loaded.has_atlas && loaded.atlas.cols == 13, "project atlas cols");
    expect(loaded.atlas.bindings.size() == 1 && loaded.atlas.bindings[0].root_x == 9, "project variant binding");
    expect(loaded.atlas.palette[7] == src.color_at(7), "project atlas palette");
    expect(!loaded.export_header && loaded.export_terrain && !loaded.export_5x3, "project export flags");
    expect(loaded.tile_mode == false, "project tile mode");
    expect(loaded.specialty.x == TilesetDoc::kPillarTop.x && loaded.atlas_cell.x == extra.x, "project selection");
    std::remove(path.c_str());
}

void test_corner_context_preview() {
    using namespace tsm;

    // Test 16x16 doc
    {
        TilesetDoc doc(16);
        const int ts = 16;

        // Set distinctive pixels:
        // Left edge (0, 1)
        doc.set_pixel(0, 1, 2, 3, 5);
        // Right edge (2, 1)
        doc.hflip_linked = false;
        doc.set_pixel(2, 1, 4, 5, 6);
        // Top edge (1, 0)
        doc.set_pixel(1, 0, 1, 2, 7);
        // Bottom edge (1, 2)
        doc.set_pixel(1, 2, 3, 4, 8);
        // Center (1, 1)
        doc.set_pixel(TilesetDoc::kCenter.x, TilesetDoc::kCenter.y, 2, 2, 9);
        // Inner corner (4, 0)
        doc.set_pixel(TilesetDoc::kInnerCorner.x, TilesetDoc::kInnerCorner.y, 2, 2, 10); // in TL quadrant
        doc.set_pixel(TilesetDoc::kInnerCorner.x, TilesetDoc::kInnerCorner.y, 10, 2, 11); // in TR quadrant
        doc.set_pixel(TilesetDoc::kInnerCorner.x, TilesetDoc::kInnerCorner.y, 2, 10, 12); // in BL quadrant
        doc.set_pixel(TilesetDoc::kInnerCorner.x, TilesetDoc::kInnerCorner.y, 10, 10, 13); // in BR quadrant

        // Check empty corners
        bool is_bg = false;
        bool is_cutout = false;
        doc.corner_context_pixel(0, 0, &is_bg, &is_cutout);
        expect(is_bg && !is_cutout, "corner_context: (0,0) must be bg");

        doc.corner_context_pixel(3 * ts + 1, 2, &is_bg, &is_cutout);
        expect(is_bg && !is_cutout, "corner_context: (3,0) tile must be bg");

        doc.corner_context_pixel(2, 3 * ts + 2, &is_bg, &is_cutout);
        expect(is_bg && !is_cutout, "corner_context: (0,3) tile must be bg");

        doc.corner_context_pixel(3 * ts + 2, 3 * ts + 2, &is_bg, &is_cutout);
        expect(is_bg && !is_cutout, "corner_context: (3,3) tile must be bg");

        // Check Left edge tile at (1, 0): doc cell (0, 1)
        int p = doc.corner_context_pixel(1 * ts + 2, 0 * ts + 3, &is_bg, &is_cutout);
        expect(!is_bg && !is_cutout && p == 5, "corner_context: (1,0) should sample left edge (0,1)");

        // Check Left edge tile at (1, 3): doc cell (0, 1)
        p = doc.corner_context_pixel(1 * ts + 2, 3 * ts + 3, &is_bg, &is_cutout);
        expect(!is_bg && !is_cutout && p == 5, "corner_context: (1,3) should sample left edge (0,1)");

        // Check Right edge tile at (2, 0): doc cell (2, 1)
        p = doc.corner_context_pixel(2 * ts + 4, 0 * ts + 5, &is_bg, &is_cutout);
        expect(!is_bg && !is_cutout && p == 6, "corner_context: (2,0) should sample right edge (2,1)");

        // Check Top edge tile at (0, 1) and (3, 1): doc cell (1, 0)
        p = doc.corner_context_pixel(0 * ts + 1, 1 * ts + 2, &is_bg, &is_cutout);
        expect(!is_bg && !is_cutout && p == 7, "corner_context: (0,1) should sample top edge (1,0)");
        p = doc.corner_context_pixel(3 * ts + 1, 1 * ts + 2, &is_bg, &is_cutout);
        expect(!is_bg && !is_cutout && p == 7, "corner_context: (3,1) should sample top edge (1,0)");

        // Check Bottom edge tile at (0, 2) and (3, 2): doc cell (1, 2)
        p = doc.corner_context_pixel(0 * ts + 3, 2 * ts + 4, &is_bg, &is_cutout);
        expect(!is_bg && !is_cutout && p == 8, "corner_context: (0,2) should sample bottom edge (1,2)");
        p = doc.corner_context_pixel(3 * ts + 3, 2 * ts + 4, &is_bg, &is_cutout);
        expect(!is_bg && !is_cutout && p == 8, "corner_context: (3,2) should sample bottom edge (1,2)");

        // Check Center 4 tiles:
        // (1, 1): TL quadrant should sample from kInnerCorner (4, 0) and flag is_cutout
        p = doc.corner_context_pixel(1 * ts + 2, 1 * ts + 2, &is_bg, &is_cutout);
        expect(!is_bg && is_cutout && p == 10, "corner_context: (1,1) TL quadrant cutout");
        // (1, 1): Outside TL quadrant should not be cutout
        p = doc.corner_context_pixel(1 * ts + 10, 1 * ts + 2, &is_bg, &is_cutout);
        expect(!is_bg && !is_cutout, "corner_context: (1,1) outside TL quadrant not cutout");

        // (2, 1): TR quadrant should sample from kInnerCorner (4, 0) and flag is_cutout
        p = doc.corner_context_pixel(2 * ts + 10, 1 * ts + 2, &is_bg, &is_cutout);
        expect(!is_bg && is_cutout && p == 11, "corner_context: (2,1) TR quadrant cutout");

        // (1, 2): BL quadrant should sample from kInnerCorner (4, 0) and flag is_cutout
        p = doc.corner_context_pixel(1 * ts + 2, 2 * ts + 10, &is_bg, &is_cutout);
        expect(!is_bg && is_cutout && p == 12, "corner_context: (1,2) BL quadrant cutout");

        // (2, 2): BR quadrant should sample from kInnerCorner (4, 0) and flag is_cutout
        p = doc.corner_context_pixel(2 * ts + 10, 2 * ts + 10, &is_bg, &is_cutout);
        expect(!is_bg && is_cutout && p == 13, "corner_context: (2,2) BR quadrant cutout");
    }

    // Test 8x8 doc
    {
        TilesetDoc doc8(8);
        const int ts = 8;
        doc8.set_pixel(TilesetDoc::kInnerCorner.x, TilesetDoc::kInnerCorner.y, 1, 1, 14);
        bool is_bg = false;
        bool is_cutout = false;
        int p = doc8.corner_context_pixel(1 * ts + 1, 1 * ts + 1, &is_bg, &is_cutout);
        expect(!is_bg && is_cutout && p == 14, "corner_context 8x8: (1,1) TL cutout");
    }
}

void test_specialty_context_cell() {
    using namespace tsm;

    // Center is always specialty
    expect(TilesetDoc::specialty_context_cell(TilesetDoc::kInnerCorner, 1, 1) == TilesetDoc::kInnerCorner,
           "specialty center must be inner corner");
    expect(TilesetDoc::specialty_context_cell(TilesetDoc::kPillarTop, 1, 1) == TilesetDoc::kPillarTop,
           "specialty center must be pillar top");

    // Inner Corner (4, 0): N, S, E, W are the edge tiles
    expect(TilesetDoc::specialty_context_cell(TilesetDoc::kInnerCorner, 1, 0) == Cell{1, 0},
           "inner corner N should be top edge (1,0)");
    expect(TilesetDoc::specialty_context_cell(TilesetDoc::kInnerCorner, 1, 2) == Cell{1, 2},
           "inner corner S should be bottom edge (1,2)");
    expect(TilesetDoc::specialty_context_cell(TilesetDoc::kInnerCorner, 0, 1) == Cell{0, 1},
           "inner corner W should be left edge (0,1)");
    expect(TilesetDoc::specialty_context_cell(TilesetDoc::kInnerCorner, 2, 1) == Cell{2, 1},
           "inner corner E should be right edge (2,1)");
    expect(TilesetDoc::specialty_context_cell(TilesetDoc::kInnerCorner, 0, 0) == Cell{-1, -1},
           "inner corner NW should be empty");
    expect(TilesetDoc::specialty_context_cell(TilesetDoc::kInnerCorner, 2, 0) == Cell{-1, -1},
           "inner corner NE should be empty");
    expect(TilesetDoc::specialty_context_cell(TilesetDoc::kInnerCorner, 0, 2) == Cell{-1, -1},
           "inner corner SW should be empty");
    expect(TilesetDoc::specialty_context_cell(TilesetDoc::kInnerCorner, 2, 2) == Cell{-1, -1},
           "inner corner SE should be empty");

    // Pillar Top (3, 0): S is Pillar Bottom (3, 1)
    expect(TilesetDoc::specialty_context_cell(TilesetDoc::kPillarTop, 1, 2) == TilesetDoc::kPillarBottom,
           "pillar top S should be pillar bottom");
    expect(TilesetDoc::specialty_context_cell(TilesetDoc::kPillarTop, 1, 0) == Cell{-1, -1},
           "pillar top N should be empty");
    expect(TilesetDoc::specialty_context_cell(TilesetDoc::kPillarTop, 0, 1) == Cell{-1, -1},
           "pillar top W should be empty");
    expect(TilesetDoc::specialty_context_cell(TilesetDoc::kPillarTop, 2, 1) == Cell{-1, -1},
           "pillar top E should be empty");

    // Pillar Bottom (3, 1): N is Pillar Top (3, 0)
    expect(TilesetDoc::specialty_context_cell(TilesetDoc::kPillarBottom, 1, 0) == TilesetDoc::kPillarTop,
           "pillar bottom N should be pillar top");
    expect(TilesetDoc::specialty_context_cell(TilesetDoc::kPillarBottom, 1, 2) == Cell{-1, -1},
           "pillar bottom S should be empty");

    // Platform Left (3, 2): E is Platform Right (4, 2)
    expect(TilesetDoc::specialty_context_cell(TilesetDoc::kPlatformLeft, 2, 1) == TilesetDoc::kPlatformRight,
           "platform left E should be platform right");
    expect(TilesetDoc::specialty_context_cell(TilesetDoc::kPlatformLeft, 0, 1) == Cell{-1, -1},
           "platform left W should be empty");

    // Platform Right (4, 2): W is Platform Left (3, 2)
    expect(TilesetDoc::specialty_context_cell(TilesetDoc::kPlatformRight, 0, 1) == TilesetDoc::kPlatformLeft,
           "platform right W should be platform left");
    expect(TilesetDoc::specialty_context_cell(TilesetDoc::kPlatformRight, 2, 1) == Cell{-1, -1},
           "platform right E should be empty");

    // Isolated (4, 1): all 4 directions are empty
    expect(TilesetDoc::specialty_context_cell(TilesetDoc::kIsolated, 1, 0) == Cell{-1, -1},
           "isolated N should be empty");
    expect(TilesetDoc::specialty_context_cell(TilesetDoc::kIsolated, 1, 2) == Cell{-1, -1},
           "isolated S should be empty");
    expect(TilesetDoc::specialty_context_cell(TilesetDoc::kIsolated, 0, 1) == Cell{-1, -1},
           "isolated W should be empty");
    expect(TilesetDoc::specialty_context_cell(TilesetDoc::kIsolated, 2, 1) == Cell{-1, -1},
           "isolated E should be empty");

    // Out of bounds
    expect(TilesetDoc::specialty_context_cell(TilesetDoc::kInnerCorner, -1, 0) == Cell{-1, -1},
           "out of bounds x should be empty");
    expect(TilesetDoc::specialty_context_cell(TilesetDoc::kInnerCorner, 0, 3) == Cell{-1, -1},
           "out of bounds y should be empty");
}

} // namespace

int main() {
    test_mirrors_and_stamps();
    test_palette();
    test_pipeline_and_export();
    test_golden_vs_cli();
    test_settings_file();
    test_project_file();
    test_corner_context_preview();
    test_specialty_context_cell();
    if (g_fails) {
        std::cerr << g_fails << " test(s) failed\n";
        return 1;
    }
    std::cout << "self-test: ok\n";
    return 0;
}
