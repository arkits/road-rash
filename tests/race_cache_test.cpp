// Plan 32 E: complete canvas output and persistent horizon regression.
#define RR_ALLOC_COUNTER_DEFINE
#include "alloc_counter.hpp"
#include "race_caches.hpp"
#include <algorithm>
#include <chrono>
#include <iostream>
#include <stdexcept>

static void check(bool ok, const char* message) {
    if (!ok) throw std::runtime_error(message);
}
static uint64_t hash_bytes(const void* data, size_t size) {
    auto* p = static_cast<const uint8_t*>(data);
    uint64_t h = 1469598103934665603ULL;
    for (size_t i = 0; i < size; ++i) {
        h ^= p[i];
        h *= 1099511628211ULL;
    }
    return h;
}
int main(int argc, char** argv) {
    const bool benchmark = argc == 2 && std::string(argv[1]) == "--benchmark";
    try {
        rr::Assets assets(RR_SOURCE_DIR "/../cdrom/ROADRASH");
        if (!assets.exists("DATA/COURSES/NAPA.CRS")) {
            std::cout << "No imported scene corpus\n";
            return 77;
        }
        rr::SurfacePtr surface(SDL_CreateSurface(640, 480, SDL_PIXELFORMAT_ARGB8888));
        check(bool(surface), SDL_GetError());
        std::unique_ptr<SDL_Renderer, decltype(&SDL_DestroyRenderer)> renderer(
            SDL_CreateSoftwareRenderer(surface.get()), SDL_DestroyRenderer);
        check(bool(renderer), SDL_GetError());
        rr::Game game;
        const auto exe = assets.read("ROADRASH.EXE");
        game.depth = rr::read_depth_table(exe);
        game.palette = rr::Palette::from_raw(assets.read("DATA/PALETTE.RAW"));
        game.config.course = "NAPA";
        game.config.opponents = 7;
        game.config.career = true;
        game.load_course(assets, exe);
        game.restart();
        check(bool(game.assets.scene) && bool(game.assets.hud), "Canvas test needs scene and dashboard");
        for (int tick = 0; tick < 300; ++tick) game.tick(rr::control_accelerate);
        const auto snapshot = game.race().snapshot();
        size_t row = 0;
        for (int scale : {1, 2, 4})
            for (bool wide : {false, true})
                for (bool rgb : {false, true}) {
                    game.widescreen = wide ? rr::Widescreen::mode16x9 : rr::Widescreen::off;
                    const auto slices =
                        rr::project_road(game.race().route(), game.race().camera_position(),
                                         game.race().camera(), game.depth, game.logical_width());
                    game.enhancements.full_colour = rgb;
                    rr::CourseCanvas canvas;
                    const auto cold_start = std::chrono::steady_clock::now();
                    canvas.draw(renderer.get(), game, slices, scale, 1);
                    const double cold_ms = std::chrono::duration<double, std::milli>(
                                               std::chrono::steady_clock::now() - cold_start)
                                               .count();
                    const uint64_t checksum =
                        rgb ? hash_bytes(canvas.rgb.row(0), size_t(canvas.scale_w) * canvas.scale_h * 4)
                            : hash_bytes(canvas.frame.row(0), size_t(canvas.scale_w) * canvas.scale_h);
                    size_t allocations = 0;
                    std::vector<double> timings;
                    timings.reserve(benchmark ? 20 : 1);
                    // Warm the pool/mip cache before measuring complete draws, including
                    // actors, HUD, horizon and upload through the software renderer.
                    canvas.draw(renderer.get(), game, slices, scale, 1);
                    for (int sample = 0; sample < (benchmark ? 20 : 1); ++sample) {
                        const auto start = std::chrono::steady_clock::now();
                        {
                            rr::test::CountAllocs counter;
                            canvas.draw(renderer.get(), game, slices, scale, 1);
                            allocations = counter.count();
                        }
                        timings.push_back(std::chrono::duration<double, std::milli>(
                                              std::chrono::steady_clock::now() - start)
                                              .count());
                    }
                    std::sort(timings.begin(), timings.end());
                    auto percentile = [&](size_t percent) {
                        return timings[(timings.size() * percent + 99) / 100 - 1];
                    };
                    canvas.draw(renderer.get(), game, slices, scale, 4);
                    const uint64_t threaded =
                        rgb ? hash_bytes(canvas.rgb.row(0), size_t(canvas.scale_w) * canvas.scale_h * 4)
                            : hash_bytes(canvas.frame.row(0), size_t(canvas.scale_w) * canvas.scale_h);
                    check(checksum == threaded, "Canvas differs between one and four threads");
                    std::cout << "row=" << row++ << " scale=" << scale << " wide=" << wide << " rgb=" << rgb
                              << " hash=" << checksum << " allocations=" << allocations
                              << " cold_ms=" << cold_ms << " p50=" << percentile(50)
                              << " p95=" << percentile(95) << " p99=" << percentile(99) << '\n';
                    // Index = scale slot*2 + wide. Entry 0 captured from fdcd4aa
                    // before this change; 4:3 entries 2 and 4 re-recorded 2026-10-15
                    // after 1b79351 (owner-approved): the needle pen band became
                    // 3*scale (was 2*scale+1), identical at scale 1 only. Wide entries
                    // (1, 3, 5) re-recorded 2026-10-15 after the Hor+ centring fix
                    // (owner-approved; already includes the 1b79351 needle): the old
                    // values encoded scenery shifted a margin off the road.
                    // All entries re-recorded 2026-10-16 (owner-approved): hill and
                    // wall terrain now comes from the live race stream via the track
                    // ring's slice builds (0x00451910), not the scene's own stream.
                    // Re-recorded 2026-10-17: ring-setup objects start on the setup
                    // tick (2) and pedestrians run during the hold, as both solo
                    // captures measure; one pedestrian pose differs at tick 300.
                    // Re-recorded 2026-10-17 (spawn-20261017): the rider build is
                    // ported after the ring (0x00405600 lane draws, 0x00405c80 brain
                    // spawn and straight-target placement), so riders stand on their
                    // original spawn laterals and opponents use their drawn lanes.
                    // Scale 2 and 4 entries re-recorded 2026-10-17 (sched-20261017):
                    // the field runs the live rider scheduler (0x00438170 cadences,
                    // window parking), so opponents move on their own intervals; the
                    // scale-1 frames are unchanged.
                    // Scale 2/4 rgb re-recorded 2026-10-26 (menu-gdi-20261026):
                    // the GDI ppem advance rule (textprobe ground truth) shifts
                    // HUD/dashboard glyphs by whole pixels; scene bytes
                    // (original_scene hashes) are unchanged.
                    // All rgb entries re-recorded 2026-10-31 (frontend-exact-20261031):
                    // HUD text renders through FreeType 2.13.3 1-bit glyphs (Wine-GDI
                    // ground truth, text-test) instead of stb coverage; original_scene
                    // hashes are unchanged.
                    static constexpr uint64_t rgb_baseline[] = {
                        10273367389924406753ULL, 8741764482650564446ULL,  11172590330531193530ULL,
                        5135492300604021233ULL,  12373773617984251365ULL, 14537787719708835766ULL};
                    if (rgb)
                        check(checksum == rgb_baseline[(row - 1) / 2],
                              "RGB canvas output changed from pre-optimization baseline");
                    // Dashboard overlays the scene; every opaque dashboard sample and
                    // the full Hor+ margins must survive composition.
                    const auto dash = game.assets.hud->compose(
                        game.assets.hud->state(game.race(), game.player_name, game.rider_names),
                        game.assets.physics, rr::DashMode::full, scale);
                    const int xoff = (game.logical_width() - 320) / 2 * 2 * scale;
                    rr::DashLut lut;
                    auto expect = [&](int x, int y, uint32_t c) {
                        if (c == 0x01000000) return;
                        if (rgb)
                            check(canvas.rgb.row(y)[x] == (0xff000000u | (c & 0xffffffu)),
                                  "RGB dashboard/panel pixel missing");
                        else
                            check(canvas.frame.row(y)[x] == lut.index(c, game.palette),
                                  "Indexed dashboard/panel pixel missing");
                    };
                    auto dark = [](uint32_t c) {
                        return (((c >> 16 & 255) / 2) << 16) | (((c >> 8 & 255) / 2) << 8) | ((c & 255) / 2);
                    };
                    for (int y = 0; y < dash.height; ++y) {
                        for (int x = 0; x < dash.width; ++x) expect(x + xoff, y, dash.at(x, y));
                        const auto lc = dash.at(0, y), rc = dash.at(dash.width - 1, y);
                        if (lc != 0x01000000)
                            for (int x = 0; x < xoff; ++x) expect(x, y, dark(lc));
                        if (rc != 0x01000000)
                            for (int x = xoff + dash.width; x < canvas.scale_w; ++x) expect(x, y, dark(rc));
                    }
                    // No-HUD Original rendering still follows the original scene path.
                    auto hud = std::move(game.assets.hud);
                    game.enhancements.full_colour = false;
                    canvas.draw(renderer.get(), game, slices, scale, 1);
                    const uint64_t no_hud =
                        hash_bytes(canvas.frame.row(0), size_t(canvas.scale_w) * canvas.scale_h);
                    // Wide entries (1, 3, 5) re-recorded 2026-10-15 after the Hor+ centring fix;
                    // all entries 2026-10-16 for ring-built terrain (owner-approved);
                    // all entries 2026-10-17 for the ported rider spawn placement.
                    static constexpr uint64_t original_scene[] = {
                        16522315210060068270ULL, 14663097353646083664ULL, 4900232200795323305ULL,
                        3274112107641630510ULL,  6150278981331920990ULL,  10581021226160029313ULL};
                    std::cout << "  original_scene=" << no_hud << '\n';
                    check(no_hud == original_scene[(row - 1) / 2], "Original scene output changed");
                    game.assets.hud = std::move(hud);
                    game.enhancements.full_colour = rgb;
                    // Compare retained horizon storage to a fresh reference frame,
                    // including negative and wrapped scroll.
                    rr::Framebuffer fresh(canvas.scale_w, canvas.scale_h);
                    canvas.draw_horizon_rgb(*game.assets.scene, game.palette, 0, game.logical_width());
                    for (int32_t scroll : {0, -721, 719, INT32_MIN, INT32_MAX}) {
                        game.assets.scene->draw_horizon(fresh, scroll, game.logical_width());
                        {
                            rr::test::CountAllocs counter;
                            canvas.draw_horizon_rgb(*game.assets.scene, game.palette, scroll,
                                                    game.logical_width());
                            check(counter.count() == 0, "Horizon allocates after warmup");
                        }
                        for (int y = 0; y < canvas.scale_h; ++y)
                            for (int x = 0; x < canvas.scale_w; ++x) {
                                const auto* c = &game.palette.rgbx[4 * fresh.row(y)[x]];
                                const uint32_t expected =
                                    0xff000000u | (uint32_t(c[0]) << 16) | (uint32_t(c[1]) << 8) | c[2];
                                check(canvas.rgb.row(y)[x] == expected,
                                      "Retained horizon differs from fresh conversion");
                            }
                    }
                }
        const auto after = game.race().snapshot();
        check(after.rng == snapshot.rng && after.tick == snapshot.tick &&
                  after.fields.size() == snapshot.fields.size(),
              "Canvas changed simulation state");
        for (size_t i = 0; i < after.fields.size(); ++i)
            check(after.fields[i].name == snapshot.fields[i].name &&
                      after.fields[i].value == snapshot.fields[i].value,
                  "Canvas changed simulation fields");
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
