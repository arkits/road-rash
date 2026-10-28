#include "rr_build_info.hpp"
#include "net/client.hpp"
#include "net/compat.hpp"
#include "net/rtc_channel.hpp"
// Shared native/Android frontend and playable race().
#include <SDL3/SDL.h>
#include "race_actions.hpp"
#include "sdl_handles.hpp"
#include "race_overlay.hpp"
#include "race_caches.hpp"
#include "race_helpers.hpp"
#include "race_screen.hpp"
#include "offline_capture.hpp"
#include "game_types.hpp"
#include "lifecycle.hpp"
#include <SDL3/SDL_main.h>
#include "assets.hpp"
#include "bob.hpp"
#include "framebuffer.hpp"
#include "input.hpp"
#include "importer.hpp"
#include "import_ui.hpp"
#include "race.hpp"
#include "race_setup.hpp"
#include "traffic.hpp"
#include "pedestrians.hpp"
#include "road_draw.hpp"
#include "session.hpp"
#include "video_player.hpp"
#include "career.hpp"
#include "course_scene.hpp"
#include "actor_nodes.hpp"
#include "hud.hpp"
#include "exe_tables.hpp"
#include "frontend.hpp"
#include "native_screens.hpp"
#include "pe_strings.hpp"
#include "settings.hpp"
#include "display.hpp"
#include "fog.hpp"
#include "framebuffer_rgb.hpp"
#include "post_chain.hpp"
#include "upscale_cache.hpp"
#include "upscale_import.hpp"
#include "raster_rgb.hpp"
#include "view_state.hpp"
#include "sound.hpp"
#include "race_music.hpp"
#include "render_threads.hpp"
#include "text.hpp"
#ifdef RR_WEB_BROWSER
#include "platform_web.hpp"
#include <emscripten.h>
#endif
#include <filesystem>
#include <future>
#include <chrono>
#include <ctime>
#include <algorithm>
#include <cstdio>
#include <memory>
#include <functional>
#include <limits>
#include <string>

namespace {
const char* const course_names[] = {"NAPA", "CITY", "HIWAY", "CANYN", "MEDLY"};
// The 640x480 original frames are scaled to arbitrary window sizes. Nearest
// sampling at non-integer factors doubles alternate texel columns (uneven,
// "soft" menu text); pixel-art sampling keeps texels sharp and even.
struct CliDisplayOverride {
    int fullscreen = -1;
    int window_scale = 0;
    long toggle_at = -1;
};
void apply_cli_overrides(rr::Settings& s, const CliDisplayOverride& o) {
    if (o.fullscreen >= 0)
        s.display_mode = o.fullscreen ? rr::DisplayMode::fullscreen : rr::DisplayMode::windowed;
    if (o.window_scale >= 1 && o.window_scale <= 4) s.window_scale = o.window_scale;
}

// Native startup feedback, available before any disc fonts/art are decoded.
void draw_loading(SDL_Renderer* renderer, const char* stage) {
#ifdef RR_WEB_BROWSER
    // Browser fonts are already verified/loaded by the launcher. Use the DOM
    // overlay so it stays readable and animates while Asyncify yields for I/O.
    (void)renderer;
    rr::web_loading(stage);
#else
    rr::draw_loading_screen(renderer, stage);
#endif
}

template <class Work> auto load_with_indicator(SDL_Renderer* renderer, const char* stage, Work work) {
    draw_loading(renderer, stage);
#ifdef __EMSCRIPTEN__
    // The web build does not enable pthreads. Native platforms animate while
    // loading; the single-thread web build still presents the loading label.
    return work();
#else
    auto pending = std::async(std::launch::async, std::move(work));
    bool quit = false;
    while (pending.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready) {
        SDL_Event event;
        while (SDL_PollEvent(&event))
            if (event.type == SDL_EVENT_QUIT) quit = true;
        draw_loading(renderer, stage);
        SDL_Delay(16);
    }
    auto result = pending.get();
    if (quit) {
        SDL_Event event{};
        event.type = SDL_EVENT_QUIT;
        SDL_PushEvent(&event);
    }
    return result;
#endif
}

// Plan 11a Task 1 Step 5: debug driver that hunts the oncoming lane, forcing
// rider–car contact so a headless run logs at least one reason-2 crash.
// Holds a fixed bias (~150 world units left of the road centre), inside the
// oncoming-traffic lanes but clear of the wall stand-in: the constant-left
// predecessor rode the shoulder clamp (x ≈ -723, wall hits every tick),
// which crashed the rider at speed > 4000 (reason 0) before traffic
// arrived. Hands off entirely while crashed (controls 0): the crash-walk
// back to the bike only converges with no steering input (0x0040a600
// state-38 walk; any held direction resets state_elapsed and the walk
// never reaches the bike), then remounts and keeps hunting. Probe
// (historical seven-opponent probe: NAPA, bike 0, level 0, 7 opponents, density 6, crash
// library): bias -150 meets a car box (dp<128 AND dl<24064) at t=783 and
// logs reason 2 the same tick (x=-114, no wall contact before the meet);
// 6 reason-2 crashes over 20000 ticks with remounts between each.
uint32_t steer_into_traffic(const rr::Race& race) {
    if (race.bike().i32(0x2f4) != 0) return 0; // crash-walk needs no input (see above)
    const auto& s = race.route()[race.position() >> 8];
    const int x = race.lateral() - s.offset;
    constexpr int bias = -150;
    uint32_t c = rr::control_accelerate;
    if (x < bias - 40)
        c |= rr::control_right;
    else if (x > bias + 40)
        c |= rr::control_left;
    return c;
}

void quad(SDL_Renderer* r, float x0, float x1, float y0, float x2, float x3, float y1, SDL_FColor c) {
    const SDL_Vertex v[4] = {{{x0, y0}, c, {}}, {{x1, y0}, c, {}}, {{x3, y1}, c, {}}, {{x2, y1}, c, {}}};
    const int index[6] = {0, 1, 2, 0, 2, 3};
    SDL_RenderGeometry(r, nullptr, v, 4, index, 6);
}

void play_video(const rr::Assets& assets, const std::string& path, SDL_Renderer* renderer,
                const std::string& screenshot, long ticks, rr::DisplayDriver& display) {
    rr::Mixer mixer(32);
    if (ticks < 0 && SDL_InitSubSystem(SDL_INIT_AUDIO) && !mixer.open_device())
        SDL_Log("Audio unavailable: %s", SDL_GetError());
    auto loaded = load_with_indicator(renderer, "Preparing video...",
                                      [&] { return std::make_unique<rr::VideoPlayer>(assets, path, mixer); });
    auto& player = *loaded;
    rr::LifecycleWatch lifecycle(&mixer);
    rr::GamepadPtr pad = rr::open_first_gamepad();
    bool running = true;
    display.frame = 0;
    player.update(ticks >= 0 ? 0 : SDL_GetTicksNS());
    if (ticks >= 0) player.update(uint64_t(ticks) * 1000000000ull / rr::ticks_per_second);
#ifdef RR_WEB_BROWSER
    rr::web_loading("");
#endif
    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (!rr::lifecycle_event(e.type)) lifecycle.state.handle(e);
            if (!mixer.handle(e)) SDL_Log("Audio reopen failed: %s", SDL_GetError());
            if (e.type == SDL_EVENT_QUIT) running = false;
            if (e.type == SDL_EVENT_GAMEPAD_ADDED && !pad) pad.reset(SDL_OpenGamepad(e.gdevice.which));
            if (e.type == SDL_EVENT_GAMEPAD_REMOVED && pad &&
                SDL_GetGamepadID(pad.get()) == e.gdevice.which) {
                pad.reset();
                lifecycle.state.pause();
            }
            if (e.type == SDL_EVENT_KEY_DOWN && !e.key.repeat) {
                if (e.key.key == SDLK_Q)
                    running = false;
                else if (e.key.key == SDLK_ESCAPE)
                    player.skip();
                else if (e.key.key == SDLK_P) {
                    if (lifecycle.state.paused())
                        lifecycle.state.resume();
                    else
                        lifecycle.state.pause();
                } else if (e.key.key == SDLK_AC_BACK)
                    lifecycle.state.pause();
                else if (lifecycle.state.paused())
                    lifecycle.state.resume();
                else
                    player.skip();
            }
            if (e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN && pad &&
                SDL_GetGamepadID(pad.get()) == e.gbutton.which) {
                if (e.gbutton.button == SDL_GAMEPAD_BUTTON_START) {
                    if (lifecycle.state.paused())
                        lifecycle.state.resume();
                    else
                        lifecycle.state.pause();
                } else if (lifecycle.state.paused())
                    lifecycle.state.resume();
                else
                    player.skip();
            }
            if (e.type == SDL_EVENT_FINGER_DOWN || e.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
                if (lifecycle.state.paused())
                    lifecycle.state.resume();
                else
                    player.skip();
            }
            if (e.type == SDL_EVENT_RENDER_DEVICE_RESET || e.type == SDL_EVENT_RENDER_TARGETS_RESET) {
                // Recreate the video texture through a separate explicit reset.
                player.reset_texture();
            }
            if (e.type == SDL_EVENT_RENDER_DEVICE_LOST) throw std::runtime_error("Render device lost");
        }
        player.pause(lifecycle.state.paused());
        if (ticks < 0 && !player.update(SDL_GetTicksNS())) running = false;
        if (lifecycle.state.background()) {
            SDL_Delay(20);
            continue;
        }
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderClear(renderer);
        player.draw(renderer);
        if (!screenshot.empty()) {
            rr::SurfacePtr capture(SDL_RenderReadPixels(renderer, nullptr));
            if (!capture || !SDL_SaveBMP(capture.get(), screenshot.c_str()))
                throw std::runtime_error(SDL_GetError());
            running = false;
        }
        SDL_RenderPresent(renderer);
        display.maybe_toggle();
        if (ticks >= 0) running = false;
    }
}
} // namespace

