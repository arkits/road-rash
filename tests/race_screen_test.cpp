// Plan 32 A/B: exercise the real screen loop with controlled frame times.
#include "race_screen.hpp"
#include "race_helpers.hpp"
#include "native_screens.hpp"
#include "pe_strings.hpp"
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <algorithm>

#ifdef __EMSCRIPTEN__
int main() {
    std::cout << "SKIP native SDL renderer lifecycle harness\n";
    return 77;
}
#else
namespace {
SDL_Event key(SDL_Keycode value) {
    SDL_Event e{};
    e.type = SDL_EVENT_KEY_DOWN;
    e.key.key = value;
    return e;
}
SDL_Event event(Uint32 type) {
    SDL_Event e{};
    e.type = type;
    return e;
}
} // namespace

int main() {
    int failures = 0;
    auto check = [&](bool ok, const char* message) {
        if (!ok) {
            ++failures;
            std::cerr << "FAIL " << message << '\n';
        }
    };
    const std::string root = RR_SOURCE_DIR "/../cdrom/ROADRASH";
    if (!std::filesystem::exists(root + "/ROADRASH.EXE")) return 77;
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) {
        std::cerr << SDL_GetError();
        return 1;
    }
    SDL_Window* window = SDL_CreateWindow("race-screen-test", 640, 480, SDL_WINDOW_HIDDEN);
    SDL_Renderer* renderer = window ? SDL_CreateRenderer(window, "software") : nullptr;
    if (!renderer) {
        std::cerr << SDL_GetError();
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    try {
        rr::Assets assets(root);
        const auto exe = assets.read("ROADRASH.EXE");
        rr::Settings settings;
        rr::DisplayDriver display;
        display.window = window;
        display.renderer = renderer;
        display.settings = &settings;
        rr::Game game;
        game.depth = rr::read_depth_table(exe);
        game.load_course(assets, exe);
        game.restart();
        check(game.race().riders().size() == 15, "screen builds player plus fourteen opponents");
        const auto names = rr::frontend_strings(rr::read_string_resources(assets.read("TEXT/ENU.DLL")));
        game.rider_names = rr::race_rider_names(exe, game.config, game.race(), names, game.player_name);
        check(game.rider_names.size() == 15 && !game.rider_names.back().empty(),
              "screen has names through row fourteen");
        // Autopilot should fight riders within reach, never shadowbox on a timer.
        {
            auto saved = game.race().riders();
            auto fixture = [&]() -> rr::Rider& {
                game.race().riders() = saved;
                auto& riders = game.race().riders();
                riders.resize(2);
                auto& me = riders[0];
                me.attack.timer = 0;
                me.finished = false;
                auto& target = riders[1];
                target.route = me.route;
                target.live = true;
                target.parked = false;
                target.finished = target.crash_active = target.half_detached = false;
                target.bike.i32(0x2f4) = 0;
                target.bike.i32(0x1c) = me.bike.i32(0x1c) + 0x20;
                target.bike.i32(0x28) = me.bike.i32(0x28) + 0x2000;
                target.bike.i32(0x20) = me.bike.i32(0x20);
                return target;
            };
            auto punches = [&]() { return (rr::autopilot(game.race(), nullptr) & rr::control_punch) != 0; };
            fixture();
            check(punches(), "autopilot fights a nearby rider on the right");
            fixture().bike.i32(0x28) = game.race().bike().i32(0x28) - 0x2000;
            check(punches(), "autopilot fights a nearby rider on the left");
            fixture().bike.i32(0x1c) = game.race().bike().i32(0x1c) - 0x20;
            check(punches(), "autopilot can strike a rider slightly behind");
            fixture().bike.i32(0x1c) = game.race().bike().i32(0x1c) + 0x40;
            check(!punches(), "no punch into empty road ahead");
            fixture().bike.i32(0x28) += 0x10000;
            check(!punches(), "no punch across distant lanes");
            fixture().bike.i32(0x20) += 0x3000;
            check(!punches(), "no punch at a different elevation");
            fixture().route ^= 1;
            check(!punches(), "no punch across road branches");
            fixture().parked = true;
            check(!punches(), "no punch at a parked rider");
            fixture().live = false;
            check(!punches(), "no punch at an inactive rider");
            fixture().crash_active = true;
            check(!punches(), "no punch at a crashed rider");
            fixture().half_detached = true;
            check(!punches(), "no punch at a detached rider");
            fixture().bike.i32(0x2f4) = 1;
            check(!punches(), "no punch at crash motion");
            fixture();
            game.race().riders()[0].attack.timer = 1;
            check(!punches(), "do not retrigger a swing in progress");
            fixture();
            game.race().riders().resize(1);
            check(!punches(), "no punch when alone even on tick zero");
            game.race().riders() = saved;
        }
        game.countdown = 0;
        rr::RaceOptions options;
        options.custom_keys = true; // zero bindings isolate the host keyboard
        options.show_fps = true;
        options.assets = &assets;
        {
            rr::RaceScreen screen(game, renderer, options, display);
            uint64_t now = 1000000000ull; // deliberately independent of SDL's clock
            screen.iterate(now);
            check(game.race().ticks() == 0, "first frame establishes its supplied timestamp");
            screen.iterate(now += 1000000);
            check(game.race().ticks() == 0, "zero-tick frame");
            screen.iterate(now += 16000000);
            check(game.race().ticks() == 1, "one-tick frame");
            screen.iterate(now += 34000000);
            check(game.race().ticks() == 3 && game.view_prev.tick == 2 && game.view_cur.tick == 3,
                  "two-tick catch-up snapshots remain adjacent");
            screen.iterate(now += 67000000);
            check(game.race().ticks() == 7 && game.view_prev.tick == 6 && game.view_cur.tick == 7,
                  "four-tick catch-up snapshots remain adjacent");
            screen.event(event(SDL_EVENT_WINDOW_FOCUS_LOST));
            screen.iterate(now += 60000000000ull);
            check(game.race().ticks() == 7 && !game.view_ready, "focus loss freezes and clears history");
            screen.event(event(SDL_EVENT_WINDOW_FOCUS_GAINED));
            screen.event(key(SDLK_RETURN));
            screen.iterate(now += 60000000000ull);
            check(game.race().ticks() == 7, "resume discards elapsed paused time");
            screen.iterate(now += 17000000);
            check(game.race().ticks() == 8 && game.view_prev.tick == 8 && game.view_cur.tick == 8,
                  "first resumed tick initializes both snapshots");
            screen.event(event(SDL_EVENT_RENDER_DEVICE_RESET));
            check(screen.iterate(now += 17000000), "renderer reset while active remains usable");
            game.race().set_finish_q8(int32_t(game.race().position()));
            for (int i = 0; i < 600 && !game.race().finished(); ++i) game.tick(rr::control_accelerate);
            check(game.race().finished(), "fixture reaches an actual race finish");
            screen.event(key(SDLK_RETURN));
            screen.iterate(now += 17000000);
            check(game.race().ticks() == 0 && game.countdown == rr::countdown_ticks,
                  "restart rebuilds and resets countdown");
            // 0x00415f70 race-start fade: 63 uploads at 14 ms block the race,
            // the 64th callback releases it and the loop clock starts then.
            const uint64_t fade_start = now;
            check(game.fade_lut && *game.fade_lut == rr::fade_in_lut(0), "fade-in starts black");
            screen.iterate(now = fade_start + 14000000ull * 30);
            check(game.countdown == rr::countdown_ticks && game.fade_lut &&
                      *game.fade_lut == rr::fade_in_lut(30),
                  "fade-in holds the countdown and follows the upload count");
            screen.iterate(now = fade_start + 14000000ull * 63 + 13000000);
            check(game.countdown == rr::countdown_ticks && *game.fade_lut == rr::half_palette_lut(),
                  "last fade-in upload is the half palette");
            screen.iterate(now = fade_start + 14000000ull * 64);
            check(game.countdown == rr::countdown_ticks && !game.fade_lut,
                  "fade release starts the clock without catch-up");
            screen.iterate(now += 17000000);
            check(game.countdown == rr::countdown_ticks - 1, "restart consumed once and countdown advances");
            screen.iterate(now += 34000000);
            check(game.countdown == rr::countdown_ticks - 3, "subsequent iterations preserve rebuilt race");
        }
        // 0x004431d0 pump messages and 0x00423408 quit: message drawn in the
        // race frame over the half palette, quit ends at once and fades out.
        {
            game.restart();
            game.countdown = 0;
            game.career = true;
            game.quit_prompt = "QUIT?";
            game.pause_prompt = "PAUSED";
            rr::RaceScreen screen(game, renderer, options, display);
            uint64_t now = 1000000000ull;
            screen.iterate(now);
            screen.iterate(now += 17000000);
            const int ticks = game.race().ticks();
            screen.event(key(SDLK_PAUSE));
            screen.iterate(now += 170000000);
            check(game.pause_message && game.banner_message == "PAUSED" && game.race().ticks() == ticks,
                  "Pause key freezes the race under string 317");
            screen.event(key(SDLK_A));
            screen.iterate(now += 17000000);
            check(!game.pause_message && game.banner_message.empty(), "any key resumes the pause message");
            screen.iterate(now += 17000000);
            const int before_quit = game.race().ticks();
            check(before_quit > ticks, "race runs again after the pause message");
            screen.event(key(SDLK_ESCAPE));
            screen.iterate(now += 17000000);
            check(game.quitting && game.banner_message == "QUIT?" && game.race().ticks() == before_quit,
                  "career Escape shows string 316 and freezes");
            screen.event(key(SDLK_ESCAPE));
            check(screen.iterate(now += 17000000), "quit confirm starts the fade-out");
            check(game.fade_lut && *game.fade_lut == rr::half_palette_lut() &&
                      game.race().ticks() == before_quit,
                  "quit fade-out starts from the half palette with no run-on ticks");
            const uint64_t fade = now;
            check(screen.iterate(now = fade + 14000000ull * 10) && *game.fade_lut == rr::fade_out_lut(10),
                  "quit fade-out follows the upload count");
            check(!screen.iterate(now = fade + 14000000ull * 64), "fade-out release ends the race");
            check(game.race().ticks() == before_quit, "no ticks during the quit fade-out");
            check(screen.result().outcome == rr::RaceOutcome::wrecked, "quit counts as wrecked");
            game.career = false;
        }
        // Live arrest -> screen outcome (plan 32 D) is covered by the riding
        // and on-foot arrest fixtures below: the periodic cop arrests the
        // parked player, the screen surfaces RaceOutcome::busted through the
        // 0xb4-tick run-on and fade-out, and Frontend queues the BUSTED video.
        // (A second parked-player fixture with the table bike relied on the
        // cop stopping inside the 0x00407860 window in a field no capture
        // covers; removed October 28, 2026 when the controller phase was
        // corrected. The arrest is verified on napa-cops-parked-20261026.)
        const rr::TextRenderer text(assets);
        auto frontend_data = rr::read_frontend_data(exe);
        rr::add_native_screens(frontend_data, false);
        rr::Career career;
        const auto strings = rr::frontend_strings(rr::read_string_resources(assets.read("TEXT/ENU.DLL")));
        rr::Frontend menu(assets, text, strings, {}, std::move(frontend_data), rr::read_career_tables(exe),
                          career, settings, "");
        options.menu = &menu;
        game.countdown = 0;
        {
            rr::RaceScreen screen(game, renderer, options, display);
            uint64_t now = 1000000000ull;
            screen.iterate(now);
            screen.event(key(SDLK_ESCAPE));
            screen.iterate(now += 17000000);
            check(menu.pause_open(), "screen opens and renders pause menu");
            for (const Uint32 type : {SDL_EVENT_RENDER_TARGETS_RESET, SDL_EVENT_RENDER_DEVICE_RESET}) {
                screen.event(event(type));
                check(screen.iterate(now += 17000000) && menu.pause_open(),
                      "reset while menu open rebuilds pause frame");
                const auto frame = rr::capture_race_frame(renderer);
                check(!frame.pixels.empty() &&
                          std::any_of(frame.pixels.begin(), frame.pixels.end(),
                                      [](uint32_t pixel) { return (pixel & 0xffffff) != 0; }),
                      "recovered pause frame has visible content");
            }
            const auto ticks = game.race().ticks();
            screen.event(key(SDLK_ESCAPE));
            screen.iterate(now += 60000000000ull);
            check(!menu.pause_open() && game.race().ticks() == ticks, "menu resume discards elapsed time");
            screen.iterate(now += 17000000);
            check(game.race().ticks() == ticks + 1, "menu resume advances normally next frame");
        }
        options.menu = nullptr;
        options.show_fps = false;
        std::array<uint8_t, 0x340> reference{};
        std::vector<uint32_t> original_frame;
        uint32_t reference_rng = 0;
        // Same no-input race at 30/60/120 presentation FPS and interpolation
        // on/off: check full bike storage and RNG after exactly 20 ticks.
        for (bool interpolate : {false, true})
            for (uint64_t hz : {30, 60, 120}) {
                game.restart();
                game.countdown = 0;
                game.enhancements.interpolate = interpolate;
                rr::RaceScreen screen(game, renderer, options, display);
                constexpr uint64_t start = 1000000000ull;
                screen.iterate(start);
                for (uint64_t frame = 1; frame <= hz / 3; ++frame)
                    screen.iterate(start + frame * 1000000000ull / hz + 1);
                check(game.race().ticks() == 20, "30/60/120 FPS advance exactly 20 ticks");
                if (!interpolate && hz == 30) {
                    reference = game.race().bike().m;
                    reference_rng = game.race().rng_state();
                    original_frame = rr::capture_race_frame(renderer).pixels;
                }
                check(game.race().bike().m == reference && game.race().rng_state() == reference_rng,
                      "presentation FPS and interpolation leave bike state and RNG unchanged");
                if (!interpolate)
                    check(game.view_drawn.camera_position_q8 == game.race().camera_position(),
                          "Original presentation uses current camera position");
                if (!interpolate)
                    check(rr::capture_race_frame(renderer).pixels == original_frame,
                          "Original pixels agree at 30/60/120 FPS");
            }
        // Plan 32 D / plan 24 Task 2: start with a real periodic cop, then
        // let the screen advance the chase and arrest. Carry its result into
        // the same frontend entry point used by the game host.
        game.config.level = 3;
        game.config.character = 1;
        game.config.bike_from_table = true;
        game.config.opponents = 7;
        game.config.career = false;
        game.load_course(assets, exe);
        settings.video_mode = 2;
        for (const bool on_foot : {false, true}) {
            game.restart();
            game.countdown = 0;
            game.career = true;
            while (game.race().ticks() < 4096) game.tick(0);
            check(game.race().cop_index() > 0 && !game.race().busted(),
                  "periodic cop enters the live game before the arrest");
            // A stamina knockdown leaves the bike usable; consuming all bike
            // condition would correctly end the screen as wrecked first.
            if (on_foot) {
                for (int tick = 0; tick < 1000 && !game.race().busted(); ++tick) {
                    const auto& cop = game.race().riders()[size_t(game.race().cop_index())];
                    if (game.race().bike().i32(0x1c) - cop.bike.i32(0x1c) < 5000) break;
                    game.tick(0);
                }
                check(!game.race().busted(), "cop approaches before the knockdown");
                game.race().knock_off(game.race().riders()[0], 5000, 0, 1);
            }
            rr::RaceScreen screen(game, renderer, options, display);
            uint64_t now = 1000000000ull;
            bool running = screen.iterate(now);
            bool walked = false;
            for (int frame = 0; running && !game.race().busted() && frame < 6000; ++frame) {
                running = screen.iterate(now += 16666667);
                walked |= game.race().bike().i32(0x2f4) == 2;
            }
            check(game.race().busted(), "screen ticks reach an actual police arrest");
            if (!game.race().busted())
                std::cerr << "arrest fixture: on_foot=" << on_foot << " tick=" << game.race().ticks()
                          << " wrecked=" << game.race().wrecked() << " finished=" << game.race().finished()
                          << " crash=" << int(game.race().bike().i32(0x2f4))
                          << " outcome=" << int(screen.result().outcome) << '\n';
            check(!on_foot || walked, "on-foot arrest reaches the walking state");
            check(!on_foot || game.race().bike().i32(0x2f4) == 2,
                  "cop arrests the player while still on foot");
            check(running && screen.result().outcome == rr::RaceOutcome::busted,
                  "screen maps the live arrest to busted before ending");
            const int arrest_tick = game.race().ticks();
            for (int frame = 0; running && frame < 100; ++frame) running = screen.iterate(now += 250000000);
            check(!running && game.race().ticks() >= arrest_tick + rr::race_end_delay - 15,
                  "arrest completes the end delay and palette fade");
            const int ended_tick = game.race().ticks();
            check(!screen.iterate(now += 1000000000ull) && game.race().ticks() == ended_tick,
                  "completed arrest screen cannot advance the race again");

            career.mode = rr::GameMode::big_game;
            career.state.level = uint8_t(game.config.level);
            career.state.character = 1;
            career.state.cash = 10000;
            career.state.qualified = 0;
            rr::FrontendEvent action;
            while (menu.poll(action)) {
            }
            const auto result = screen.result();
            menu.race_finished(result.outcome, result.place, {});
            check(menu.screen() == rr::screen_id::busted && career.state.cash == 8400,
                  "live arrest opens busted results and charges the level-four fine");
            check(career.state.qualified == 0, "arrest does not qualify a course");
            int busted_videos = 0;
            while (menu.poll(action))
                if (action.kind == rr::FrontendEvent::Kind::play_video &&
                    action.video.rfind("VIDEO/BUSTED", 0) == 0)
                    ++busted_videos;
            check(busted_videos == 1, "live arrest queues exactly one BUSTED movie");
            std::cout << (on_foot ? "On-foot" : "Riding")
                      << " arrest reaches screen result, fine and BUSTED movie\n";
        }
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        ++failures;
    }
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    std::cout << failures << " failures\n";
    return failures ? 1 : 0;
}
#endif
