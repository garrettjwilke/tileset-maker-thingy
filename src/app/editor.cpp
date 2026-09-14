#include "editor.h"
#include "settings.h"
#include "tileset_editor.h"

#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_sdlrenderer3.h"
#include "nfd.h"

#include <SDL3/SDL.h>
#include <cstdio>
#include <string>

namespace {

SDL_Window* g_window = nullptr;

bool window_rect_visible(int x, int y, int w, int h) {
    int n = 0;
    SDL_DisplayID* displays = SDL_GetDisplays(&n);
    if (!displays || n <= 0) return false;
    bool ok = false;
    for (int i = 0; i < n; ++i) {
        SDL_Rect b{};
        if (!SDL_GetDisplayUsableBounds(displays[i], &b)) continue;
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

void capture_window(tsm::Settings& settings) {
    if (!g_window) return;
    const SDL_WindowFlags flags = SDL_GetWindowFlags(g_window);
    if (flags & SDL_WINDOW_MINIMIZED) return;
    settings.window_placed = true;
    settings.window_maximized = (flags & SDL_WINDOW_MAXIMIZED) != 0;
    if (!settings.window_maximized) {
        SDL_GetWindowPosition(g_window, &settings.window_x, &settings.window_y);
        SDL_GetWindowSize(g_window, &settings.window_w, &settings.window_h);
    }
}

void apply_window_geometry(const tsm::Settings& settings) {
    if (!g_window) return;
    SDL_SetWindowSize(g_window, settings.window_w, settings.window_h);
    if (settings.window_placed &&
        window_rect_visible(settings.window_x, settings.window_y, settings.window_w, settings.window_h)) {
        SDL_SetWindowPosition(g_window, settings.window_x, settings.window_y);
    }
    if (settings.window_maximized) {
        SDL_MaximizeWindow(g_window);
    }
}

void persist_settings(tsm::Settings& settings, bool capture = true) {
    if (capture) {
        capture_window(settings);
    }
    tsm::save_settings_file(settings, tsm::settings_path());
}

void load_settings(tsm::Settings& settings) {
    if (tsm::load_settings_file(settings, tsm::settings_path())) return;
    char* pref = SDL_GetPrefPath("chuchu-soldier", "tileset-maker-thingy");
    if (pref) {
        const std::string old = std::string(pref) + "settings.cfg";
        SDL_free(pref);
        if (tsm::load_settings_file(settings, old)) {
            persist_settings(settings);
            return;
        }
    }
    tsm::load_settings_file(settings, "tileset-maker-settings.cfg");
}

void apply_appearance(const tsm::Settings& settings) {
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
    if (settings.dark) {
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
    s.ScaleAllSizes(settings.scale);
    s.FontScaleMain = settings.scale;
}

} // namespace

int run_editor() {
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) {
        SDL_Log("SDL_Init failed: %s", SDL_GetError());
        return 1;
    }
    NFD_Init();
    tsm::TilesetEditor ed;
    load_settings(ed.settings);

    SDL_Window* window = SDL_CreateWindow("tileset maker thingy", ed.settings.window_w, ed.settings.window_h,
                                         SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    g_window = window;
    SDL_Renderer* renderer = SDL_CreateRenderer(window, nullptr);
    if (!window || !renderer) {
        SDL_Log("SDL window/renderer failed: %s", SDL_GetError());
        return 1;
    }
    apply_window_geometry(ed.settings);
    persist_settings(ed.settings, false);
    SDL_SetRenderVSync(renderer, 1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    tsm::ensure_config_dir();
    std::string ini_path = tsm::imgui_ini_path();
    io.IniFilename = ini_path.c_str();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
#ifdef IMGUI_HAS_DOCK
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
#endif
    apply_appearance(ed.settings);
    ImGui_ImplSDL3_InitForSDLRenderer(window, renderer);
    ImGui_ImplSDLRenderer3_Init(renderer);

    bool running = true;
    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL3_ProcessEvent(&event);
            if (event.type == SDL_EVENT_QUIT) {
                if (!ed.ui.project_open || !ed.dirty) {
                    running = false;
                } else {
                    ed.ui.pending = tsm::PendingAction::Quit;
                    ed.ui.show_unsaved = true;
                }
            }
        }

        apply_appearance(ed.settings);
        ImGui_ImplSDLRenderer3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();

        if (ImGui::BeginMainMenuBar()) {
            if (ImGui::BeginMenu("File")) {
                if (ImGui::MenuItem("New project", "Ctrl+N")) {
                    ed.ui.show_new = true;
                    ed.ui.new_focus_name = true;
                }
                if (ImGui::MenuItem("Open project...", "Ctrl+O")) {
                    ed.try_open_project_dialog();
                }
                ImGui::BeginDisabled(!ed.ui.project_open);
                if (ImGui::MenuItem("Save project", "Ctrl+S")) {
                    ed.save_project(false);
                }
                if (ImGui::MenuItem("Save project as...", "Ctrl+Shift+S")) {
                    ed.save_project(true);
                }
                ImGui::EndDisabled();
                ImGui::Separator();
                if (ImGui::MenuItem("Import 12x4 Tileset")) {
                    ed.try_import_12x4_dialog();
                }
                if (ImGui::MenuItem("Import 5x3 Tileset")) {
                    ed.try_import_5x3_dialog();
                }
                ImGui::BeginDisabled(!ed.ui.project_open);
                if (ImGui::MenuItem("Export...", "Ctrl+E")) {
                    const std::string err = ed.ensure_atlas();
                    if (err.empty()) {
                        ed.ui.export_zoom = std::max(2, 192 / std::max(1, ed.atlas.tile_size * ed.atlas.cols));
                        ed.ui.show_export = true;
                    } else {
                        ed.status = err;
                    }
                }
                ImGui::EndDisabled();
                ImGui::Separator();
                if (ImGui::MenuItem("Quit", "Ctrl+Q")) {
                    if (!ed.ui.project_open || !ed.dirty) {
                        running = false;
                    } else {
                        ed.ui.pending = tsm::PendingAction::Quit;
                        ed.ui.show_unsaved = true;
                    }
                }
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Settings")) {
                ImGui::TextUnformatted("Theme");
                if (ImGui::MenuItem("Dark", nullptr, ed.settings.dark)) {
                    ed.settings.dark = true;
                    persist_settings(ed.settings);
                }
                if (ImGui::MenuItem("Light", nullptr, !ed.settings.dark)) {
                    ed.settings.dark = false;
                    persist_settings(ed.settings);
                }
                ImGui::Separator();
                ImGui::TextUnformatted("UI scale");
                float percent = ed.settings.scale * 100.0f;
                ImGui::SetNextItemWidth(220.0f);
                if (ImGui::SliderFloat("##menuscale", &percent, 75.0f, 200.0f, "%.0f%%")) {
                    ed.settings.scale = percent / 100.0f;
                    persist_settings(ed.settings);
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Pixel grid", nullptr, ed.settings.pixel_grid)) {
                    ed.settings.pixel_grid = !ed.settings.pixel_grid;
                    persist_settings(ed.settings);
                }
                ImGui::EndMenu();
            }
            ImGui::EndMainMenuBar();
        }

        const bool cmd = io.KeyCtrl || io.KeySuper;
        if (!io.WantTextInput) {
            if (cmd && ImGui::IsKeyPressed(ImGuiKey_N)) {
                ed.ui.show_new = true;
                ed.ui.new_focus_name = true;
            }
            if (cmd && ImGui::IsKeyPressed(ImGuiKey_O)) {
                ed.try_open_project_dialog();
            }
            if (cmd && ImGui::IsKeyPressed(ImGuiKey_S) && ed.ui.project_open) {
                ed.save_project(io.KeyShift);
            }
            if (cmd && ImGui::IsKeyPressed(ImGuiKey_E) && ed.ui.project_open) {
                const std::string err = ed.ensure_atlas();
                if (err.empty()) {
                    ed.ui.export_zoom = std::max(2, 192 / std::max(1, ed.atlas.tile_size * ed.atlas.cols));
                    ed.ui.show_export = true;
                } else {
                    ed.status = err;
                }
            }
            if (cmd && ImGui::IsKeyPressed(ImGuiKey_Q)) {
                if (!ed.ui.project_open || !ed.dirty) {
                    running = false;
                } else {
                    ed.ui.pending = tsm::PendingAction::Quit;
                    ed.ui.show_unsaved = true;
                }
            }
        }

        if (ed.ui.project_open) {
            ed.handle_shortcuts(io);

            const ImGuiViewport* vp = ImGui::GetMainViewport();
            ImGui::SetNextWindowPos(vp->WorkPos);
            ImGui::SetNextWindowSize(vp->WorkSize);
            ImGui::Begin("Tileset Maker", nullptr,
                         ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
                         ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus |
                         ImGuiWindowFlags_NoSavedSettings);

            ed.draw_content(renderer, window);
            ImGui::End();
        }

        ed.draw_modals(running);

        ImGui::Render();
        const ImVec4 bg = ed.settings.dark ? ImVec4(0.10f, 0.11f, 0.13f, 1.0f) : ImVec4(0.90f, 0.91f, 0.93f, 1.0f);
        SDL_SetRenderDrawColor(renderer, static_cast<Uint8>(bg.x * 255), static_cast<Uint8>(bg.y * 255),
                               static_cast<Uint8>(bg.z * 255), 255);
        SDL_RenderClear(renderer);
        ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer);
        SDL_RenderPresent(renderer);
    }

    persist_settings(ed.settings);
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
