#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_JPEG
#define STBI_NO_STDIO
#include "stb_image.h"
#include "road.hpp"
#include "assets.hpp"
#include "road_draw.hpp"
#include <memory>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

struct Screen {
    const char* path;
    int width;
    int height;
};
// BOB dimensions recovered from 0x0044c5d0 / 0x0044c440 and checked against file sizes.
static const Screen screens[] = {{"SPLASH.RRI", 0, 0},           {"MAIN.RRI", 0, 0},
                                 {"OLLEYS.RRI", 0, 0},           {"PANZER.RRI", 0, 0},
                                 {"STREET.RRI", 0, 0},           {"CREDITS.RRI", 0, 0},
                                 {"BIGMETER.BOB", 190, 64},      {"HORIZONS/NAPA.BOB", 720, 320},
                                 {"HORIZONS/CITY.BOB", 720, 320}};
static constexpr int image_count = sizeof(screens) / sizeof(screens[0]);
static const char* const courses[] = {"NAPA", "CITY", "HIWAY", "CANYN", "MEDLY"};
static constexpr int screen_count = image_count + sizeof(courses) / sizeof(courses[0]);

static SDL_Surface* load_image(const rr::Assets& assets, const Screen& screen) {
    const std::string path = std::string("IMAGES/") + screen.path;
    auto bytes = assets.read(path);
    if (screen.width) {
        auto palette = assets.read("DATA/PALETTE.RAW");
        if (bytes.size() != size_t(screen.width * screen.height) || palette.size() != 1024)
            throw std::runtime_error("Invalid indexed BOB image or palette size");
        // Original startup (0x00448f90) forces palette entry zero to black.
        palette[0] = palette[1] = palette[2] = 0;
        SDL_Surface* surface = SDL_CreateSurface(screen.width, screen.height, SDL_PIXELFORMAT_RGBA32);
        if (!surface) throw std::runtime_error(SDL_GetError());
        for (int y = 0; y < screen.height; ++y) {
            auto row = static_cast<uint8_t*>(surface->pixels) + y * surface->pitch;
            for (int x = 0; x < screen.width; ++x) {
                const size_t color = 4 * bytes[y * screen.width + x];
                for (int c = 0; c < 3; ++c) row[x * 4 + c] = palette[color + c];
                row[x * 4 + 3] = 255;
            }
        }
        return surface;
    }
    if (bytes.size() > INT_MAX) throw std::runtime_error("Image too large");
    int w, h, channels;
    stbi_uc* pixels = stbi_load_from_memory(bytes.data(), int(bytes.size()), &w, &h, &channels, 4);
    if (!pixels) throw std::runtime_error(path + ": JPEG decode failed");
    SDL_Surface* borrowed = SDL_CreateSurfaceFrom(w, h, SDL_PIXELFORMAT_RGBA32, pixels, w * 4);
    SDL_Surface* owned = borrowed ? SDL_DuplicateSurface(borrowed) : nullptr;
    SDL_DestroySurface(borrowed);
    stbi_image_free(pixels);
    if (!owned) throw std::runtime_error(SDL_GetError());
    return owned;
}

struct RoadView {
    rr::Course course;
    std::vector<rr::RouteSample> route;
    bool second_branch = false;
    uint32_t position = 0;
};