// Original course files by course number 1..5 (0x0046df58): City, Sierra Nevada, Peninsula, Napa, Pacific Highway.
const char* const original_course_files[5] = {"CITY", "HIWAY", "MEDLY", "NAPA", "CANYN"};

// --bench report: the run context (settings that cost frame time, the
// effective surface, build type), then the timings from FrameBench.
void write_bench_report(const rr::FrameBench& bench, const rr::Settings& s, const rr::DisplayDriver& d,
                        SDL_Window* window, SDL_Renderer* renderer, const rr::Game& g, int course,
                        int render_scale) {
    int ww = 0, wh = 0, pw = 0, ph = 0;
    SDL_GetWindowSize(window, &ww, &wh);
    SDL_GetWindowSizeInPixels(window, &pw, &ph);
    const int scale = render_scale ? render_scale : rr::effective_render_scale(s, ww, wh, rr::kMobileDisplay);
    const auto& e = s.enhancements;
    const char* renderer_name = SDL_GetRendererName(renderer);
#ifdef NDEBUG
    const char* build = "release";
#else
    const char* build = "debug";
#endif
    char ctx[1536];
    std::snprintf(
        ctx, sizeof ctx,
        "  \"build\": \"%s\",\n  \"renderer\": \"%s\",\n  \"course\": %d,\n  \"window\": [%d, %d],\n"
        "  \"window_pixels\": [%d, %d],\n  \"render_scale\": %d,\n  \"logical_width\": %d,\n"
        "  \"surface\": [%d, %d],\n  \"render_threads\": %d,\n  \"vsync\": %d,\n"
        "  \"settings\": {\"hires\": %d, \"render_scale\": %d, \"widescreen\": %d, \"texture_detail\": %d, "
        "\"filter\": %d, \"interpolate\": %d, \"perspective\": %d, \"full_colour\": %d, \"enh_filter\": %d, "
        "\"fog\": %d, \"grade\": %d, \"draw_slices\": %d, \"bloom\": %d, \"vignette\": %d, \"motion_blur\": "
        "%d},\n",
        build, renderer_name ? renderer_name : "?", course, ww, wh, pw, ph, scale, int(g.logical_width()),
        int(g.logical_width()) * 2 * scale, 480 * scale, d.render_threads, s.vsync ? 1 : 0,
        int(s.high_resolution), s.render_scale, int(s.widescreen), s.texture_detail, int(s.filter),
        int(e.interpolate), int(e.perspective), int(e.full_colour), int(e.filter), e.fog, int(e.grade),
        e.draw_slices, int(e.bloom), int(e.vignette), int(e.motion_blur));
    const std::string report = bench.report(ctx);
    std::fputs(report.c_str(), stdout);
    std::fflush(stdout);
    if (!bench.out_path.empty()) {
        FILE* f = std::fopen(bench.out_path.c_str(), "wb");
        if (!f || std::fputs(report.c_str(), f) < 0 || std::fclose(f) != 0)
            throw std::runtime_error("Cannot write " + bench.out_path);
    }
}

// SDL3's Emscripten backend yields in RenderPresent and SDL_Delay when
// Asyncify is enabled. Do not add a second yield between presented frames.
rr::RaceEnd run_race(rr::Game& g, SDL_Renderer* renderer, const rr::RaceOptions& opt,
                     rr::DisplayDriver& display) {
    rr::RaceScreen screen(g, renderer, opt, display);
#ifdef RR_WEB_BROWSER
    rr::web_loading("");
#endif
    for (;;) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            // Offline takes no keyboard, controller, focus or resize input.
            if (!opt.capture || e.type == SDL_EVENT_QUIT) screen.event(e);
        }
        if (!screen.iterate(SDL_GetTicksNS())) break;
    }
    return screen.result();
}

// Quick race through the front end: a Thrash race on one course, then the
// original results/standings screens (0x1e..0x22) and the Thrash menus.
struct QuickStart {
    int course = 0;    // original 1..5 (0x0046df58 order); 0 = normal start
    int character = 1; // 1..8 (0x00415fc0)
    int level = 1;     // 1..5 (0x004453f0)
    int bike = -1;     // explicit bike id, -1 = the character's level bike
};

