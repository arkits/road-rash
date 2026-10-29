#include "course_scene.hpp"
#include <SDL3/SDL.h>
#include <filesystem>
#include <set>
#include <tuple>
#include <iostream>
int main(int argc, char** argv) {
    try {
        {
            // 0x00452710 returns 0x80000000 at an event endpoint. Its
            // low byte is zero, but 0x00454240 still flattens hill heights.
            rr::TerrainSlice input, output;
            input.type = output.type = 4;
            for (auto& point : input.points) point = {100, -64};
            rr::PathSample path{};
            path.zone_end = true;
            rr::ring_terrain_onto(output, path, input);
            for (const auto& point : output.points)
                if (point[1] != 0) throw std::runtime_error("Hill event endpoint not flattened");
            path.zone_end = false;
            rr::ring_terrain_onto(output, path, input);
            if (output.points != input.points) throw std::runtime_error("Ordinary hill flattened");
            path.marking_flags = 0x40;
            rr::ring_terrain_onto(output, path, input);
            if (output.points != input.points) throw std::runtime_error("Tunnel hill flattened");
        }
        rr::Assets assets(RR_SOURCE_DIR "/../cdrom/ROADRASH");
        if (!assets.exists("DATA/COURSES/NAPA.CRS")) {
            std::cout << "No imported scene corpus\n";
            return 0;
        }
        const auto exe = assets.read("ROADRASH.EXE");
        const auto depths = rr::read_depth_table(exe);
        {
            // Reported City gap at ~00:38: left lanes grow from 2 to 2+21/256
            // at sample 159. The right lane exposed background scenery because
            // the far lane grid used a different rounded left-lane count.
            const auto city = rr::read_course(assets.read("DATA/COURSES/CITY.CRS"));
            const rr::CourseScene city_scene(assets, exe, city, "CITY");
            const auto route = rr::make_route(city, false);
            std::set<uint8_t> pavement;
            const auto materials = rr::read_road_materials(exe, assets.read("DATA/GLOBAL.RSC"));
            for (const auto& entry : materials.entries)
                for (const auto& mip : entry.texture->mips)
                    pavement.insert(mip.pixels.begin(), mip.pixels.end());
            for (const int width : {320, 426}) {
                rr::Framebuffer frame(width * 2, 480);
                city_scene.draw_horizon(frame, 0, width);
                const auto slices = rr::project_road(route, 156 * 256 + 128, {0, 150}, depths, width);
                auto mode = rr::view_mode(true);
                mode.surface_width = mode.display_width = width * 2;
                rr::render_nodes(frame, city_scene.draw_nodes(route, slices, {0, 150}, depths, {}, width),
                                 mode, 16);
                for (int y = 190; y < 195; ++y)
                    for (int x = 550 + width - 320; x < 600 + width - 320; ++x)
                        if (!pavement.count(frame.row(y)[x]))
                            throw std::runtime_error(
                                "City sample 159 width transition exposes scenery inside road");
            }
        }
        const auto course = rr::read_course(assets.read("DATA/COURSES/NAPA.CRS"));
        rr::CourseScene scene(assets, exe, course, "NAPA");
        {
            // Reported web corruption at 2.6–2.8 miles, approaching Napa's
            // first merge. Inner hills from the other branch must not extend
            // across the road or turn into a full-screen green curtain.
            const auto route = rr::make_route(course, false);
            std::set<uint8_t> pavement;
            const auto materials = rr::read_road_materials(exe, assets.read("DATA/GLOBAL.RSC"));
            for (const auto& entry : materials.entries)
                for (const auto& mip : entry.texture->mips)
                    pavement.insert(mip.pixels.begin(), mip.pixels.end());
            for (int width : {320, 426}) {
                rr::Framebuffer sky(width * 2, 480), frame(width * 2, 480);
                scene.draw_horizon(sky, 0, width);
                auto mode = rr::view_mode(true);
                mode.surface_width = mode.display_width = width * 2;
                for (uint32_t sample : {940u, 945u, 970u, 975u}) {
                    scene.draw_horizon(frame, 0, width);
                    const auto slices = rr::project_road(route, sample * 256 + 128, {0, 150}, depths, width);
                    rr::render_nodes(frame, scene.draw_nodes(route, slices, {0, 150}, depths, {}, width),
                                     mode, 16);
                    for (int y = 20; y < 80; ++y)
                        for (int x = width - 10; x < width + 10; ++x)
                            if (frame.row(y)[x] != sky.row(y)[x])
                                throw std::runtime_error("Napa merge terrain covers the sky at " +
                                                         std::to_string(sample));
                    for (int y = 330; y < 350; ++y)
                        for (int x = width - 10; x < width + 10; ++x)
                            if (!pavement.count(frame.row(y)[x]))
                                throw std::runtime_error("Napa merge terrain covers the roadway at " +
                                                         std::to_string(sample));
                }
            }
        }
        std::map<unsigned, size_t> building_flags;
        size_t samples = 0, missing = 0, hill_samples = 0, nodes = 0, views = 0, objects = 0,
               missing_objects = 0;
        for (size_t section = 0; section < course.sections.size(); ++section)
            for (size_t local = 0; local < course.sections[section].samples.size(); ++local) {
                ++samples;
                const auto& terrain = scene.terrain(uint32_t(section), uint32_t(local));
                for (const auto& object : scene.objects(uint32_t(section), uint32_t(local))) {
                    ++objects;
                    const auto* frame = scene.object_frame(uint32_t(section), uint32_t(local), object);
                    if (!frame || !frame->texture) ++missing_objects;
                }
                if (terrain.type == 0)
                    ++building_flags[course.sections[section].samples[local].marking_flags];
                if (terrain.type != 4) continue;
                ++hill_samples;
                for (int side = 0; side < 2; ++side)
                    for (int band = 0; band < 3; ++band)
                        if (!scene.material(uint32_t(section), uint32_t(local), terrain.selectors[side], 1,
                                            uint8_t(band), 64 * 65536, 16 * 65536)) {
                            ++missing;
                            if (band == 0)
                                std::cerr << "Unresolved hill at " << section << ':' << local << " side "
                                          << side << " selector " << int(terrain.selectors[side]) << '\n';
                        }
            }
        rr::Framebuffer sweep(640, 480);
        for (bool second : {false, true}) {
            const auto route = rr::make_route(course, second);
            for (uint32_t p = 0; p < route.size(); p += 5) {
                const auto slices = rr::project_road(route, p * 256 + 127, {0, 150}, depths);
                const auto draw = scene.draw_nodes(route, slices, {0, 150}, depths);
                nodes += draw.size();
                ++views;
                if (p % 5 == 0) {
                    scene.draw_horizon(sweep, 0);
                    try {
                        for (const auto& node : draw) {
                            try {
                                rr::render_nodes(sweep, {node}, rr::view_mode(true), 16);
                            } catch (const std::exception& e) {
                                if (node.texture)
                                    std::cerr << node.texture->name << " bounds " << node.texture->bounds[0]
                                              << ',' << node.texture->bounds[1] << ','
                                              << node.texture->bounds[2] << ',' << node.texture->bounds[3]
                                              << " mip0 " << node.texture->mips[0].width << 'x'
                                              << node.texture->mips[0].height << '\n';
                                for (const auto& v : node.view_points) std::cerr << v.x << ',' << v.y << ' ';
                                std::cerr << " flips " << node.flip_u << ',' << node.flip_v << '\n';
                                throw;
                            }
                        }
                    } catch (const std::exception& e) {
                        throw std::runtime_error("Napa sample " + std::to_string(p) + " branch " +
                                                 std::to_string(second) + ": " + e.what());
                    }
                }
            }
        }
        // Every course, every route sample, at the camera fraction that puts
        // slice 32 farthest (frac 0: depth 8448, building forward lookups
        // reach the table's zero padding). HIWAY/CANYN once threw here.
        for (const char* name : {"CITY", "HIWAY", "MEDLY", "NAPA", "CANYN"}) {
            const auto other = rr::read_course(assets.read(std::string("DATA/COURSES/") + name + ".CRS"));
            const rr::CourseScene other_scene(assets, exe, other, name);
            size_t course_views = 0;
            for (bool second : {false, true}) {
                const auto route = rr::make_route(other, second);
                for (uint32_t p = 0; p + 1 < route.size(); ++p)
                    for (const uint32_t frac : {0u, 255u}) {
                        const auto slices = rr::project_road(route, p * 256 + frac, {0, 150}, depths);
                        try {
                            const auto draw = other_scene.draw_nodes(route, slices, {0, 150}, depths);
                            if (p % 97 == 0) rr::render_nodes(sweep, draw, rr::view_mode(true), 16);
                        } catch (const std::exception& e) {
                            throw std::runtime_error(std::string(name) + " sample " + std::to_string(p) +
                                                     " frac " + std::to_string(frac) + " branch " +
                                                     std::to_string(second) + ": " + e.what());
                        }
                        ++course_views;
                    }
            }
            std::cout << name << ": " << course_views << " views built\n";
        }
        // Mountain (terrain type 1) sides: 0x0040dfd0 walls and 0x0040d030
        // cliffs with water. Unported, these sections left the clear colour
        // below the 320-row horizon beside the road (Pacific Highway's first
        // section). The original CANYN capture pch-terrain-20261016 has no
        // index-0 pixel below row 320. Count them over views whose camera
        // sample is a mountain sample, at 4:3 and Hor+ 426.
        for (const char* name : {"HIWAY", "MEDLY", "CANYN"}) {
            const auto other = rr::read_course(assets.read(std::string("DATA/COURSES/") + name + ".CRS"));
            const rr::CourseScene other_scene(assets, exe, other, name);
            const auto route = rr::make_route(other, false);
            size_t views_checked = 0, bad_views = 0, worst = 0;
            for (const int32_t width : {320, 426}) {
                rr::Framebuffer frame(width * 2, 480);
                for (uint32_t p = 0; p + 40 < route.size(); p += 11) {
                    if (other_scene.terrain(route[p].section, route[p].local).type != 1) continue;
                    const auto slices = rr::project_road(route, p * 256 + 128, {0, 150}, depths, width);
                    other_scene.draw_horizon(frame, 0, width);
                    auto mode = rr::view_mode(true);
                    mode.surface_width = mode.display_width = width * 2;
                    rr::render_nodes(
                        frame, other_scene.draw_nodes(route, slices, {0, 150}, depths, {}, width), mode, 16);
                    size_t zeros = 0;
                    for (int y = 320; y < 480; ++y)
                        for (int x = 0; x < frame.width(); ++x) zeros += frame.row(y)[x] == 0;
                    ++views_checked;
                    worst = std::max(worst, zeros);
                    if (zeros * 100 > size_t(frame.width()) * 160) ++bad_views;
                }
            }
            std::cout << name << " mountain views: " << views_checked << ", " << bad_views
                      << " with over 1% clear colour below the horizon, worst " << worst << " pixels\n";
            if (!views_checked || bad_views)
                throw std::runtime_error(std::string(name) +
                                         ": mountain terrain left the clear colour below the horizon");
        }
        // Tunnels (mountain samples with marking flag 0x40, 0x00454240 type
        // 3): 0x0040dac0 arches, 0x0040d420 portals. Before they were ported
        // the sky showed through. The original CANYN capture
        // pch-tunnel-20261016 frames 3000-3150 show only arch above the road.
        // From inside a tunnel with 8 tunnel samples ahead, at most 5% of the
        // top 160 rows may still equal the bare horizon.
        for (const char* name : {"MEDLY", "CANYN"}) {
            const auto other = rr::read_course(assets.read(std::string("DATA/COURSES/") + name + ".CRS"));
            const rr::CourseScene other_scene(assets, exe, other, name);
            rr::Framebuffer sky(640, 480), frame(640, 480);
            other_scene.draw_horizon(sky, 0);
            size_t inside = 0, open = 0, worst = 0;
            for (bool second : {false, true}) {
                const auto route = rr::make_route(other, second);
                for (uint32_t p = 0; p + 9 < route.size(); ++p) {
                    if (!other_scene.tunnel(route[p].section, route[p].local) ||
                        !other_scene.tunnel(route[p + 8].section, route[p + 8].local))
                        continue;
                    const auto slices = rr::project_road(route, p * 256 + 128, {0, 150}, depths);
                    other_scene.draw_horizon(frame, 0);
                    rr::render_nodes(frame, other_scene.draw_nodes(route, slices, {0, 150}, depths),
                                     rr::view_mode(true), 16);
                    size_t same = 0;
                    for (int y = 0; y < 160; ++y)
                        for (int x = 0; x < 640; ++x) same += frame.row(y)[x] == sky.row(y)[x];
                    ++inside;
                    worst = std::max(worst, same);
                    if (same * 20 > size_t(640 * 160)) ++open;
                }
            }
            std::cout << name << " tunnel views: " << inside << ", " << open << " showing sky, worst "
                      << worst << " horizon pixels\n";
            if (!inside || open) throw std::runtime_error(std::string(name) + ": tunnel arch missing");
        }
        // Hor+ widescreen: road slices and every builder centre on the same
        // logical_width/2. Each node fully inside the 4:3 view must reappear
        // at 426 shifted by exactly 53 columns. Regression: panel inputs kept
        // their 320 default (sidewalk/hill strips tore) and nodes were shifted
        // a second time by the canvas margin (scenery stood in the road).
        for (const char* name : {"CITY", "NAPA"}) {
            const auto other = rr::read_course(assets.read(std::string("DATA/COURSES/") + name + ".CRS"));
            const rr::CourseScene other_scene(assets, exe, other, name);
            const auto route = rr::make_route(other, false);
            using Key = std::tuple<const rr::CelTexture*, int, bool, bool, std::array<int32_t, 8>>;
            auto key = [](const rr::DrawNode& n, int32_t dx) {
                std::array<int32_t, 8> v{};
                for (size_t k = 0; k < 4; ++k) {
                    v[2 * k] = n.view_points[k].x + dx;
                    v[2 * k + 1] = n.view_points[k].y;
                }
                return Key{n.texture, n.flat_index ? int(*n.flat_index) : -1, n.flip_u, n.flip_v, v};
            };
            size_t interior = 0, unmatched = 0;
            for (const rr::Camera camera : {rr::Camera{0, 150}, rr::Camera{-300, 170}})
                for (uint32_t p = 0; p + 40 < route.size(); p += 37) {
                    const auto narrow =
                        other_scene.draw_nodes(route, rr::project_road(route, p * 256 + 64, camera, depths),
                                               camera, depths, {}, 320);
                    const auto wide = other_scene.draw_nodes(
                        route, rr::project_road(route, p * 256 + 64, camera, depths, 426), camera, depths, {},
                        426);
                    std::multiset<Key> present;
                    for (const auto& n : wide) present.insert(key(n, 0));
                    for (const auto& n : narrow) {
                        bool inside = true;
                        for (const auto& v : n.view_points) inside &= v.x >= 8 * 65536 && v.x <= 312 * 65536;
                        if (!inside) continue;
                        ++interior;
                        const auto it = present.find(key(n, 53 * 65536));
                        if (it == present.end())
                            ++unmatched;
                        else
                            present.erase(it);
                    }
                }
            std::cout << name << " widescreen: " << interior << " interior nodes, " << unmatched
                      << " not at +53\n";
            if (!interior || unmatched)
                throw std::runtime_error(std::string(name) + ": widescreen nodes not centred with the road");
        }
        std::cout << samples << " terrain samples, " << hill_samples << " hill samples, " << missing
                  << " unresolved hill materials, " << views << " views, " << nodes << " emitted nodes\n";
        for (const auto& entry : building_flags)
            std::cout << "Building flags " << entry.first << ": " << entry.second << " samples\n";
        std::cout << objects << " scenery objects, " << missing_objects << " unresolved scenery frames\n";
        if (missing_objects) throw std::runtime_error("Unresolved Napa scenery frame");
        if (missing) throw std::runtime_error("Unresolved Napa hill material");
        // Optional research preview, drawn entirely from native course state.
        if (argc > 1) {
            const auto route = rr::make_route(course, false);
            const uint32_t position = argc > 2 ? uint32_t(std::stoul(argv[2])) : 49 * 256;
            rr::Framebuffer fb(640, 480);
            scene.draw_horizon(fb, 0);
            const auto slices = rr::project_road(route, position, {0, 150}, depths);
            rr::render_nodes(fb, scene.draw_nodes(route, slices, {0, 150}, depths), rr::view_mode(true), 16);
            std::vector<uint8_t> rgba(640 * 480 * 4);
            fb.to_rgba(rr::Palette::from_raw(assets.read("DATA/PALETTE.RAW")), rgba.data(), 640 * 4);
            SDL_Surface* surface =
                SDL_CreateSurfaceFrom(640, 480, SDL_PIXELFORMAT_RGBA32, rgba.data(), 640 * 4);
            if (!surface || !SDL_SaveBMP(surface, argv[1])) throw std::runtime_error(SDL_GetError());
            SDL_DestroySurface(surface);
        }
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
