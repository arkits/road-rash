#include "race_setup.hpp"
#include "race_state.hpp"
#include "bike.hpp"
#include "career.hpp"
#include "exe_tables.hpp"
#include "scenery.hpp"
#include <algorithm>
#include <stdexcept>

namespace rr {
namespace {
// DZHR solid-object tables (plan 16 Task 2): one span list per route
// sample, from the scene's placed billboards through the pure per-sample
// helper. Centre is the route-frame road centre in Q8 (route offset plus
// the replay anchor), the same frame rider laterals use. Consumes no
// randomness: placement already ran once at course load, so the shared
// race stream is untouched and table builds are deterministic.
std::vector<std::vector<DzhrSolid>> dzhr_table(const std::vector<RouteSample>& route,
                                               const CourseScene& scene, int32_t road_base_q8) {
    std::vector<std::vector<DzhrSolid>> table(route.size());
    for (size_t i = 0; i < route.size(); ++i) {
        const RouteSample& s = route[i];
        table[i] =
            dzhr_solids_for_sample(route[i].offset * 256 + road_base_q8, scene.objects(s.section, s.local));
    }
    return table;
}
// 0x00463c78 rider-table row (0x40 bytes): weight +0xc, weapon base +0x18,
// fight stats +0x30..+0x3c. Returns the halved weight (rider value).
int32_t rider_value_for(const std::vector<uint8_t>& exe, int rider) {
    if (rider < 0 || rider > 9) throw std::runtime_error("Rider index out of range");
    const auto body = read_exe_data(exe, 0x00463c78 + uint32_t(rider) * 0x40 + 0xc, 4); // 0x00463c78
    return int32_t(uint32_t(body[0]) | uint32_t(body[1]) << 8) / 2;
}
} // namespace

AttackCodes read_attack_codes(const std::vector<uint8_t>& exe) {
    AttackCodes attack_codes{};
    for (int side = 0; side < 2; ++side) {
        const auto bytes = read_exe_data(exe, side ? 0x464518 : 0x4644f8, 32);
        for (int kind = 0; kind < 2; ++kind)
            for (int weapon = 0; weapon < 4; ++weapon)
                attack_codes[side][kind][weapon] = int8_t(bytes[(kind * 4 + weapon) * 4]);
    }
    return attack_codes;
}

NitroConfig nitro_from_spec(const BikeSpec& spec) {
    // 0x00405600 -> 0x0040b0e0(spec + 0x14c) copies the six words even when
    // they are all zero (no nitro: charge max 0 << 8 = 0, no charges); the
    // EXE defaults at 0x00465a28 apply only to a null spec.
    NitroConfig nitro;
    // +0x14c is spec word 83 (BIKESPEC: only the nitro bikes 7..10 carry
    // nonzero words there).
    nitro.recharge = spec.d[83];
    nitro.boost = spec.d[84];
    nitro.max = spec.d[85];
    nitro.charges = spec.d[86];
    nitro.minimum = spec.d[87];
    nitro.tail = spec.d[88];
    return nitro;
}

RaceAssets load_race_assets(const Assets& assets, const std::vector<uint8_t>& exe, const RaceConfig& config) {
    // Moved out of Game::load_course/restart (src/game_main.cpp) without the
    // course_name=="NAPA" condition: the original scene, dashboard and actors
    // on every course. The race starts and finishes at race_bounds.
    if (config.level < 0 || config.level > 4) throw std::runtime_error("race setup: level out of range");
    RaceAssets out;
    try {
        out.course =
            std::make_unique<Course>(read_course(assets.read("DATA/COURSES/" + config.course + ".CRS")));
    } catch (const std::exception& e) {
        throw std::runtime_error("DATA/COURSES/" + config.course + ".CRS: " + e.what());
    }
    // Player bike/rider through the career tables (0x00415fc0 character ->
    // rider, 0x004641c8 level_bike, 0x00463c78 rider row); quick races and
    // career races use the explicit bike index (the shop's purchased bike).
    // The level table applies only when bike_from_table is set (oracle
    // harness and course sweeps reproduce the original default-bike setup).
    // The original setup index is not derivable solely from the front-end
    // character: row identity lives in the bike/active rider record. Allow the
    // replay harness to supply it explicitly from recorded 0x00463c78 state.
    int bike_index = config.bike;
    int rider = config.player_rider;
    if (rider == -1)
        rider = character_rider(config.character >= 1 && config.character <= 8 ? config.character : 1);
    if (rider < 0 || rider > 9) throw std::runtime_error("race setup: player rider out of range");
    if (config.career && config.bike_from_table) {
        const CareerTables tables = read_career_tables(exe);
        const size_t slot = size_t(config.level) + size_t(rider) * 5; // 0x004641c8
        if (slot >= tables.level_bike.size())
            throw std::runtime_error("career level_bike: slot out of range");
        bike_index = tables.level_bike[slot];
    }
    try {
        out.specs = read_bike_specs(assets.read("DATA/BIKESPEC.RSC"));
    } catch (const std::exception& e) {
        throw std::runtime_error(std::string("DATA/BIKESPEC.RSC: ") + e.what());
    }
    if (bike_index < 0 || size_t(bike_index) >= out.specs.size())
        throw std::runtime_error("BIKESPEC: bike index out of range");
    out.physics = read_physics_tables(exe);
    out.rider_value = rider_value_for(exe, rider);
    out.bike_index = bike_index;
    // Bounds (plan 17 Task 1 traces the exact per-level nodes; until then the
    // grid slot-0 position and the route finish). The grid tables
    // (0x00464088/0x00464108) place slot 0 at sample 0x32.
    const auto route_a = make_route(*out.course, false);
    const auto route_b = make_route(*out.course, true);
    if (route_a.size() < 64 || route_b.size() < 64)
        throw std::runtime_error("race setup: route too short for a race");
    // Original per-course/per-level start and level-tagged finish line
    // (plan 17 Task 1, race_bounds); the route end remains only for course
    // files outside the five originals.
    out.bounds.start_position_q8 = 0x3200; // 0x00464088 slot 0
    out.bounds.finish_position_q8 = uint32_t((route_a.size() - 1) << 8);
    out.bounds.route_hint = 0;
    {
        static const char* originals[] = {"CITY", "HIWAY", "MEDLY", "NAPA", "CANYN"}; // 0x0046df58
        for (int c = 0; c < 5; ++c)
            if (config.course == originals[c]) {
                // Finish only: 0x00464218 is indexed by network slot (0
                // offline), so the player starts at 0x3200 on every level;
                // race_bounds' start reads the table by level and is unused.
                out.bounds.finish_position_q8 =
                    race_bounds(exe, *out.course, c + 1, config.level).finish_position_q8;
                break;
            }
    }
    try {
        out.scene = std::make_unique<CourseScene>(assets, exe, *out.course, config.course, 1, config.level);
    } catch (const std::exception& e) {
        if (!config.optional_scene)
            throw std::runtime_error("course scene " + config.course + ": " + e.what());
        out.scene.reset();
    }
    try {
        out.hud = std::make_unique<Hud>(assets, exe, bike_index, config.metric);
    } catch (const std::exception& e) {
        throw std::runtime_error("dashboard: " + std::string(e.what()));
    }
    try {
        out.actors = std::make_unique<ActorNodes>(assets, exe, config.high_resolution);
    } catch (const std::exception& e) {
        throw std::runtime_error("actors: " + std::string(e.what()));
    }
    out.crash = read_crash_config(exe);
    out.police = read_police_config(exe);
    if (assets.exists("DATA/CARS/" + config.course + ".CAR")) {
        // Free traffic from the course's own vehicle container (0x0044db80 /
        // 0x004465f0) WITH its EXE descriptors (views + playback banks).
        try {
            out.vehicles =
                std::make_unique<VehicleLibrary>(assets.read("DATA/CARS/" + config.course + ".CAR"), exe);
        } catch (const std::exception& e) {
            if (!config.optional_traffic)
                throw std::runtime_error("DATA/CARS/" + config.course + ".CAR: " + e.what());
            out.vehicles.reset(); // game logs the course at fallback time (finding 2)
        }
    }
    out.attack_codes = std::make_unique<AttackCodes>(read_attack_codes(exe));
    out.personalities = read_personalities(exe);
    return out;
}

std::vector<std::string> race_rider_names(const std::vector<uint8_t>& exe, const RaceConfig& config,
                                          const Race& race, const std::vector<std::string>& strings,
                                          const std::string& player) {
    auto word = [&](uint32_t address) {
        const auto b = read_exe_data(exe, address, 4);
        return uint32_t(b[0]) | uint32_t(b[1]) << 8 | uint32_t(b[2]) << 16 | uint32_t(b[3]) << 24;
    };
    const std::array<std::string, 5> courses = {"CITY", "HIWAY", "MEDLY", "NAPA", "CANYN"};
    const auto course = std::find(courses.begin(), courses.end(), config.course);
    if (course == courses.end()) throw std::runtime_error("Unknown course for rider names");
    const uint32_t first = word(0x004635b8 + uint32_t(course - courses.begin()) * 4);
    std::vector<std::string> names;
    for (const auto& rider : race.riders()) {
        if (names.empty()) {
            names.push_back(player);
            continue;
        }
        // Police use a separate personality; no racer-name row is assigned.
        if (rider.cop) {
            names.emplace_back();
            continue;
        }
        const int index = rider.brain.overtake_index; // personality +0x165
        if (index < 0 || index > 15) {
            names.emplace_back();
            continue;
        }
        uint32_t slot = word(0x00464188 + uint32_t(index) * 4);
        if (index >= 10) {
            const uint32_t spare = uint32_t(index - 10);
            if (race.name_roll() == 42 || race.name_roll() == 69) {
                const uint32_t address = word((race.name_roll() == 42 ? 0x004635d0 : 0x004635e8) + spare * 4);
                std::string name;
                for (uint32_t i = 0; i < 128; ++i) {
                    const char c = char(read_exe_data(exe, address + i, 1)[0]);
                    if (!c) break;
                    name.push_back(c);
                }
                names.push_back(std::move(name));
                continue;
            }
            slot = first + spare;
        }
        names.push_back(slot < strings.size() ? strings[slot] : std::string());
    }
    return names;
}

int net_rider_count(int humans, int ai_bikers) {
    // 0x00405600:21-31: Full keeps the offline 0x10 (15 riders), Limited 9
    // (8 riders), humans only 0x00431840() + 1 (seats are contiguous here).
    const int riders = ai_bikers >= 2 ? 15 : ai_bikers == 1 ? 8 : humans;
    return std::max(riders, humans);
}

namespace {
// Plan 34: human seats 1..n-1 as player-built riders on grid rows 1..n-1
// (0x00464218, or 0x00464258 above four humans, 0x004056a6), and the A.I.
// on rows n..riders-1 with rider rows equal to their entry (0x00405600).
void net_field(const RaceAssets& assets, const std::vector<uint8_t>& exe, const RaceConfig& config,
               std::vector<Opponent>& opponents, int32_t& seat0_pos, int32_t& seat0_lat,
               std::vector<NitroConfig>& nitro) {
    const auto& humans = config.net.humans;
    const int n = int(humans.size());
    if (n < 1 || n > 8) throw std::runtime_error("net race: 1..8 humans");
    for (int k = 0; k < n; ++k)
        if (humans[size_t(k)].seat != k) throw std::runtime_error("net race: seats must be 0..n-1 in order");
    const int riders = net_rider_count(n, config.net.ai_bikers);
    const CareerTables tables = read_career_tables(exe);
    const uint32_t grid = n > 4 ? 0x00464258u : 0x00464218u;
    auto grid_row = [&](int seat, int32_t& pos, int32_t& lat) {
        const auto bytes = read_exe_data(exe, grid + 8u * uint32_t(seat), 8);
        lat = int32_t(uint32_t(bytes[0]) | uint32_t(bytes[1]) << 8 | uint32_t(bytes[2]) << 16 |
                      uint32_t(bytes[3]) << 24);
        pos = int32_t(uint32_t(bytes[4]) | uint32_t(bytes[5]) << 8 | uint32_t(bytes[6]) << 16 |
                      uint32_t(bytes[7]) << 24);
    };
    grid_row(0, seat0_pos, seat0_lat);
    auto all =
        original_opponents(exe, assets.specs, config.level, riders - 1); // rows 1..riders-1, slot = row
    std::vector<Opponent> out;
    for (int k = 1; k < n; ++k) {
        const HumanSeat& h = humans[size_t(k)];
        const int rider = character_rider(h.character >= 1 && h.character <= 8 ? h.character : 1);
        int bike = h.bike;
        if (bike < 0) bike = tables.level_bike.at(size_t(config.level) + size_t(rider) * 5);
        if (size_t(bike) >= assets.specs.size())
            throw std::runtime_error("net race: bike index out of range");
        // The human's own rider row: value, stats, weapon (0x00405c80); the
        // brain comes from the same row's personality, ready for a hand-over.
        Opponent o = original_opponents(exe, assets.specs, config.level, std::max(rider, 1)).back();
        if (rider == 0) o = original_opponents(exe, assets.specs, config.level, 1).front();
        o.spec = assets.specs[size_t(bike)];
        nitro.push_back(nitro_from_spec(o.spec));
        o.rider = rider;
        o.slot = k;
        grid_row(k, o.grid_pos, o.grid_lat);
        o.human = k;
        o.lane = -1;
        out.push_back(o);
    }
    for (Opponent& o : all)
        if (o.slot >= n) out.push_back(o);
    opponents = std::move(out);
}
} // namespace

RaceInstance build_race(const RaceAssets& assets, const std::vector<uint8_t>& exe, const RaceConfig& config) {
    RaceInstance out;
    // 1. Routes, Race ctor (player bike/rider, opponents, level, anchor,
    // personalities, EXE image); solo freezes every opponent node.
    auto route_a = make_route(*assets.course, false);
    auto route_b = make_route(*assets.course, true);
    auto opponents = original_opponents(exe, assets.specs, config.level, config.solo ? 14 : config.opponents);
    // 0x00405783..0x004057a4: Big Game leaves both place-table entries
    // empty for DAT_00467a6c; retain the remaining rows' original grid slots.
    if (config.career && config.mode == GameMode::big_game) {
        const int player =
            config.player_rider >= 0
                ? config.player_rider
                : character_rider(config.character >= 1 && config.character <= 8 ? config.character : 1);
        std::erase_if(opponents, [player](const Opponent& o) { return o.rider == player; });
    }
    if (config.solo)
        for (auto& o : opponents) o.frozen = true;
    int32_t seat0_pos = 0, seat0_lat = 0;
    std::vector<NitroConfig> human_nitro; // seats 1..n-1
    if (!config.net.humans.empty())
        net_field(assets, exe, config, opponents, seat0_pos, seat0_lat, human_nitro);
    auto race = std::make_unique<Race>(std::move(route_a), std::move(route_b), assets.physics,
                                       assets.specs[size_t(assets.bike_index)], assets.rider_value,
                                       std::move(opponents), config.level, config.road_base_q8,
                                       assets.personalities, exe, seat0_pos, seat0_lat);
    race->set_player_bike(assets.bike_index);
    if (!config.net.humans.empty()) race->set_net(true);
    race->set_pedestrian_setup_tick(config.pedestrian_setup_tick);
    // 0x00407990 builds rows 9..14 from the 0x004c5f20 copies: base and
    // current speed are the jittered +0x1bc[level], spd_b the five words.
    if (config.spare_levels) {
        for (auto& r : race->riders()) {
            if (!r.ai || r.cop || r.rider_idx < 9 || r.rider_idx > 14) continue;
            const auto& levels = (*config.spare_levels)[size_t(r.rider_idx - 9)];
            std::copy(levels.begin(), levels.end(), r.brain.spd_b);
            if (config.level >= 0 && config.level < 5)
                r.brain.base_speed = r.brain.current_speed = levels[size_t(config.level)];
        }
    }
    // 2. Prepare hook (demo hold) runs after the ctor, before the ring.
    if (config.prepare) config.prepare(*race);
    // 3. Pedestrians: ring-driven DZHR spawns take a static empty actor
    // source (as the harness does); otherwise the scene's DZHR records.
    // Looks always come from the scene's pedestrian looks.
    static const std::vector<DzhrActor> no_actors;
    if (assets.scene) {
        auto* scene = assets.scene.get();
        if (config.track_ring) {
            out.pedestrians = std::make_unique<PedestrianSystem>(
                [](const RouteSample&) -> const std::vector<DzhrActor>& { return no_actors; },
                [scene](const RouteSample& s, uint8_t selector) {
                    return scene->pedestrian_look(s.section, s.local, selector);
                },
                exe);
        } else {
            out.pedestrians = std::make_unique<PedestrianSystem>(
                [scene](const RouteSample& s) -> const std::vector<DzhrActor>& {
                    return scene->dzhr_actors(s.section, s.local);
                },
                [scene](const RouteSample& s, uint8_t selector) {
                    return scene->pedestrian_look(s.section, s.local, selector);
                },
                exe);
        }
        race->set_pedestrians(out.pedestrians.get());
        if (config.solo) race->set_freeze_actors(true);
    }
    // 3b. Base terrain and the ring-terrain overlay before the ring builds
    // its first slices (plan 34 Task 3: the simulation reads ring terrain
    // from the race, not from the shared scene).
    if (assets.scene) {
        auto* scene = assets.scene.get();
        race->set_terrain_source([scene](const RouteSample& sample) -> const TerrainSlice& {
            return scene->base_terrain(sample.section, sample.local);
        });
        race->set_ground_layout(scene->terrain_section_sizes());
    }
    // 4. RNG state, then the track ring (0x004552e0 setup on the loaded state).
    if (config.ring_rng_state) race->set_rng_state(*config.ring_rng_state);
    if (!config.net.humans.empty()) race->set_rng_state(config.net.seed); // every client: the server's seed
    if (config.track_ring) {
        out.ring = std::make_unique<TrackRing>(*assets.course, config.level);
        // Live slice statics for the 0x004373f0 sweep (boxes via the scene's
        // family residency, 0x0044b630).
        if (assets.scene) {
            auto* scene = assets.scene.get();
            Race* r = race.get();
            r->set_static_frames([scene](uint32_t section, uint32_t local, uint8_t selector, bool billboard) {
                return scene->static_frame(section, local, selector, billboard);
            });
            out.ring->set_static_sink(
                [r](int32_t sample, uint32_t section, uint32_t local, const RingStatics& statics) {
                    r->ring_statics(sample, section, local, statics);
                });
        }
        // Ring slices carry the race stream's terrain (0x00451910); the
        // scene's isolated-stream values stand in only where the ring has
        // not built a slice (splits/merges, the native quick setup).
        if (assets.scene) {
            auto* scene = assets.scene.get();
            Race* r = race.get();
            out.ring->set_slice_sink([scene, r](uint32_t section, uint32_t local, const PathSample& path,
                                                const TerrainSlice& slice) {
                scene->apply_ring_terrain(section, local, path, slice); // render view
                r->apply_ring_terrain(section, local, path, slice);     // simulation
            });
        }
        race->set_track_ring(out.ring.get());
        // Plan 34 C1: one window per further human seat. Statics go to the
        // race only for slices no other window holds; ground heights come
        // from seat 0's ring or the scene (as at splits).
        for (int seat = 1; seat < race->humans(); ++seat) {
            auto ring = std::make_unique<TrackRing>(*assets.course, config.level);
            Race* r = race.get();
            if (assets.scene)
                ring->set_static_sink(
                    [r, seat](int32_t sample, uint32_t section, uint32_t local, const RingStatics& statics) {
                        if (!r->sample_in_other_window(seat, sample))
                            r->ring_statics(sample, section, local, statics);
                    });
            race->add_human_ring(seat, ring.get());
            out.seat_rings.push_back(std::move(ring));
        }
    }
    // 4b. Rider build draws (0x00405600 after the ring): lanes, name roll.
    race->build_draws();
    // 5. Finish line + terrain source.
    race->set_finish_q8(int32_t(assets.bounds.finish_position_q8));
    if (config.live_schedule && !config.solo) race->set_live_schedule(true);
    if (assets.scene) {
        auto* scene = assets.scene.get();
        race->set_terrain_source([scene](const RouteSample& sample) -> const TerrainSlice& {
            return scene->base_terrain(sample.section, sample.local);
        });
        race->set_rail_height_source(
            [scene](const RouteSample& sample) { return scene->rail_height(sample.section, sample.local); });
        // 5b. DZHR roadside solidity (plan 16 Task 2): solid billboards
        // collide through the class-1 hazard path; BORR spans stay test-only
        // until AI avoidance lands (the autopilot cannot dodge in-road spans).
        race->set_roadside(dzhr_table(race->route_for(0), *scene, config.road_base_q8),
                           dzhr_table(race->route_for(1), *scene, config.road_base_q8));
    }
    // 6. Traffic from the course's own vehicle container.
    if (assets.vehicles) {
        out.traffic = std::make_unique<TrafficSystem>(*assets.vehicles, exe);
        if (!config.net.humans.empty()) out.traffic->set_net(true); // x4 density, gates (plan 34 C3)
        out.traffic->configure(6);
        out.traffic->set_setup_tick(config.ring_setup_tick);
        race->set_traffic(out.traffic.get());
    }
    // 7. Crash library, ride library + attack codes, nitro from the player spec.
    race->set_crash_library(assets.actors->library(), assets.crash);
    race->set_ride_library(&assets.actors->library(), assets.attack_codes.get());
    race->set_nitro(nitro_from_spec(assets.specs[size_t(assets.bike_index)]));
    for (size_t k = 0; k < human_nitro.size(); ++k) race->set_human_nitro(int(k) + 1, human_nitro[k]);
    if (out.traffic && !config.net.humans.empty() && !config.net.cars) out.traffic->set_spawn_enabled(false);
    // 8. Police (0x00406b80 / 0x00407f30): periodic and DZHR cops, arrest -> busted.
    if (!config.solo) race->set_police(assets.police, assets.specs, &assets.actors->library());
    out.race = std::move(race);
    return out;
}

RaceConfig net_race_config(const std::string& course, int level, const NetRaceConfig& net) {
    RaceConfig c;
    c.course = course;
    c.level = level;
    c.career = true;
    c.bike_from_table = true; // each human rides the character's level bike (0x004641c8)
    c.character = net.humans.empty() ? 1 : net.humans[0].character;
    c.mode = GameMode::mano;
    c.net = net;
    return c;
}

RaceSetup make_race_setup(const Assets& assets, const std::vector<uint8_t>& exe, const RaceConfig& config) {
    RaceSetup setup;
    setup.assets = load_race_assets(assets, exe, config);
    setup.instance = build_race(setup.assets, exe, config);
    return setup;
}
void copy_race_presentation(RaceInstance& into, const RaceInstance& from, int seat) {
    auto copy = [](auto& target, const auto& from) {
        if (!from) {
            target.reset();
            return;
        }
        if (target)
            *target = *from;
        else
            target = std::make_unique<std::remove_reference_t<decltype(*from)>>(*from);
    };
    if (into.race)
        *into.race = from.race->presentation_copy(seat);
    else
        into.race = std::make_unique<Race>(from.race->presentation_copy(seat));
    copy(into.traffic, from.traffic);
    copy(into.pedestrians, from.pedestrians);
}

namespace {
uint32_t startup_script(int seat, int t) {
    uint32_t w = rr::control_accelerate;
    const int phase = (t / (29 + 13 * seat)) % 7;
    if (phase == 1 || phase == 5) w |= rr::control_left;
    if (phase == 3) w |= rr::control_right;
    if ((t + 11 * seat) % 61 < 5) w |= rr::control_punch;
    if ((t + 3 * seat) % 97 < 3) w |= rr::control_kick;
    if (t % 1300 > 1290) w |= rr::control_nitro;
    if (t % 2100 > 2060) w = rr::control_brake | rr::control_right;
    return w;
}

// Synthetic course: a winding road with crests, built in code.
std::vector<rr::RouteSample> startup_course(size_t n, int phase) {
    std::vector<rr::RouteSample> out(n);
    for (size_t i = 0; i < n; ++i) {
        rr::RouteSample s{};
        const int k = int(i) + phase;
        s.path.bend = ((k / 90) % 4 == 1) ? 6 : ((k / 90) % 4 == 3) ? -5 : 0;
        s.path.left = s.path.right = 500;
        s.path.shoulder_left = s.path.shoulder_right = 200;
        s.path.left_q8 = s.path.right_q8 = 512;
        s.section = uint32_t(i / 256);
        s.local = uint32_t(i % 256);
        out[i] = s;
    }
    return out;
}

rr::BikeSpec startup_spec(int variant) {
    rr::BikeSpec b{};
    b.d[1] = -800;
    b.d[2] = 1024;
    b.d[3] = 160;
    b.d[4] = 160;
    b.d[5] = 1;
    b.d[6] = 196;
    auto sp = [&](int offset) -> int32_t& { return b.d[size_t(7 + (offset - 0x1c4) / 4)]; };
    const int up[6] = {3000, 5000, 7000, 8500, 21504, 16777215},
              down[6] = {-999, 2500, 4500, 6500, 8000, 17152};
    for (int g = 0; g < 6; ++g) {
        sp(0x1d0 + 16 * g) = 256 - 8 * g + variant;
        sp(0x1d4 + 16 * g) = up[g];
        sp(0x1d8 + 16 * g) = down[g];
    }
    sp(0x248) = 20000 + 500 * variant;
    sp(0x250) = 38;
    sp(0x254) = 10;
    sp(0x258) = 60;
    sp(0x25c) = 64;
    sp(0x26c) = -60;
    sp(0x270) = 10;
    sp(0x274) = 10;
    sp(0x290) = 64;
    sp(0x294) = 320;
    sp(0x29c) = 11776;
    sp(0x2a0) = 50;
    sp(0x2a4) = 30;
    sp(0x2b0) = 120;
    sp(0x2b8) = 196;
    sp(0x2bc) = 2560;
    sp(0x2d0) = 346;
    sp(0x2d4) = 128;
    return b;
}

} // namespace
uint64_t network_self_test_hash() {
    auto tables = PhysicsTables::synthetic();
    tables.bike_object = {3,     4,    1, 0,    0,      0,    0,      0x12c, 0x100, 0x82, 8, 0x25, -0xa00,
                          0x100, 0x4a, 4, 0x18, 0x1e00, 0x40, 0x2300, 0xc0,  0x100, 0,    1, 1,    0};
    std::vector<Opponent> field;
    for (int k = 1; k < 12; ++k) {
        Opponent rider{startup_spec(k % 4), 80 + k, 90 + k, 80 + k, k % 3, k, k, 0, 0, false, 100, {}, k % 4};
        if (k < 8) rider.human = k;
        field.push_back(rider);
    }
    RaceInstance instance;
    instance.race = std::make_unique<Race>(startup_course(1024, 0), startup_course(1024, 45), tables,
                                           startup_spec(0), 90, field, 2);
    instance.race->set_net(true);
    std::array<uint32_t, 8> words{};
    uint64_t hash = 14695981039346656037ull;
    for (int tick = 0; tick < 180; ++tick) {
        if (tick < 60)
            instance.race->countdown_step();
        else {
            for (int seat = 0; seat < 8; ++seat) words[size_t(seat)] = startup_script(seat, tick);
            instance.race->step(words);
        }
        hash = (hash ^ state_hash(instance)) * 1099511628211ull;
    }
    return hash;
}

} // namespace rr
