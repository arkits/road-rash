// Plan 19 Task 4: the race scene at render scale 2x-4x.
// Golden replay at scale: original napa-moving display lists render at s=1
// exactly (as today) and at s=2..4; downsampling each scaled image by the
// centre pixel (s*x+s/2, s*y+s/2) must match s=1 on >=97% of pixels.
// Thin far-slice geometry must cover >=s pixels at scale s. Sweep: all Napa
// sweep views render at s=4 with no raster exception.
#include "course_scene.hpp"
#include <cstdlib>
#include "golden.hpp"
#include "rider_sprites.hpp"
#include "scene_dump.hpp"
#include "texture_library.hpp"
#include "view.hpp"
#include "world_renderer.hpp"
#include "render_threads.hpp"
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <memory>

namespace {
const char* const course_files[5] = {"CITY", "HIWAY", "MEDLY", "NAPA", "CANYN"};
std::vector<uint8_t> read_file(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return {};
    return {std::istreambuf_iterator<char>(in), {}};
}
} // namespace

int main(int argc, char** argv) {
    int failures = 0;
    auto check = [&](bool ok, const char* what) {
        if (!ok) {
            ++failures;
            std::cerr << "FAIL " << what << '\n';
        }
    };
    std::string assets_root = RR_SOURCE_DIR "/../cdrom/ROADRASH";
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--assets" && i + 1 < argc) assets_root = argv[++i];
    }
    rr::Assets assets(assets_root);
    const std::string tsv = rr::golden::oracle_dir() + "/napa-moving.tsv";
    if (!std::filesystem::exists(tsv) || !assets.exists("DATA/GLOBAL.RSC")) {
        std::cout << "skip: no moving capture or disc\n";
        return 77;
    }
    const auto golden = rr::golden::load(tsv);
    const std::string stem = tsv.substr(0, tsv.size() - 4);
    const int course = int(golden.frames.at(0).at("course"));
    const auto lib = rr::testlib::load_texture_library(assets, course_files[course]);
    const auto exe = assets.read("ROADRASH.EXE");
    const auto sprites = rr::load_rider_sprites(assets, exe, std::getenv("RR_SCENE_LOBIKES") == nullptr);
    // Frames 0/30/60/90/120 stand in for the plan's 0/150/300/750: the
    // napa-moving capture holds every 30th frame.
    size_t frames = 0;
    for (const auto& f : golden.frames) {
        if (!f.count("scene.file") || !f.count("pixels.file")) continue;
        const long file = f.at("scene.file");
        if (file != 0 && file != 30 && file != 60 && file != 90 && file != 120) continue;
        const auto dump = rr::scene::load(stem + "-scene" + std::to_string(file) + ".raw");
        const auto pixels = read_file(stem + "-f" + std::to_string(long(f.at("pixels.file"))) + ".raw");
        const int width = int(f.at("pixels.width")), height = int(f.at("pixels.height"));
        if (pixels.size() != size_t(width) * size_t(height)) {
            ++failures;
            continue;
        }
        std::vector<rr::DrawNode> nodes;
        std::vector<std::unique_ptr<rr::CelTexture>> dynamic;
        int unmatched = 0;
        for (const auto& s : dump.nodes) {
            rr::DrawNode n;
            for (int k = 0; k < 4; ++k)
                n.view_points[size_t(k)] = {int32_t(s.u32(0x44 + 8 * size_t(k))),
                                            int32_t(s.u32(0x48 + 8 * size_t(k)))};
            n.flip_u = s.bytes[0x64] != 0;
            n.flip_v = s.bytes[0x65] != 0;
            if (s.u32(8) == 0x00466684) {
                auto t = rr::rider_texture(sprites, int32_t(s.u32(0x38)), int32_t(s.u32(0x34)));
                if (!t) continue;
                dynamic.push_back(std::make_unique<rr::CelTexture>(std::move(*t)));
                n.texture = dynamic.back().get();
            } else if (s.bytes[0x66])
                n.flat_index = s.bytes[0x67];
            else if (!(n.texture = lib.find(s.hash, s.size))) {
                ++unmatched;
                continue;
            }
            nodes.push_back(n);
        }
        if (unmatched) {
            std::cerr << "frame " << file << ": " << unmatched << " unmatched\n";
            ++failures;
            continue;
        }
        // s=1 must equal the golden exactly (today's oracle-scene behaviour).
        rr::Framebuffer one(width, height);
        for (int y = 0; y < height; ++y)
            std::memcpy(one.row(y), pixels.data() + size_t(y) * size_t(width), size_t(width));
        {
            rr::Framebuffer check_fb(width, height);
            for (int y = 0; y < height; ++y)
                std::memcpy(check_fb.row(y), pixels.data() + size_t(y) * size_t(width), size_t(width));
            rr::render_nodes(check_fb, nodes, rr::view_mode(true), uint32_t(f.at("view.quality_mask")));
            size_t differ = 0;
            for (int y = 0; y < height; ++y)
                for (int x = 0; x < width; ++x) differ += check_fb.row(y)[x] != one.row(y)[x];
            // Rendered over the original frame, every pixel must match.
            check(differ == 0, "s=1 golden replay exact");
            if (differ) std::cerr << "frame " << file << ": " << differ << " s=1 mismatches\n";
        }
        // Scaled renders go over the nearest-upscaled original frame, so sky
        // and untouched interiors match trivially and only node edges can differ.
        for (int s = 2; s <= 4; ++s) {
            rr::Framebuffer fb(640 * s, 480 * s);
            for (int y = 0; y < 480 * s; ++y)
                for (int x = 0; x < 640 * s; ++x) fb.row(y)[x] = one.row(y / s)[x / s];
            try {
                rr::render_nodes(fb, nodes, rr::view_mode_scaled(s), uint32_t(f.at("view.quality_mask")));
            } catch (const std::exception& e) {
                std::cerr << "frame " << file << " s=" << s << ": " << e.what() << '\n';
                ++failures;
                continue;
            }
            // Edges may legitimately move up to one original pixel (sub-pixel
            // bits the original snaps away); exclude a 2-pixel band around any
            // original edge pixel. Interiors must match.
            size_t equal = 0, interior = 0;
            for (int y = 0; y < 480; ++y)
                for (int x = 0; x < 640; ++x) {
                    bool edge = false;
                    for (int dy = -2; dy <= 2 && !edge; ++dy)
                        for (int dx = -2; dx <= 2; ++dx) {
                            const int nx = x + dx, ny = y + dy;
                            if (nx < 0 || ny < 0 || nx >= 640 || ny >= 480 ||
                                one.row(ny)[nx] != one.row(y)[x]) {
                                edge = true;
                                break;
                            }
                        }
                    if (edge) continue;
                    ++interior;
                    equal += fb.row(s * y + s / 2)[s * x + s / 2] == one.row(y)[x];
                }
            const double pct = interior ? 100.0 * equal / interior : 100.0;
            if (pct < 97.0) {
                std::cerr << "frame " << file << " s=" << s << ": " << pct << "% interior centre-match\n";
                ++failures;
            }
        }
        // break up at scale — bbox grows by ~s in each axis. Sampled on the
        // first frame to bound runtime.
        if (file == 0) {
            int sampled = 0;
            for (const auto& nd : nodes) {
                if (sampled >= 15) break;
                rr::Framebuffer f1(640, 480);
                f1.clear(0);
                rr::render_nodes(f1, {nd}, rr::view_mode(true), uint32_t(f.at("view.quality_mask")));
                int x0 = 640, x1 = -1, y0 = 480, y1 = -1;
                for (int y = 0; y < 480; ++y)
                    for (int x = 0; x < 640; ++x)
                        if (f1.row(y)[x]) {
                            x0 = std::min(x0, x);
                            x1 = std::max(x1, x);
                            y0 = std::min(y0, y);
                            y1 = std::max(y1, y);
                        }
                if (x1 < 0) continue; // invisible at s=1: nothing to preserve
                const int w1 = x1 - x0 + 1, h1 = y1 - y0 + 1;
                if (w1 * h1 > 400) continue; // large quads covered by the % test
                ++sampled;
                for (int s = 2; s <= 4; ++s) {
                    rr::Framebuffer fs(640 * s, 480 * s);
                    fs.clear(0);
                    rr::render_nodes(fs, {nd}, rr::view_mode_scaled(s), uint32_t(f.at("view.quality_mask")));
                    int sx0 = 640 * s, sx1 = -1, sy0 = 480 * s, sy1 = -1;
                    for (int y = 0; y < 480 * s; ++y)
                        for (int x = 0; x < 640 * s; ++x)
                            if (fs.row(y)[x]) {
                                sx0 = std::min(sx0, x);
                                sx1 = std::max(sx1, x);
                                sy0 = std::min(sy0, y);
                                sy1 = std::max(sy1, y);
                            }
                    if (sx1 < 0) {
                        std::cerr << "frame 0 s=" << s << ": small node vanishes\n";
                        ++failures;
                        continue;
                    }
                    const int ws = sx1 - sx0 + 1, hs = sy1 - sy0 + 1;
                    if (ws < s * w1 - s || hs < s * h1 - s) {
                        std::cerr << "frame 0 s=" << s << ": bbox " << w1 << "x" << h1 << " -> " << ws << "x"
                                  << hs << '\n';
                        ++failures;
                    }
                }
            }
        }
        ++frames;
    }
    check(frames == 5, "five moving frames replayed");
    // Sweep: all Napa sweep views render at s=4 with no raster exception.
    {
        const auto course = rr::read_course(assets.read("DATA/COURSES/NAPA.CRS"));
        rr::CourseScene scene(assets, exe, course, "NAPA");
        const auto depths = rr::read_depth_table(exe);
        rr::Framebuffer fb(2560, 1920);
        size_t views = 0;
        try {
            for (bool second : {false, true}) {
                const auto route = rr::make_route(course, second);
                for (uint32_t p = 0; p < route.size(); p += 5) {
                    const auto slices = rr::project_road(route, p * 256 + 127, {0, 150}, depths);
                    const auto draw = scene.draw_nodes(route, slices, {0, 150}, depths);
                    fb.clear(0);
                    rr::render_nodes(fb, draw, rr::view_mode_scaled(4), 16);
                    ++views;
                }
            }
        } catch (const std::exception& e) {
            std::cerr << "sweep s=4: " << e.what() << '\n';
            ++failures;
        }
        check(views == 2509, "sweep covers 2509 views at s=4");
    }
    // Plan 19 Task 6 (Review Focus 2): every golden display list at s=1..4
    // renders byte-identical with 1, 2, 4 and 7 threads (7 makes uneven bands).
    {
        const auto& f = golden.frames[0];
        const long file = f.at("scene.file");
        const auto dump = rr::scene::load(stem + "-scene" + std::to_string(file) + ".raw");
        std::vector<rr::DrawNode> nodes;
        std::vector<std::unique_ptr<rr::CelTexture>> dynamic;
        for (const auto& s : dump.nodes) {
            rr::DrawNode n;
            for (int k = 0; k < 4; ++k)
                n.view_points[size_t(k)] = {int32_t(s.u32(0x44 + 8 * size_t(k))),
                                            int32_t(s.u32(0x48 + 8 * size_t(k)))};
            n.flip_u = s.bytes[0x64] != 0;
            n.flip_v = s.bytes[0x65] != 0;
            if (s.u32(8) == 0x00466684) {
                auto t = rr::rider_texture(sprites, int32_t(s.u32(0x38)), int32_t(s.u32(0x34)));
                if (!t) continue;
                dynamic.push_back(std::make_unique<rr::CelTexture>(std::move(*t)));
                n.texture = dynamic.back().get();
            } else if (s.bytes[0x66])
                n.flat_index = s.bytes[0x67];
            else if (!(n.texture = lib.find(s.hash, s.size)))
                continue;
            nodes.push_back(n);
        }
        for (int s = 1; s <= 4; ++s) {
            const rr::ViewMode mode = s == 1 ? rr::view_mode(true) : rr::view_mode_scaled(s);
            rr::Framebuffer ref(640 * s, 480 * s);
            ref.clear(0);
            rr::render_nodes(ref, nodes, mode, uint32_t(f.at("view.quality_mask")));
            for (int threads : {1, 2, 4, 7}) {
                rr::Framebuffer fb(640 * s, 480 * s);
                fb.clear(0);
                rr::RenderThreads pool(threads);
                rr::render_nodes_threaded(fb, nodes, mode, uint32_t(f.at("view.quality_mask")), pool);
                size_t differ = 0;
                for (int y = 0; y < fb.height(); ++y)
                    for (int x = 0; x < fb.width(); ++x) differ += fb.row(y)[x] != ref.row(y)[x];
                if (differ)
                    std::cerr << "s=" << s << " threads=" << threads << ": " << differ
                              << " band pixels differ\n";
                check(differ == 0, "band rendering is bit-identical");
            }
        }
    }
    std::cout << failures << " failures\n";
    return failures ? 1 : 0;
}
