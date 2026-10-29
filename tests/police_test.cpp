#include "police.hpp"
#include "assets.hpp"
#include "career.hpp"
#include "actor_descriptors.hpp"
#include "race_setup.hpp"
#include <fstream>
#include <sstream>
#include <array>
#include <map>
#include <tuple>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <utility>
static void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
int main() {
    try {
        rr::PoliceArrestInput in;
        in.found_cop = true;
        require(rr::police_arrest(in), "Stopped rider arrest");
        for (int speed : {299, 300}) {
            in.bike_speed = speed;
            require(rr::police_arrest(in) == (speed < 300), "Normal speed boundary");
        }
        in.crash = 2;
        for (int speed : {-1800, -1799, 1799, 1800}) {
            in.bike_speed = speed;
            in.cop_speed = speed;
            require(rr::police_arrest(in) == (speed > -1800 && speed < 1800), "Crashed speed boundary");
        }
        in.bike_speed = in.cop_speed = 0;
        for (int pos : {-512, -511, 511, 512}) {
            in.cop_position = pos;
            require(rr::police_arrest(in) == (pos > -512 && pos < 512), "Cop position boundary");
        }
        in.cop_position = 0;
        for (int speed : {-5000, -4999, 4999, 5000}) {
            in.cop_speed = speed;
            require(rr::police_arrest(in) == (speed > -5000 && speed < 5000), "Relative speed boundary");
        }
        in.cop_speed = 0;
        in.cop_crashed = true;
        require(!rr::police_arrest(in), "Crashed cop exclusion");
        in.cop_crashed = false;
        {
            // 0x00407f76..0x00407fc8 takes a wrapped int32 absolute value
            // and compares it as signed; only INT32_MIN stays negative.
            rr::PoliceArrestInput edge;
            edge.found_cop = true;
            edge.crash = 2;
            for (int32_t speed : {INT32_MIN, INT32_MIN + 1}) {
                edge.bike_speed = edge.cop_speed = speed;
                require(rr::police_arrest(edge) == (speed == INT32_MIN),
                        "Original crashed speed absolute-value wrap");
            }
            edge.crash = 0;
            edge.bike_speed = edge.cop_speed = 0;
            for (const auto positions :
                 {std::pair{INT32_MIN, 0}, std::pair{0, INT32_MIN}, std::pair{INT32_MAX, -1}}) {
                edge.cop_position = positions.first;
                edge.rider_position = positions.second;
                require(rr::police_arrest(edge), "Original position subtraction and absolute-value wrap");
            }
            edge.cop_position = INT32_MIN + 1;
            edge.rider_position = 0;
            require(!rr::police_arrest(edge), "Position next to absolute-value wrap rejected");
            edge.cop_position = edge.rider_position = 0;
            for (int32_t speed : {INT32_MIN, INT32_MIN + 1}) {
                edge.cop_speed = speed;
                require(rr::police_arrest(edge) == (speed == INT32_MIN),
                        "Original relative-speed absolute-value wrap");
            }
        }
        in.rider_flags = 0x10;
        require(!rr::police_arrest(in), "Rider flag exclusion");
        in.rider_flags = 0;
        in.bike_flags = 0x10;
        require(!rr::police_arrest(in), "Bike flag exclusion");
        in.bike_flags = 0;
        in.owner = 1;
        require(!rr::police_arrest(in), "Owner exclusion");
        in.owner = -1;
        require(!rr::police_arrest(in), "Remote owner does not decide local outcome");
        require(rr::police_spawn_due(4096, 0, 0, false) && !rr::police_spawn_due(4095, 0, 0, false) &&
                    !rr::police_spawn_due(4096, 0, 0, true) && !rr::police_spawn_due(4096, 1, 0, false) &&
                    !rr::police_spawn_due(4096, 0, 0x10, false),
                "Spawn boundaries");
        rr::Assets assets(RR_SOURCE_DIR "/../cdrom/ROADRASH");
        if (!assets.exists("ROADRASH.EXE")) {
            std::cout << "Police threshold tests passed; imported corpus absent\n";
            return 0;
        }
        const auto config = rr::read_police_config(assets.read("ROADRASH.EXE"));
        require(config.bikes[0] == 4 && config.stats[3] == 171 && config.stats[12] == 6 &&
                    config.stats[13] == 2200,
                "Imported opening police configuration");
        for (int level = 0; level < 5; ++level)
            for (int place = 0; place < 16; ++place)
                for (int personality = 0; personality < 10; ++personality) {
                    const auto pursuit = rr::police_pursuit(config, level, place, personality);
                    require(pursuit.personality[0x1a4 / 4] == config.personality[0x1bc / 4 + size_t(level)],
                            "Imported pursuit speed");
                    require(pursuit.personality[0x80 / 4 + size_t(personality) * 3] ==
                                pursuit.personality[2 + size_t(personality) * 3],
                            "Target/current pursuit relationship");
                }
        std::ifstream file(RR_SOURCE_DIR "/analysis/oracle/napa-police-spawn.tsv");
        if (file) {
            std::map<std::string, int64_t> values;
            std::string line, key;
            int64_t value;
            while (std::getline(file, line)) {
                std::istringstream row(line);
                if (row >> key >> value) values[key] = value;
            }
            const auto root = std::string("police.spawn.");
            require(values.at(root + "present") == 1, "Original cop spawn capture");
            const auto pursuit = rr::police_pursuit(config, int(values.at(root + "level")),
                                                    int(values.at(root + "player.place")),
                                                    int(values.at(root + "player.personality")));
            for (int offset : {0x4, 0x8, 0x7c, 0x80, 0x1a4, 0x1bc, 0x1c0, 0x1c4, 0x1c8, 0x1cc}) {
                std::ostringstream hex;
                hex << std::hex << offset;
                require(pursuit.personality[size_t(offset) / 4] == values.at(root + "brain." + hex.str()),
                        "Original police pursuit initialization");
            }
            const auto exe = assets.read("ROADRASH.EXE");
            const rr::ActorAnimationLibrary library(exe, assets.read("DATA/GLOBAL.RSC"));
            rr::PoliceSpawnInput input;
            input.level = int(values.at(root + "level"));
            input.player_place = int(values.at(root + "player.place"));
            input.player_personality = int(values.at(root + "player.personality"));
            input.position = int32_t(values.at(root + "bike.1c"));
            input.lane = int(values.at(root + "brain.18c"));
            input.lateral_q8 = 0;
            input.lane_target_q8 = 0;
            const auto actor =
                rr::make_police_actor(config, rr::read_physics_tables(exe),
                                      rr::read_bike_specs(assets.read("DATA/BIKESPEC.RSC")), library, input);
            const int bike_fields[] = {0x18,  0x1c,  0x20,  0x24,  0x28,  0xc0,  0xc4,  0xdc,  0xe0,  0xe4,
                                       0xe8,  0xec,  0xf0,  0xf4,  0x148, 0x14c, 0x150, 0x154, 0x15c, 0x160,
                                       0x168, 0x16c, 0x170, 0x174, 0x178, 0x17c, 0x180, 0x184, 0x188, 0x18c,
                                       0x190, 0x194, 0x198, 0x1a0, 0x1a4, 0x1a8, 0x1ac, 0x1b0, 0x1b4, 0x1b8,
                                       0x1d0, 0x1e0, 0x1f0, 0x200, 0x260, 0x2ec, 0x2f0, 0x2f4, 0x324, 0x328};
            for (int a = 0; a < 2; ++a)
                for (int offset : bike_fields) {
                    if (a && (offset == 0x1e0 || offset >= 0x208))
                        continue; // stats pointer and fields beyond the rider allocation
                    std::ostringstream hex;
                    hex << std::hex << offset;
                    const auto key = root + (a ? "rider." : "bike.") + hex.str();
                    const auto actual = (a ? actor.rider : actor.bike).i32(offset);
                    if (actual != values.at(key))
                        throw std::runtime_error(key + " local constructor native=" + std::to_string(actual) +
                                                 " original=" + std::to_string(values.at(key)));
                }
            std::cout << "Original opening Napa local police actor numeric fields match\n";
            for (int offset : {0xc, 0x30, 0x34, 0x38, 0x3c}) {
                std::ostringstream hex;
                hex << std::hex << offset;
                require(config.stats[size_t(offset) / 4] == values.at(root + "stats." + hex.str()),
                        "Original police rider statistics");
            }
            std::cout << "Original opening Napa police pursuit and stats match\n";
        }
        std::ifstream arrest_file(RR_SOURCE_DIR "/analysis/oracle/napa-police-arrest.tsv");
        if (arrest_file) {
            std::map<std::string, int64_t> values;
            std::string line, key;
            int64_t value;
            while (std::getline(arrest_file, line)) {
                std::istringstream row(line);
                if (row >> key >> value) values[key] = value;
            }
            require(values.at("police.arrest.count") == 1024 && values.at("police.arrest.restored") == 1,
                    "Complete and restored original arrest capture");
            for (int c = 0; c < 1024; ++c) {
                const auto root = "police.arrest." + std::to_string(c) + ".";
                auto get = [&](const char* key) { return values.at(root + key); };
                rr::PoliceArrestInput input;
                input.owner = int(get("owner"));
                input.crash = int(get("crash"));
                input.rider_flags = uint8_t(get("rider_flags"));
                input.found_cop = get("found") != 0;
                input.bike_speed = int32_t(get("bike_speed"));
                input.cop_speed = int32_t(get("cop_speed"));
                input.rider_position = int32_t(get("rider_position"));
                input.cop_position = int32_t(get("cop_position"));
                if (rr::police_arrest(input) != (get("arrested") != 0) ||
                    get("delay") != (get("arrested") ? 180 : 0))
                    throw std::runtime_error(root + "original arrest outcome mismatch");
            }
            std::cout << "1,024 original local arrest outcomes and delays match\n";
        }
        {
            // Live wiring (plan 32 D): a parked player on Napa level 3 meets the
            // periodic cop at tick 4096 (0x00407f30) and the cop pursues it
            // through the rider think/move path. The cop never takes a place.
            // Whether it then stops within the 0x00407860 arrest window depends
            // on the whole field (this game config builds a different field
            // from the oracle bootstrap's 14 opponents); the arrest itself is
            // verified on the original capture of this scenario below
            // (napa-cops-parked-20261026, arrest on tick 4552 as captured).
            const auto exe = assets.read("ROADRASH.EXE");
            rr::RaceConfig config;
            config.course = "NAPA";
            config.level = 3;
            config.character = 1;
            config.bike_from_table = true;
            config.career = false;
            rr::RaceSetup setup = rr::make_race_setup(assets, exe, config);
            auto& race = *setup.instance.race;
            require(race.police_enabled() && race.cop_index() < 0 && !race.busted(),
                    "Police enabled without a cop at the start");
            const int place_before = race.place(0);
            while (race.ticks() < 4095) {
                race.step(0);
                require(race.cop_index() < 0, "No cop before tick 4096");
            }
            race.step(0);
            require(race.ticks() == 4096 && race.cop_index() > 0, "Periodic cop spawns at tick 4096");
            const auto& cop = race.riders()[size_t(race.cop_index())];
            require(cop.cop && cop.ai && cop.bike.i32(0x324) == 2 && cop.bike.u8(0x328) == 15,
                    "Cop actor identity");
            require(cop.bike.i32(0x1c) < race.bike().i32(0x1c), "Periodic cop starts behind the player");
            require(race.place(0) <= race.riders().size(), "Cop does not take a place");
            (void)place_before;
            int arrested = -1, sirens = 0;
            bool pursued = false;
            for (int t = 0; t < 6000 && arrested < 0; ++t) {
                race.step(0);
                if (race.cop_index() >= 0) {
                    const auto& c = race.riders()[size_t(race.cop_index())];
                    pursued = pursued || (c.brain.state == 4 && c.brain.target_rider == 0);
                }
                for (const auto& e : race.take_sound_events())
                    sirens += e.code == rr::SoundEvent::siren && e.position != 0;
                if (race.busted()) arrested = t;
                if (std::getenv("RR_COP_TRACE") &&
                    t % std::max(1, std::atoi(std::getenv("RR_COP_TRACE"))) == 0 && race.cop_index() >= 0) {
                    const auto& c = race.riders()[size_t(race.cop_index())];
                    std::cout << t << " cop pos " << c.bike.i32(0x1c) << " spd " << c.bike.i32(0xec)
                              << " state " << c.brain.state << " tgt " << c.brain.target_rider << " player "
                              << race.bike().i32(0x1c) << "\n";
                }
            }
            std::cout << "cop present=" << (race.cop_index() >= 0) << " arrested_after=" << arrested << "\n";
            require(pursued, "Cop pursues the stopped player (state 4, target the player)");
            require(sirens > 0, "Cop posts siren events with its position");
        }
        struct CopCapture {
            const char* course;
            const char* name;
            int asserted_end, knock, end, arrest, rng_end, field_end;
        };
        // Original-capture comparisons (git-ignored `analysis/oracle/<name>.tsv`): level byte
        // 2, seed 1, the human player parked on the grid (zero controls), the original cop
        // spawned by 0x00407f30 at tick 4096. The cop's position and speed (and, with a
        // knock-off, the player's crash state) must match on every tick from the spawn to
        // `asserted_end`; later ticks are reported. `arrest` is the original arrest tick
        // (first tick of the 0xb4 race-end timer, read from `race.end_timer` when the
        // capture has it, else from the camera pull-back 0x00437c30), -1 for none before
        // `end`, 0 when it falls after the asserted span.
        //
        // The whole race is free-run from one RNG anchor at the end of tick 1 (the setup
        // stream and tick 1 are not reproduced). The original's stream differs between
        // runs because rider rows 9..14 carry the launch-time 0x004078f0 speed jitter,
        // drawn from srand(time(NULL)) (0x00448200) before the oracle pins the seed; the
        // AI overtake rolls (0x004092e0/0x00409460) then fire on different ticks. The
        // capture's frame-1 +0x1bc..+0x1c4 words of those rows are therefore race input
        // (RaceConfig::spare_levels). Offline 0x00405600 builds 14 opponents. The ring and
        // pedestrians of these human captures are set up while the tick counter reads 0.
        // The RNG must match on every tick to `rng_end` and every opponent's position and
        // speed to `field_end`:
        // - Napa (20261026 runs): parked reaches the capture end (arrest 4552,
        //   `race.end_timer` = 180 ticks, outcome 4); crashed shows no arrest
        //   through tick 5031 across two identical runs (player crash 1 at
        //   4400, on-foot, remount; the cop never closes to 512) with a
        //   single RNG-state gap at 4716 and full field/cop parity to 5030.
        //   Foot runs arrest on foot (4616 and 4544) with full RNG/field/cop
        //   parity to tick 4700.
        // - CITY (20261026): RNG matches to tick 954, opponents and cop match
        //   on every tick to the 4551 end; the original arrests at 4552
        //   (outcome 4, measured) but no arrest tick is asserted while City
        //   AI RNG stays open past 954.
        //   See docs/VERIFICATION.md for measured spans and capture provenance.
        // - CITY: to 234; tick 235 draws in the pedestrian think (0x004410b5), and rider
        //   row 11 differs from tick 148 (City AI not at parity). The cop matches to the end.
        for (const CopCapture& scenario :
             {CopCapture{"NAPA", "napa-cops-parked-20261026", 4730, 0, 4730, 4552, 4730, 4730},
              CopCapture{"CITY", "city-cops-parked-20261026", 4551, 0, 4551, 0, 954, 4551},
              CopCapture{"NAPA", "napa-cops-crashed-20261026", 5030, 4400, 5030, -1, 4715, 5030},
              CopCapture{"NAPA", "napa-cops-foot-20261026", 4700, 4460, 4700, 4616, 4700, 4700},
              CopCapture{"NAPA", "napa-cops-foot2-20261026", 4700, 4460, 4700, 4544, 4700, 4700},
              // Two launches one second apart (no cop: end 1200): rows 9..14 differ, so
              // the overtake draws fall on different ticks (245.. vs 403..).
              CopCapture{"NAPA", "napa-randlog1-20261018", 0, 0, 1200, -1, 1200, 1200},
              CopCapture{"NAPA", "napa-randlog2-20261018", 0, 0, 1200, -1, 1200, 1200},
              CopCapture{"NAPA", "napa-backward-ring-randlog-20261018", 0, 300, 700, -1, 700, 700}}) {
            std::ifstream capture(std::string(RR_SOURCE_DIR "/analysis/oracle/") + scenario.name + ".tsv");
            if (capture) {
                std::map<int, std::pair<int, int>> original; // tick -> cop (position, speed)
                std::map<int, int> player_crash;
                int crash0 = 0;
                int arrest_tick = -1; // first tick of the race-end timer (race.end_timer)
                std::map<int, std::pair<int, int>>
                    pos_speed;                   // dump rider index -> (position, speed) of the current frame
                std::map<int, int> row_now;      // dump rider index -> row (+0x1e0 -> 0x00463c78 + row*0x40)
                std::map<int, long long> rng_at; // tick -> original end-of-tick RNG
                std::map<int, std::map<int, std::pair<int, int>>>
                    field_at;                                        // tick -> row -> (position, speed)
                std::map<int, std::array<long long, 3>> levels_at_1; // frame 1: row -> +0x1bc..+0x1c4
                std::map<int, std::array<long long, 3>> levels_now;
                std::string line;
                int tick = 0, slot = -1;
                while (std::getline(capture, line)) {
                    if (!line.empty() && line.back() == '\r') line.pop_back();
                    const size_t tab = line.find('\t');
                    const std::string key = line.substr(0, tab);
                    const long long value = tab == std::string::npos ? 0 : std::atoll(line.c_str() + tab + 1);
                    if (key == "frame" || key == "end") {
                        if (slot >= 0 && tick >= 4096) {
                            original[tick] = pos_speed[slot];
                            player_crash[tick] = crash0;
                        }
                        for (const auto& [who, ps] : pos_speed)
                            if (row_now.count(who)) field_at[tick][row_now.at(who)] = ps;
                        if (tick == 1)
                            for (const auto& [who, levels] : levels_now)
                                if (row_now.count(who)) levels_at_1[row_now.at(who)] = levels;
                        pos_speed.clear();
                        row_now.clear();
                        levels_now.clear();
                        slot = -1;
                    } else if (key == "tick")
                        tick = int(value);
                    else if (key == "rng")
                        rng_at[tick] = value;
                    else if (key == "race.end_timer" && value > 0 && arrest_tick < 0)
                        arrest_tick = tick;
                    else if (key == "police.slot")
                        slot = int(value);
                    else if (key.rfind("rider.", 0) == 0) {
                        const size_t dot = key.find('.', 6);
                        const int who = std::atoi(key.c_str() + 6);
                        const std::string field = key.substr(dot + 1);
                        if (field == "position")
                            pos_speed[who].first = int(value);
                        else if (field == "speed")
                            pos_speed[who].second = int(value);
                        else if (field == "crash" && who == 0)
                            crash0 = int(value);
                        else if (field == "raw_1e0")
                            row_now[who] = int((value - 0x00463c78) / 0x40);
                        else if (tick == 1 &&
                                 (field == "brain_1bc" || field == "brain_1c0" || field == "brain_1c4"))
                            levels_now[who][size_t((std::stoi(field.substr(6), nullptr, 16) - 0x1bc) / 4)] =
                                value;
                    }
                }
                const bool cop_span = scenario.end > 4096;
                require(rng_at.count(1) &&
                            (!cop_span || (original.count(4096) && original.count(scenario.end))),
                        "Cop capture covers the spawn and the asserted span");
                const auto exe = assets.read("ROADRASH.EXE");
                rr::RaceConfig config;
                config.course = scenario.course;
                config.level = 2;
                config.character = 1;
                config.bike_from_table = true;
                config.career = false;
                config.opponents = 14;            // offline 0x00405600 builds 14 (DAT_004642d4 = 0x10)
                config.pedestrian_setup_tick = 0; // ring setup and first pedestrian moves on tick counter 0
                config.ring_setup_tick = 0;       // crossing think on 1 + 30k (napa: 0x0041f040 at 751)
                const auto tables = rr::read_career_tables(exe);
                std::array<std::array<int32_t, 5>, 6> spare{};
                for (auto& row : spare)
                    for (size_t l = 0; l < 5; ++l) row[l] = tables.spare_levels[l];
                int spare_rows = 0; // levels 0..2 are dumped (level 2 is raced); 3..4 keep the base words
                for (const auto& [row, levels] : levels_at_1)
                    if (row >= 9 && row <= 14) {
                        for (size_t l = 0; l < 3; ++l) spare[size_t(row - 9)][l] = int32_t(levels[l]);
                        ++spare_rows;
                    }
                require(spare_rows == 6, "Capture dumps the six spare rows' level speeds");
                config.spare_levels = spare;
                rr::RaceSetup setup = rr::make_race_setup(assets, exe, config);
                auto& race = *setup.instance.race;
                int compared = 0, reported = 0, busted_at = -1, rng_differs = -1, field_differs = -1;
                const int knock = scenario.knock, end = scenario.end;
                while (race.ticks() < end) {
                    if (knock && race.ticks() == knock - 1) race.crash_now(race.riders()[0]);
                    race.step(0);
                    const int t = int(race.ticks());
                    if (t == 1) {
                        race.set_rng_state(uint32_t(rng_at.at(1)));
                        continue;
                    } // the one anchor
                    if (rng_differs < 0 && rng_at.count(t) && race.rng_state() != uint32_t(rng_at.at(t)))
                        rng_differs = t;
                    if (field_differs < 0 && field_at.count(t))
                        for (const auto& [row, ps] : field_at.at(t)) {
                            if (row < 1 || size_t(row) >= race.riders().size() ||
                                race.riders()[size_t(row)].cop)
                                continue;
                            const auto& b = race.riders()[size_t(row)].bike;
                            if (b.i32(0x1c) != ps.first || b.i32(0xec) != ps.second) {
                                field_differs = t;
                                std::cout << scenario.name << " rider row " << row << " differs at tick " << t
                                          << ": " << b.i32(0x1c) << "/" << b.i32(0xec) << " vs " << ps.first
                                          << "/" << ps.second << "\n";
                                break;
                            }
                        }
                    if (rng_differs > 0 && rng_differs <= scenario.rng_end)
                        throw std::runtime_error(std::string(scenario.name) +
                                                 ": RNG differs from the original at tick " +
                                                 std::to_string(rng_differs));
                    if (field_differs > 0 && field_differs <= scenario.field_end)
                        throw std::runtime_error(std::string(scenario.name) +
                                                 ": opponent differs from the original at tick " +
                                                 std::to_string(field_differs));
                    if (busted_at < 0 && race.busted()) busted_at = t;
                    if (t < 4096 || race.cop_index() < 0 || !original.count(t)) continue;
                    const auto& cop = race.riders()[size_t(race.cop_index())];
                    const auto want = original.at(t);
                    const bool differs = cop.bike.i32(0x1c) != want.first ||
                                         cop.bike.i32(0xec) != want.second ||
                                         (knock && race.bike().i32(0x2f4) != player_crash.at(t));
                    if (differs && t > scenario.asserted_end) {
                        if (!reported++)
                            std::cout << scenario.course << " known divergence from tick " << t
                                      << " (reported, not asserted)\n";
                        continue;
                    }
                    if (differs)
                        throw std::runtime_error(
                            "cop differs from the original at tick " + std::to_string(t) + ": position " +
                            std::to_string(cop.bike.i32(0x1c)) + " vs " + std::to_string(want.first) +
                            ", speed " + std::to_string(cop.bike.i32(0xec)) + " vs " +
                            std::to_string(want.second) + ", player crash " +
                            std::to_string(race.bike().i32(0x2f4)));
                    ++compared;
                }
                require(!cop_span || compared >= 400 || (knock && compared >= 150),
                        "Cop compared over the pursuit");
                const int arrest = arrest_tick >= 0 ? arrest_tick : scenario.arrest;
                if (arrest > 0 && arrest <= scenario.asserted_end) {
                    require(busted_at == arrest, "Native arrest on the original tick");
                    std::cout << scenario.course << " arrest on tick " << busted_at << " as the original\n";
                }
                if (arrest < 0 && busted_at >= 0)
                    throw std::runtime_error(std::string(scenario.name) + ": native arrest on tick " +
                                             std::to_string(busted_at) + " where the original has none");
                std::cout << scenario.name << ": cop matches on " << compared << " ticks; RNG matches to "
                          << (rng_differs < 0 ? end : rng_differs - 1) << ", opponents to "
                          << (field_differs < 0 ? end : field_differs - 1) << " (one anchor at tick 1)\n";
            } else
                std::cout << "skipped: " << scenario.name << " fixture absent\n";
        }
        {
            // Arrest while the rider walks (crash state 2, 0x00407f30): the half is the
            // walking body, so the cop must come within 512 of it.
            const auto exe = assets.read("ROADRASH.EXE");
            rr::RaceConfig config;
            config.course = "NAPA";
            config.level = 3;
            config.character = 1;
            config.bike_from_table = true;
            config.career = false;
            rr::RaceSetup setup = rr::make_race_setup(assets, exe, config);
            auto& race = *setup.instance.race;
            while (race.ticks() < 4096) race.step(0);
            require(race.cop_index() > 0, "On-foot scenario cop spawned");
            race.knock_off(race.riders()[0], 5000, 5000, 1);
            bool on_foot = false;
            int arrested = -1;
            for (int t = 0; t < 6000 && arrested < 0; ++t) {
                race.step(0);
                on_foot |= race.bike().i32(0x2f4) == 2;
                if (race.busted()) arrested = t;
                if (std::getenv("RR_COP_TRACE") && t % 60 == 0)
                    std::cout << "foot " << t << " crash " << race.bike().i32(0x2f4) << " body "
                              << race.riders()[0].crash_actor.motion.i32(0x1c) << " bflags "
                              << int(race.bike().u8(0xc0)) << " hflags "
                              << int(race.riders()[0].half_object.u8(0xc0)) << " bspd "
                              << race.bike().i32(0xec) << " cspd "
                              << (race.cop_index() >= 0
                                      ? race.riders()[size_t(race.cop_index())].bike.i32(0xec)
                                      : -1)
                              << " cop "
                              << (race.cop_index() >= 0
                                      ? race.riders()[size_t(race.cop_index())].bike.i32(0x1c)
                                      : -1)
                              << "\n";
            }
            std::cout << "on_foot=" << on_foot << " arrested_after=" << arrested << "\n";
            require(on_foot, "Rider reaches the on-foot state");
            require(arrested >= 0, "Cop arrests the rider walking beside the wreck");
        }
        std::cout << "Police spawn/arrest boundary tests passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