// Menus -> videos -> races -> results: the original main loop (0x00448200) around the front end.
int run_game(const rr::Assets& assets, SDL_Window* window, SDL_Renderer* renderer, rr::Game& g,
             const std::vector<uint8_t>& exe, const std::string& screenshot, long menu_frames,
             const CliDisplayOverride& cli, const std::string& menu_script, const std::string& net_invite,
             const std::string& net_server, bool rtc, const QuickStart& quick = {}) {
    const std::string pref = rr::pref_directory();
    const std::string settings_path = (std::filesystem::path(pref) / "settings.txt").string();
    rr::Settings settings = rr::load_settings(settings_path);
    apply_cli_overrides(settings, cli);
    if (!net_server.empty()) settings.net_server = net_server;
    rr::DisplayDriver display{window, renderer, &settings, settings_path};
    display.toggle_at = cli.toggle_at;
    display.apply(480);
    draw_loading(renderer, "Preparing menus and sound...");
    const rr::TextRenderer text(assets);
    const auto tables = rr::read_career_tables(exe);
    rr::Career career;
    // 0x00448200: srand(time(NULL)) at launch; 0x004490d0 -> 0x00442960 then
    // runs 0x004078f0 on that stream (spare-row speed jitter, 30 draws).
    career.rng = uint32_t(std::time(nullptr));
    rr::reset_relations(career, tables);
    const auto resources = rr::read_string_resources(assets.read(rr::language_dll(settings.language)));
    const auto strings = rr::frontend_strings(resources);
    auto frontend_data = rr::read_frontend_data(exe);
    rr::add_native_screens(frontend_data, rr::kMobileDisplay);
    rr::Frontend fe(assets, text, strings, rr::credit_lines(resources), std::move(frontend_data), tables,
                    career, settings, (std::filesystem::path(pref) / "saves").string());
#ifdef RR_NET_TRANSPORT
    try {
        const auto hello = load_with_indicator(renderer, "Checking multiplayer game data...", [&] {
#if defined(__EMSCRIPTEN__)
            constexpr auto platform = rr::net::Platform::web;
#elif defined(__ANDROID__)
        constexpr auto platform=rr::net::Platform::android;
#elif defined(__APPLE__)
        constexpr auto platform=rr::net::Platform::macos;
#elif defined(_WIN32)
        constexpr auto platform=rr::net::Platform::windows;
#else
        constexpr auto platform=rr::net::Platform::linux_desktop;
#endif
            return rr::net::make_hello(settings.alias, platform, rr::net::verified_data_digest(assets));
        });
        fe.net_attach(
            [&settings, rtc] {
                return rr::net::ws_connect(settings.net_server, rr::net::RtcOptions{.enabled = rtc});
            },
            hello);
    } catch (const std::exception& error) {
        SDL_Log("Multiplayer unavailable: %s", error.what());
    }
#endif
    // Audio: one mixer for the soundtrack (0x00403040) and race sounds (0x00435da0).
    rr::Mixer mixer(48);
    if (menu_frames < 0 && (!SDL_InitSubSystem(SDL_INIT_AUDIO) || !mixer.open_device()))
        SDL_Log("Audio unavailable: %s", SDL_GetError());
    std::unique_ptr<rr::SoundSystem> sound;
    try {
        sound = std::make_unique<rr::SoundSystem>(assets, mixer, rr::read_volume_curve(exe),
                                                  int(rr::read_exe_data(exe, 0x0046dce5, 1)[0]));
    } catch (const std::exception& e) {
        SDL_Log("Race sounds unavailable: %s", e.what());
    }
    rr::MusicPlayer music(assets, mixer);
    rr::RaceMusicPlayer race_music(assets, mixer); // 0x00433bb0: MIDI by course, Klown by alias
    auto apply_levels = [&] {
        rr::SoundLevels l;
        l.master = settings.master_volume;
        l.effects = settings.effects_volume;
        l.engine = settings.engine_volume;
        l.others = settings.others_volume;
        l.siren = settings.siren_volume;
        l.music = settings.music_volume;
        l.interface = settings.interface_volume;
        l.mute = settings.mute;
        l.swap_speakers = settings.swap_speakers;
        if (sound) sound->set_levels(l);
        music.set_volume(settings.mute ? 0
                                       : 127 * settings.music_volume / 100 * settings.master_volume / 100);
    };
    apply_levels();
    if (menu_frames < 0)
        load_with_indicator(renderer, "Loading menu music...", [&] {
            music.play(settings.music_track);
            return 0;
        });
    g.mixer = &mixer;
    rr::LifecycleWatch audio_lifecycle(&mixer);
    int menu_scale = 0;
    rr::TexturePtr texture;
    rr::GamepadPtr pad = rr::open_first_gamepad();
    rr::MenuStickInput menu_stick;
    rr::LifecycleWatch lifecycle;
    rr::RgbImage frame;
    if (quick.course) {
        // Thrash setup as the character (0x0041f5a0) and level (0x004453f0)
        // screens leave it, then the course item's race start (0x004160c0).
        rr::select_mode(career, tables, rr::GameMode::thrash);
        rr::choose_character(career, tables, quick.character);
        rr::set_level(career, tables, quick.level);
        if (quick.bike >= 0) career.state.bike = uint8_t(quick.bike);
        rr::start_race(career, quick.course);
        fe.enter(rr::screen_id::thrash_courses);
        fe.enter(quick.course);
    } else if (net_invite.empty())
        fe.start();
    else {
        fe.enter(9);
        fe.net_join_invite(net_invite);
    }
    if (!menu_script.empty()) { // debug: comma-separated screen names entered in order
        static const std::pair<const char*, int> kMenuScriptScreens[] = {
            {"main", 9},      {"restroom", 0x10},   {"graphics", 0x13}, {"display", 0x24},
            {"street", 0x0f}, {"characters", 0x0e}, {"jukebox", 0x11},
        };
        size_t from = 0;
        while (from <= menu_script.size()) {
            size_t to = menu_script.find(',', from);
            if (to == std::string::npos) to = menu_script.size();
            const std::string name = menu_script.substr(from, to - from);
            for (const auto& [key, screen] : kMenuScriptScreens)
                if (name == key) {
                    fe.enter(screen);
                    break;
                }
            from = to + 1;
        }
    }
    bool running = true, text_input = false;
#ifdef RR_WEB_BROWSER
    rr::web_loading("");
#endif
    long frames = 0;
    display.frame = 0;
    while (running) {
        rr::FrontendEvent ev;
        while (running && fe.poll(ev)) {
            if (ev.kind == rr::FrontendEvent::Kind::quit)
                running = false;
            else if (ev.kind == rr::FrontendEvent::Kind::select_soundtrack)
                music.play(ev.track);
            else if (ev.kind == rr::FrontendEvent::Kind::apply_display)
                display.apply(480);
            else if (ev.kind == rr::FrontendEvent::Kind::apply_mixer)
                apply_levels();
            else if (ev.kind == rr::FrontendEvent::Kind::play_video) {
                if (menu_frames < 0) {
                    // 0x004023e0: showroom clips (flag 1) only quieten the soundtrack; other videos stop it.
                    const bool showroom = ev.video.find("/RAT/") != std::string::npos ||
                                          ev.video.find("/SPORT/") != std::string::npos ||
                                          ev.video.find("/SUPER/") != std::string::npos;
                    if (showroom)
                        music.set_volume(0);
                    else
                        music.stop();
                    play_video(assets, ev.video, renderer, {}, -1, display);
                    menu_stick.suspend();
                    apply_levels();
                    if (!showroom && !music.active()) music.play(music.track());
                }
            } else if (ev.kind == rr::FrontendEvent::Kind::copy_text) {
                SDL_SetClipboardText(ev.text.c_str());
            } else if (ev.kind == rr::FrontendEvent::Kind::start_net_race) {
                if (menu_frames >= 0) {
                    running = false;
                    break;
                }
                auto* room = fe.net_client();
                if (!room) continue;
                rr::net::Client client(room->release_channel(), {}, [&](const rr::net::Start& start) {
                    return std::make_unique<rr::RaceSetup>(
                        rr::make_race_setup(assets, exe, rr::net::race_config(start)));
                });
                client.begin(ev.net_start, rr::net::steady_ms());
                music.stop();
                race_music.play_race(ev.net_start.course, fe.player_name());
                g.race_music = &race_music;
                g.sound = sound.get();
                g.exe_ = exe;
                size_t local = 0;
                for (size_t i = 0; i < ev.net_start.humans.size(); ++i)
                    if (ev.net_start.humans[i].seat == ev.net_start.your_seat) local = i;
                if (sound)
                    sound->start_race(ev.net_start.humans[size_t(local)].bike,
                                      ev.net_start.humans[size_t(local)].character);
                const auto end = run_race(g, renderer,
                                          {.key_codes = settings.controls,
                                           .custom_keys = settings.custom_controls,
                                           .swap_punch_kick = settings.swap_punch_kick,
                                           .stick_deadzone = settings.stick_deadzone,
                                           .left_handed = settings.left_handed,
                                           .show_fps = settings.show_fps,
                                           .assets = &assets,
                                           .settings = &settings,
                                           .lobby_event =
                                               [room](const rr::net::Message& message) {
                                                   room->inject(message, rr::net::steady_ms());
                                               },
                                           .online = &client,
                                           .chat_messages = fe.chat_messages()},
                                          display);
                while (auto message = client.poll_event())
                    if (!std::holds_alternative<rr::net::Start>(*message))
                        room->inject(*message, rr::net::steady_ms());
                room->resume(client.release_channel());
                room->take_official();
                if (sound) sound->stop_race();
                race_music.stop();
                g.sound = nullptr;
                g.race_music = nullptr;
                menu_stick.suspend();
                music.play(music.track());
                display.apply(480);
                if (end.quit_app) {
                    running = false;
                    break;
                }
                if (end.official) {
                    std::vector<std::string> aliases(8);
                    for (const auto& human : ev.net_start.humans) aliases[human.seat] = human.alias;
                    fe.mano_finished(
                        *end.official, aliases,
                        ev.net_start.your_seat == 0xff ? -1 : ev.net_start.humans[size_t(local)].seat);
                } else
                    fe.enter(11);
            } else if (ev.kind == rr::FrontendEvent::Kind::start_race) {
                // Race set-up from the career: course, level, bike, rider weight, opponents (plan 05).
                g.course_name = original_course_files[ev.course - 1];
                g.config.course = g.course_name;
                g.config.level = career.state.level;
                g.config.bike = career.state.bike;
                g.config.character = career.state.character;
                g.config.career = true;
                g.config.opponents = 14;
                g.config.mode = ev.demo ? rr::GameMode::attract : career.mode;
                g.config.spare_levels = career.spare_levels;
                g.metric = settings.metric;
                g.player_name = fe.player_name();
                g.load_course(assets, exe);
                g.career = true;
                g.quit_prompt = fe.string_slot(316);
                g.pause_prompt = fe.string_slot(317);
                g.restart();
                g.rider_names = rr::race_rider_names(exe, g.config, g.race(), strings, g.player_name);
                if (ev.demo) {
                    // Attract demo (plan 13 Task 5): AI drives (DAT_004753c8); any input exits to the menu.
                    g.career = false;
                    g.race().set_demo_hold(true, 95);
                    music.stop();
                    race_music.play_race(ev.course, fe.player_name());
                    g.race_music = &race_music;
                    const rr::RaceEnd demo_end = run_race(g, renderer,
                                                          {.demo = true,
                                                           .key_codes = settings.controls,
                                                           .custom_keys = settings.custom_controls,
                                                           .swap_punch_kick = settings.swap_punch_kick,
                                                           .stick_deadzone = settings.stick_deadzone,
                                                           .left_handed = settings.left_handed,
                                                           .menu = &fe,
                                                           .apply_settings = apply_levels,
                                                           .show_fps = settings.show_fps,
                                                           .assets = &assets,
                                                           .settings = &settings},
                                                          display);
                    menu_stick.suspend();
                    race_music.stop();
                    g.race_music = nullptr;
                    settings.music_track = music.track();
                    music.play(music.track());
                    display.apply(480);
                    if (demo_end.quit_app) {
                        running = false;
                        break;
                    }
                    fe.enter(rr::screen_id::main_menu); // re-arm the 60 s timer
                    continue;
                }
                if (menu_frames >= 0) {
                    running = false;
                    break;
                } // smoke run: stop at the first race
                music.stop(); // 0x004160c0 -> 0x00402f50
                // Plan 11 race-start point: race MIDI replaces the RRA soundtrack.
                // ev.course is the original 1..5 index (0x0046df58 order), matching race_song_name.
                race_music.play_race(ev.course, fe.player_name());
                g.race_music = &race_music;
                race_music.set_music_level(
                    settings.mute ? 0 : settings.music_volume * settings.master_volume / 100);
                g.sound = sound.get();
                if (sound) sound->start_race(career.state.bike, career.state.character);
                const rr::RaceEnd end = run_race(
                    g, renderer,
                    {.key_codes = settings.controls,
                     .custom_keys = settings.custom_controls,
                     .swap_punch_kick = settings.swap_punch_kick,
                     .stick_deadzone = settings.stick_deadzone,
                     .left_handed = settings.left_handed,
                     .menu = &fe,
                     .apply_settings =
                         [&] {
                             apply_levels();
                             race_music.set_music_level(
                                 settings.mute ? 0 : settings.music_volume * settings.master_volume / 100);
                         },
                     .show_fps = settings.show_fps,
                     .assets = &assets,
                     .settings = &settings},
                    display);
                menu_stick.suspend();
                if (sound) sound->stop_race();
                race_music.stop(); // race end
                g.sound = nullptr;
                g.race_music = nullptr;
                settings.music_track = music.track();
                music.play(music.track());
                display.apply(480);
                if (end.quit_app) {
                    running = false;
                    break;
                }
                // Finishing order: riders who finished, by finish tick (0x00420e30 rows).
                std::vector<std::pair<int32_t, size_t>> order;
                for (size_t i = 0; i < g.race().riders().size(); ++i)
                    if (!g.race().riders()[i].cop && g.race().riders()[i].finished)
                        order.emplace_back(g.race().riders()[i].finish_tick, i);
                std::sort(order.begin(), order.end());
                std::vector<rr::Standing> standings;
                for (const auto& [tick, i] : order)
                    standings.push_back(
                        {g.rider_names.at(i), tick * 100 / rr::ticks_per_second / 10, i == 0});
                fe.race_finished(end.outcome, end.place, standings);
                rr::save_settings(settings_path, settings);
            }
        }
        if (!running) break;
        if (fe.editing_text() != text_input) {
            text_input = fe.editing_text();
            if (text_input)
                SDL_StartTextInput(window);
            else
                SDL_StopTextInput(window);
        }
        rr::MenuInput in;
        auto append_text = [&](const char* raw) {
            // UTF-8 -> Windows-1252 for the alias.
            std::u16string u;
            const unsigned char* t = reinterpret_cast<const unsigned char*>(raw);
            for (size_t i = 0; t[i];) {
                uint32_t c = t[i];
                int n = c < 0x80 ? 1 : c < 0xe0 ? 2 : c < 0xf0 ? 3 : 4;
                if (n > 1) {
                    c &= 0x3f >> (n - 1);
                    for (int k = 1; k < n && t[i + size_t(k)]; ++k) c = c << 6 | (t[i + size_t(k)] & 0x3f);
                }
                u.push_back(c < 0x10000 ? char16_t(c) : u'?');
                i += size_t(n);
            }
            in.text += rr::to_cp1252(u);
        };
        // Nested race/video loops may have consumed the removal notification.
        if (pad && !SDL_GamepadConnected(pad.get())) {
            pad = rr::open_first_gamepad();
            menu_stick.suspend();
            lifecycle.state.pause();
        }
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_EVENT_QUIT) running = false;
            if (!rr::lifecycle_event(e.type)) lifecycle.state.handle(e);
            if (!mixer.handle(e)) SDL_Log("Audio reopen failed: %s", SDL_GetError());
            if (e.type == SDL_EVENT_KEY_DOWN && !e.key.repeat) {
                // Plan 18 Task 3: Alt+Enter and Cmd+F toggle fullscreen (saved + re-applied).
                if ((e.key.key == SDLK_RETURN && (e.key.mod & SDL_KMOD_ALT)) ||
                    (e.key.key == SDLK_F && (e.key.mod & SDL_KMOD_GUI))) {
                    display.toggle_fullscreen();
                    continue;
                }
            }
            if (e.type == SDL_EVENT_KEY_DOWN) {
                // Plan 18 Task 7: raw scancode for press-a-key capture (0 = none).
                if (!e.key.repeat && e.key.scancode > 0 && e.key.scancode < SDL_SCANCODE_COUNT)
                    in.capture_scancode = int(e.key.scancode);
                switch (e.key.key) {
                case SDLK_LEFT:
                    in.left = true;
                    break;
                case SDLK_RIGHT:
                    in.right = true;
                    break;
                case SDLK_UP:
                    in.up = true;
                    break;
                case SDLK_DOWN:
                    in.down = true;
                    break;
                case SDLK_TAB:
                    (e.key.mod & SDL_KMOD_SHIFT ? in.shift_tab : in.tab) = true;
                    break;
                case SDLK_HOME:
                    in.home = true;
                    break;
                case SDLK_END:
                    in.end = true;
                    break;
                case SDLK_RETURN:
                case SDLK_KP_ENTER:
                case SDLK_SPACE:
                    if (!e.key.repeat && !(text_input && e.key.key == SDLK_SPACE)) in.select = true;
                    break;
                case SDLK_ESCAPE:
                case SDLK_AC_BACK:
                    if (!e.key.repeat) in.back = true;
                    break;
                case SDLK_V:
                    if (text_input && (e.key.mod & (SDL_KMOD_CTRL | SDL_KMOD_GUI))) {
                        char* clip = SDL_GetClipboardText();
                        if (clip) {
                            append_text(clip);
                            SDL_free(clip);
                        }
                    }
                    break;
                case SDLK_BACKSPACE:
                    in.backspace = true;
                    break;
                case SDLK_A:
                    if (!text_input && !e.key.repeat) in.alternate_artwork = true;
                    break;
                case SDLK_SLASH:
                    if (!text_input && !e.key.repeat) in.opponent_key = true;
                    break;
                case SDLK_KP_PLUS:
                case SDLK_EQUALS:
                    if (!text_input) in.text += '+';
                    break;
                case SDLK_KP_MINUS:
                case SDLK_MINUS:
                    if (!text_input) in.text += '-';
                    break;
                default:
                    break;
                }
            }
            if (e.type == SDL_EVENT_TEXT_INPUT && text_input) {
                append_text(e.text.text);
            }
            if (e.type == SDL_EVENT_GAMEPAD_ADDED && !pad) pad.reset(SDL_OpenGamepad(e.gdevice.which));
            if (e.type == SDL_EVENT_GAMEPAD_REMOVED && pad &&
                SDL_GetGamepadID(pad.get()) == e.gdevice.which) {
                pad = rr::open_first_gamepad();
                menu_stick.suspend();
                lifecycle.state.pause();
            }
            if (e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN && pad &&
                SDL_GetGamepadID(pad.get()) == e.gbutton.which) {
                switch (e.gbutton.button) {
                case SDL_GAMEPAD_BUTTON_DPAD_LEFT:
                    in.left = true;
                    break;
                case SDL_GAMEPAD_BUTTON_DPAD_RIGHT:
                    in.right = true;
                    break;
                case SDL_GAMEPAD_BUTTON_DPAD_UP:
                    in.up = true;
                    break;
                case SDL_GAMEPAD_BUTTON_DPAD_DOWN:
                    in.down = true;
                    break;
                case SDL_GAMEPAD_BUTTON_SOUTH:
                case SDL_GAMEPAD_BUTTON_START:
                    in.select = true;
                    break;
                case SDL_GAMEPAD_BUTTON_EAST:
                case SDL_GAMEPAD_BUTTON_BACK:
                    in.back = true;
                    break;
                default:
                    break;
                }
            }
            if (e.type == SDL_EVENT_MOUSE_MOTION || e.type == SDL_EVENT_MOUSE_BUTTON_DOWN ||
                e.type == SDL_EVENT_FINGER_DOWN) {
                float x = 0, y = 0;
                if (e.type == SDL_EVENT_FINGER_DOWN) {
                    int w = 0, h = 0;
                    SDL_GetWindowSize(window, &w, &h);
                    SDL_RenderCoordinatesFromWindow(renderer, e.tfinger.x * float(w), e.tfinger.y * float(h),
                                                    &x, &y);
                } else if (e.type == SDL_EVENT_MOUSE_MOTION) {
                    if (e.motion.which == SDL_TOUCH_MOUSEID) continue;
                    SDL_RenderCoordinatesFromWindow(renderer, e.motion.x, e.motion.y, &x, &y);
                } else {
                    if (e.button.which == SDL_TOUCH_MOUSEID) continue;
                    SDL_RenderCoordinatesFromWindow(renderer, e.button.x, e.button.y, &x, &y);
                }
                in.pointer_x = int(x);
                in.pointer_y = int(y);
                in.pointer_moved = true;
                if (e.type != SDL_EVENT_MOUSE_MOTION) in.pointer_click = true;
            }
            if (e.type == SDL_EVENT_RENDER_DEVICE_RESET || e.type == SDL_EVENT_RENDER_TARGETS_RESET) {
                texture.reset();
                menu_scale = 0;
            }
            if (e.type == SDL_EVENT_RENDER_DEVICE_LOST) throw std::runtime_error("Render device lost");
        }
        if (!running) break;
        if (lifecycle.state.take_clock_reset() || lifecycle.state.paused()) menu_stick.suspend();
        if (lifecycle.state.paused() && (in.select || in.pointer_click)) {
            lifecycle.state.resume();
            in = {};
        }
        if (!lifecycle.state.paused() && pad) {
            const auto stick =
                menu_stick.update(SDL_GetGamepadAxis(pad.get(), SDL_GAMEPAD_AXIS_LEFTX),
                                  SDL_GetGamepadAxis(pad.get(), SDL_GAMEPAD_AXIS_LEFTY), SDL_GetTicks());
            in.left |= stick.left;
            in.right |= stick.right;
            in.up |= stick.up;
            in.down |= stick.down;
        }
        if (!lifecycle.state.paused()) fe.update(in, SDL_GetTicks());
        music.update();
        mixer.pause(lifecycle.state.paused() || audio_lifecycle.state.paused());
        if (lifecycle.state.background()) {
            SDL_Delay(20);
            continue;
        }
        display.apply(480);
        int dw = 0, dh = 0;
        SDL_GetWindowSize(window, &dw, &dh);
        fe.set_display_size(dw, dh);
        const int menu_lw = fe.menu_width(dw, dh);
        if (menu_lw != 640)
            SDL_SetRenderLogicalPresentation(renderer, menu_lw, 480, display.config.presentation);
        fe.draw(frame, display.render_scale);
        if (!texture || menu_scale != frame.width) {
            menu_scale = frame.width;
            texture.reset(SDL_CreateTexture(renderer, SDL_PIXELFORMAT_XRGB8888, SDL_TEXTUREACCESS_STREAMING,
                                            frame.width, frame.height));
            if (!texture) throw std::runtime_error(SDL_GetError());
            rr::apply_texture_scale(texture.get(), display.config);
        }
        SDL_UpdateTexture(texture.get(), nullptr, frame.pixels.data(), frame.width * 4);
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderClear(renderer);
        SDL_RenderTexture(renderer, texture.get(), nullptr, nullptr);
        if (menu_frames >= 0 && ++frames >= menu_frames) {
            if (!screenshot.empty()) {
                rr::SurfacePtr capture(SDL_RenderReadPixels(renderer, nullptr));
                if (!capture || !SDL_SaveBMP(capture.get(), screenshot.c_str()))
                    throw std::runtime_error(SDL_GetError());
            }
            running = false;
        }
        SDL_RenderPresent(renderer);
        display.maybe_toggle();
    }
    settings.music_track = music.track();
    rr::save_settings(settings_path, settings);
    music.stop();
    if (text_input) SDL_StopTextInput(window);
    return 0;
}

