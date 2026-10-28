#include "import_ui.hpp"
#include "importer.hpp"
#include <algorithm>
#include <atomic>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
namespace rr {
namespace {
struct ImportUI {
    std::mutex mutex;
    std::string selected, message;
    ImportProgress progress{};
    std::atomic<bool> dialog_pending{false}, working{false}, complete{false}, cancel{false};
};
void SDLCALL selected_file(void* data, const char* const* files, int) {
    std::unique_ptr<std::shared_ptr<ImportUI>> owner(static_cast<std::shared_ptr<ImportUI>*>(data));
    const auto state = *owner;
    {
        std::lock_guard<std::mutex> lock(state->mutex);
        if (!files)
            state->message = std::string("File selection failed: ") + SDL_GetError();
        else if (files[0]) {
            state->selected = files[0];
            state->message.clear();
        }
    }
    state->dialog_pending = false;
}
} // namespace
bool run_import_screen(SDL_Window* window, SDL_Renderer* renderer, const std::string& destination,
                       const std::string& screenshot, bool smoke) {
    const auto state = std::make_shared<ImportUI>();
    std::thread worker;
    bool running = true;
    static const SDL_DialogFileFilter filter{"Disc image", "iso"};
    // Plain centered layout on the 640x480 logical canvas; the same rects drive
    // painting and hit tests.
#ifdef __ANDROID__
    static const SDL_FRect iso_button{200, 264, 240, 44};
#else
    static const SDL_FRect iso_button{110, 264, 200, 44};
    static const SDL_FRect folder_button{330, 264, 200, 44};
#endif
    auto inside = [](const SDL_FRect& r, float x, float y) {
        return x >= r.x && x <= r.x + r.w && y >= r.y && y <= r.y + r.h;
    };
    auto dialog = [&](bool folder) {
        if (state->working || state->dialog_pending) return;
        state->dialog_pending = true;
        auto* owner = new std::shared_ptr<ImportUI>(state);
        if (folder)
            SDL_ShowOpenFolderDialog(selected_file, owner, window, nullptr, false);
        else
            SDL_ShowOpenFileDialog(selected_file, owner, window, &filter, 1, nullptr, false);
    };
    while (running && !state->complete) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT) running = false;
            if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat) {
                if (event.key.key == SDLK_ESCAPE || event.key.key == SDLK_AC_BACK || event.key.key == SDLK_Q)
                    running = false;
                if (event.key.key == SDLK_I || event.key.key == SDLK_RETURN) dialog(false);
#ifndef __ANDROID__
                if (event.key.key == SDLK_F) dialog(true);
#endif
            }
            if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN || event.type == SDL_EVENT_FINGER_DOWN) {
                float x, y;
                if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
                    x = event.button.x;
                    y = event.button.y;
                } else {
                    int w, h;
                    SDL_GetWindowSize(window, &w, &h);
                    x = event.tfinger.x * w;
                    y = event.tfinger.y * h;
                }
                float logical_x, logical_y;
                SDL_RenderCoordinatesFromWindow(renderer, x, y, &logical_x, &logical_y);
                if (inside(iso_button, logical_x, logical_y)) dialog(false);
#ifndef __ANDROID__
                if (inside(folder_button, logical_x, logical_y)) dialog(true);
#endif
            }
        }
        std::string selection;
        {
            std::lock_guard<std::mutex> lock(state->mutex);
            selection.swap(state->selected);
        }
        if (!selection.empty() && !state->working) {
            if (worker.joinable()) worker.join();
            state->working = true;
            worker = std::thread([state, selection, destination]() {
                const auto result = import_game(selection, destination, [state](const ImportProgress& p) {
                    if (state->cancel) throw std::runtime_error("Import canceled");
                    std::lock_guard<std::mutex> lock(state->mutex);
                    state->progress = p;
                });
                {
                    std::lock_guard<std::mutex> lock(state->mutex);
                    state->message = result.error;
                    if (!result.missing.empty()) state->message = "Missing: " + result.missing.front();
                    if (!result.mismatched.empty())
                        state->message = "Hash mismatch: " + result.mismatched.front();
                }
                state->complete = result.ok;
                state->working = false;
            });
        }
        auto fill = [&](const SDL_FRect& rect, uint8_t r, uint8_t g, uint8_t b) {
            SDL_SetRenderDrawColor(renderer, r, g, b, 255);
            SDL_RenderFillRect(renderer, &rect);
        };
        // Debug font glyphs are 8x8 before scaling.
        auto centered = [&](float y, const std::string& text, uint8_t r, uint8_t g, uint8_t b,
                            float scale = 1, float cx = 320) {
            SDL_SetRenderScale(renderer, scale, scale);
            SDL_SetRenderDrawColor(renderer, r, g, b, 255);
            SDL_RenderDebugText(renderer, (cx - text.size() * 4 * scale) / scale, y / scale, text.c_str());
            SDL_SetRenderScale(renderer, 1, 1);
        };
        auto button = [&](const SDL_FRect& rect, const char* label, bool primary) {
            fill(rect, primary ? 255 : 38, primary ? 196 : 41, primary ? 0 : 50);
            const uint8_t ink = primary ? 16 : 232;
            centered(rect.y + rect.h / 2 - 4, label, ink, ink, ink, 1, rect.x + rect.w / 2);
        };
        fill({0, 0, 640, 480}, 14, 15, 19);
        centered(168, "ROAD RASH", 255, 196, 0, 4);
        centered(226, "Import your 1997 Windows disc to begin.", 150, 156, 168);
#ifdef __ANDROID__
        button(iso_button, "Choose ISO", true);
#else
        button(iso_button, "Choose ISO  (I)", true);
        button(folder_button, "Choose folder  (F)", false);
#endif
        {
            std::lock_guard<std::mutex> lock(state->mutex);
            if (state->working) {
                const auto& p = state->progress;
                centered(340, "Verifying " + std::to_string(p.done) + " / " + std::to_string(p.total), 232,
                         230, 224);
                const float frac = p.total ? float(p.done) / float(p.total) : 0.f;
                fill({120, 360, 400, 4}, 38, 41, 50);
                fill({120, 360, 400.f * std::min(1.f, std::max(0.f, frac)), 4}, 255, 196, 0);
                centered(376, p.current.substr(0, 60), 110, 116, 128);
            } else if (state->dialog_pending)
                centered(340, "Waiting for the file picker...", 150, 156, 168);
            else if (!state->message.empty()) {
                centered(340, state->message.substr(0, 62), 255, 120, 100);
                centered(360, "Choose the correct disc to retry.", 110, 116, 128);
            }
        }
        if (!screenshot.empty()) {
            SDL_Surface* pixels = SDL_RenderReadPixels(renderer, nullptr);
            if (!pixels || !SDL_SaveBMP(pixels, screenshot.c_str())) {
                SDL_DestroySurface(pixels);
                state->cancel = true;
                if (worker.joinable()) worker.join();
                throw std::runtime_error(SDL_GetError());
            }
            SDL_DestroySurface(pixels);
        }
        SDL_RenderPresent(renderer);
        if (smoke || !screenshot.empty()) break;
        SDL_Delay(16);
    }
    state->cancel = true;
    if (worker.joinable()) worker.join();
    return state->complete;
}
} // namespace rr
