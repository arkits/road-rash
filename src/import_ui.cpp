#include "import_ui.hpp"
#include "system_fonts.hpp"
#include "importer.hpp"
#include "file_browser.hpp"
#include "input.hpp"
#include "platform_linux.hpp"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <map>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <tuple>
#include <vector>
// rr-game has no vendor include path; keep the vendored header's warnings
// out of strict builds (rr-resource gets the same via SYSTEM includes).
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wconversion"
#pragma GCC diagnostic ignored "-Wshadow"
#pragma GCC diagnostic ignored "-Wsign-compare"
#elif defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4244 4245 4267 4456 4457 4505 4701)
#endif
#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#include "../third_party/stb_truetype.h"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#elif defined(_MSC_VER)
#pragma warning(pop)
#endif
namespace rr {
namespace {
// The disc's own fonts are unavailable before import, so this screen uses a
// system face (Futura on macOS, like the original; see system_fonts.cpp) and
// falls back to SDL's debug font only when no candidate loads.
struct SystemFont {
    std::vector<uint8_t> bytes;
    stbtt_fontinfo info{};
    bool ok = false;
};
SystemFont load_font(const std::vector<FontCandidate>& candidates) {
    SystemFont font;
    for (const auto& c : candidates) {
        size_t size = 0;
        void* data = SDL_LoadFile(c.path.c_str(), &size);
        if (!data) continue;
        font.bytes.assign(static_cast<uint8_t*>(data), static_cast<uint8_t*>(data) + size);
        SDL_free(data);
        const int offset = stbtt_GetFontOffsetForIndex(font.bytes.data(), c.index);
        if (offset >= 0 && stbtt_InitFont(&font.info, font.bytes.data(), offset)) {
            font.ok = true;
            return font;
        }
    }
    font.bytes.clear();
    return font;
}
struct Fonts {
    SystemFont title, bold, body;
    Fonts()
        : title(load_font(system_font_candidates(FontWeight::title))),
          bold(load_font(system_font_candidates(FontWeight::bold))),
          body(load_font(system_font_candidates(FontWeight::body))) {}
};
// Anti-aliased white text rasterized at output resolution, tinted when drawn.
class TextCache {
  public:
    explicit TextCache(SDL_Renderer* renderer) : renderer_(renderer) {}
    ~TextCache() { clear(); }
    TextCache(const TextCache&) = delete;
    TextCache& operator=(const TextCache&) = delete;
    // Draws text centred on (cx, cy) in logical units (or starting at cx when
    // `left`); cy is the middle of the cap height. Returns false when the font
    // did not load.
    bool draw(const SystemFont& font, float size, float cx, float cy, const std::string& text, uint8_t r,
              uint8_t g, uint8_t b, bool left = false) {
        if (!font.ok || text.empty()) return font.ok;
        SDL_FRect present{};
        SDL_GetRenderLogicalPresentationRect(renderer_, &present);
        int lw = 0, lh = 0;
        SDL_RendererLogicalPresentation mode;
        SDL_GetRenderLogicalPresentation(renderer_, &lw, &lh, &mode);
        const float density = lh > 0 && present.h > 0 ? present.h / float(lh) : 1.f;
        const int px = std::max(1, int(std::lround(size * density)));
        if (cache_.size() > 64) clear();
        const auto key = std::make_tuple(&font, px, text);
        auto it = cache_.find(key);
        if (it == cache_.end()) it = cache_.emplace(key, render(font, px, text)).first;
        const Entry& e = it->second;
        if (!e.texture) return true;
        SDL_SetTextureColorMod(e.texture, r, g, b);
        const float x = left ? cx : cx - float(e.width) / density / 2;
        const SDL_FRect dst{std::round(x * density) / density,
                            std::round((cy + e.cap / density / 2 - e.baseline / density) * density) / density,
                            e.width / density, e.height / density};
        SDL_RenderTexture(renderer_, e.texture, nullptr, &dst);
        return true;
    }

