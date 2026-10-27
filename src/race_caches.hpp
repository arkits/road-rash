// Plan 28 Task 4: race sprite cache + course canvas (moved from game_main.cpp).
#pragma once
#include <SDL3/SDL.h>
#include <cstdlib>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>
#include "actor_nodes.hpp"
#include "display.hpp"
#include "fog.hpp"
#include "framebuffer.hpp"
#include "framebuffer_rgb.hpp"
#include "frame_profile.hpp"
#include "game_types.hpp"
#include "lifecycle.hpp"
#include "post_chain.hpp"
#include "render_threads.hpp"
#include "road_draw.hpp"
#include "sdl_handles.hpp"
#include "upscale_cache.hpp"
#include "raster_rgb.hpp"
#include <filesystem>
#include <string>
#include "view_state.hpp"

namespace rr {
std::string pref_directory(); // game_main.cpp; plan 20 Task 3 cache root
struct SpriteCache {
    std::vector<TexturePtr> textures;
    void clear() { textures.clear(); }
    ~SpriteCache() { clear(); }
    SDL_Texture* get(SDL_Renderer* r, const Game& g, int frame, int rider) {
        if (textures.empty()) textures.resize(g.bobs.size() * g.remaps.tables.size());
        TexturePtr& t = textures.at(size_t(rider) * g.bobs.size() + size_t(frame));
        if (t) return t.get();
        const Image8 img = remap(g.bobs.at(size_t(frame)), g.remaps, rider);
        if (!img.width || !img.height) return nullptr;
        std::vector<uint8_t> rgba(size_t(img.width) * img.height * 4);
        for (size_t i = 0; i < img.pixels.size(); ++i) {
            const uint8_t* c = &g.palette.rgbx[4 * img.pixels[i]];
            rgba[4 * i] = c[0];
            rgba[4 * i + 1] = c[1];
            rgba[4 * i + 2] = c[2];
            rgba[4 * i + 3] = img.pixels[i] ? 255 : 0; // index 0 transparent
        }
        SurfacePtr s(
            SDL_CreateSurfaceFrom(img.width, img.height, SDL_PIXELFORMAT_RGBA32, rgba.data(), img.width * 4));
        t.reset(s ? SDL_CreateTextureFromSurface(r, s.get()) : nullptr);
        if (t) SDL_SetTextureScaleMode(t.get(), crisp_scale);
        return t.get();
    }
};

struct CourseCanvas {
    Framebuffer frame{640, 480};
    std::vector<uint8_t> rgba = std::vector<uint8_t>(640 * 480 * 4);
    int scale_w = 640, scale_h = 480; // plan 19 Task 4: 640s x 480s surface
    int wide_w = 320;                 // plan 21 Task 8: logical width of the surface
    TexturePtr texture;
    FrameProfile* profile = nullptr;   // --bench phase timings (null: off)
    bool texture_is_rgb = false;       // plan 21 Task 4: RGB path needs ARGB8888 upload
    SDL_ScaleMode scale = crisp_scale; // plan 18 Task 3: from DisplayDriver (filter setting)
    ~CourseCanvas() { clear(); }
    void clear() { texture.reset(); }
    void resize(int s, int logical_width = 320) {
        if (s < 1 || s > 4) throw std::runtime_error("Render scale out of range 1..4");
        if (logical_width * 2 * s == scale_w && 480 * s == scale_h) return;
        wide_w = logical_width;
        scale_w = logical_width * 2 * s;
        scale_h = 480 * s;
        frame = Framebuffer(scale_w, scale_h);
        rgba.assign(size_t(scale_w) * size_t(scale_h) * 4, 0);
        clear();
    }

