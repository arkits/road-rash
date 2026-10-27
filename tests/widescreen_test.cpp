// Plan 21 Task 8: Hor+ widescreen (failing test).
// The port's scene builders cull at the 320-wide logical view: an actor at
// logical x in [320,426) is dropped even when the caller asks for the 16:9
// width, and CourseScene::draw_nodes has no width parameter at all, so the
// side bands of a 16:9 frame stay empty. This test pins both seams:
//   1. actor_node at logical_width=426 keeps a side actor (today: nullopt).
//   2. draw_nodes renders side scenery at 426 (today: no width param;
//      emulated by filtering golden nodes through the port's 320 cull).
// The golden replay (centre-crop identity + side-content) is the PASS
// criterion after the fix and is kept in this file below.
#include "actor_render.hpp"
#include <cstdlib>
#include "animation.hpp"
#include "bike.hpp"
#include "course.hpp"
#include "course_scene.hpp"
#include "race.hpp"
#include "golden.hpp"
#include "rider_sprites.hpp"
#include "scene_dump.hpp"
#include "texture_library.hpp"
#include "view.hpp"
#include "world_renderer.hpp"
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
// Wide logical width for 16:9 at the same view height: 320*16/9/(4/3) = 426.
constexpr int kWide = 426;
constexpr int kSide = (kWide - 320) / 2; // 53 logical pixels per side
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
    // Seam 1: a side actor must survive at the wide width. Geometry comes
    // from a real race frame (route/camera/depths), not synthetic values:
    // run a 200-tick Napa autopilot race, then sweep a probe laterally from
    // the road centre until 320 culls it and require 426 to keep it.
    {
        const auto exe = assets.read("ROADRASH.EXE");
        const auto sprites = rr::load_rider_sprites(assets, exe, true);
        const auto depths = rr::read_depth_table(exe);
        const auto course = rr::read_course(assets.read("DATA/COURSES/NAPA.CRS"));
        const auto tables = rr::read_physics_tables(exe);
        const auto specs = rr::read_bike_specs(assets.read("DATA/BIKESPEC.RSC"));
        const auto route = rr::make_route(course, false);
        // One opponent so riders()[1] below indexes a real rider (a solo
        // race holds only the player; reading [1] is heap-buffer-overflow).
        auto opponents = rr::original_opponents(exe, specs, 0, 1);
        rr::Race race(route, rr::make_route(course, true), tables, specs.at(0), 90, std::move(opponents));
        for (int t = 0; t < 200; ++t) race.step(rr::control_accelerate);
        const auto slices = rr::project_road(route, race.camera_position(), race.camera(), depths);
        const auto& near = slices.at(4);
        // Road-relative projection of the opponent keeps the probe geometry
        // (depth/fraction/ground) live; the absolute lateral itself is swept
        // below, so which flank holds the side band does not matter.
        const auto& rd = race.riders()[1];
        const auto& sample = route.at(near.sample);
        rr::ActorProjection base;
        base.road_x = near.road[near.own].road_x + race.camera().x;
        base.far_x = slices.at(5).road[near.own].road_x + race.camera().x;
        base.road_y = near.road[near.own].world_y;
        base.far_y = slices.at(5).road[near.own].world_y;
        base.lateral = rd.bike.i32(0x28) - sample.offset * 256;
        base.ground = rd.bike.i32(0x24);
        base.depth = near.depth;
        base.fraction = uint8_t(rd.bike.i32(0x1c));
        base.occlusion = 240 * 65536;
        base.size = 512;
        const auto& state_lib = sprites;
        (void)state_lib;
        rr::AnimationFrame frame;
        frame.width = 64;
        frame.height = 64;
        frame.hotspot[0] = 32;
        frame.hotspot[1] = 64;
        rr::CelTexture tex;
        tex.bounds = {0, 0, 63, 63};
        rr::CelMip m;
        m.pixels.assign(64 * 64, 1);
        m.width = 64;
        m.height = 64;
        m.stride_shift = 6;
        tex.mips.push_back(std::move(m));
        const rr::Camera camera = race.camera();
        rr::ActorProjection probe = base;
        // Absolute lateral sweep: the rider's own lateral (road-relative)
        // sits far off-screen at this frame, so anchor the probe at the
        // road centre and scan the negative flank for the first lateral
        // the 320-wide view culls but the wide view keeps.
        probe.lateral = 0;
        int shifts = 0;
        for (; shifts < 2000; ++shifts) {
            probe.lateral = -shifts * 256;
            if (!rr::actor_node(frame, &tex, probe, camera, depths, 320).has_value()) break;
        }
        check(shifts < 1500, "cull edge inside the wide view");
        probe.lateral = -shifts * 256;
        check(!rr::actor_node(frame, &tex, probe, camera, depths, 320).has_value(),
              "side actor culled at 320 (original behaviour)");
        check(rr::actor_node(frame, &tex, probe, camera, depths, kWide).has_value(),
              "side actor visible at 426");
    }
    // Seam 2 + golden replay: 16:9 centre crop equals 4:3; side bands reveal.
    const auto golden = rr::golden::load(tsv);
    const std::string stem = tsv.substr(0, tsv.size() - 4);
    const int course = int(golden.frames.at(0).at("course"));
    const auto lib = rr::testlib::load_texture_library(assets, course_files[course]);
    const auto exe = assets.read("ROADRASH.EXE");
    const auto sprites = rr::load_rider_sprites(assets, exe, std::getenv("RR_SCENE_LOBIKES") == nullptr);
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
        const int wide_w0 = kWide * 4, wide_h0 = 960;
        rr::ViewMode wide_mode0 = rr::view_mode_scaled(2);
        wide_mode0.surface_width = wide_mode0.display_width = wide_w0;
        rr::Framebuffer refbig(wide_w0, wide_h0);
        rr::render_nodes(refbig, nodes, wide_mode0, uint32_t(f.at("view.quality_mask")));
        rr::Framebuffer ref(1280, 960);
        for (int y = 0; y < 960; ++y)
            for (int x = 0; x < 1280; ++x) ref.row(y)[x] = refbig.row(y)[x];
        const int wide_w = kWide * 4, wide_h = 960;
        // The port keeps side nodes at the wide width (draw_nodes takes
        // logical_width); shift into the centred 16:9 placement.
        std::vector<rr::DrawNode> shifted = nodes;
        for (auto& n : shifted)
            for (auto& v : n.view_points) v.x += int32_t(kSide * 65536);
        const int x0 = (wide_w - 1280) / 2;
        rr::Framebuffer wide(wide_w, wide_h);
        rr::ViewMode wide_mode = rr::view_mode_scaled(2);
        wide_mode.surface_width = wide_mode.display_width = wide_w;
        rr::render_nodes(wide, shifted, wide_mode, uint32_t(f.at("view.quality_mask")));
        size_t differ = 0;
        for (int y = 0; y < 960; ++y)
            for (int x = 0; x < 1280; ++x) differ += wide.row(y)[x0 + x] != ref.row(y)[x];
        check(differ == 0, "16:9 centre crop equals 4:3 frame");
        if (differ) std::cerr << "frame " << file << ": " << differ << " centre mismatches\n";
        {
            size_t side_pixels = 0, side_total = 0;
            for (int y = 0; y < 960; ++y)
                for (int x = 0; x < wide_w; ++x) {
                    if (x >= x0 && x < x0 + 1280) continue;
                    ++side_total;
                    const int rx = x < x0 ? 0 : 1279;
                    if (wide.row(y)[x] != ref.row(y)[rx]) ++side_pixels;
                }
            check(side_pixels > side_total / 100, "side bands reveal geometry");
            if (side_pixels <= side_total / 100)
                std::cerr << "frame " << file << " side-content=" << side_pixels << "/" << side_total << "\n";
        }
        ++frames;
    }
    check(frames > 0, "golden frames rendered");
    if (failures)
        std::cout << failures << " failures\n";
    else
        std::cout << "0 failures\n";
    return failures ? 1 : 0;
}