  private:
    struct Entry {
        SDL_Texture* texture = nullptr;
        int width = 0, height = 0, baseline = 0, cap = 0;
    };
    Entry render(const SystemFont& font, int px, const std::string& text) {
        Entry e;
        const float scale = stbtt_ScaleForPixelHeight(&font.info, float(px));
        int ascent, descent, gap;
        stbtt_GetFontVMetrics(&font.info, &ascent, &descent, &gap);
        e.baseline = int(std::ceil(float(ascent) * scale));
        e.height = e.baseline + int(std::ceil(float(-descent) * scale));
        int x0, y0, x1, y1;
        e.cap = stbtt_GetCodepointBox(&font.info, 'H', &x0, &y0, &x1, &y1)
                    ? int(std::lround(float(y1) * scale))
                    : e.baseline * 7 / 10;
        float pen = 0;
        for (size_t i = 0; i < text.size(); ++i) {
            int advance, bearing;
            stbtt_GetCodepointHMetrics(&font.info, uint8_t(text[i]), &advance, &bearing);
            pen += float(advance) * scale;
            if (i + 1 < text.size())
                pen += scale * float(stbtt_GetCodepointKernAdvance(&font.info, uint8_t(text[i]),
                                                                   uint8_t(text[i + 1])));
        }
        e.width = int(std::ceil(pen)) + 2;
        std::vector<uint8_t> coverage(size_t(e.width) * size_t(e.height));
        pen = 1;
        for (size_t i = 0; i < text.size(); ++i) {
            const int ch = uint8_t(text[i]);
            const float shift = pen - std::floor(pen);
            stbtt_GetCodepointBitmapBoxSubpixel(&font.info, ch, scale, scale, shift, 0, &x0, &y0, &x1, &y1);
            const int gw = x1 - x0, gh = y1 - y0;
            if (gw > 0 && gh > 0) {
                std::vector<uint8_t> glyph(size_t(gw) * size_t(gh));
                stbtt_MakeCodepointBitmapSubpixel(&font.info, glyph.data(), gw, gh, gw, scale, scale, shift,
                                                  0, ch);
                for (int y = 0; y < gh; ++y)
                    for (int x = 0; x < gw; ++x) {
                        const int dx = int(std::floor(pen)) + x0 + x, dy = e.baseline + y0 + y;
                        if (dx < 0 || dy < 0 || dx >= e.width || dy >= e.height) continue;
                        auto& dst = coverage[size_t(dy) * size_t(e.width) + size_t(dx)];
                        dst = std::max(dst, glyph[size_t(y) * size_t(gw) + size_t(x)]);
                    }
            }
            int advance, bearing;
            stbtt_GetCodepointHMetrics(&font.info, ch, &advance, &bearing);
            pen += float(advance) * scale;
            if (i + 1 < text.size())
                pen += scale * float(stbtt_GetCodepointKernAdvance(&font.info, ch, uint8_t(text[i + 1])));
        }
        std::vector<uint32_t> rgba(coverage.size());
        for (size_t i = 0; i < coverage.size(); ++i) rgba[i] = 0x00ffffffu | (uint32_t(coverage[i]) << 24);
        SDL_Surface* surface =
            SDL_CreateSurfaceFrom(e.width, e.height, SDL_PIXELFORMAT_ARGB8888, rgba.data(), e.width * 4);
        if (surface) {
            e.texture = SDL_CreateTextureFromSurface(renderer_, surface);
            SDL_DestroySurface(surface);
        }
        if (e.texture) {
            SDL_SetTextureBlendMode(e.texture, SDL_BLENDMODE_BLEND);
            SDL_SetTextureScaleMode(e.texture, SDL_SCALEMODE_LINEAR);
        }
        return e;
    }
    void clear() {
        for (auto& [key, e] : cache_) SDL_DestroyTexture(e.texture);
        cache_.clear();
    }
    SDL_Renderer* renderer_;
    std::map<std::tuple<const SystemFont*, int, std::string>, Entry> cache_;
};
// Fonts and cached text shared by the import and loading screens. The
// renderer outlives both; textures are released with it at exit.
struct ScreenUI {
    Fonts fonts;
    TextCache cache;
    SDL_Renderer* renderer;
    explicit ScreenUI(SDL_Renderer* r) : cache(r), renderer(r) {}
    void fill(const SDL_FRect& rect, uint8_t r, uint8_t g, uint8_t b) {
        SDL_SetRenderDrawColor(renderer, r, g, b, 255);
        SDL_RenderFillRect(renderer, &rect);
    }
    // Text centred on (cx, cy), or starting at cx when `left`; SDL's 8x8
    // debug font stands in without a system face.
    void text(const SystemFont& font, float size, float cx, float cy, const std::string& str, uint8_t r,
              uint8_t g, uint8_t b, bool left = false) {
        if (cache.draw(font, size, cx, cy, str, r, g, b, left)) return;
        const float scale = std::max(1.f, std::round(size / 16.f));
        SDL_SetRenderScale(renderer, scale, scale);
        SDL_SetRenderDrawColor(renderer, r, g, b, 255);
        const float x = left ? cx : cx - float(str.size()) * 4 * scale;
        SDL_RenderDebugText(renderer, x / scale, (cy - 4 * scale) / scale, str.c_str());
        SDL_SetRenderScale(renderer, 1, 1);
    }
};
ScreenUI& screen_ui(SDL_Renderer* renderer) {
    static ScreenUI* ui = nullptr;
    if (!ui || ui->renderer != renderer) ui = new ScreenUI(renderer);
    return *ui;
}
struct ImportUI {
    std::mutex mutex;
    std::string selected, message;
    ImportProgress progress{};
    std::atomic<bool> dialog_pending{false}, dialog_failed{false}, working{false}, complete{false},
        cancel{false};
    // Plan 33a Task 5 Step 3: disc discovery runs on its own thread.
    std::vector<DiscCandidate> found;
    std::atomic<bool> discovery_done{false}, stop_discovery{false};
};
void SDLCALL selected_file(void* data, const char* const* files, int) {
    std::unique_ptr<std::shared_ptr<ImportUI>> owner(static_cast<std::shared_ptr<ImportUI>*>(data));
    const auto state = *owner;
    {
        std::lock_guard<std::mutex> lock(state->mutex);
        if (!files) {
            state->message = std::string("File selection failed: ") + SDL_GetError();
            state->dialog_failed = true;
        } else if (files[0]) {
            state->selected = files[0];
            state->message.clear();
        }
    }
    state->dialog_pending = false;
}
// Shortens from the left so the end of a path stays visible.
std::string tail(const std::string& s, size_t n) {
    return s.size() <= n ? s : "..." + s.substr(s.size() - (n - 3));
}
} // namespace
bool run_import_screen(SDL_Window* window, SDL_Renderer* renderer, const std::string& destination,
                       const std::string& screenshot, bool smoke, bool start_browser) {
    const auto state = std::make_shared<ImportUI>();
    std::thread worker, discovery;
    bool running = true;
    static const SDL_DialogFileFilter filter{"Disc image", "iso"};
    ScreenUI& ui = screen_ui(renderer);
    const Fonts& fonts = ui.fonts;
    // Plain centered layout on the 640x480 logical canvas; the same rects drive
    // painting and hit tests.
#ifdef __ANDROID__
    constexpr bool folder_buttons = false; // the system picker only; no file-system browser
    static const SDL_FRect buttons[] = {{200, 264, 240, 44}};
#else
    constexpr bool folder_buttons = true;
    static const SDL_FRect buttons[] = {{30, 264, 180, 44}, {230, 264, 180, 44}, {430, 264, 180, 44}};
#endif
    constexpr float row_top = 392, row_h = 22, list_top = 74, list_row = 24;
    constexpr int max_rows = 3, list_rows = 14;
    const auto roots = folder_buttons ? platform::browse_roots() : std::vector<platform::BrowseRoot>{};
    ImportMenu menu(roots, read_directory, folder_buttons);
    NavInput nav;
    GamepadPtr pad;
    {
        int count = 0;
        SDL_JoystickID* ids = SDL_GetGamepads(&count);
        if (ids && count > 0) pad.reset(SDL_OpenGamepad(ids[0]));
        SDL_free(ids);
    }
    const uint64_t started = SDL_GetTicks();
    if (folder_buttons && !smoke && screenshot.empty()) {
        // Name-based search of the browser's roots, capped at 2 s and
        // stopped as soon as the user acts.
        discovery = std::thread([state, roots, started] {
            auto found = discover_discs(roots, read_directory, [&] {
                return state->stop_discovery.load() || SDL_GetTicks() - started > 2000;
            });
            {
                std::lock_guard<std::mutex> lock(state->mutex);
                state->found = std::move(found);
            }
            state->discovery_done = true;
        });
    }
    bool candidates_shown = false;
    // Game Mode (gamescope): neither the portal nor zenity is known to appear
    // above the game, so the in-game browser is the default action.
    if (folder_buttons && (start_browser || platform::is_gamescope(platform::process_env())))
        menu.open_browser();
    uint64_t dialog_started = 0;
    bool dialog_fallback_done = false;
    auto inside = [](const SDL_FRect& r, float x, float y) {
        return x >= r.x && x <= r.x + r.w && y >= r.y && y <= r.y + r.h;
    };
    auto dialog = [&](bool folder) {
        if (state->working || state->dialog_pending) return;
        state->dialog_pending = true;
        state->dialog_failed = false;
        dialog_started = SDL_GetTicks();
        dialog_fallback_done = false;
        auto* owner = new std::shared_ptr<ImportUI>(state);
        if (folder)
            SDL_ShowOpenFolderDialog(selected_file, owner, window, nullptr, false);
        else
            SDL_ShowOpenFileDialog(selected_file, owner, window, &filter, 1, nullptr, false);
    };
    auto perform = [&](const ImportMenu::Action& a) {
        switch (a.kind) {
        case ImportMenu::Action::Kind::iso_dialog:
            dialog(false);
            break;
        case ImportMenu::Action::Kind::folder_dialog:
            dialog(true);
            break;
        case ImportMenu::Action::Kind::import_path: {
            std::lock_guard<std::mutex> lock(state->mutex);
            if (!state->working) state->selected = a.path;
            break;
        }
        case ImportMenu::Action::Kind::none:
            break;
        }
    };
    auto list_first = [&] {
        const int focus = menu.browser().focus(), count = int(menu.browser().items().size());
        return std::clamp(focus - list_rows / 2, 0, std::max(0, count - list_rows));
    };
    while (running && !state->complete) {
        const uint64_t now = SDL_GetTicks();
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT) running = false;
            if (track_gamepad(pad, event)) nav.release();
            const bool user_input =
                event.type == SDL_EVENT_KEY_DOWN || event.type == SDL_EVENT_MOUSE_BUTTON_DOWN ||
                event.type == SDL_EVENT_FINGER_DOWN || event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN;
            if (user_input) state->stop_discovery = true;
            if (event.type == SDL_EVENT_WINDOW_FOCUS_LOST) nav.release();
            const bool browsing = menu.view() == ImportMenu::View::browser;
            if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat && !browsing) {
                const auto key = event.key.key;
                if (key == SDLK_ESCAPE || key == SDLK_AC_BACK || key == SDLK_Q) {
                    running = false;
                    continue;
                }
                if (key == SDLK_I) perform(menu.press(ImportMenu::Button::iso));
                if (folder_buttons && key == SDLK_F) perform(menu.press(ImportMenu::Button::folder));
                if (folder_buttons && key == SDLK_B) perform(menu.press(ImportMenu::Button::browse));
            }
            if (event.type == SDL_EVENT_MOUSE_WHEEL && browsing)
                menu.browser().move(event.wheel.y > 0 ? -3 : event.wheel.y < 0 ? 3 : 0);
            if (event.type == SDL_EVENT_MOUSE_MOTION || event.type == SDL_EVENT_MOUSE_BUTTON_DOWN ||
                event.type == SDL_EVENT_FINGER_DOWN) {
                float x, y;
                if (event.type == SDL_EVENT_MOUSE_MOTION) {
                    x = event.motion.x;
                    y = event.motion.y;
                } else if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
                    x = event.button.x;
                    y = event.button.y;
                } else {
                    int w, h;
                    SDL_GetWindowSize(window, &w, &h);
                    x = event.tfinger.x * float(w);
                    y = event.tfinger.y * float(h);
                }
                float lx, ly;
                SDL_RenderCoordinatesFromWindow(renderer, x, y, &lx, &ly);
                const bool click = event.type != SDL_EVENT_MOUSE_MOTION;
                if (browsing) {
                    const int row = int(std::floor((ly - list_top) / list_row));
                    const int index = list_first() + row;
                    if (row >= 0 && row < list_rows && lx >= 20 && lx <= 620 &&
                        index < int(menu.browser().items().size())) {
                        menu.browser().set_focus(index);
                        if (click) perform(menu.nav(Nav::activate));
                    }
                } else {
                    for (int i = 0; i < menu.button_count(); ++i)
                        if (inside(buttons[i], lx, ly)) {
                            menu.set_focus(i);
                            if (click) perform(menu.press(ImportMenu::Button(i)));
                        }
                    const int shown = std::min(max_rows, int(menu.candidates().size()));
                    for (int i = 0; i < shown; ++i)
                        if (inside({120, row_top + float(i) * row_h, 400, row_h - 2}, lx, ly)) {
                            menu.set_focus(menu.button_count() + i);
                            if (click) perform(menu.choose_candidate(i));
                        }
                }
                nav.event(event, now);
                continue;
            }
            const Nav n = nav.event(event, now);
            if (n != Nav::none) perform(menu.nav(n));
        }
        if (const Nav n = nav.tick(now); n != Nav::none) perform(menu.nav(n));
        // Discovered discs appear once the search ends (or was stopped).
        if (!candidates_shown && state->discovery_done) {
            candidates_shown = true;
            std::lock_guard<std::mutex> lock(state->mutex);
            if (state->found.size() > size_t(max_rows)) state->found.resize(size_t(max_rows));
            menu.set_candidates(state->found);
        }
        // A failed dialog, or one with no answer after 3 s, opens the browser
        // as well; a selection the dialog delivers later is still used.
        if (folder_buttons && !dialog_fallback_done &&
            (state->dialog_failed || (state->dialog_pending && now - dialog_started > 3000))) {
            dialog_fallback_done = true;
            state->dialog_failed = false;
            menu.open_browser();
        }
        std::string selection;
        {
            std::lock_guard<std::mutex> lock(state->mutex);
            selection.swap(state->selected);
        }
        if (!selection.empty() && !state->working) {
            if (worker.joinable()) worker.join();
            state->stop_discovery = true;
            if (menu.view() == ImportMenu::View::browser) menu.close_browser();
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
        auto fill = [&](const SDL_FRect& rect, uint8_t r, uint8_t g, uint8_t b) { ui.fill(rect, r, g, b); };
        auto text = [&](const SystemFont& font, float size, float cy, const std::string& str, uint8_t r,
                        uint8_t g, uint8_t b, float cx = 320) { ui.text(font, size, cx, cy, str, r, g, b); };
        const bool pad_hints = nav.gamepad_last() || (pad && !SDL_HasKeyboard());
        fill({0, 0, 640, 480}, 14, 15, 19);
        if (menu.view() == ImportMenu::View::browser) {
            const FileBrowser& b = menu.browser();
            text(fonts.bold, 20, 26, "Choose your disc image or ROADRASH folder", 255, 196, 0);
            text(fonts.body, 13, 50, b.directory().empty() ? "Places" : tail(b.directory(), 70), 150, 156,
                 168);
            const auto& items = b.items();
            const int first = list_first();
            for (int row = 0; row < list_rows && first + row < int(items.size()); ++row) {
                const auto& item = items[size_t(first + row)];
                const bool focused = first + row == b.focus();
                const float y = list_top + float(row) * list_row;
                if (focused) fill({20, y, 600, list_row - 2}, 255, 196, 0);
                const uint8_t ink = focused ? 16 : item.kind == FileBrowser::Kind::iso ? 255 : 220;
                std::string label = item.label;
                if (item.kind == FileBrowser::Kind::root) label += "   " + tail(item.path, 44);
                const bool strong =
                    item.kind == FileBrowser::Kind::use_folder || item.kind == FileBrowser::Kind::iso;
                ui.text(strong ? fonts.bold : fonts.body, 15, 32, y + list_row / 2 - 1, tail(label, 64), ink,
                        ink,
                        focused                               ? 16
                        : item.kind == FileBrowser::Kind::iso ? 196
                                                              : 220,
                        true);
            }
            if (items.empty())
                ui.text(fonts.body, 15, 32, list_top + 12, "No folders or disc images here", 150, 156, 168,
                        true);
            if (!b.error().empty()) text(fonts.body, 14, 430, tail(b.error(), 62), 255, 120, 100);
            text(fonts.body, 14, 458,
                 pad_hints ? "A open    B back    Y places    LB/RB page"
                           : "Enter open    Esc back    Page Up/Down    click to open",
                 110, 116, 128);
        } else {
            auto button = [&](int i, const char* label, bool primary) {
                const SDL_FRect& rect = buttons[i];
                const bool focused = menu.focus() == i;
                if (focused) fill({rect.x - 3, rect.y - 3, rect.w + 6, rect.h + 6}, 255, 255, 255);
                fill(rect, primary ? 255 : 38, primary ? 196 : 41, primary ? 0 : 50);
                const uint8_t ink = primary ? 16 : 232;
                text(fonts.bold, 17, rect.y + rect.h / 2, label, ink, ink, ink, rect.x + rect.w / 2);
            };
            text(fonts.title, 84, 172, "ROAD RASH", 255, 196, 0);
            text(fonts.body, 18, 234, "Import your 1997 Windows disc to begin.", 150, 156, 168);
#ifdef __ANDROID__
            button(0, "Choose ISO", true);
#else
            button(0, pad_hints ? "Choose ISO" : "Choose ISO  (I)", true);
            button(1, pad_hints ? "Choose folder" : "Choose folder  (F)", false);
            button(2, pad_hints ? "Browse" : "Browse  (B)", false);
#endif
            std::lock_guard<std::mutex> lock(state->mutex);
            if (state->working) {
                const auto& p = state->progress;
                text(fonts.body, 16, 336,
                     "Verifying " + std::to_string(p.done) + " / " + std::to_string(p.total), 232, 230, 224);
                const float frac = p.total ? float(p.done) / float(p.total) : 0.f;
                fill({120, 352, 400, 4}, 38, 41, 50);
                fill({120, 352, 400.f * std::min(1.f, std::max(0.f, frac)), 4}, 255, 196, 0);
                text(fonts.body, 13, 370, p.current.substr(0, 60), 110, 116, 128);
            } else {
                if (state->dialog_pending)
                    text(fonts.body, 16, 336, "Waiting for the file picker...", 150, 156, 168);
                else if (!state->message.empty()) {
                    text(fonts.body, 16, 336, state->message.substr(0, 62), 255, 120, 100);
                    text(fonts.body, 13, 354, "Choose the correct disc to retry.", 110, 116, 128);
                }
                const auto& found = menu.candidates();
                if (!found.empty()) {
                    text(fonts.body, 13, row_top - 12, "Found on this computer:", 150, 156, 168);
                    for (int i = 0; i < int(found.size()) && i < max_rows; ++i) {
                        const float y = row_top + float(i) * row_h;
                        const bool focused = menu.focus() == menu.button_count() + i;
                        fill({120, y, 400, row_h - 2}, focused ? 255 : 38, focused ? 196 : 41,
                             focused ? 0 : 50);
                        const uint8_t ink = focused ? 16 : 232;
                        text(fonts.body, 13, y + row_h / 2 - 1, tail(found[size_t(i)].label, 58), ink, ink,
                             ink);
                    }
                }
            }
            if (folder_buttons)
                text(fonts.body, 13, 466,
                     pad_hints ? "D-pad: move    A: choose" : "Arrows and Enter, or click", 90, 96, 108);
        }
        if (!screenshot.empty()) {
            SDL_Surface* pixels = SDL_RenderReadPixels(renderer, nullptr);
            if (!pixels || !SDL_SaveBMP(pixels, screenshot.c_str())) {
                SDL_DestroySurface(pixels);
                state->cancel = true;
                state->stop_discovery = true;
                if (worker.joinable()) worker.join();
                if (discovery.joinable()) discovery.join();
                throw std::runtime_error(SDL_GetError());
            }
            SDL_DestroySurface(pixels);
        }
        SDL_RenderPresent(renderer);
        if (smoke || !screenshot.empty()) break;
        SDL_Delay(16);
    }
    state->cancel = true;
    state->stop_discovery = true;
    if (worker.joinable()) worker.join();
    if (discovery.joinable()) discovery.join();
    return state->complete;
}
void draw_loading_screen(SDL_Renderer* renderer, const char* stage) {
    ScreenUI& ui = screen_ui(renderer);
    int lw = 0, lh = 0;
    SDL_RendererLogicalPresentation mode;
    if (!SDL_GetRenderLogicalPresentation(renderer, &lw, &lh, &mode) || lw <= 0 || lh <= 0) {
        lw = 640;
        lh = 480;
    }
    const float cx = float(lw) / 2.f, mid = float(lh) / 2.f;
    SDL_SetRenderDrawColor(renderer, 14, 15, 19, 255);
    SDL_RenderClear(renderer);
    ui.text(ui.fonts.title, 84, cx, mid - 40, "ROAD RASH", 255, 196, 0);
    ui.text(ui.fonts.body, 18, cx, mid + 22, stage, 150, 156, 168);
    ui.fill({cx - 140, mid + 44, 280, 4}, 38, 41, 50);
    ui.fill({cx - 140 + float((SDL_GetTicks() / 4) % 224), mid + 44, 56, 4}, 255, 196, 0);
    SDL_RenderPresent(renderer);
}
} // namespace rr