    std::unique_ptr<RenderThreads> workers;
    int worker_count = 0;
    FramebufferRgb rgb;                // plan 21 Task 4: full-colour frame (resized with frame)
    PostChain post_chain;              // plan 21 Task 7: per-race post history
    std::vector<uint32_t> grade_table; // grade_lut(grade_for), rebuilt on change
    Grade grade_for = Grade::none;
    // Review 2026-10-13 item 2: cached RGB555 -> palette-index LUT for the
    // dashboard map (replaces the per-pixel 255-entry search). Reset when
    // the palette generation changes.
    DashLut dash_lut;
    uint32_t dash_lut_gen = 0;
    uint8_t dash_index(uint32_t c, const Palette& pal) { return dash_lut.index(c, pal); }
    // Presented palette: the course palette through a race fade/countdown
    // channel map (hud.hpp ChannelLut), cached per palette generation + map.
    Palette shown;
    uint32_t shown_gen = 0;
    ChannelLut shown_lut{};
    bool shown_ready = false;
    const Palette& shown_palette(const Palette& pal, const ChannelLut& lut) {
        const uint32_t gen = palette_generation(pal);
        if (!shown_ready || gen != shown_gen || lut != shown_lut) {
            shown = pal;
            for (size_t i = 0; i < 256; ++i)
                for (size_t c = 0; c < 3; ++c) shown.rgbx[4 * i + c] = lut[pal.rgbx[4 * i + c]];
            shown_gen = gen;
            shown_lut = lut;
            shown_ready = true;
        }
        return shown;
    }
    // Plan 32 E: every horizon pixel is converted, including the zero-filled
    // lower rows. Storage is retained across frames and rebuilt only on resize.
    void draw_horizon_rgb(const CourseScene& scene, const Palette& palette, int32_t scroll,
                          int32_t logical_width) {
        if (rgb.width() != scale_w || rgb.height() != scale_h) rgb.resize(scale_w, scale_h);
        scene.draw_horizon(frame, scroll, logical_width);
        uint32_t argb[256];
        for (int i = 0; i < 256; ++i) {
            const uint8_t* c = &palette.rgbx[4 * i];
            argb[i] = 0xff000000u | (uint32_t(c[0]) << 16) | (uint32_t(c[1]) << 8) | uint32_t(c[2]);
        }
        rows([&](int y) {
            const uint8_t* src = frame.row(y);
            uint32_t* dst = rgb.row(y);
            for (int x = 0; x < scale_w; ++x) dst[x] = argb[src[x]];
        });
    }
    // Row-parallel pass over the surface on the render pool (each row is
    // independent, so the result equals a single-threaded loop).
    template <class F> void rows(F&& fn) {
        if (!workers || workers->count() <= 1) {
            for (int y = 0; y < scale_h; ++y) fn(y);
            return;
        }
        workers->run(workers->count(), scale_h, [&](int, int top, int bottom) {
            for (int y = top; y <= bottom; ++y) fn(y);
        });
    }
    void draw(SDL_Renderer* renderer, const Game& game, const std::vector<RoadSlice>& slices, int s,
              int threads) {
        const int32_t wide = game.logical_width();
        resize(s, wide);
        // Hor+ (plan 21 Task 8): road slices (project_road) and every scene
        // builder centre at wide/2, so nodes are already in wide coordinates;
        // the 4:3 picture is the centre crop. The margin only places the 4:3
        // dashboard. Shifting nodes here as well moved everything but the
        // road surface right by a margin and broke terrain subdivision seams.
        const int32_t margin = (wide - 320) / 2;
        // Plan 19 Task 8: the dashboard composes at the race scale (Task 5).
        // The quick-race boot path has no HUD (dashboard is a career-mode
        // surface); without one the scene stands alone as in Task 4 shots.
        // Review 2026-10-13 item 2: compose once per frame; the indexed
        // branch maps through the cached LUT, the RGB branch uses it directly.
        // Plan 21 Task 4: the game uses the RGB path when full_colour is on
        // or filter != nearest; otherwise the indexed path (bit-identical).
        const bool use_rgb = game.enhancements.full_colour ||
                             game.enhancements.filter != rr::TextureFilter::nearest ||
                             game.enhancements.fog > 0 || game.enhancements.grade != Grade::none;
        // 0x0044c780 / 0x00446c30 countdown presentation (hud.hpp): no
        // dashboard on the start-value frame, the digit inside the dashboard
        // draw, and the half-brightness palette until the timer expires.
        const CountdownBanner banner = countdown_banner(game.countdown, countdown_ticks);
        const bool need_dash = game.assets.hud != nullptr && banner.dashboard;
        // A RaceScreen fade (0x00415d90 / 0x00415e90) overrides the
        // countdown's half palette while it runs.
        // 0x004431d0 uploads the half palette while its message is up (the
        // countdown already shows it) and the message replaces the digit.
        const bool message = !game.banner_message.empty();
        const bool dim = banner.dim || message;
        const bool remap = game.fade_lut.has_value() || dim;
        const ChannelLut lut = game.fade_lut ? *game.fade_lut : dim ? half_palette_lut() : identity_lut();
        std::optional<ProfileScope> dash_scope(std::in_place, profile, FrameProfile::dash);
        RgbImage dash = need_dash
                            ? game.assets.hud->compose(
                                  game.assets.hud->state(game.race(), game.player_name, game.rider_names),
                                  game.assets.physics, rr::DashMode::full, s)
                            : RgbImage(0, 0, 0);
        if (need_dash && message)
            game.assets.hud->draw_message(dash, game.banner_message, s);
        else if (need_dash)
            game.assets.hud->draw_countdown(dash, banner.text, s);
        dash_scope.reset();
        if (!workers || worker_count != threads) {
            workers = std::make_unique<RenderThreads>(threads);
            worker_count = threads;
        }
        // Plan 20 Task 3: lazy attach/detach on detail flips (load_course runs
        // before the settings sync, so attach here where the setting is live).
        if (game.texture_detail && !game.upscaled_attached && game.scene()) {
            UpscaleCache cache((std::filesystem::path(pref_directory()) / "upscaled").string());
            game.assets.scene->attach_upscaled(cache);
            game.upscaled_attached = true;
        } else if (!game.texture_detail && game.upscaled_attached && game.scene()) {
            game.assets.scene->clear_upscaled();
            game.upscaled_attached = false;
        }
        std::optional<ProfileScope> nodes_scope(std::in_place, profile, FrameProfile::scene_nodes);
        auto nodes = game.assets.scene->draw_nodes(
            game.race().route(), slices, game.race().camera(), game.depth,
            [&](const rr::RoadSlice& near, const rr::RoadSlice& far, int road, int32_t ceiling, bool back) {
                return game.rider_nodes(near, far, road, ceiling, back);
            },
            wide);
        nodes_scope.reset();
        if (use_rgb) {
            // Reuse the indexed canvas as horizon scratch; the RGB path
            // never composites a dashboard into that buffer.
            {
                ProfileScope ps(profile, FrameProfile::horizon);
                draw_horizon_rgb(*game.assets.scene, game.palette, game.race().horizon_scroll(), wide);
            }
            // Plan 21 Task 5: fog colour from the horizon's own bottom rows
            // (user's art, computed at runtime); sky/horizon exempt from fog.
            const uint32_t fog_rgb = fog_colour(rgb.row(0), scale_w, scale_h);
            {
                ProfileScope ps(profile, FrameProfile::raster);
                render_nodes_rgb_fog(rgb, nodes, view_mode_wide(s, wide), 16, game.palette, game.enhancements,
                                     *workers, fog_rgb);
            }
            // Grade post-pass (identity when grade == none).
            if (game.enhancements.grade != Grade::none) {
                ProfileScope ps(profile, FrameProfile::grade);
                if (grade_for != game.enhancements.grade) {
                    grade_table = grade_lut(game.enhancements.grade);
                    grade_for = game.enhancements.grade;
                }
                const auto& lut = grade_table;
                rows([&](int y) {
                    // Runs of one colour are common (sky, flat road): reuse
                    // the previous result for an identical input pixel.
                    uint32_t last_in = 0, last_out = 0;
                    bool have = false;
                    uint32_t* row = rgb.row(y);
                    for (int x = 0; x < scale_w; ++x) {
                        const uint32_t p = row[x];
                        if (!(p >> 24)) continue;
                        const uint32_t c = p & 0xffffffu;
                        if (!have || c != last_in) {
                            last_in = c;
                            last_out = grade_apply(lut, c);
                            have = true;
                        }
                        row[x] = (p & 0xff000000u) | last_out;
                    }
                });
            }
            // Plan 21 Task 7: CPU post chain (motion blur -> bloom -> vignette).
            // Speed from the race's speedometer state (presentation read of the
            // drawn tick, never a sim write); pause/teleport resets history.
            post_chain.configure(game.enhancements);
            if (post_chain.any()) {
                ProfileScope ps(profile, FrameProfile::post);
                const float speed = float(std::abs(game.race().speed()));
                post_chain.apply(reinterpret_cast<uint32_t*>(rgb.row(0)), scale_w, scale_h, speed,
                                 game.post_teleported, 20000.0f, workers.get());
                game.post_teleported = false;
            }
            // Dashboard composited directly (no palette round-trip; the
            // once-per-frame image from above).
            if (need_dash) {
                ProfileScope ps(profile, FrameProfile::dash);
                const int xoff = margin * 2 * s;
                const int panel_w = margin * 2 * s;
                for (int y = 0; y < dash.height && y < scale_h; ++y)
                    for (int x = 0; x < dash.width && x + xoff < scale_w; ++x) {
                        const uint32_t c = dash.at(x, y);
                        if (c == 0x01000000) continue;
                        rgb.row(y)[x + xoff] = 0xff000000u | (c & 0xffffffu);
                    }
                // Full-width side panels from darkened edge columns.
                if (margin > 0) {
                    for (int y = 0; y < dash.height && y < scale_h; ++y) {
                        const uint32_t lc = dash.at(0, y), rc = dash.at(dash.width - 1, y);
                        if (lc == 0x01000000 && rc == 0x01000000) continue;
                        for (int x = 0; x < panel_w; ++x) {
                            if (lc != 0x01000000) {
                                const uint8_t r = uint8_t(((lc >> 16) & 0xff) / 2),
                                              g = uint8_t(((lc >> 8) & 0xff) / 2),
                                              b = uint8_t((lc & 0xff) / 2);
                                rgb.row(y)[x] = 0xff000000u | (uint32_t(r) << 16) | (uint32_t(g) << 8) | b;
                            }
                            if (rc != 0x01000000) {
                                const uint8_t r = uint8_t(((rc >> 16) & 0xff) / 2),
                                              g = uint8_t(((rc >> 8) & 0xff) / 2),
                                              b = uint8_t((rc & 0xff) / 2);
                                rgb.row(y)[xoff + dash.width + x] =
                                    0xff000000u | (uint32_t(r) << 16) | (uint32_t(g) << 8) | b;
                            }
                        }
                    }
                }
            }
            // The palette upload applies to the whole presented frame.
            if (remap) {
                ProfileScope ps(profile, FrameProfile::remap);
                rows([&](int y) {
                    uint32_t* row = rgb.row(y);
                    for (int x = 0; x < scale_w; ++x) {
                        const uint32_t p = row[x];
                        row[x] = (p & 0xff000000u) | (uint32_t(lut[(p >> 16) & 0xff]) << 16) |
                                 (uint32_t(lut[(p >> 8) & 0xff]) << 8) | lut[p & 0xff];
                    }
                });
            }
        } else {
            {
                ProfileScope ps(profile, FrameProfile::horizon);
                game.assets.scene->draw_horizon(frame, game.race().horizon_scroll(), wide);
            }
            // The dashboard stays a centred 4:3 image (plan 21 Task 8); side
            // panels are drawn from its edge columns below.
            {
                ProfileScope ps(profile, FrameProfile::raster);
                rr::render_nodes_threaded(frame, nodes, view_mode_wide(s, wide), 16, *workers,
                                          game.enhancements.perspective);
            }
            if (need_dash) {
                // Plan 19 Task 8: the dashboard composes at the race scale. Its
                // RgbImage pixels are palette-mapped into the indexed frame.
                // Centred 4:3 dashboard (plan 21 Task 8): offset by the side
                // margin so it sits in the middle of the wide frame.
                const int xoff = margin * 2 * s;
                const uint32_t gen = palette_generation(game.palette);
                if (gen != dash_lut_gen) {
                    dash_lut.reset();
                    dash_lut_gen = gen;
                }
                for (int y = 0; y < dash.height && y < frame.height(); ++y)
                    for (int x = 0; x < dash.width && x + xoff < frame.width(); ++x) {
                        const uint32_t c = dash.at(x, y);
                        if (c == 0x01000000) continue;
                        frame.row(y)[x + xoff] = dash_index(c, game.palette);
                    }
                // Side panels: stretched + darkened edge columns (generated at
                // runtime from the user's own dash art, no shipped assets).
                // Full-width margins on both sides (not half width).
                if (margin > 0) {
                    const int panel_w = margin * 2 * s;
                    for (int y = 0; y < dash.height && y < frame.height(); ++y) {
                        const uint32_t lc = dash.at(0, y), rc = dash.at(dash.width - 1, y);
                        if (lc == 0x01000000 && rc == 0x01000000) continue;
                        for (int x = 0; x < panel_w; ++x) {
                            if (lc != 0x01000000) {
                                const int r = int((lc >> 16) & 0xff) / 2, gg = int((lc >> 8) & 0xff) / 2,
                                          bb = int(lc & 0xff) / 2;
                                const uint32_t dark =
                                    (uint32_t(r) << 16) | (uint32_t(gg) << 8) | uint32_t(bb);
                                frame.row(y)[x] = dash_index(dark, game.palette);
                            }
                            if (rc != 0x01000000) {
                                const int r = int((rc >> 16) & 0xff) / 2, gg = int((rc >> 8) & 0xff) / 2,
                                          bb = int(rc & 0xff) / 2;
                                const uint32_t dark =
                                    (uint32_t(r) << 16) | (uint32_t(gg) << 8) | uint32_t(bb);
                                frame.row(y)[xoff + dash.width + x] = dash_index(dark, game.palette);
                            }
                        }
                    }
                }
            }
            ProfileScope ps(profile, FrameProfile::remap);
            frame.to_rgba(remap ? shown_palette(game.palette, lut) : game.palette, rgba.data(), scale_w * 4);
        }
        ProfileScope upload_scope(profile, FrameProfile::upload);
        if (!texture || texture_is_rgb != use_rgb) {
            texture.reset(SDL_CreateTexture(renderer,
                                            use_rgb ? SDL_PIXELFORMAT_ARGB8888 : SDL_PIXELFORMAT_RGBA32,
                                            SDL_TEXTUREACCESS_STREAMING, scale_w, scale_h));
            if (!texture) throw std::runtime_error(SDL_GetError());
            SDL_SetTextureScaleMode(texture.get(), scale);
            texture_is_rgb = use_rgb;
        }
        if (use_rgb) {
            if (!SDL_UpdateTexture(texture.get(), nullptr, rgb.row(0), scale_w * 4))
                throw std::runtime_error(SDL_GetError());
        } else if (!SDL_UpdateTexture(texture.get(), nullptr, rgba.data(), scale_w * 4))
            throw std::runtime_error(SDL_GetError());
        // Hor+ (plan 21 Task 8): logical presentation is wide*2 x 480,
        // set once per resize below; the texture fills it exactly.
        const SDL_FRect dst{0, 0, float(scale_w), float(scale_h)};
        SDL_SetRenderLogicalPresentation(renderer, float(scale_w), float(scale_h),
                                         SDL_LOGICAL_PRESENTATION_LETTERBOX);
        if (!SDL_RenderTexture(renderer, texture.get(), nullptr, &dst))
            throw std::runtime_error(SDL_GetError());
    }
};
} // namespace rr
