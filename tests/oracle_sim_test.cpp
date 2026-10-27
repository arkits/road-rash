#include "assets.hpp"
#include <cstdlib>
#include "bike.hpp"
#include "career.hpp"
#include "course.hpp"
#include "exe_tables.hpp"
#include "golden.hpp"
#include "frozen_state.hpp"
#include "field_replay.hpp"
#include "race.hpp"
#include "race_setup.hpp"
#include "traffic.hpp"
#include "pedestrians.hpp"
#include <algorithm>
#include <set>
#include <cstdio>
#include <filesystem>
#include <functional>
#include <optional>
#include <sstream>
#include <iostream>

// Plan 10 task 5 step 2a (+ replay-harness corrections, Oct 12): DEFAULT is a
// single RNG anchor at the green light (set_rng_state once from the golden
// header, free-run after) so missing or extra rand() calls surface as rng
// divergence. --reanchor keeps the older per-tick re-anchoring as a
// diagnostic mode for localising field divergences; it MUST NOT be used for
// acceptance. --capabilities reports the replay contract the binary
// implements (see tools/verify_sim.py acceptance gate).
// Exit 0 on full match, 1 on divergence (first tick/field + last-8 RNG both
// sides), 77 when the golden is missing. Car/node comparison is task 7 work.
namespace {
// The replay builds its race through the shared rr::make_race_setup factory
// (plan 26 Task 2), the same mechanism the game uses: header-driven
// bike/rider selection (no SPEC-1 default), the course's own vehicle library
// WITH its EXE descriptors (traffic.cpp requires the EXE for views/playback),
// traffic + crash config attached to the Race, and grid-lateral placement
// from the game's own route data (no absolute course-specific anchor: Race
// lateralises from route offset + grid word, exactly as 0x00405990 reads
// sub-slice +0xcc plus the grid offset).
int32_t exe_i32(const std::vector<uint8_t>& exe, uint32_t va) {
    // .data section mapping of the reference executable (see read_physics_tables).
    constexpr uint32_t data_va = 0x00463000, data_raw = 0x61600, data_size = 0x16400;
    if (va < data_va || va + 4 > data_va + data_size || data_raw + (va - data_va) + 4 > exe.size())
        throw std::runtime_error("EXE word outside mapped sections");
    const size_t p = data_raw + (va - data_va);
    return int32_t(uint32_t(exe[p]) | uint32_t(exe[p + 1]) << 8 | uint32_t(exe[p + 2]) << 16 |
                   uint32_t(exe[p + 3]) << 24);
}
// Rider-table row (0x00463c78, 0x40 bytes): weight +0xc, weapon base +0x18,
// fight stats +0x30..+0x3c. Returns {value, weapon0, stats}.
struct RiderRow {
    int32_t value;
    int32_t weapon0;
    rr::RiderStats stats;
};
RiderRow read_rider_row(const std::vector<uint8_t>& exe, int rider) {
    if (rider < 0 || rider > 9) throw std::runtime_error("Rider index out of range");
    const uint32_t row = 0x00463c78 + 0x40u * uint32_t(rider);
    return {exe_i32(exe, row + 0xc) / 2,
            exe_i32(exe, row + 0x18),
            {exe_i32(exe, row + 0x30), exe_i32(exe, row + 0x34), exe_i32(exe, row + 0x38),
             exe_i32(exe, row + 0x3c)}};
}
// The intended per-tick comparison set (plan 10 acceptance): every native
// snapshot field for every live rider plus every traffic pool slot, plus
// rng. This is a REQUIRED set: a missing golden field is a FAIL (exit 1),
// never a skip-green. Golden keys are resolved through the era map below:
// the proxy schema grew over time (10-field era -> think/raw era -> full
// brain/bike era), so a native field maps to its golden key by absolute
// original offset. Player-think evidence (rider.0.think == 0x00434660 =
// 4408928, measured) is checked once at green: the branch is read from the
// original DAT_004753c8 probe (player.demo_hold) or the explicit
// --driver=human|demo flag — never from solo, the filename, or nonzero
// recorded input. Animation fields use absolute original ticks (deadlines
// accumulate from absolute ticks; no local-zero rebase). Golden-only
// extras (raw_/bike_ windows, node.*, probes) are diagnostic context,
// counted, never skipped.
const char* golden_key_for(const std::string& slot_prefix, const std::string& suffix,
                           const std::map<std::string, int64_t>& golden, std::string& out) {
    // Direct bike-half fields share names across eras. REQUIRED whenever
    // the proxy dumps that era's schema: the golden MUST carry the key or
    // the run FAILS (missing field = FAIL, never skip-green). The solo
    // golden's think/raw era predates lat_vel (bike +0x10c): the proxy
    // must add it (with car/police/animation fields) before acceptance.
    // Until then the era-gap rule in compare_fields counts it uncovered.
    static const char* direct[] = {"position",  "lateral", "height", "velocity", "speed", "gear",
                                   "condition", "crash",   "flags",  "lat_vel",  nullptr};
    if (suffix == "slice") {
        // Era rename: the oracle dumps bike +0x140 as slice_ptr (a host
        // pointer in current captures, a debug-only value). The sample
        // index is derived from the golden's position word (grid selector
        // = sample, 0x00405990) instead of comparing against the pointer.
        out = slot_prefix + "slice_ptr";
        return out.c_str();
    }
    if (suffix == "steer" || suffix == "steer_rate" || suffix == "ground" || false) {
        return nullptr;
    }
    // Animation/combat/nitro fields (plan 16): direct golden keys when the
    // proxy dumps them, else uncovered (reported, never failing).
    static const char* passthrough[] = {"anim",          "anim_frame",      "anim_queued", "anim_loops",
                                        "anim_deadline", "stamina_current", "stamina_max", nullptr};
    for (const char** d = passthrough; *d; ++d)
        if (suffix == *d) {
            out = slot_prefix + suffix;
            if (golden.find(out) == golden.end()) return nullptr;
            return out.c_str();
        }
    // Rider-half words (0x00434660/0x004096f0, object *(bike+0x2fc)): the
    // raw window rider.N.raw_<hex> covers +0x180..+0x1fc in the full era.
    static const struct {
        const char* suffix;
        const char* key;
    } half_map[] = {
        {"weapon", "raw_1c8"},
        {"steal_until", "raw_1cc"},
        {"hit_until", "raw_1d0"},
        {"attack_weapon", "raw_1d4"},
    };
    for (const auto& m : half_map)
        if (suffix == m.suffix) {
            out = slot_prefix + m.key;
            if (golden.find(out) == golden.end()) return nullptr;
            return out.c_str();
        }
    // Nitro state is global in the original (0x0040b110/0x0040b1a0); the
    // native keeps a per-rider charge, compared for the player only.
    if ((suffix == "nitro_charge" || suffix == "nitro_charges") && slot_prefix == "rider.0.") {
        out = suffix == "nitro_charge" ? "nitro.charge" : "nitro.charges";
        if (golden.find(out) == golden.end()) return nullptr;
        return out.c_str();
    }
    // AI offsets live in the brain window (rider.N.brain_<hex>, brain
    // +0x138..+0x1c4) in the full era; earlier eras lack them.
    static const struct {
        const char* suffix;
        const char* hex;
    } ai_map[] = {
        {"ai_target_x", "194"},     {"ai_target_position", "198"},
        {"ai_target_speed", "19c"}, {"ai_current_speed", "1a4"},
        {"ai_base_speed", "1bc"},   {"ai_replan", "188"},
        {"ai_lane", "18c"},         {"ai_bias", "190"},
    };
    for (const auto& m : ai_map)
        if (suffix == m.suffix) {
            out = slot_prefix + "brain_" + m.hex;
            if (golden.find(out) == golden.end()) return nullptr;
            return out.c_str();
        }
    return nullptr;
}
// --coverage: uncovered native field names (digits folded to N) with counts.
std::map<std::string, size_t> g_uncovered_names;
// Golden keys some comparison consumed (acceptance coverage ledger).
std::set<std::string> g_used_keys;
bool g_solo_nulled_cars = false; // solo capture whose frames carry no car.* rows
std::string fold_digits(const std::string& n) {
    std::string out;
    bool in = false;
    for (char c : n) {
        if (c >= '0' && c <= '9') {
            if (!in) out += 'N';
            in = true;
        } else {
            out += c;
            in = false;
        }
    }
    return out;
}
int compare_fields(const std::map<std::string, int64_t>& golden, const std::vector<rr::FieldValue>& native,
                   size_t frame, int64_t tick, const std::vector<std::pair<int64_t, uint64_t>>& rng_history,
                   size_t& uncovered, std::string& missing) {
    for (const auto& f : native) {
        const auto dot = f.name.rfind('.');
        const std::string prefix = f.name.substr(0, dot + 1);
        const std::string suffix = f.name.substr(dot + 1);
        auto it = golden.find(f.name);
        if (it != golden.end()) {
            g_used_keys.insert(f.name);
            if (it->second != f.value) {
                std::cout << "first divergence at frame " << frame << " (tick " << tick << "): " << f.name
                          << " original " << it->second << " native " << f.value << '\n';
                std::cout << "rng history (last " << rng_history.size() << " ticks, golden vs native):";
                for (const auto& e : rng_history) std::cout << " (" << e.first << "," << e.second << ")";
                std::cout << '\n';
                return 1;
            }
            continue;
        }
        // Car pool slots have no stable index across implementations: match
        // a native car row to the golden ring node with equal
        // kind+position+lateral, then compare the remaining field there.
        // Unmatched cars (or ambiguous matches) stay uncovered, reported
        // per frame, never skipped-green.
        // Solo captures null the car pool (no car rows in any frame): the
        // native pool must stay empty, so an inactive native row is covered
        // and an active one is a divergence.
        if (prefix.rfind("car.", 0) == 0 && g_solo_nulled_cars) {
            int64_t active = 0;
            for (const auto& n : native)
                if (n.name == prefix + "active") {
                    active = n.value;
                    break;
                }
            if (active != 0) {
                std::cout << "first divergence at frame " << frame << " (tick " << tick << "): " << prefix
                          << "active native " << active << " (solo capture has no cars)\n";
                return 1;
            }
            continue;
        }
        if (prefix.rfind("car.", 0) == 0 &&
            (suffix == "vehicle" || suffix == "lane" || suffix == "speed" || suffix == "phase")) {
            auto car_field = [&](const std::string& addr, const char* field) -> const int64_t* {
                auto jt = golden.find("ring." + addr + "." + field);
                return jt == golden.end() ? nullptr : &jt->second;
            };
            int64_t kind = -1, position = 0, lateral = 0;
            {
                auto jk = golden.find(prefix + "kind");
                auto jp = golden.find(prefix + "position");
                auto jl = golden.find(prefix + "lateral");
                if (jk == golden.end() || jp == golden.end() || jl == golden.end()) {
                    ++uncovered;
                    ++g_uncovered_names[fold_digits(f.name)];
                    continue;
                }
                kind = jk->second;
                position = jp->second;
                lateral = jl->second;
            }
            std::string match_addr;
            for (const auto& kv : golden) {
                if (kv.first.rfind("ring.", 0) != 0) continue;
                const size_t dot = kv.first.find('.', 5);
                if (dot == std::string::npos) continue;
                const std::string addr = kv.first.substr(5, dot - 5);
                auto k = car_field(addr, "kind"), p = car_field(addr, "position"),
                     l = car_field(addr, "lateral");
                if (k && p && l && *k == kind && *p == position && *l == lateral) {
                    if (!match_addr.empty()) {
                        match_addr.clear();
                        break;
                    } // ambiguous
                    match_addr = addr;
                }
            }
            auto want = match_addr.empty() ? nullptr : car_field(match_addr, suffix.c_str());
            if (want) g_used_keys.insert("ring." + match_addr + "." + suffix);
            if (!want) {
                ++uncovered;
                ++g_uncovered_names[fold_digits(f.name)];
                continue;
            }
            if (*want != f.value) {
                std::cout << "first divergence at frame " << frame << " (tick " << tick << "): " << f.name
                          << " original " << *want << " native " << f.value << " (via ring." << match_addr
                          << "." << suffix << ")\n";
                std::cout << "rng history (last " << rng_history.size() << " ticks, golden vs native):";
                for (const auto& e : rng_history) std::cout << " (" << e.first << "," << e.second << ")";
                std::cout << '\n';
                return 1;
            }
            continue;
        }
        std::string key;
        const char* mapped = golden_key_for(prefix, suffix, golden, key);
        // Era-coverage carve-out: car.* rows only exist when the proxy
        // dumps them (traffic pool absent in solo captures), and several
        // offsets predate the dumped eras (lat_vel/steer/ground/stamina/
        // weapon via no key in the think/raw era, slice via pointer,
        // ai/anim/nitro windows undumped). Those stay counted-as-uncovered
        // (reported, never skipped-green) so old captures still parse;
        // every other intended rider field is REQUIRED (missing = FAIL).
        // The proxy MUST add lat_vel + car/police/animation fields before
        // acceptance; then this carve-out shrinks to car-rows-absent only.
        const bool car_row = prefix.rfind("car.", 0) == 0;
        const bool era_gap = mapped == nullptr || golden.find(key) == golden.end();
        if (era_gap && (car_row || suffix == "lat_vel" || suffix == "steer" || suffix == "steer_rate" ||
                        suffix == "ground" || suffix == "stamina_current" || suffix == "stamina_max" ||
                        suffix == "weapon" || suffix == "slice" || suffix.rfind("ai_", 0) == 0 ||
                        suffix.rfind("anim", 0) == 0 || suffix == "attack_weapon" ||
                        suffix == "steal_until" || suffix == "hit_until" || suffix.rfind("nitro_", 0) == 0)) {
            ++uncovered;
            ++g_uncovered_names[fold_digits(f.name)];
            continue;
        }
        if (!mapped) {
            missing = f.name + " (no golden key in any era)";
            return 1;
        }
        auto jt = golden.find(key);
        if (jt == golden.end()) {
            missing = f.name + " (via " + key + ": absent)";
            return 1;
        }
        g_used_keys.insert(key);
        int64_t want = jt->second;
        if (suffix == "slice") {
            // Pointer-valued slice_ptr: derive the sample from position.
            auto pos = golden.find(prefix + "position");
            if (pos == golden.end()) {
                missing = f.name + " (no position for slice derive)";
                return 1;
            }
            want = pos->second >> 8;
        }
        if (want != f.value) {
            std::cout << "first divergence at frame " << frame << " (tick " << tick << "): " << f.name
                      << " original " << want << " native " << f.value << " (via " << key << ")\n";
            std::cout << "rng history (last " << rng_history.size() << " ticks, golden vs native):";
            for (const auto& e : rng_history) std::cout << " (" << e.first << "," << e.second << ")";
            std::cout << '\n';
            return 1;
        }
    }
    return 0;
}
} // namespace
int main(int argc, char** argv) {
    bool reanchor = false;
    std::string driver_cli;
    std::vector<std::string> args;
    int64_t ring_pre_cli = -1;
    bool coverage = false;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--reanchor")
            reanchor = true;
        else if (a == "--coverage")
            coverage = true;
        else if (a.rfind("--ring-pre-draws=", 0) == 0)
            ring_pre_cli = std::stoll(a.substr(17)); // diagnostic search only
        else if (a.rfind("--driver=", 0) == 0) {
            driver_cli = a.substr(9);
            if (driver_cli != "human" && driver_cli != "demo")
                throw std::runtime_error("--driver must be human|demo");
        } else if (a == "--capabilities") {
            // Replay contract (checked by tools/verify_sim.py acceptance):
            // human control words consumed every tick with a single RNG
            // anchor at green; the demo branch is an explicit --driver
            // choice, never inferred.
            std::cout << "human-controls-single-anchor\n";
            std::cout << "driver-axis\n";
            std::cout << "green-pre-step-anchor\n";
            std::cout << "strict-field-set\n";
            return 0;
        } else
            args.push_back(a);
    }
    const std::string file = args.size() > 0 ? args[0] : rr::golden::oracle_dir() + "/sim-napa-solo.tsv";
    if (!std::filesystem::exists(file)) {
        std::cout << "SKIP missing " << file << '\n';
        return rr::golden::skip;
    }
    const auto g = rr::golden::load(file);
    if (g.exe != "73d7f9744b5b1a9482b3909950cdf3fe34119fe13d023b031e9c6739a60f18a2")
        throw std::runtime_error("different executable");
    if (g.frames.empty()) throw std::runtime_error("no frames");
    const auto& h = g.frames.at(0);
    const auto ring_schema = h.find("ring.terrain.schema");
    const bool ring_terrain = ring_schema != h.end();
    if (ring_terrain && ring_schema->second != 1) throw std::runtime_error("unsupported ring.terrain.schema");
    auto get = [&](const char* k) {
        auto it = h.find(k);
        if (it == h.end()) throw std::runtime_error(std::string("header lacks ") + k);
        return it->second;
    };
    static const char* original_courses[] = {"CITY", "HIWAY", "MEDLY", "NAPA", "CANYN"}; // 0x0046df58
    const int course_entry = int(get("course"));
    if (course_entry < 1 || course_entry > 5) throw std::runtime_error("course out of range");
    const int level = int(get("level"));
    const std::string assets_root =
        args.size() > 1 ? args[1] : std::string(RR_SOURCE_DIR) + "/../cdrom/ROADRASH";
    if (!std::filesystem::exists(assets_root + "/ROADRASH.EXE")) {
        std::cout << "SKIP no disc\n";
        return rr::golden::skip;
    }
    // driver=human|demo is an axis independent of solo: solo isolates the
    // field (scheduler freeze), driver selects the 0x00434660 branch.
    // --driver defaults to the golden's measured player.demo_hold header
    // (the oracle's DAT_004753c8 sample); a CLI value overrides the header.
    // STOP: neither the solo flag, the filename, nor nonzero recorded input
    // proves the branch. All current captures predate the demo_hold probe
    // and carry no measurement: without --driver they are unverified.
    int measured_hold = -1; // player.demo_hold sampled across frames; -1 = unmeasured
    {
        bool any = false, all0 = true, all1 = true;
        for (const auto& f : g.frames) {
            auto it = f.find("player.demo_hold");
            if (it == f.end()) continue;
            any = true;
            if (it->second != 0) all0 = false;
            if (it->second != 1) all1 = false;
        }
        if (any) measured_hold = all0 ? 0 : all1 ? 1 : -2; // -2 = mixed (mid-capture transition)
    }
    // 0x00434660 player-think evidence: entry address + invocation counter
    // when the probe recorded them (player.think / player.think_count).
    // Measured in current captures: rider.0.think == 4225680 (0x00407a90,
    // the AI-think callee) because the solo golden runs the demo/AI side;
    // the player-think entry 0x00434660 (= 4408928) shows as rider.1.think
    // (the frozen opponent keeps the player-think dispatch word). New
    // captures MUST record player.think/player.think_count directly.
    auto hfind = [&](const char* k) {
        auto it = h.find(k);
        return it == h.end() ? -1 : it->second;
    };
    const int64_t think_entry = hfind("player.think");
    const int64_t think_count = hfind("player.think_count");
    const int64_t r0_think = hfind("rider.0.think"), r1_think = hfind("rider.1.think");
    const bool demo = !driver_cli.empty()  ? driver_cli == "demo"
                      : measured_hold == 1 ? true
                      : measured_hold == 0
                          ? false
                          : throw std::runtime_error(
                                "golden has no player.demo_hold measurement: pass --driver=human|demo");
    if (measured_hold == -2 && driver_cli.empty())
        throw std::runtime_error("golden player.demo_hold changes mid-capture: pass --driver=human|demo");
    const bool solo = h.find("solo") != h.end() && h.at("solo") != 0;
    if (solo) {
        bool any_car = false;
        for (const auto& fr : g.frames) {
            for (const auto& kv : fr)
                if (kv.first.rfind("car.", 0) == 0) {
                    any_car = true;
                    break;
                }
            if (any_car) break;
        }
        g_solo_nulled_cars = !any_car;
    }
    rr::Assets assets(assets_root);
    std::vector<uint8_t> exe = assets.read("ROADRASH.EXE");
    const std::string course_name = original_courses[course_entry - 1];
    // Grid lateral: the original places the player at sub-slice +0xcc (the
    // absolute road centre at the grid sample) plus the 0x00464218 grid
    // word. The native route stores centres relative to the course root,
    // so the harness passes the difference as the Race road_base_q8 anchor
    // via the shared rr::race_road_base_q8 helper (same ctor the game
    // uses; the game passes 0 = relative frame). Measured per golden (not
    // a course constant), reported every run.
    int32_t road_base = 0;
    {
        // Local course read for the sample offset (the factory reads the
        // course again for the race itself).
        const rr::Course course = rr::read_course(assets.read("DATA/COURSES/" + course_name + ".CRS"));
        const std::vector<rr::RouteSample>& ref = rr::make_route(course, false);
        auto hpos = h.find("rider.0.position"), hlad = h.find("rider.0.lateral");
        if (hpos != h.end() && hlad != h.end()) {
            const size_t sample = size_t(uint32_t(hpos->second) >> 8);
            const int32_t grid_word = exe_i32(exe, 0x00464218 + 8u * uint32_t(level));
            if (sample < ref.size())
                road_base = rr::race_road_base_q8(int32_t(hlad->second), ref[sample].offset, grid_word);
            std::cout << "road_base: " << road_base << " (golden lat " << hlad->second << " - route["
                      << sample << "].offset*256 - grid " << grid_word << ")\n";
        }
    }
    rr::RaceConfig config;
    config.course = course_name;
    config.level = level;
    config.character = 1;
    config.career = true;
    config.bike_from_table = true; // default-bike setup stream (finding 1, 2026-10-13)
    // Original setup row identity is not purely character 1: derive it from
    // the recorded rider-half table pointer when it lands in 0x00463c78..+0x280
    // on a 0x40 boundary.
    {
        constexpr int64_t row_base = 0x00463c78, row_count = 10, row_stride = 0x40;
        const int64_t base = (h.find("rider.0.raw_1e0") == h.end()) ? -1 : h.at("rider.0.raw_1e0");
        if (base >= row_base && base < row_base + row_count * row_stride &&
            ((base - row_base) % row_stride) == 0)
            config.player_rider = int((base - row_base) / row_stride);
    }
    config.solo = solo;
    // Offline 0x00405600 always builds 14 opponents (DAT_004642d4 = 0x10).
    config.opponents = 14;
    config.road_base_q8 = road_base;
    // Field captures (no solo freeze) run the live rider scheduler.
    config.live_schedule = !solo;
    // The field capture's setup pedestrians first run at tick 1 (frame-0
    // positions +36, one tick-1 move with dt 1 + interval), where the solo
    // captures measured next = 2 (see Race::set_pedestrian_setup_tick).
    if (!solo) config.pedestrian_setup_tick = 0;
    // Track ring (plan 10 Task 5): the original builds it inside the setup
    // stream, srand(seed) + ring.pre_draws (9 on the Napa solo captures:
    // the 9,098 ring-probe measured before 0x00453b60's window walks were
    // ported held 9,089 of their terrain draws), then 0x00405600 and the countdown
    // the rider build (0x00405600) draw before green. The factory loads this
    // state just before ring setup (0x004552e0).
    {
        const int64_t pre = ring_pre_cli >= 0           ? ring_pre_cli
                            : h.count("ring.pre_draws") ? h.at("ring.pre_draws")
                                                        : 0;
        uint32_t state = uint32_t(h.count("seed") ? h.at("seed") : 1);
        for (int64_t k = 0; k < pre; ++k) state = state * 0x343fdu + 0x269ec3u;
        config.ring_rng_state = state;
    }
    // Demo goldens (DAT_004753c8=1): the "player" is AI-driven and ignores
    // the recorded control words. Human goldens (DAT_004753c8=0) leave demo
    // hold clear so race.step consumes the control words via apply_controls.
    // The oracle demo player uses character 1's rider 8 for bike/weight/
    // weapon/stats (its 0x00463c78 row), but its AI brain is the 0x004c6b40
    // copy of personality row DAT_00467a6c — which the RR_ORACLE_COURSE
    // bootstrap leaves at its startup value 0 (menus bypassed), NOT the
    // career rider 8. Measured: golden base/current2 = pers0 L0 (87/70).
    // The harness therefore seeds the player brain from the golden's
    // measured base (demo.base_speed) or personality row 0, while
    // bike/value/weapon/stats stay rider-8 (character 1). A future oracle
    // probe should record DAT_00467a6c directly (open: analysis/replay-notes.md).
    // Runs after the ctor, before the ring (the factory's prepare hook).
    if (demo) {
        config.prepare = [&](rr::Race& rr) {
            const int rider = config.player_rider >= 0 ? config.player_rider : rr::character_rider(1);
            const RiderRow row = read_rider_row(exe, rider);
            const auto personalities = rr::read_personalities(exe);
            const int64_t dbase = hfind("demo.base_speed");
            const int64_t dlane = hfind("demo.lane"), dbias = hfind("grid.bias");
            const int64_t dweapon = hfind("demo.weapon");
            const int64_t dstr = hfind("demo.stats_strength"), dbas = hfind("demo.stats_base");
            const int64_t dmax = hfind("demo.stats_max"), dcur_st = hfind("demo.stats_current");
            // Brain row: measured base matched against the personality table
            // (proves which row the original used); default row 0 (bootstrap).
            int brain_row = 0;
            if (dbase >= 0 && !personalities.empty()) {
                for (size_t k = 0; k < personalities.size(); ++k)
                    if (personalities[k].cur_levels[0] == int(dbase)) {
                        brain_row = int(k);
                        break;
                    }
            }
            const int base = dbase >= 0               ? int(dbase)
                             : !personalities.empty() ? personalities[size_t(brain_row)].cur_levels[0]
                                                      : 95;
            const int lane = dlane >= 0 ? int(dlane) : 1;
            // Unmeasured: the grid offset the ctor stored (0x00405990 -> +0x190;
            // the human-era golden reads brain_190 = 16384 = the level-0 word).
            const int32_t bias = dbias >= 0 ? int32_t(dbias) : rr.riders().at(0).brain.bias;
            const int weapon = dweapon >= 0 ? int(dweapon) : row.weapon0;
            const rr::RiderStats stats{dstr >= 0 ? int32_t(dstr) : row.stats.strength,
                                       dbas >= 0 ? int32_t(dbas) : row.stats.base,
                                       dmax >= 0 ? int32_t(dmax) : row.stats.max,
                                       dcur_st >= 0 ? int32_t(dcur_st) : row.stats.current};
            // Seed the player brain tables from the measured row BEFORE
            // set_demo_hold (the ctor loaded personalities[0]; correct it when
            // the measurement points elsewhere).
            if (!personalities.empty() && brain_row != 0 && !rr.riders().empty()) {
                auto& brain = rr.riders()[0].brain;
                const auto& p = personalities[size_t(brain_row)];
                for (int i = 0; i < 5; ++i) {
                    brain.spd_a[i] = p.cur2_levels[i];
                    brain.spd_b[i] = p.cur_levels[i];
                }
                for (int i = 0; i < 30; ++i) {
                    brain.rating[i] = p.rating[i];
                    brain.rating_tgt[i] = p.rating_tgt[i];
                }
                for (int i = 0; i < 3; ++i) brain.ease[i] = p.ease[i];
                brain.overtake_index = p.overtake_index;
                brain.reach = p.reach;
            }
            rr.set_demo_hold(true, base, lane, bias, weapon, stats);
        };
    }
    // Field captures: brains of rider rows 9..14 are personality 0 with the
    // five level speeds +0x1bc jittered by rand()%13-6 (0x004078f0 into
    // 0x004c5f20, the 0x004c4cfc + row*0x204 brains of 0x00407990). Those
    // 30 draws run before the oracle's srand, so they are capture input: the
    // dumped levels (+0x1bc..+0x1cc) are loaded for every slotted row
    // before the spawn reads them. Older captures stop at +0x1c4 (levels
    // 0..2); level 5 replan reads the level-4 word, so the extended schema
    // is required for display levels 4..5.
    if (!solo) {
        auto prepare = config.prepare;
        config.prepare = [&h, prepare](rr::Race& rr) {
            if (prepare) prepare(rr);
            for (int k = 1;; ++k) {
                const std::string p = "rider." + std::to_string(k) + ".";
                const auto row = h.find(p + "raw_1e0");
                if (row == h.end()) break;
                const int64_t index = (row->second - 0x00463c78) / 0x40;
                if (index < 9 || index > 14 || size_t(index) >= rr.riders().size()) continue;
                auto& brain = rr.riders()[size_t(index)].brain;
                for (int level = 0; level < 5; ++level) {
                    char key[32];
                    std::snprintf(key, sizeof key, "brain_%x", 0x1bc + 4 * level);
                    const auto it = h.find(p + key);
                    if (it != h.end()) brain.spd_b[level] = int32_t(it->second);
                }
            }
        };
    }
    rr::RaceSetup setup = rr::make_race_setup(assets, exe, config);
    rr::Race& race = *setup.instance.race;
    // Solo captures null the car pool, but 0x0044dd90 still draws a lane
    // at gated timer expiries. Keep the timers attached and skip placement.
    if (solo && setup.instance.traffic) setup.instance.traffic->set_spawn_enabled(false);
    // The factory's last RNG draw is the rider build's tail (0x00405600
    // after the 0x004552e0 ring setup); ring.post_draws (default 0) would
    // count any further draws before green.
    const std::optional<uint32_t> ring_end_state = race.rng_state();
    const int64_t ring_post = h.count("ring.post_draws") ? h.at("ring.post_draws") : 0;
    // Field captures defer the green closure to after their countdown replay
    // (pre-green countdown consumers, e.g. the level-5 pedestrian re-trigger).
    std::optional<uint32_t> field_factory_end, field_anchor;
    int64_t field_ring_post = 0;
    // The golden rng is the live CRT state AFTER each tick's step (the hook
    // dumps after 0x0044b3b0). The race-setup stream (menu entry through grid
    // build, 11,461 calls) is not reproduced: a single anchor at green covers
    // it (known limitation until the setup consumers are ported).
    const int64_t tick0 = get("tick");
    // Optional original walker-state diagnostics are required in every frame
    // once declared. Read the initialized walkers before any countdown replay;
    // comparing value snapshots neither advances them nor consumes randomness.
    auto compare_ring_terrain = [&](size_t frame) {
        if (!ring_terrain) return false;
        std::string difference;
        auto compare = [&](const std::string& key, int64_t native) {
            if (!difference.empty()) return;
            const auto found = g.frames.at(frame).find(key);
            if (found == g.frames.at(frame).end())
                difference = "missing required " + key;
            else if (found->second != native)
                difference =
                    key + " original " + std::to_string(found->second) + " native " + std::to_string(native);
        };
        if (!setup.instance.ring)
            difference = "native track ring unavailable";
        else {
            const auto state = setup.instance.ring->diagnostic_state();
            auto walker = [&](const char* name, const rr::TrackWalkerState& walk) {
                const std::string prefix = std::string("ring.walk.") + name + ".";
                compare(prefix + "position", walk.position);
                compare(prefix + "terrain.available", walk.terrain.has_value());
                if (!walk.terrain) return;
                const auto& terrain = *walk.terrain;
                compare(prefix + "terrain.position", terrain.position);
                compare(prefix + "terrain.last_sample", terrain.last_sample);
                compare(prefix + "terrain.record", int64_t(terrain.record));
                for (size_t i = 0; i < terrain.points.size(); ++i) {
                    const auto& point = terrain.points[i];
                    const std::string key = prefix + "terrain.point." + std::to_string(i) + ".";
                    compare(key + "start", point.start);
                    compare(key + "end", point.end);
                    compare(key + "x", point.x);
                    compare(key + "height", point.height);
                    compare(key + "dx", point.dx);
                    compare(key + "dh", point.dh);
                }
            };
            walker("front", state.front);
            walker("back", state.back);
        }
        if (difference.empty()) return false;
        const auto tick = g.frames.at(frame).find("tick");
        std::cout << "first divergence at frame " << frame << " (tick "
                  << (tick == g.frames.at(frame).end() ? tick0 + int64_t(frame) : tick->second)
                  << ", ring terrain): " << difference << '\n';
        return true;
    };
    if (compare_ring_terrain(0)) return 1;
    std::function<std::string(const std::map<std::string, int64_t>&)> brain_line;
    // Start-hold: the golden's leading frames run during the original's
    // pre-green countdown (menu-layer, wall-clock); rider state is frozen and
    // rng unconsumed. The port holds its countdown outside Race (game_main),
    // so replay begins at the first frame where player state moves (green),
    // with RNG anchored once there. Hold frames are asserted frozen below.
    // Green-boundary rule (off-by-one fix): the dump is POST-step, so the
    // green frame's rng field is the state AFTER the green tick already
    // consumed its draws. Anchoring from it then re-executing the green
    // control word would shift the stream by one tick. The anchor is
    // therefore the PRE-step state of the green tick, i.e. the preceding
    // frame's post-step rng (proven equivalent: hold frames consume zero
    // draws — rng_calls==0 and rng constant across every hold frame, and
    // the branch proof above shows the player think took the same path).
    // Replay then executes the green frame's own control word first.
    size_t green = 1;
    {
        const auto& f0 = g.frames.at(0);
        auto pos0 = f0.find("rider.0.position"), spd0 = f0.find("rider.0.speed");
        for (size_t i = 1; i < g.frames.size(); ++i) {
            const auto& f = g.frames[i];
            auto p = f.find("rider.0.position"), s = f.find("rider.0.speed");
            if ((p != f.end() && pos0 != f0.end() && p->second != pos0->second) ||
                (s != f.end() && spd0 != f0.end() && s->second != spd0->second)) {
                green = i;
                break;
            }
        }
        if (green == 0 || green >= g.frames.size())
            throw std::runtime_error("no green frame: player state never moves");
        const auto& fg = g.frames.at(green);
        const auto& fp = g.frames.at(green - 1);
        auto frg = fg.find("rng");
        auto frp = fp.find("rng");
        auto calls = fg.find("rng_calls");
        if (frg == fg.end() || frp == fp.end()) throw std::runtime_error("green frames lack rng");
        // Pre-step equivalence proof: every hold frame (0..green-1) must
        // show zero consumed draws and a constant rng; otherwise the
        // preceding post-step state is NOT the green pre-step state.
        int64_t anchor = frp->second;
        bool proven = true;
        for (size_t i = 0; i < green; ++i) {
            const auto& f = g.frames[i];
            auto fr = f.find("rng"), fc = f.find("rng_calls");
            if (fr == f.end() || fr->second != anchor) {
                proven = false;
                break;
            }
            if (fc != f.end() && fc->second != 0) {
                proven = false;
                break;
            }
        }
        if (!proven) {
            std::cout << "green pre-step anchor unproven at frame " << green << " (tick "
                      << tick0 + int64_t(green)
                      << "): hold frames consume rng or vary; capture pre-step RNG to proceed\n";
            return 1;
        }
        (void)calls;
        if (ring_end_state) {
            // Closure: the ring setup must end exactly ring.post_draws draws
            // before the green pre-step state.
            uint32_t x = *ring_end_state;
            for (int64_t k = 0; k < ring_post; ++k) x = x * 0x343fdu + 0x269ec3u;
            if (x != uint32_t(anchor)) {
                if (!solo) {
                    // Field captures: the original can consume measured draws
                    // in the pre-green countdown window (a pedestrian
                    // re-trigger, 0x00440dd0 at level 5) which the native
                    // countdown replay reproduces inside Race::countdown_step.
                    // Defer the closure to FieldReplay, after those ticks.
                    field_factory_end = *ring_end_state;
                    field_anchor = uint32_t(anchor);
                    field_ring_post = ring_post;
                } else {
                    // Report how far off the setup ends, so ring.pre_draws can be
                    // corrected when a setup consumer changes its draw count.
                    int64_t off = 0;
                    uint32_t a = *ring_end_state, b = uint32_t(anchor);
                    for (int64_t n = 1; n <= 200000 && !off; ++n) {
                        a = a * 0x343fdu + 0x269ec3u;
                        b = b * 0x343fdu + 0x269ec3u;
                        if (a == uint32_t(anchor))
                            off = n; // setup ends n draws early
                        else if (b == *ring_end_state)
                            off = -n; // setup ends n draws late
                    }
                    std::cout << "ring setup does not close on the green anchor (" << ring_post
                              << " post draws";
                    if (off) std::cout << "; setup end is " << off << " draws before green";
                    std::cout << ")\n";
                    return 1;
                }
            }
            if (!field_anchor)
                std::cout << "ring setup closes on the green anchor (+" << ring_post << " draws)\n";
        }
        if (!field_anchor) race.set_rng_state(uint32_t(anchor));
        std::cout << (reanchor ? "[reanchor] " : "[single-anchor] ") << "start-hold: " << green
                  << " frozen frames, green at tick " << (tick0 + int64_t(green))
                  << " driver=" << (demo ? "demo" : "human")
                  << (driver_cli.empty()
                          ? " (measured player.demo_hold=" + std::to_string(measured_hold) + ")"
                          : " (cli override)")
                  << " think=0x" << std::hex << think_entry << std::dec << " count=" << think_count
                  << " rider.0.think=" << r0_think << " rider.1.think=" << r1_think
                  << " (0x00434660=4408928)\n";
    }
    // Field captures: live scheduler replay with list-order slot identity
    // (tests/field_replay.hpp). Solo captures continue below.
    if (!solo) {
        rr::golden::FieldReplay field{g, setup, tick0, green, demo, coverage};
        field.closure_factory_end = field_factory_end;
        field.closure_anchor = field_anchor;
        field.closure_post = field_ring_post;
        return field.run();
    }
    // Default: free-run from the single green anchor, so any missing or
    // extra Race::random() call shows up as rng divergence. --reanchor
    // restores per-tick re-anchoring (diagnostic only): scenery (0x451xxx),
    // combat (0x404bxx) and horn/traffic (0x44dexx) consumers run in the
    // original tick but are not ported yet, so stream-position equality is
    // not expected there; field comparison is the signal. The last-8 rng
    // history is kept for diagnosis in both modes.
    // Tick values stay absolute (original ticks): animation deadlines
    // accumulate from absolute tick counts, so a rebased local zero would
    // silently shift every deadline comparison.
    // Golden object pointers (brain +0x168/+0x16c/+0x170/+0x180) resolve
    // through the frame-0 node dump to a (position, type) pair; native riders
    // are matched by bike position.
    std::map<int64_t, std::pair<int64_t, int64_t>> node_at; // address -> (position, type)
    for (const auto& kv : h) {
        if (kv.first.rfind("nodeaddr.", 0) != 0) continue;
        const std::string n = kv.first.substr(9);
        if (n.find('.') != std::string::npos) continue;
        auto p = h.find("nodeaddr." + n + ".position"), t = h.find("nodeaddr." + n + ".type");
        if (p == h.end() || t == h.end()) continue;
        node_at[kv.second] = {p->second, t->second};
    }
    // Original pointers identify nodes for the whole replay. Populate this
    // once after frame-0 hydration; positions may change under contact.
    std::map<int64_t, int> rider_by_address;
    auto golden_rider = [&](const std::map<std::string, int64_t>& f, const char* key) -> int {
        auto it = f.find(key);
        if (it == f.end() || it->second == 0) return -1;
        auto n = rider_by_address.find(it->second);
        return n == rider_by_address.end() ? -2 : n->second;
    };
    auto gpos = [&](const std::map<std::string, int64_t>& f, const char* key) -> int64_t {
        auto it = f.find(key);
        if (it == f.end()) return -2;
        if (it->second == 0) return -1;
        auto n = node_at.find(it->second);
        return n == node_at.end() ? it->second : n->second.first;
    };
    auto gval = [](const std::map<std::string, int64_t>& f, const std::string& key) -> int64_t {
        auto it = f.find(key);
        return it == f.end() ? -2 : it->second;
    };
    auto pos_of = [&](int idx) -> int64_t {
        return idx < 0 ? -1 : race.riders()[size_t(idx)].bike.i32(0x1c);
    };
    brain_line = [&](const std::map<std::string, int64_t>& frame) {
        const auto& br = race.riders()[0].brain;
        std::ostringstream o;
        o << "brain state=" << br.state << "/" << gval(frame, "rider.0.brain_160")
          << " target=" << pos_of(br.target_rider) << "/" << gpos(frame, "rider.0.brain_168")
          << " ahead=" << pos_of(br.nbr_ahead) << "/" << gpos(frame, "rider.0.brain_16c")
          << " behind=" << pos_of(br.nbr_behind) << "/" << gpos(frame, "rider.0.brain_170")
          << " latched=" << (br.overtake_type ? br.overtake_position : -1) << "/"
          << gpos(frame, "rider.0.brain_180") << " step=" << br.target_step << "/"
          << gval(frame, "rider.0.brain_174") << " ov=" << br.ov_ahead << "," << br.ov_behind << "/"
          << gval(frame, "rider.0.brain_178") << "," << gval(frame, "rider.0.brain_17c")
          << " cur2=" << br.current2 << "/" << gval(frame, "rider.0.brain_1a0") << " cur=" << br.current_speed
          << "/" << gval(frame, "rider.0.brain_1a4") << " tx=" << br.target_x << "/"
          << gval(frame, "rider.0.brain_194") << " tpos=" << br.target_position << "/"
          << gval(frame, "rider.0.brain_198") << " tspd=" << br.target_speed << "/"
          << gval(frame, "rider.0.brain_19c") << " replan=" << br.replan_ticks << "/"
          << gval(frame, "rider.0.brain_188");
        return o.str();
    };
    // First brain word that differs from the golden window (empty if the
    // golden carries no brain window or everything matches).
    auto brain_diff = [&](const std::map<std::string, int64_t>& f) -> std::string {
        if (f.find("rider.0.brain_160") == f.end()) return {};
        const auto& br = race.riders()[0].brain;
        const std::pair<const char*, int64_t> words[] = {
            {"160", br.state},        {"174", br.target_step},  {"178", br.ov_ahead},
            {"17c", br.ov_behind},    {"188", br.replan_ticks}, {"18c", br.lane},
            {"190", br.bias},         {"194", br.target_x},     {"198", br.target_position},
            {"19c", br.target_speed}, {"1a0", br.current2},     {"1a4", br.current_speed}};
        for (const auto& w : words) {
            const int64_t want = gval(f, std::string("rider.0.brain_") + w.first);
            if (want != w.second)
                return std::string("brain +0x") + w.first + " original " + std::to_string(want) + " native " +
                       std::to_string(w.second);
        }
        for (int st = 0; st < 10; ++st) {
            char key[32];
            std::snprintf(key, sizeof key, "rider.0.brain_%x", 0x138 + 4 * st);
            const int64_t want = gval(f, key);
            if (want != -2 && want != br.state_ticks[size_t(st)])
                return std::string("brain +0x") + (key + 14) + " original " + std::to_string(want) +
                       " native " + std::to_string(br.state_ticks[size_t(st)]);
        }
        const std::pair<const char*, int> ptrs[] = {{"rider.0.brain_168", br.target_rider},
                                                    {"rider.0.brain_16c", br.nbr_ahead},
                                                    {"rider.0.brain_170", br.nbr_behind}};
        for (const auto& p : ptrs) {
            const int want = golden_rider(f, p.first);
            if (want != p.second)
                return std::string(p.first + 8) + " original rider " + std::to_string(want) + " native " +
                       std::to_string(p.second);
        }
        const int64_t latched = gpos(f, "rider.0.brain_180");
        const int64_t mine =
            race.riders()[0].brain.overtake_type ? race.riders()[0].brain.overtake_position : -1;
        if (latched != -2 && latched != mine)
            return "brain_180 original position " + std::to_string(latched) + " native " +
                   std::to_string(mine);
        return {};
    };
    // Whole player brain window (rider.0.brain_138..1c4) against the native
    // AiState: every word, pointers through rider identity / node position.
    // Records each consumed key in the coverage ledger.
    auto brain_window_diff = [&](const std::map<std::string, int64_t>& f) -> std::string {
        const auto& br = race.riders()[0].brain;
        for (uint32_t off = 0x138; off <= 0x1c4; off += 4) {
            char key[32];
            std::snprintf(key, sizeof key, "rider.0.brain_%x", off);
            const auto it = f.find(key);
            if (it == f.end()) continue;
            g_used_keys.insert(key);
            if (off == 0x168 || off == 0x16c || off == 0x170) {
                const int want = golden_rider(f, key);
                const int mine = off == 0x168 ? br.target_rider : off == 0x16c ? br.nbr_ahead : br.nbr_behind;
                if (want != mine)
                    return std::string(key + 8) + " original rider " + std::to_string(want) + " native " +
                           std::to_string(mine);
                continue;
            }
            if (off == 0x180) {
                const int64_t latched = gpos(f, key);
                const int64_t mine = br.overtake_type ? br.overtake_position : -1;
                if (latched != mine)
                    return std::string(key + 8) + " original position " + std::to_string(latched) +
                           " native " + std::to_string(mine);
                continue;
            }
            int64_t mine;
            if (off < 0x160)
                mine = br.state_ticks[size_t((off - 0x138) / 4)];
            else if (off == 0x160)
                mine = br.state;
            else if (off == 0x164)
                mine = int64_t(br.state_flag) | int64_t(uint8_t(br.overtake_index)) << 8;
            else if (off == 0x174)
                mine = br.target_step;
            else if (off == 0x178)
                mine = br.ov_ahead;
            else if (off == 0x17c)
                mine = br.ov_behind;
            else if (off == 0x184)
                mine = br.word_184;
            else if (off == 0x188)
                mine = br.replan_ticks;
            else if (off == 0x18c)
                mine = br.lane;
            else if (off == 0x190)
                mine = br.bias;
            else if (off == 0x194)
                mine = br.target_x;
            else if (off == 0x198)
                mine = br.target_position;
            else if (off == 0x19c)
                mine = br.target_speed;
            else if (off == 0x1a0)
                mine = br.current2;
            else if (off == 0x1a4)
                mine = br.current_speed;
            else if (off < 0x1bc)
                mine = br.spd_a[(off - 0x1a8) / 4];
            else
                mine = br.spd_b[(off - 0x1bc) / 4];
            if (mine != it->second)
                return std::string(key + 8) + " original " + std::to_string(it->second) + " native " +
                       std::to_string(mine);
        }
        return {};
    };
    // Countdown (plan 10 Task 5): the original thinks through the hold frames.
    // Captures with a brain window seed the frozen field and the demo brain
    // from frame 0 (post tick 1: the global brain copy carries words from its
    // previous use, and frozen bikes sit off their grid words), then run the
    // native countdown ticks, comparing the brain every frame.
    // Older captures without node laterals or a brain window keep the native
    // grid placement and the ctor brain, and run the countdown from tick 1.
    const bool brain_window = h.find("rider.0.brain_160") != h.end();
    // Declared frozen words hydrate human and demo solo captures once.
    // Older captures retain their existing demo-only lateral fallback.
    // Spawn placement check (declared frozen-word captures): 0x00405c80
    // puts every bike on its 0x004091f0 straight target from the drawn lane,
    // so the native spawn laterals must equal the captured node laterals
    // before hydration copies them.
    if (solo && h.count("probe.frozen_words")) {
        std::map<int64_t, int64_t> captured;
        for (const auto& [key, value] : h) {
            if (key.rfind("nodeaddr.", 0) != 0 || key.find('.', 9) != std::string::npos) continue;
            const auto t = h.find(key + ".type"), p = h.find(key + ".position"), l = h.find(key + ".lateral");
            if (t != h.end() && p != h.end() && l != h.end() && t->second == 4)
                captured[p->second] = l->second;
        }
        for (const auto& r : race.riders()) {
            if (!r.frozen) continue;
            const auto c = captured.find(r.bike.i32(0x1c));
            if (c == captured.end() || c->second != r.bike.i32(0x28)) {
                std::cout << "first divergence at spawn: rider at " << r.bike.i32(0x1c)
                          << " lateral original "
                          << (c == captured.end() ? std::string("missing") : std::to_string(c->second))
                          << " native " << r.bike.i32(0x28) << '\n';
                return 1;
            }
        }
    }
    if (solo) {
        try {
            rr::golden::apply_frozen_state(h, race.riders(), demo);
        } catch (const std::runtime_error& error) {
            std::cout << error.what() << '\n';
            return 1;
        }
    }
    rider_by_address = rr::golden::rider_node_identities(h, race.riders());
    // Parking (0x00406940) nulls both slices; frozen riders the capture shows
    // with NULL bike and half slices were parked before frame 0.
    for (auto& fr : race.riders())
        if (fr.frozen && !fr.bike_slice_present && !fr.half_slice_present) fr.parked = true;
    // Spawn-frozen player scheduler (frame 0 is post tick 1): hydrate the
    // slotted rider.0.bike_c/10/14/18 keys when the capture dumps them, so
    // the launch ramp consumes the previous run's dt (tick-2 dt 2) while
    // contact reads the fresh one. Legacy captures without the keys keep
    // the build/countdown dt management.
    if (solo) {
        auto& p = race.riders()[0];
        auto skey = [&](const char* k) {
            auto it = h.find(std::string("rider.0.bike_") + k);
            return it == h.end() ? nullptr : &it->second;
        };
        const int64_t* s_last = skey("10");
        const int64_t* s_next = skey("c");
        const int64_t* s_interval = skey("14");
        const int64_t* s_dt = skey("18");
        if (s_last && s_next && s_interval && s_dt && *s_last >= std::numeric_limits<int32_t>::min() &&
            *s_last <= std::numeric_limits<int32_t>::max() &&
            *s_next >= std::numeric_limits<int32_t>::min() &&
            *s_next <= std::numeric_limits<int32_t>::max() && *s_interval >= 1 &&
            *s_interval <= std::numeric_limits<int32_t>::max() &&
            *s_dt >= std::numeric_limits<int32_t>::min() && *s_dt <= std::numeric_limits<int32_t>::max()) {
            p.sched_last = int32_t(*s_last);
            p.sched_next = int32_t(*s_next);
            p.sched_interval = int32_t(*s_interval);
            p.bike.i32(0x18) = int32_t(*s_dt);
            p.sched_live = true;
        }
    }
    // Frame 0 is post tick 1, which the replay does not re-run: take the
    // player half's standings words from it (the countdown computes them
    // natively from tick 1 in a live game).
    // The rider half's riding-animation block (+0x80..+0x88): the queue
    // (2) is the native spawn value (0x00405990 -> 0x00446060 ->
    // 0x0043ae60) and is compared from frame 0 on; the selection, frame,
    // loops and deadline are tick 1's think (0x00407f30), which the replay
    // does not re-run, so those come from frame 0.
    if (solo && h.count("rider.0.anim_queued")) {
        auto& st = race.riders()[0].ride_animation;
        st.animation = int8_t(h.at("rider.0.anim"));
        st.frame = int8_t(h.at("rider.0.anim_frame"));
        st.loops = uint8_t(h.at("rider.0.anim_loops"));
        st.deadline = uint32_t(h.at("rider.0.anim_deadline"));
    }
    if (solo && h.count("rider.0.raw_1fc") && h.count("rider.0.raw_1f4")) {
        auto& p = race.riders()[0];
        p.place = int(h.at("rider.0.raw_1fc"));
        p.place_refresh = int32_t(h.at("rider.0.raw_1f4"));
    }
    // Measured spawn-frozen brain snapshot (frame 0 is post tick 1): hydrate
    // once for a demo solo capture with a brain window (its tick-1 think
    // stepped the brain). A human run never steps the player brain after
    // the 0x00405990/0x00405c80 build, so its window is the native spawn
    // state, compared on every frame.
    if (solo && brain_window && demo) {
        auto& br = race.riders()[0].brain;
        auto w = [&](const char* k) { return int32_t(gval(h, std::string("rider.0.brain_") + k)); };
        br.state = w("160");
        br.target_step = w("174");
        br.ov_ahead = w("178");
        br.ov_behind = w("17c");
        br.replan_ticks = w("188");
        br.lane = w("18c");
        br.bias = w("190");
        br.target_x = w("194");
        br.target_position = w("198");
        br.target_speed = w("19c");
        br.current2 = w("1a0");
        br.current_speed = w("1a4");
        br.base_speed = w("1bc");
        br.state_flag = uint8_t(w("164"));
        for (int st = 0; st < 10; ++st) {
            char key[16];
            std::snprintf(key, sizeof key, "%x", 0x138 + 4 * st);
            br.state_ticks[size_t(st)] = w(key);
        }
        br.target_rider = golden_rider(h, "rider.0.brain_168");
        br.nbr_ahead = golden_rider(h, "rider.0.brain_16c");
        br.nbr_behind = golden_rider(h, "rider.0.brain_170");
    }
    // Brain/scheduler hydration skips the post-tick-1 frame. Traffic timers
    // start from setup, so bank that frame explicitly without rerunning
    // the hydrated think/scheduler. Human solo also skips its hold frames.
    if (solo && setup.instance.traffic) {
        const size_t timer_frames = demo ? (brain_window ? 1 : 0) : green;
        for (size_t i = 0; i < timer_frames; ++i)
            setup.instance.traffic->countdown_step(
                race.route_for(race.riders()[0].route), int32_t(race.camera_position()) + 0x5100,
                []() -> uint32_t { throw std::runtime_error("hydrated hold frame consumed RNG"); },
                uint32_t(tick0 + int64_t(i)));
    }
    if (demo && solo) {
        // Frame 0 is post tick 1: seeded captures resume at tick 2, others
        // run tick 1 natively too.
        race.set_ticks(int32_t(brain_window ? tick0 : tick0 - 1));
        const uint32_t rng_before = race.rng_state();
        if (!brain_window) race.countdown_step();
        for (size_t i = 1; i < green; ++i) {
            race.countdown_step();
            if (compare_ring_terrain(i)) return 1;
            if (std::getenv("RR_SIM_TRACE"))
                std::cout << "countdown tick " << tick0 + int64_t(i) << ": " << brain_line(g.frames[i])
                          << '\n';
            const std::string d = brain_diff(g.frames[i]);
            if (!d.empty()) {
                std::cout << "first divergence at frame " << i << " (tick " << tick0 + int64_t(i)
                          << ", countdown): " << d << '\n';
                if (!std::getenv("RR_SIM_CONTINUE")) return 1;
            }
        }
        if (race.rng_state() != rng_before) {
            std::cout << "countdown consumed draws\n";
            return 1;
        }
        if (std::getenv("RR_SIM_SIZES"))
            for (const auto& r : race.riders())
                std::cout << "sizes slot " << r.slot << " pos " << r.bike.i32(0x1c) << " w "
                          << r.bike.i32(0x174) << " l " << r.bike.i32(0x178) << " h " << r.bike.i32(0x17c)
                          << '\n';
        std::cout << "countdown: " << green - 1 << " native ticks, brain equal\n";
    } else {
        // These replay modes hold the race outside Race::countdown_step.
        // The walkers remain still, but each declared hold frame is required.
        for (size_t i = 1; i < green; ++i)
            if (compare_ring_terrain(i)) return 1;
        race.set_ticks(int32_t(tick0 + int64_t(green) - 1));
    }
    std::vector<std::pair<int64_t, uint64_t>> rng_history; // (golden, native)
    size_t uncovered = 0;        // native fields no golden era dumps (acceptance gaps, counted not skipped)
    size_t rpm_timing_notes = 0; // nondeterministic sound-thread rpm channel (diagnostic only)
    for (size_t i = green; i < g.frames.size(); ++i) {
        const auto& frame = g.frames[i];
        auto fc = frame.find("controls");
        if (fc == frame.end()) throw std::runtime_error("frame lacks controls");
        if (reanchor && i > green) {
            auto fp = g.frames[i - 1].find("rng");
            if (fp != g.frames[i - 1].end()) race.set_rng_state(uint32_t(fp->second));
        }
        // Demo goldens ignore control words (AI branch never reads them);
        // human goldens consume them through apply_controls (0x00434660).
        race.step(demo ? 0 : uint32_t(fc->second));
        if (compare_ring_terrain(i)) return 1;
        auto snap = race.snapshot();
        // Frozen opponents never move and the solo dump names only the
        // player's bike and half (rider.0/rider.1): drop their rows.
        if (solo) {
            std::vector<rr::FieldValue> kept;
            for (auto& f : snap.fields) {
                const bool frozen_row = f.name.rfind("rider.", 0) == 0 && f.name.rfind("rider.0.", 0) != 0;
                if (!frozen_row) kept.push_back(std::move(f));
            }
            snap.fields = std::move(kept);
        }
        // Diagnostic: RR_SIM_TRACE=<prefix> prints every native field with
        // that prefix beside its golden value (same key) each replayed tick.
        if (const char* trace = std::getenv("RR_SIM_TRACE")) {
            std::cout << "tick " << tick0 + int64_t(i) << ':';
            for (const auto& f : snap.fields) {
                if (f.name.rfind(trace, 0) != 0) continue;
                auto it = frame.find(f.name);
                std::cout << ' ' << f.name.substr(f.name.rfind('.') + 1) << '=' << f.value;
                if (it != frame.end() && it->second != f.value) std::cout << "/orig " << it->second;
            }
            std::cout << "  " << brain_line(frame) << '\n';
        }
        const auto fr = frame.find("rng");
        const uint64_t want_rng = fr == frame.end() ? 0 : uint64_t(fr->second);
        rng_history.push_back({want_rng, snap.rng});
        if (rng_history.size() > 8) rng_history.erase(rng_history.begin());
        const auto ft = frame.find("tick");
        if (ft != frame.end() && ft->second != tick0 + int64_t(i)) {
            std::cout << "tick not contiguous at frame " << i << '\n';
            return 1;
        }
        // RNG is compared first every tick (plan 10 review focus 1): a
        // stream-position break is reported before any field, with the
        // last-8-ticks history for both sides.
        // Diagnostic: RR_SIM_CONTINUE=1 keeps replaying past divergences
        // (each tick re-anchors nothing; use with RR_SIM_TRACE).
        static const bool keep_going = std::getenv("RR_SIM_CONTINUE") != nullptr;
        if (keep_going) continue;
        if (want_rng != snap.rng) {
            std::cout << "first divergence at frame " << i << " (tick " << tick0 + int64_t(i)
                      << "): rng original " << want_rng << " native " << snap.rng << '\n';
            std::cout << "rng history (last " << rng_history.size() << " ticks, golden vs native):";
            for (const auto& e : rng_history) std::cout << " (" << e.first << "," << e.second << ")";
            std::cout << '\n';
            return 1;
        }
        if (solo && !demo && brain_window) {
            if (const std::string d = brain_window_diff(frame); !d.empty()) {
                std::cout << "first divergence at frame " << i << " (tick " << tick0 + int64_t(i)
                          << "): " << d << " (player brain)\n";
                return 1;
            }
        }
        if (const std::string d = brain_diff(frame);
            !d.empty() && h.find("rider.0.brain_160") != h.end() && demo && solo) {
            std::cout << "first divergence at frame " << i << " (tick " << tick0 + int64_t(i) << "): " << d
                      << '\n';
            return 1;
        }
        std::string missing;
        if (int rc =
                compare_fields(frame, snap.fields, i, tick0 + int64_t(i), rng_history, uncovered, missing)) {
            if (!missing.empty())
                std::cout << "missing required field at frame " << i << " (tick " << tick0 + int64_t(i)
                          << "): " << missing << '\n';
            return rc;
        }
        // Player bike-image words the proxy dumps beside the field set
        // (rider.0.bike_<hex>, lat104/108/10c, yaw60): the native Bike is the
        // original object image, so each compares at its own offset. The
        // scheduler words +0x0c/+0x10/+0x14 are kept on Rider (sched_*), and
        // +0x44 is the 0x00438450 render-prep copy (presentation, not ported).
        {
            const rr::Rider& pr = race.riders()[0];
            for (const auto& kv : frame) {
                const std::string& k = kv.first;
                int64_t native;
                if (k == "rider.0.raw_1f8" || k == "rider.0.raw_1fc" || k == "rider.0.raw_1f4") {
                    native = k == "rider.0.raw_1f8"   ? pr.finish_distance
                             : k == "rider.0.raw_1fc" ? pr.place
                                                      : pr.place_refresh;
                    g_used_keys.insert(k);
                    if (native != kv.second) {
                        std::cout << "first divergence at frame " << i << " (tick " << tick0 + int64_t(i)
                                  << "): " << k << " original " << kv.second << " native " << native
                                  << " (standings)\n";
                        return 1;
                    }
                    continue;
                }
                if (k == "rider.0.bike_c")
                    native = pr.sched_next;
                else if (k == "rider.0.bike_10")
                    native = pr.sched_last;
                else if (k == "rider.0.bike_14")
                    native = pr.sched_interval;
                else if (k == "rider.0.bike_44")
                    continue;
                else if (k.rfind("rider.0.bike_", 0) == 0)
                    native = pr.bike.i32(uint32_t(std::stoul(k.substr(13), nullptr, 16)));
                else if (k.rfind("rider.0.lat1", 0) == 0)
                    native = pr.bike.i32(uint32_t(std::stoul(k.substr(11), nullptr, 16)));
                else if (k == "rider.0.yaw60")
                    native = pr.bike.i32(0x60);
                // Rider-half raw window (+0x180..+0x1fc of *(bike+0x2fc)):
                // the half's own words come from its object image; weapon
                // (+0x1c8..+0x1d4) and standings words compare elsewhere,
                // +0x1a4/+0x1c4/+0x1d8/+0x1e0 are pointers (non-state).
                else if (k.rfind("rider.0.raw_", 0) == 0) {
                    const uint32_t off = uint32_t(std::stoul(k.substr(12), nullptr, 16));
                    if ((off >= 0x1c4 && off <= 0x1d8) || off == 0x1a4 || off == 0x1e0) continue;
                    native = pr.half_object.i32(int(off));
                } else
                    continue;
                g_used_keys.insert(k);
                if (native != kv.second) {
                    // Engine rpm (+0x2dc) and its speed cache (+0x2e8) are
                    // wall-clock driven (async sound-pump thread), not
                    // sim-deterministic: the two identical split captures
                    // (same seed, same 1*4500 script) disagree on +0x2dc at
                    // 252 ticks starting at tick 20, and on +0x44 at 185
                    // ticks, while positions, speeds, RNG and crash state
                    // match on every tick. They feed no physics (HUD needle
                    // and engine pitch only), so a mismatch is diagnostic
                    // context, never a sim divergence: report and continue.
                    if (k == "rider.0.bike_2dc" || k == "rider.0.bike_2e8") {
                        std::cout << "rpm timing at frame " << i << " (tick " << tick0 + int64_t(i)
                                  << "): " << k << " original " << kv.second << " native " << native
                                  << " (bike image, nondeterministic channel)\n";
                        ++rpm_timing_notes;
                        continue;
                    }
                    std::cout << "first divergence at frame " << i << " (tick " << tick0 + int64_t(i)
                              << "): " << k << " original " << kv.second << " native " << native
                              << " (bike image)\n";
                    return 1;
                }
            }
        }
        // Player rider half (rider.1, think 0x00434660 on solo captures):
        // motion words against the native mirror; its scheduler words follow
        // the bike's (same cadence); slice_ptr through the derived sample.
        if (solo) {
            const rr::Rider& pr = race.riders()[0];
            const auto& m = pr.half_mirror;
            for (const auto& kv : frame) {
                const std::string& k = kv.first;
                if (k.rfind("rider.1.", 0) != 0) continue;
                const std::string f = k.substr(8);
                int64_t native;
                if (f == "position")
                    native = m.position;
                else if (f == "lateral")
                    native = m.lateral;
                else if (f == "height")
                    native = m.height;
                else if (f == "velocity")
                    native = m.velocity;
                else if (f == "speed" || f == "lat108")
                    native = f == "speed" ? m.speed : m.lat108;
                else if (f == "steer_rate")
                    native = m.steer_rate;
                else if (f == "lat_vel" || f == "lat10c")
                    native = m.lat_vel;
                else if (f == "yaw60")
                    native = m.yaw;
                else if (f == "bike_c")
                    native = pr.sched_next;
                else if (f == "bike_10")
                    native = pr.sched_last;
                else if (f == "bike_14")
                    native = pr.sched_interval;
                else if (f == "bike_18")
                    native = pr.bike.i32(0x18);
                // The half's own object words (0x004387a0 from 0x00463c10)
                // at the bike-layout offsets the proxy dumps for every slot.
                else if (f == "flags")
                    native = pr.half_object.u8(0xc0);
                else if (f == "ground")
                    native = pr.half_object.i32(0x160);
                else if (f == "gear")
                    native = pr.half_object.i32(0x23c);
                else if (f == "condition")
                    native = pr.half_object.i32(0x2f0);
                else if (f == "crash")
                    native = pr.half_object.i32(0x2f4);
                else if (f == "steer")
                    native = pr.half_object.i32(0x288);
                else if (f == "lat104")
                    native = pr.half_object.i32(0x104);
                else if (f == "bike_44")
                    continue; // 0x00438450 render-prep copy (non-state)
                else if (f.rfind("bike_", 0) == 0)
                    native = pr.half_object.i32(int(std::stoul(f.substr(5), nullptr, 16)));
                else
                    continue;
                g_used_keys.insert(k);
                if (native != kv.second) {
                    std::cout << "first divergence at frame " << i << " (tick " << tick0 + int64_t(i)
                              << "): " << k << " original " << kv.second << " native " << native
                              << " (rider half)\n";
                    return 1;
                }
            }
        }
        // Scheduler node list (node.N.*: every list object except the rider
        // slots): compared per frame as a multiset of (draw callback +0xdc,
        // position). Native members: frozen riders' bike and half (draw
        // 0x00446250 / 0x004463e0, 0 once parked by the 0x00437e60 window),
        // active pedestrians (0x00440680) and the intersection controller and
        // signals (no draw). +0x73 (node kind) is the top byte of the +0x70
        // pointer and is not state.
        // Gated on a declared probe.frozen_words capture: parking is
        // reconstructed from its frozen slice flags (apply_frozen_state), so a
        // legacy demo capture without them cannot rebuild the original list.
        if (solo && h.find("probe.frozen_words") != h.end()) {
            std::vector<std::pair<int64_t, int64_t>> want, have;
            for (const auto& kv : frame) {
                if (kv.first.rfind("node.", 0) != 0) continue;
                const size_t dot = kv.first.rfind('.');
                if (kv.first.compare(dot, std::string::npos, ".think") != 0) continue;
                const std::string base = kv.first.substr(0, dot);
                const auto pos = frame.find(base + ".position");
                if (pos == frame.end()) continue;
                want.push_back({kv.second, pos->second});
                g_used_keys.insert(kv.first);
                g_used_keys.insert(base + ".position");
            }
            if (!want.empty()) {
                const auto& riders = race.riders();
                for (size_t r = 1; r < riders.size(); ++r) {
                    const rr::Rider& fr = riders[r];
                    if (!fr.frozen) continue;
                    const int64_t half = fr.half_detached ? fr.half_position : fr.bike.i32(0x1c);
                    have.push_back({fr.parked ? 0 : 0x00446250, fr.bike.i32(0x1c)});
                    have.push_back({fr.parked ? 0 : 0x004463e0, half});
                }
                // The player crash body once the solo freeze-hack nulls the
                // half (Rider::half_frozen): the proxy dumps scheduler nodes
                // whose think is not a rider think, including nulled ones,
                // with their draw callback. The live player half (rider
                // slot) is never dumped; the nulled body appears as a half
                // node (split-left 2102: (0x004463e0, 77996)).
                if (!riders.empty() && riders[0].crash_active && riders[0].half_frozen)
                    have.push_back({0x004463e0, riders[0].crash_actor.motion.i32(0x1c)});
                if (setup.instance.pedestrians)
                    for (const auto& p : setup.instance.pedestrians->pool())
                        if (p.active) have.push_back({0x00440680, p.body.bike.i32(0x1c)});
                if (setup.instance.traffic) {
                    for (const auto& c : setup.instance.traffic->controllers())
                        if (c.active) have.push_back({0, c.position_q8});
                    for (const auto& sg : setup.instance.traffic->signals())
                        if (sg.active) have.push_back({0, sg.position_q8});
                }
                std::sort(want.begin(), want.end());
                std::sort(have.begin(), have.end());
                if (want != have) {
                    std::cout << "first divergence at frame " << i << " (tick " << tick0 + int64_t(i)
                              << "): node list original";
                    for (const auto& w : want) std::cout << " (" << w.first << "," << w.second << ")";
                    std::cout << " native";
                    for (const auto& w : have) std::cout << " (" << w.first << "," << w.second << ")";
                    std::cout << '\n';
                    return 1;
                }
            }
        }
        // Ring cursors and frozen-node scheduler words (sched.<addr>.*,
        // matched to native riders through the frame-0 address identities).
        {
            for (const auto& kv : frame) {
                const std::string& k = kv.first;
                int64_t native = 0;
                if (k == "ring.front" || k == "ring.back" || k == "ring.camera") {
                    if (!setup.instance.ring) continue;
                    native = k == "ring.front"  ? setup.instance.ring->front_q8()
                             : k == "ring.back" ? setup.instance.ring->back_q8()
                                                : int64_t(setup.instance.ring->front_q8()) -
                                                      0x5000; // DAT_004b6e5c ring camera
                } else if (k.rfind("sched.", 0) == 0) {
                    const size_t dot = k.find('.', 6);
                    if (dot == std::string::npos) continue;
                    const auto who =
                        rider_by_address.find(int64_t(std::stoul(k.substr(6, dot - 6), nullptr, 16)));
                    if (who == rider_by_address.end() || who->second < 0) continue;
                    const rr::Rider& fr = race.riders()[size_t(who->second)];
                    const std::string f = k.substr(dot + 1);
                    native = f == "next"       ? fr.sched_next
                             : f == "last"     ? fr.sched_last
                             : f == "interval" ? fr.sched_interval
                             : f == "dt"       ? fr.bike.i32(0x18)
                                               : INT64_MIN;
                    if (native == INT64_MIN) continue;
                } else
                    continue;
                g_used_keys.insert(k);
                if (native != kv.second) {
                    std::cout << "first divergence at frame " << i << " (tick " << tick0 + int64_t(i)
                              << "): " << k << " original " << kv.second << " native " << native << '\n';
                    return 1;
                }
            }
        }
        // Per-tick hook 0x00434980 (the game calls it after each tick: sound
        // pump or game_types without sound): player rpm +0x2dc and its speed
        // cache +0x2e8, read by the next tick's dump. While riding (or
        // airborne) it runs once per render frame: the render prep
        // 0x00438450 copies +0x1c into +0x44 once per frame, so a frame ends
        // after this tick exactly when the next dump's +0x44 is this tick's
        // position (ticks 13, 493 and 1197 share a frame with the tick
        // before on sched-human-20261016; cov2-1300 and schema2-1234
        // likewise skip). While crashed on the ground the engine-sound
        // path is off and the direct 0x004349c0 runs every tick:
        // split-left 2099-2102 decay the rpm on ticks whose +0x44 copy did
        // not advance, while split-right 2062 (crashed but airborne,
        // height 3091) skips like the riding case. Rule: frame end, or
        // crashed with height below the 0x65 airborne line.
        bool frame_ends = true;
        if (i + 1 < g.frames.size()) {
            const auto next44 = g.frames[i + 1].find("rider.0.bike_44");
            const auto pos = frame.find("rider.0.position");
            if (next44 != g.frames[i + 1].end() && pos != frame.end())
                frame_ends = next44->second == pos->second;
        }
        const rr::Bike& pb = race.riders()[0].bike;
        if (frame_ends || (pb.i32(0x2f4) != 0 && pb.i32(0x20) < 0x65))
            rr::update_engine_rpm(race.riders()[0].bike);
    }
    // Coverage report. Acceptance needs every native field compared
    // (uncovered == 0) and every golden key either compared (ledger) or
    // listed below as a non-state key; only then is the bare
    // "N ticks identical" line printed (tools/verify_sim.py strict mode).
    {
        // Non-state golden keys: header/branch probes, inputs (controls and
        // the frame schedule +0x44 of the 0x00438450 render prep), and
        // process-specific code/object pointers (think callbacks are checked
        // at green; slice_ptr is compared through the derived sample).
        auto non_state = [](const std::string& k) {
            static const std::set<std::string> exact = {"tick",
                                                        "rng_calls",
                                                        "controls",
                                                        "riders",
                                                        "course",
                                                        "level",
                                                        "bike",
                                                        "character",
                                                        "seed",
                                                        "solo",
                                                        "tick1_caller",
                                                        "probe.S",
                                                        "probe.end",
                                                        "probe.player",
                                                        "player.demo_hold",
                                                        "player.think",
                                                        "player.think_count",
                                                        "grid.bias",
                                                        "demo.base_speed",
                                                        "demo.lane"};
            if (exact.count(k)) return true;
            if (k.rfind("rider.", 0) != 0 && k.rfind("node.", 0) != 0) return false;
            const std::string suffix = k.substr(k.rfind('.') + 1);
            if (k.rfind("node.", 0) == 0) return suffix == "kind";
            return suffix == "think" || suffix == "slice_ptr" || suffix == "bike_44" || suffix == "raw_1a4" ||
                   suffix == "raw_1c4" || suffix == "raw_1d8" || suffix == "raw_1e0";
        };
        g_used_keys.insert("rng"); // compared every tick before the fields
        std::map<std::string, size_t> fam;
        size_t extras = 0;
        for (const auto& kv : g.frames.back()) {
            if (g_used_keys.count(kv.first) || non_state(kv.first)) continue;
            ++extras;
            ++fam[fold_digits(kv.first)];
        }
        std::string uncovered_names;
        for (const auto& kv : g_uncovered_names) {
            const std::string suffix = kv.first.substr(kv.first.rfind('.') + 1);
            if (uncovered_names.find(suffix) == std::string::npos)
                uncovered_names += (uncovered_names.empty() ? "" : ",") + suffix;
        }
        if (coverage) {
            for (const auto& kv : g_uncovered_names)
                std::cout << "uncovered " << kv.first << ' ' << kv.second << '\n';
            for (const auto& kv : fam) std::cout << "golden-only " << kv.first << ' ' << kv.second << '\n';
        }
        if (uncovered == 0 && extras == 0 && rpm_timing_notes == 0)
            std::cout << g.frames.size() << " ticks identical\n";
        else
            std::cout << g.frames.size() << " ticks identical (" << extras
                      << " golden-only fields not compared; " << uncovered
                      << " field-comparisons uncovered: " << uncovered_names << "; " << rpm_timing_notes
                      << " rpm-timing notes on the nondeterministic channel)\n";
    }
    return 0;
}