int main(int argc, char** argv) {
    if (argc == 2 && std::string(argv[1]) == "--build-info") {
        std::puts(rr::build_info_json);
        return 0;
    }
    std::string root, screenshot, video, net_invite, net_server;
    bool rtc = false;
    int course = 0, bike = 0, level = 0, opponents = 14, character = 1;
    bool explicit_bike = false;
    long run_ticks = -1, menu_frames = -1;
    bool auto_drive = false, into_traffic = false, start_paused = false, first_run_smoke = false,
         quick_race = false;
    bool print_wiring = false; // plan 26 Task 3: print race wiring after the first tick and exit
    int render_scale = 1;      // plan 19 Task 4: --render-scale 1..4
    bool explicit_render_scale = false;
    rr::FrameBench bench; // --bench FRAMES: timed autopilot race (docs/PERFORMANCE.md)
    bool benchmark = false, bench_vsync = false;
    rr::OfflineCapture capture;
    std::string menu_script;
    CliDisplayOverride cli;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--rtc")
            rtc = true;
        else if (a == "--no-udp")
            rtc = false;
        else if (a == "--room" && i + 1 < argc)
            net_invite = argv[++i];
        else if (a == "--net-server" && i + 1 < argc)
            net_server = argv[++i];
        else if (rr::net::parse_room_code(a) && a.find("room=") != std::string::npos)
            net_invite = a;
        else if (a == "--assets" && i + 1 < argc)
            root = argv[++i];
        else if (a == "--video" && i + 1 < argc)
            video = argv[++i];
        else if (a == "--course" && i + 1 < argc) {
            course = std::atoi(argv[++i]);
            quick_race = true;
        } else if (a == "--bike" && i + 1 < argc) {
            bike = std::atoi(argv[++i]);
            explicit_bike = quick_race = true;
        } else if (a == "--character" && i + 1 < argc) {
            character = std::atoi(argv[++i]);
            quick_race = true;
        } else if (a == "--ticks" && i + 1 < argc) {
            run_ticks = std::atol(argv[++i]);
            quick_race = true;
        } else if (a == "--screenshot" && i + 1 < argc)
            screenshot = argv[++i];
        else if (a == "--autopilot") {
            auto_drive = true;
            quick_race = true;
        } else if (a == "--steer-into-traffic") {
            into_traffic = true;
            quick_race = true;
        } else if (a == "--paused")
            start_paused = true;
        else if (a == "--race")
            quick_race = true;
        else if (a == "--menu-frames" && i + 1 < argc)
            menu_frames = std::atol(argv[++i]);
        else if (a == "--menu-script" && i + 1 < argc)
            menu_script = argv[++i];
        else if (a == "--first-run-smoke")
            first_run_smoke = true;
        else if (a == "--level" && i + 1 < argc) {
            level = std::atoi(argv[++i]) - 1;
            quick_race = true;
        } else if (a == "--opponents" && i + 1 < argc) {
            opponents = std::atoi(argv[++i]);
            quick_race = true;
        } else if (a == "--fullscreen")
            cli.fullscreen = 1;
        else if (a == "--windowed")
            cli.fullscreen = 0;
        else if (a == "--window-scale" && i + 1 < argc)
            cli.window_scale = std::atoi(argv[++i]);
        else if (a == "--render-scale" && i + 1 < argc) {
            render_scale = std::atoi(argv[++i]);
            explicit_render_scale = true;
            quick_race = true;
        } else if (a == "--capture-video" && i + 1 < argc) {
            capture.video_path = argv[++i];
            auto_drive = quick_race = true;
        } else if (a == "--capture-audio" && i + 1 < argc)
            capture.audio_path = argv[++i];
        else if (a == "--capture-frames" && i + 1 < argc)
            capture.frames = std::atol(argv[++i]);
        else if (a == "--capture-warmup" && i + 1 < argc)
            capture.warmup = std::atol(argv[++i]);
        else if (a == "--bench" && i + 1 < argc) {
            bench.frames = std::atol(argv[++i]);
            benchmark = auto_drive = quick_race = true;
        } else if (a == "--bench-warmup" && i + 1 < argc)
            bench.warmup = std::atol(argv[++i]);
        else if (a == "--bench-out" && i + 1 < argc)
            bench.out_path = argv[++i];
        else if (a == "--bench-label" && i + 1 < argc)
            bench.label = argv[++i];
        else if (a == "--bench-vsync")
            bench_vsync = true;
        else if (a == "--toggle-fullscreen-at" && i + 1 < argc)
            cli.toggle_at = std::atol(argv[++i]);
        else if (a == "--print-wiring") {
            print_wiring = true;
            quick_race = true;
        }
    }
    if (course < 0 || course > 4 || level < 0 || level > 4 || opponents < 0 || opponents > 14 ||
        character < 1 || character > 8 || (explicit_bike && (bike < 0 || bike > 14)))
        return 2;
    if (render_scale < 1 || render_scale > 4) return 2;
    if (benchmark && (bench.frames < 1 || bench.warmup < 0)) return 2;
    const bool capturing = !capture.video_path.empty() || !capture.audio_path.empty() || capture.frames != 0;
    if (capturing && (!capture.enabled() || capture.video_path.empty() || capture.audio_path.empty() ||
                      capture.warmup < 0 || run_ticks >= 0 || benchmark || into_traffic || start_paused ||
                      !video.empty() || !screenshot.empty()))
        return 2;
    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;