int main(int argc, char** argv) {
    std::string root = "game";
    bool smoke = false;
    int initial_screen = 0;
    uint32_t initial_position = 0;
    std::string screenshot;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--assets" && i + 1 < argc)
            root = argv[++i];
        else if (arg == "--smoke")
            smoke = true;
        else if (arg == "--screenshot" && i + 1 < argc)
            screenshot = argv[++i];
        else if (arg == "--screen" && i + 1 < argc) {
            char* end = nullptr;
            long value = std::strtol(argv[++i], &end, 10);
            if (*end || value < 0 || value >= screen_count) return 2;
            initial_screen = int(value);
        } else if (arg == "--position" && i + 1 < argc)
            initial_position = uint32_t(std::strtoul(argv[++i], nullptr, 10)) << 8;
        else {
            std::fprintf(stderr,
                         "Usage: rr-viewer [--assets ROADRASH_DIR] [--smoke] [--screen 0..%d] [--position "
                         "SAMPLE] [--screenshot FILE.bmp]\n",
                         screen_count - 1);
            return 2;
        }
    }
    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;
    SDL_Texture* texture = nullptr;
    int result = 0;
    try {
        const rr::Assets assets(root);
        auto entries = rr::read_resources(assets.read("DATA/BIKESPEC.RSC"));
        std::printf("Original bike resource directory: %zu entries\n", entries.size());
        const auto table = rr::read_depth_table(assets.read("ROADRASH.EXE"));
        std::unique_ptr<RoadView> road;
        if (!SDL_Init(SDL_INIT_VIDEO)) throw std::runtime_error(SDL_GetError());
        if (!SDL_CreateWindowAndRenderer("Road Rash native research viewer", 960, 760, SDL_WINDOW_RESIZABLE,
                                         &window, &renderer))
            throw std::runtime_error(SDL_GetError());
        SDL_SetRenderLogicalPresentation(renderer, 640, 510, SDL_LOGICAL_PRESENTATION_LETTERBOX);
        SDL_SetRenderVSync(renderer, 1);
        int screen = initial_screen, loaded = -1, frames = 0, speed = 64;
        bool running = true;
        while (running) {
            SDL_Event e;
            while (SDL_PollEvent(&e)) {
                if (e.type == SDL_EVENT_QUIT) running = false;
                if (e.type == SDL_EVENT_KEY_DOWN && !e.key.repeat) {
                    if (e.key.key == SDLK_ESCAPE) running = false;
                    if (e.key.key == SDLK_RIGHT || e.key.key == SDLK_SPACE)
                        screen = (screen + 1) % screen_count;
                    if (e.key.key == SDLK_LEFT) screen = (screen + screen_count - 1) % screen_count;
                    if (e.key.key == SDLK_UP) speed = SDL_min(speed + 32, 512);
                    if (e.key.key == SDLK_DOWN) speed = SDL_max(speed - 32, -512);
                    if (e.key.key == SDLK_B && road) {
                        road->second_branch = !road->second_branch;
                        road->route = rr::make_route(road->course, road->second_branch);
                        road->position = SDL_min(road->position, uint32_t(road->route.size() - 1) << 8);
                    }
                }
                if (e.type == SDL_EVENT_FINGER_UP) screen = (screen + 1) % screen_count;
                if (e.type == SDL_EVENT_MOUSE_BUTTON_UP && e.button.which != SDL_TOUCH_MOUSEID)
                    screen = (screen + 1) % screen_count;
                if (e.type == SDL_EVENT_RENDER_DEVICE_RESET) loaded = -1;
            }
            if (screen != loaded && screen >= image_count) {
                const std::string name = courses[screen - image_count];
                road = std::make_unique<RoadView>();
                road->course = rr::read_course(assets.read("DATA/COURSES/" + name + ".CRS"));
                road->route = rr::make_route(road->course, false);
                road->position = SDL_min(initial_position, uint32_t(road->route.size() - 1) << 8);
                std::printf("Decoded %s.CRS: %zu route samples\n", name.c_str(), road->route.size());
                loaded = screen;
            }
            if (screen != loaded) {
                road.reset();
                SDL_Surface* surface = load_image(assets, screens[screen]);
                SDL_DestroyTexture(texture);
                texture = SDL_CreateTextureFromSurface(renderer, surface);
                std::printf("Decoded %s: %dx%d\n", screens[screen].path, surface->w, surface->h);
                SDL_DestroySurface(surface);
                if (!texture) throw std::runtime_error(SDL_GetError());
                loaded = screen;
            }
            SDL_SetRenderDrawColor(renderer, 15, 18, 22, 255);
            SDL_RenderClear(renderer);
            if (road) {
                rr::draw_road(renderer, rr::project_road(road->route, road->position, {}, table));
                const uint32_t end = uint32_t(road->route.size() - 1) << 8;
                const int64_t next = int64_t(road->position) + speed;
                road->position = next < 0 ? end : next > end ? 0 : uint32_t(next);
                char line[96];
                SDL_snprintf(line, sizeof line, "%s.CRS  sample %u/%zu  branch %c",
                             courses[screen - image_count], road->position >> 8, road->route.size(),
                             road->second_branch ? 'B' : 'A');
                SDL_SetRenderDrawColor(renderer, 235, 235, 235, 255);
                SDL_RenderDebugText(renderer, 8, 483, line);
                SDL_RenderDebugText(renderer, 8, 497,
                                    "Original road geometry, placeholder shading. Up/Down speed, B branch");
            } else {
                float w, h;
                SDL_GetTextureSize(texture, &w, &h);
                float scale = SDL_min(640.f / w, 480.f / h);
                SDL_FRect dst = {(640.f - w * scale) / 2, (480.f - h * scale) / 2, w * scale, h * scale};
                SDL_RenderTexture(renderer, texture, nullptr, &dst);
                SDL_SetRenderDrawColor(renderer, 235, 235, 235, 255);
                SDL_RenderDebugText(renderer, 8, 483, "NATIVE ASSET VIEWER - gameplay not yet ported");
                SDL_RenderDebugText(renderer, 8, 497, "Arrows / click / tap: next screen    Esc: exit");
            }
            if (!screenshot.empty() && frames == 0) {
                SDL_Surface* capture = SDL_RenderReadPixels(renderer, nullptr);
                if (!capture) throw std::runtime_error(SDL_GetError());
                bool ok = SDL_SaveBMP(capture, screenshot.c_str());
                SDL_DestroySurface(capture);
                if (!ok) throw std::runtime_error(SDL_GetError());
                running = false;
            }
            SDL_RenderPresent(renderer);
            ++frames;
            if (smoke) {
                if (frames == screen_count)
                    running = false;
                else
                    screen = (screen + 1) % screen_count;
            }
            SDL_Delay(16);
        }
    } catch (const std::exception& e) {
        SDL_Log("%s", e.what());
        result = 1;
    }
    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return result;
}