#ifdef RR_WEB_BROWSER
    if (net_invite.empty()) net_invite = rr::web_room_invite();
#endif
    int result = 0;
    try {
        if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) throw std::runtime_error(SDL_GetError());
        if (!SDL_CreateWindowAndRenderer("Road Rash Native", 960, 720,
                                         SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY, &window,
                                         &renderer))
            throw std::runtime_error(SDL_GetError());
        rr::Settings boot_settings =
            rr::load_settings((std::filesystem::path(rr::pref_directory()) / "settings.txt").string());
        apply_cli_overrides(boot_settings, cli);
        // The benchmark measures throughput: no vsync wait unless asked, and
        // the race scale follows the settings file unless --render-scale.
        if (benchmark && !bench_vsync) boot_settings.vsync = false;
        if (benchmark && !explicit_render_scale) render_scale = 0;
        if (capturing) {
            // Isolated, reproducible presentation; never save over user settings.
            boot_settings = rr::default_settings();
            boot_settings.vsync = false;
            boot_settings.window_scale = 2;
            boot_settings.show_fps = false;
            boot_settings.widescreen = rr::Widescreen::off;
        }
        rr::DisplayDriver boot{window, renderer, &boot_settings,
                               (std::filesystem::path(rr::pref_directory()) / "settings.txt").string()};
        boot.toggle_at = cli.toggle_at;
        boot.apply(480);
        if (capturing) SDL_HideWindow(window);
        if (root.empty()) {
            root = rr::data_root();
#ifdef RR_DEV_ASSETS
            if (!rr::Assets(root).exists("ROADRASH.EXE")) root = "game";
#endif
        }
        if (print_wiring && !rr::Assets(root).exists("ROADRASH.EXE")) {
            SDL_DestroyRenderer(renderer);
            SDL_DestroyWindow(window);
            SDL_Quit();
            return 77;
        } // plan 26 Task 3: skip without the disc
        if (!rr::Assets(root).exists("ROADRASH.EXE")) {
            if (!rr::run_import_screen(window, renderer, root, screenshot, first_run_smoke)) {
                SDL_DestroyRenderer(renderer);
                SDL_DestroyWindow(window);
                SDL_Quit();
                return 0;
            }
        }
        const rr::Assets assets(root);
        if (!video.empty()) {
            boot.frame = 0;
            boot.apply(480);
            play_video(assets, video, renderer, screenshot, run_ticks, boot);
            SDL_DestroyRenderer(renderer);
            SDL_DestroyWindow(window);
            SDL_Quit();
            return 0;
        }
        rr::Game g;
        const auto exe = load_with_indicator(renderer, "Reading game data...", [&] {
            auto bytes = assets.read("ROADRASH.EXE");
            g.depth = rr::read_depth_table(bytes);
            g.assets.physics = rr::read_physics_tables(bytes);
            // specs validated below from a local read (factory owns the game copy).
            (void)rr::read_bike_specs(assets.read("DATA/BIKESPEC.RSC"));
            g.bobs = rr::read_bobs(assets.read("DATA/BIKERS/HIBOB.DAT"), rr::bob_frames);
            g.remaps = rr::read_rider_remaps(bytes, 9);
            g.palette = rr::Palette::from_raw(assets.read("DATA/PALETTE.RAW"));
            return bytes;
        });
        // Public quick-race order differs from the original course index.
        const int original_index[] = {4, 1, 2, 5, 3};
        // An interactive quick race is a Thrash race in the front end, so it
        // ends on the original results screens; tool runs (fixed ticks,
        // autopilot, captures, benchmarks) keep the standalone race below.
        const bool tool_run = run_ticks >= 0 || auto_drive || into_traffic || start_paused || benchmark ||
                              capturing || print_wiring || explicit_render_scale || !screenshot.empty() ||
                              opponents != 14;
        if (!quick_race || !tool_run) {
            boot.apply(480);
            QuickStart quick;
            if (quick_race) quick = {original_index[course], character, level + 1, explicit_bike ? bike : -1};
            result = run_game(assets, window, renderer, g, exe, screenshot, menu_frames, cli, menu_script,
                              net_invite, net_server, rtc, quick);
        } else {
            g.course_name = course_names[course];
            {
                const auto specs = rr::read_bike_specs(assets.read("DATA/BIKESPEC.RSC"));
                if (bike < 0 || size_t(bike) >= specs.size())
                    throw std::runtime_error("Bike index out of range");
                for (const auto& o : rr::original_opponents(exe, specs, level, opponents)) (void)o;
            }
            g.config.course = g.course_name;
            g.config.level = level;
            g.config.bike = bike;
            g.config.character = character;
            g.config.career = false;
            g.config.opponents = opponents;
            {
                // Launch-time 0x004078f0 jitter (see run_game) for rows 9..14.
                rr::Career launch;
                launch.rng = capturing ? 1u : uint32_t(std::time(nullptr));
                rr::reset_relations(launch, rr::read_career_tables(exe));
                g.config.spare_levels = launch.spare_levels;
            }
            g.load_course(assets, exe);
            // Dashboard target names: personality +0x165 -> 0x00464188 ->
            // string slot (0x00437860), as the career path resolves them.
            {
                const auto strings = rr::frontend_strings(
                    rr::read_string_resources(assets.read(rr::language_dll(rr::default_language()))));
                if (strings.size() > 317) g.pause_prompt = strings[317];
                g.restart();
                g.rider_names = rr::race_rider_names(exe, g.config, g.race(), strings, g.player_name);
            }
            if (print_wiring) { // plan 26 Task 3: wiring report after the first tick, then exit
                g.tick(0);
                const rr::RaceWiring w = g.race().wiring();
                std::printf("terrain=%d traffic=%d pedestrians=%d ring=%d crash_library=%d ride_library=%d "
                            "nitro=%d finish=%d personalities=%d racers=%zu\n",
                            int(w.terrain), int(w.traffic), int(w.pedestrians), int(w.ring),
                            int(w.crash_library), int(w.ride_library), int(w.nitro), int(w.finish),
                            int(w.personalities), g.race().riders().size());
                std::fflush(stdout);
                SDL_DestroyRenderer(renderer);
                SDL_DestroyWindow(window);
                SDL_Quit();
                return 0;
            }
            if (run_ticks >= 0) { // headless-style verification run
                const bool hunt = into_traffic;
                if (hunt) auto_drive = true;
                int car_crashes = 0, any_crash = 0;
                long t = 0;
                for (; t < run_ticks && !g.race().finished(); ++t) {
                    g.tick(hunt ? steer_into_traffic(g.race())
                                : (auto_drive ? rr::autopilot(g.race(), g.traffic()) : 0));
                    const auto& b = g.race().bike();
                    if (hunt && t % 1000 == 0) {
                        int active = 0;
                        if (g.traffic())
                            for (const auto& car : g.instance.traffic->cars())
                                if (car.active) ++active;
                        std::printf("hunt t=%ld pos=%d lat=%d speed=%d crash=%d reason=%d cars=%d\n", t,
                                    b.i32(0x1c), b.i32(0x28), b.i32(0xec), b.i32(0x2f4), b.i32(0x2f8),
                                    active);
                        std::fflush(stdout);
                    }
                    if (b.i32(0x2f4) != 0 && any_crash == 0) {
                        any_crash = 1;
                        std::printf("crash tick %ld reason %d speed %d pos %d lat %d\n", t, b.i32(0x2f8),
                                    b.i32(0xec), b.i32(0x1c), b.i32(0x28));
                        std::fflush(stdout);
                    } else if (b.i32(0x2f4) == 0) {
                        any_crash = 0; // re-arm after remount
                        car_crashes = 0;
                    }
                    if (b.i32(0x2f4) != 0 && b.i32(0x2f8) == 2 && car_crashes == 0) {
                        std::printf("car-crash tick %ld reason 2 speed %d pos %d\n", t, b.i32(0xec),
                                    b.i32(0x1c));
                        std::fflush(stdout);
                        ++car_crashes;
                    }
                }
                std::printf("end tick %ld pos %u of %zu finished %d place %d\n", t, g.race().position() >> 8,
                            g.race().route().size(), int(g.race().finished()), g.race().place(0));
                if (g.traffic()) {
                    int cars = 0, crossing = 0, controllers = 0;
                    for (const auto& car : g.instance.traffic->cars())
                        if (car.active) {
                            ++cars;
                            crossing += car.kind & 1;
                        }
                    for (const auto& c : g.instance.traffic->controllers()) controllers += c.active;
                    std::printf("traffic cars %d crossing %d intersections %d\n", cars, crossing,
                                controllers);
                }
                if (g.instance.pedestrians) {
                    int active = 0, crossing = 0, down = 0;
                    for (const auto& p : g.instance.pedestrians->pool())
                        if (p.active) {
                            ++active;
                            crossing += p.state == 1;
                            down += p.state == 2;
                        }
                    std::printf("pedestrians spawned %d active %d crossing %d down %d\n",
                                g.instance.pedestrians->spawned(), active, crossing, down);
                }
                std::fflush(stdout);
            }
            if (!into_traffic) {
                rr::Mixer mixer(48);
                rr::RaceMusicPlayer race_music(assets, mixer);
                std::unique_ptr<rr::SoundSystem> sound;
                if (run_ticks < 0) {
                    const auto settings =
                        capturing
                            ? boot_settings
                            : rr::load_settings(
                                  (std::filesystem::path(rr::pref_directory()) / "settings.txt").string());
                    if (!capturing && (!SDL_InitSubSystem(SDL_INIT_AUDIO) || !mixer.open_device()))
                        SDL_Log("Audio unavailable: %s", SDL_GetError());
                    sound = std::make_unique<rr::SoundSystem>(assets, mixer, rr::read_volume_curve(exe),
                                                              int(rr::read_exe_data(exe, 0x0046dce5, 1)[0]));
                    rr::SoundLevels levels;
                    levels.master = settings.master_volume;
                    levels.engine = settings.engine_volume;
                    levels.effects = settings.effects_volume;
                    levels.others = settings.others_volume;
                    levels.mute = settings.mute;
                    levels.swap_speakers = settings.swap_speakers;
                    sound->set_levels(levels);
                    sound->start_race(bike, character);
                    race_music.set_music_level(
                        settings.mute ? 0 : settings.music_volume * settings.master_volume / 100);
                    if (!capturing) race_music.play_race(original_index[course], "");
                    g.mixer = &mixer;
                    g.sound = sound.get();
                    g.race_music = &race_music;
                }
                boot.frame = 0;
                boot.apply(480);
                if (capturing) capture.open();
                run_race(g, renderer,
                         {.auto_drive = auto_drive,
                          .start_paused = start_paused,
                          .screenshot = screenshot,
                          .key_codes = boot_settings.controls,
                          .custom_keys = boot_settings.custom_controls,
                          .swap_punch_kick = boot_settings.swap_punch_kick,
                          .stick_deadzone = boot_settings.stick_deadzone,
                          .left_handed = boot_settings.left_handed,
                          .show_fps = boot_settings.show_fps,
                          .assets = &assets,
                          .render_scale = render_scale,
                          .settings = &boot_settings,
                          .capture = capturing ? &capture : nullptr,
                          .bench = benchmark ? &bench : nullptr},
                         boot);
                if (capturing) capture.finish();
                if (benchmark)
                    write_bench_report(bench, boot_settings, boot, window, renderer, g, course, render_scale);
                g.mixer = nullptr;
                g.sound = nullptr;
                g.race_music = nullptr;
            }
        }
    } catch (const std::exception& e) {
        SDL_Log("%s", e.what());
#ifdef RR_WEB_BROWSER
        EM_ASM(
            {
                Module.gameError = UTF8ToString($0);
                // Emscripten 6.0.10 retains the thrown CppException through this
                // catch. Copy its JS stack before end_catch releases the C++ object.
                // Do not retain its native pointer/refcount beyond the catch.
                const error = new Error(Module.gameError);
            if (typeof exceptionLast !== 'undefined' && exceptionLast?.stack)
                error.stack = exceptionLast.stack;
            Module.gameException = error;
            },
            e.what());
#endif
        result = 1;
    }
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return result;
}
