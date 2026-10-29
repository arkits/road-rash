#pragma once
#include "frozen_state.hpp"
#include "golden.hpp"
#include "pedestrians.hpp"
#include "race.hpp"
#include "race_setup.hpp"
#include "traffic.hpp"
#include "track_ring.hpp"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <tuple>
#include <vector>

// Field-capture replay (plan 10 Task 6): a capture taken without the solo
// freeze, with the live rider scheduler on both sides.
//
// Slot identity. The proxy keys rider.<k> by its walk of the scheduler
// list (0x00438170 order, front to back): the player bike is pinned to
// rider.0, and every other node whose think is 0x00407a90 (a live bike;
// parking nulls it) or 0x00434660 (the player half) takes the next index.
// The native side derives the same list from Race::schedule_order(), so
// rider.<k> names the same object on both sides while the replay matches;
// a reordering shows as a field divergence, never as a silent remap.
// Every other node is dumped as node.<n> (draw callback +0xdc, position)
// and compared as a multiset; parked bikes also carry sched.<addr>.*,
// resolved to native riders through the frame-0 node addresses.
namespace rr::golden {
struct FieldReplay {
    const File& g;
    RaceSetup& setup;
    int64_t tick0 = 1;
    size_t green = 1;
    bool demo = true;
    bool coverage = false;
    // Deferred green closure (field captures): the factory end state and the
    // green pre-step anchor when the original consumed measured draws in the
    // pre-green countdown window, which the countdown replay below reproduces.
    std::optional<uint32_t> closure_factory_end;
    std::optional<uint32_t> closure_anchor;
    int64_t closure_post = 0;
    std::set<std::string> used;
    std::map<int64_t, int> identity;         // node address -> native rider (-1 other)
    std::map<int64_t, int64_t> address_type; // node address -> +0xc4 at frame 0
    Race& race() { return *setup.instance.race; }

    // Native slot list: rider index and node type per golden rider.<k>.
    std::vector<std::pair<int, uint32_t>> slots() {
        std::vector<std::pair<int, uint32_t>> out{{0, 4}};
        for (const auto& n : race().schedule_order()) {
            const Rider& r = race().riders()[size_t(n.rider)];
            if (n.type == 4 && n.rider != 0 && !r.parked) out.push_back({n.rider, 4});
            if (n.type == 8 && n.rider == 0) out.push_back({0, 8});
        }
        return out;
    }
    std::string diff;
    // Nondeterministic sound-thread rpm channel (see the solo replay): count
    // mismatches as diagnostic notes, never sim divergences.
    size_t rpm_timing_notes = 0;
    size_t slice_load_notes = 0; // far-edge box-load gaps (see riders())
    // Diagnostic: RR_FIELD_ALL=1 prints every differing check of the first
    // diverging frame, not just the first.
    const bool all = std::getenv("RR_FIELD_ALL") != nullptr;
    // Diagnostic only: RR_FIELD_IGNORE_CARS=1 leaves free cars (draw
    // 0x00446830) out of the node list to look past a traffic divergence.
    // A run using it never reports "identical".
    const bool ignore_cars = std::getenv("RR_FIELD_IGNORE_CARS") != nullptr;
    const bool ignore_peds =
        ignore_cars && std::getenv("RR_FIELD_IGNORE_PEDS") != nullptr; // also pedestrians (0x00440680)
    // Diagnostic only (with RR_FIELD_IGNORE_CARS): after each compared frame
    // copy the captured free cars' position, lateral and speed onto the
    // native cars (matched by creation order), so the riders see the
    // original traffic while the car cadence (plan 10 Task 7) is unported.
    const bool cars_from_golden = ignore_cars && std::getenv("RR_FIELD_CARS_FROM_GOLDEN") != nullptr;
    bool load_cars(const std::map<std::string, int64_t>& f) {
        std::vector<std::string> addrs;
        for (const auto& [key, value] : f)
            if (key.rfind("ring.", 0) == 0 && key.size() > 9 &&
                key.compare(key.size() - 9, 9, ".position") == 0)
                addrs.push_back(key.substr(5, key.size() - 14));
        std::sort(addrs.begin(), addrs.end(), [](const std::string& a, const std::string& b) {
            return std::stoul(a, nullptr, 16) < std::stoul(b, nullptr, 16);
        });
        std::vector<TrafficCar*> cars;
        for (auto& c : setup.instance.traffic->cars_mut())
            if (c.active) cars.push_back(&c);
        std::sort(cars.begin(), cars.end(),
                  [](const TrafficCar* a, const TrafficCar* b) { return a->serial < b->serial; });
        if (cars.size() != addrs.size()) {
            std::cout << "car count original " << addrs.size() << " native " << cars.size()
                      << " (diagnostic car load)\n";
            return false;
        }
        for (size_t k = 0; k < cars.size(); ++k) {
            cars[k]->position = int32_t(f.at("ring." + addrs[k] + ".position"));
            cars[k]->lateral = int32_t(f.at("ring." + addrs[k] + ".lateral"));
            cars[k]->speed = int32_t(f.at("ring." + addrs[k] + ".speed"));
        }
        return true;
    }
    void check(const std::map<std::string, int64_t>& f, const std::string& key, int64_t native) {
        if (all && !diff.empty()) {
            const auto it = f.find(key);
            if (it != f.end() && it->second != native)
                std::cout << "  also " << key << " original " << it->second << " native " << native << '\n';
            return;
        }
        if (!diff.empty()) return;
        const auto it = f.find(key);
        if (it == f.end()) {
            diff = "missing required " + key;
            return;
        }
        used.insert(key);
        if (it->second != native)
            diff = key + " original " + std::to_string(it->second) + " native " + std::to_string(native);
    }
    void fail(const std::string& message) {
        if (diff.empty())
            diff = message;
        else if (all)
            std::cout << "  also " << message << '\n';
    }
    // Golden pointer word -> native rider (-1 null, -2 non-rider node).
    int pointer_rider(int64_t address) const {
        if (address == 0) return -1;
        const auto it = identity.find(address);
        return it == identity.end() || it->second < 0 ? -2 : it->second;
    }
    void brain(const std::map<std::string, int64_t>& f, const std::string& p, const Rider& r) {
        const AiState& br = r.brain;
        // Captures after the +0x1cc schema extension also compare the level
        // 3/4 speed words; older ones stop at +0x1c4.
        const uint32_t last = f.count(p + "brain_1cc") ? 0x1cc : 0x1c4;
        for (uint32_t off = 0x138; off <= last && (diff.empty() || all); off += 4) {
            char hex[8];
            std::snprintf(hex, sizeof hex, "%x", off);
            const std::string key = p + "brain_" + hex;
            const auto it = f.find(key);
            if (it == f.end()) {
                fail("missing required " + key);
                return;
            }
            used.insert(key);
            if (off == 0x168 || off == 0x16c || off == 0x170) {
                const int want = pointer_rider(it->second);
                const int mine = off == 0x168 ? br.target_rider : off == 0x16c ? br.nbr_ahead : br.nbr_behind;
                if (want != mine)
                    fail(key + " original rider " + std::to_string(want) + " native " + std::to_string(mine));
                continue;
            }
            if (off == 0x180) {
                // Latched overtake node: rider identity and type, or a
                // non-rider node of a type other than 4/8.
                const int want = pointer_rider(it->second);
                const auto t = address_type.find(it->second);
                bool same;
                if (want == -1)
                    same = br.overtake_type == 0;
                else if (want >= 0)
                    same = br.overtake_latched == want && t != address_type.end() &&
                           int64_t(br.overtake_type) == t->second;
                else
                    same = br.overtake_latched < 0 && br.overtake_type != 0 && br.overtake_type != 4 &&
                           br.overtake_type != 8;
                if (!same)
                    fail(key + " original rider " + std::to_string(want) + " native " +
                         std::to_string(br.overtake_latched) + " type " + std::to_string(br.overtake_type));
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
                fail(key + " original " + std::to_string(it->second) + " native " + std::to_string(mine));
        }
    }
    void bike_slot(const std::map<std::string, int64_t>& f, const std::string& p, const Rider& r,
                   bool player) {
        const Bike& b = r.bike;
        check(f, p + "position", b.i32(0x1c));
        check(f, p + "lateral", b.i32(0x28));
        check(f, p + "height", b.i32(0x20));
        check(f, p + "velocity", b.i32(0xf4));
        check(f, p + "speed", b.i32(0xec));
        check(f, p + "gear", b.i32(0x23c));
        check(f, p + "condition", b.i32(0x2f0));
        check(f, p + "crash", b.i32(0x2f4));
        check(f, p + "flags", b.u8(0xc0));
        check(f, p + "steer", b.i32(0x288));
        check(f, p + "steer_rate", b.i32(0xf0));
        check(f, p + "lat_vel", b.i32(0x10c));
        check(f, p + "ground", b.i32(0x160));
        check(f, p + "lat104", b.i32(0x104));
        check(f, p + "lat108", b.i32(0x108));
        check(f, p + "lat10c", b.i32(0x10c));
        check(f, p + "yaw60", b.i32(0x60));
        // slice_ptr is a host pointer: compare the sample through position.
        if (diff.empty()) {
            const auto pos = f.find(p + "position");
            if (pos != f.end() && size_t(uint32_t(pos->second) >> 8) != r.sample)
                diff = p + "slice sample original " + std::to_string(uint32_t(pos->second) >> 8) +
                       " native " + std::to_string(r.sample);
        }
        for (const auto& [key, value] : f) {
            if (!diff.empty() && !all) return;
            if (key.rfind(p + "bike_", 0) != 0) continue;
            const uint32_t off = uint32_t(std::stoul(key.substr(p.size() + 5), nullptr, 16));
            if (off == 0x44) continue;  // 0x00438450 render-prep copy (non-state)
            if (off == 0x1a0) continue; // contact-link host pointer (see half raw set below)
            const int64_t native = off == 0x0c   ? r.sched_next
                                   : off == 0x10 ? r.sched_last
                                   : off == 0x14 ? r.sched_interval
                                                 : b.i32(int(off));
            if (player && (off == 0x2dc || off == 0x2e8)) {
                // Nondeterministic sound-thread channel (see the solo
                // replay): diagnostic note, never a sim divergence.
                used.insert(key);
                const auto it = f.find(key);
                if (it != f.end() && it->second != native) {
                    ++rpm_timing_notes;
                    std::cout << "rpm timing " << key << " original " << it->second << " native " << native
                              << " (nondeterministic channel)\n";
                }
                continue;
            }
            check(f, key, native);
        }
        for (uint32_t off = 0x180; off <= 0x1fc && (diff.empty() || all); off += 4) {
            char hex[8];
            std::snprintf(hex, sizeof hex, "%x", off);
            const std::string key = p + "raw_" + hex;
            if (off == 0x1a4 || off == 0x1c4 || off == 0x1d8 || off == 0x1e0) {
                used.insert(key);
                continue;
            } // pointers (raw_1a0 contact link likewise below)
            if (off == 0x1a0) {
                // 0x004368f0 contact-link host pointer (+0x68): set to the
                // partner object when a pair contacts (here the crash body
                // linked to its bike at tick 2700). Process-specific address,
                // never equal across runs: compare as non-state (like
                // bike_44/slice_ptr). The link only gates body-static
                // contacts; any behavioral effect surfaces as a sim-field
                // divergence, which remains fatal. Revisit with identity
                // resolution if one traces here.
                used.insert(key);
                continue;
            }
            int64_t native;
            if (off == 0x1c8)
                native = r.weapon;
            else if (off == 0x1cc)
                native = r.steal_until;
            else if (off == 0x1d0)
                native = r.hit_until;
            else if (off == 0x1d4)
                native = r.attack.weapon;
            else if (off == 0x1f4)
                native = r.place_refresh;
            else if (off == 0x1f8)
                native = r.finish_distance;
            else if (off == 0x1fc)
                native = r.place;
            else
                native = r.half_object.i32(int(off));
            check(f, key, native);
        }
        // Crash body (the half while the bike is crashed): rider.N.body_<hex>
        // in captures since traffic-field-body (October 28, 2026).
        if (f.count(p + "body_1c")) {
            if (!r.crash_active) {
                if (diff.empty()) diff = p + "body present in original, native rider not crashed";
            } else
                for (int off : {0x1c, 0x20, 0x24, 0x28, 0x34, 0x38, 0xec, 0xf0, 0xf4, 0x108, 0x10c, 0x110,
                                0x114, 0x118, 0x11c, 0x12c, 0x17c}) {
                    char key[16];
                    std::snprintf(key, sizeof key, "body_%x", off);
                    if (f.count(p + key)) check(f, p + key, r.crash_actor.motion.i32(off));
                }
            for (const char* k : {"body_c0", "body_2f4"}) used.insert(p + k);
        }
        // Own sub-slice static-collision state (rider.N.slice_*): 0x004373f0
        // sweeps only boxes the renderer has loaded (0x0044b630).
        if (f.count(p + "slice_60")) {
            // The player's half slot (think 0x00434660) reads the half's own
            // slice, which is the crash body's while the rider is down.
            const auto think = f.find(p + "think");
            const bool half = think != f.end() && think->second == 0x00434660;
            const auto st = half && r.crash_active
                                ? race().static_slice_state(r.crash_actor.route, r.crash_actor.sample)
                                : race().static_slice_state(r);
            check(f, p + "slice_60", st.borr_count);
            check(f, p + "slice_3d", st.billboards);
            if (st.borr_count) {
                check(f, p + "slice_6c", st.borr0);
                const auto loaded = f.find(p + "slice_61");
                used.insert(p + "slice_61");
                // Box loading follows the previous frame's draw. Native loads
                // slices starting <= ring camera + 0x1e00 (exact on 22,800
                // samples); the original also skips a few far-edge slices
                // (projection visibility, not ported): a counted note, since
                // any contact it changes surfaces as a field/RNG divergence.
                if (loaded != f.end() && (loaded->second != 0) != st.boxes_ready) {
                    ++slice_load_notes;
                    std::cout << "slice load note tick " << race().ticks() << ' ' << p << "original "
                              << (loaded->second != 0) << " native " << st.boxes_ready << " (borr lateral "
                              << st.borr0 << ")\n";
                }
            } else
                for (const char* k : {"slice_61", "slice_6c"}) used.insert(p + k);
            for (const char* k : {"slice_64", "slice_68"}) used.insert(p + k);
        }
        if (f.count(p + "rating_0"))
            for (unsigned i = 0; i < 30; ++i) {
                char key[32];
                std::snprintf(key, sizeof key, "rating_%x", i * 4);
                check(f, p + key, r.brain.rating[i]);
            }
        brain(f, p, r);
        if (player || f.count(p + "anim")) {
            check(f, p + "anim", r.ride_animation.animation);
            check(f, p + "anim_frame", r.ride_animation.frame);
            check(f, p + "anim_queued", r.ride_animation.queued);
            check(f, p + "anim_loops", r.ride_animation.loops);
            check(f, p + "anim_deadline", int32_t(r.ride_animation.deadline));
        }
        if (player) {
            check(f, p + "stamina_max", r.stamina.max);
            check(f, p + "stamina_current", r.stamina.current);
            check(f, "nitro.charge", r.nitro_charge);
            check(f, "nitro.charges", race().nitro_charges());
        }
    }
    void half_slot(const std::map<std::string, int64_t>& f, const std::string& p, const Rider& r) {
        const auto& m = r.half_mirror;
        check(f, p + "position", m.position);
        check(f, p + "lateral", m.lateral);
        check(f, p + "height", m.height);
        check(f, p + "velocity", m.velocity);
        check(f, p + "speed", m.speed);
        check(f, p + "steer_rate", m.steer_rate);
        check(f, p + "lat_vel", m.lat_vel);
        check(f, p + "lat10c", m.lat_vel);
        check(f, p + "lat108", m.lat108);
        check(f, p + "yaw60", m.yaw);
        check(f, p + "flags", r.half_object.u8(0xc0));
        check(f, p + "ground", r.half_object.i32(0x160));
        check(f, p + "gear", r.half_object.i32(0x23c));
        check(f, p + "condition", r.half_object.i32(0x2f0));
        check(f, p + "crash", r.half_object.i32(0x2f4));
        check(f, p + "steer", r.half_object.i32(0x288));
        check(f, p + "lat104", r.half_object.i32(0x104));
        for (const auto& [key, value] : f) {
            if (!diff.empty() && !all) return;
            if (key.rfind(p + "bike_", 0) != 0) continue;
            const uint32_t off = uint32_t(std::stoul(key.substr(p.size() + 5), nullptr, 16));
            if (off == 0x44) continue;
            const int64_t native = off == 0x0c   ? r.half_sched_next
                                   : off == 0x10 ? r.half_sched_last
                                   : off == 0x14 ? r.half_sched_interval
                                   : off == 0x18 ? r.half_dt
                                                 : r.half_object.i32(int(off));
            check(f, key, native);
        }
    }
    // The capture's ring.<pool-address> rows carry reliable motion words
    // (its legacy kind/phase offsets are not the car dword offsets). Pool
    // addresses are not native pointers: compare the motion triples as a
    // multiset, like the node list, without hydrating simulation state.
    void cars(const std::map<std::string, int64_t>& f) {
        if (ignore_cars || !setup.instance.traffic) return;
        std::vector<std::string> addresses;
        for (const auto& [key, value] : f)
            if (key.rfind("ring.", 0) == 0 && key.ends_with(".position"))
                addresses.push_back(key.substr(5, key.size() - 14));
        std::sort(addresses.begin(), addresses.end(), [](const auto& a, const auto& b) {
            return std::stoul(a, nullptr, 16) < std::stoul(b, nullptr, 16);
        });
        std::vector<const TrafficCar*> active;
        for (const auto& c : setup.instance.traffic->cars())
            if (c.active) active.push_back(&c);
        if (active.size() != addresses.size()) {
            fail("car pool active count original " + std::to_string(addresses.size()) + " native " +
                 std::to_string(active.size()));
            return;
        }
        std::sort(addresses.begin(), addresses.end(), [&](const auto& a, const auto& b) {
            auto motion = [&](const auto& addr) {
                const auto p = "ring." + addr + ".";
                return std::tuple{f.at(p + "position"), f.at(p + "lateral"), f.at(p + "speed")};
            };
            return motion(a) < motion(b);
        });
        std::sort(active.begin(), active.end(), [](const auto* a, const auto* b) {
            return std::tuple{a->position, a->lateral, a->speed} <
                   std::tuple{b->position, b->lateral, b->speed};
        });
        for (size_t i = 0; i < active.size(); ++i) {
            const std::string p = "ring." + addresses[i] + ".";
            check(f, p + "position", active[i]->position);
            check(f, p + "lateral", active[i]->lateral);
            check(f, p + "speed", active[i]->speed);
            if (g.frames[0].count("ring.car_schema") && g.frames[0].at("ring.car_schema") == 2) {
                check(f, p + "kind", active[i]->kind);
                check(f, p + "vehicle", active[i]->vehicle);
                check(f, p + "lane", active[i]->lane);
                check(f, p + "phase", active[i]->phase);
            } else {
                // Legacy probe used decompiler int-pointer element indices
                // as byte offsets. These four aliases are capture defects,
                // not observations of the named car state. Keep a visible
                // schema ledger; never hydrate or compare their junk values.
                for (const char* alias : {"kind", "vehicle", "lane", "phase"}) used.insert(p + alias);
            }
        }
    }
    void nodes(const std::map<std::string, int64_t>& f) {
        std::vector<std::pair<int64_t, int64_t>> want, have;
        for (int n = 0;; ++n) {
            const std::string base = "node." + std::to_string(n) + ".";
            const auto t = f.find(base + "think"), pos = f.find(base + "position");
            if (t == f.end() || pos == f.end()) break;
            if (ignore_cars && (t->second == 0x00446830 || (ignore_peds && t->second == 0x00440680)))
                continue;
            want.push_back({t->second, pos->second});
            used.insert(base + "think");
            used.insert(base + "position");
        }
        const auto& riders = race().riders();
        for (size_t i = 1; i < riders.size(); ++i) {
            const Rider& r = riders[i];
            if (r.parked) have.push_back({0, r.bike.i32(0x1c)});
            have.push_back({r.parked ? 0 : 0x004463e0, r.half_mirror.position});
        }
        if (setup.instance.pedestrians)
            for (const auto& p : setup.instance.pedestrians->pool())
                if (p.active && !ignore_peds) have.push_back({0x00440680, p.body.bike.i32(0x1c)});
        if (setup.instance.traffic) {
            for (const auto& c : setup.instance.traffic->controllers())
                if (c.active) have.push_back({0, c.position_q8});
            for (const auto& sg : setup.instance.traffic->signals())
                if (sg.active) have.push_back({sg.quiet ? 0 : 0x0041f220, sg.position_q8});
            if (!ignore_cars)
                for (const auto& c : setup.instance.traffic->cars())
                    if (c.active) have.push_back({0x00446830, c.position});
        }
        std::sort(want.begin(), want.end());
        std::sort(have.begin(), have.end());
        if (want != have) {
            diff = "node list original";
            for (const auto& w : want)
                diff += " (" + std::to_string(w.first) + "," + std::to_string(w.second) + ")";
            diff += " native";
            for (const auto& w : have)
                diff += " (" + std::to_string(w.first) + "," + std::to_string(w.second) + ")";
            if (setup.instance.traffic)
                for (const auto& c : setup.instance.traffic->cars())
                    if (c.active)
                        diff += " car(kind " + std::to_string(c.kind) + " lane " + std::to_string(c.lane) +
                                " lat " + std::to_string(c.lateral) + " speed " + std::to_string(c.speed) +
                                " serial " + std::to_string(c.serial) + ")";
        }
    }
    void sched_and_ring(const std::map<std::string, int64_t>& f) {
        for (const auto& [key, value] : f) {
            if (!diff.empty()) return;
            if (key.rfind("sched.", 0) == 0) {
                const size_t dot = key.find('.', 6);
                const auto who = identity.find(int64_t(std::stoul(key.substr(6, dot - 6), nullptr, 16)));
                if (who == identity.end() || who->second < 0) {
                    diff = key + " names no native rider";
                    return;
                }
                const Rider& r = race().riders()[size_t(who->second)];
                const std::string field = key.substr(dot + 1);
                if (!r.parked) {
                    diff = key + ": native rider " + std::to_string(who->second) + " is live";
                    return;
                }
                check(f, key,
                      field == "next"       ? r.sched_next
                      : field == "last"     ? r.sched_last
                      : field == "interval" ? r.sched_interval
                                            : r.bike.i32(0x18));
            }
        }
        if (const auto* ring = setup.instance.ring.get()) {
            check(f, "ring.front", ring->front_q8());
            check(f, "ring.back", ring->back_q8());
            check(f, "ring.camera", ring->front_q8() - 0x5000);
        }
    }
    // Diagnostic: RR_FIELD_TRACE=<native rider> prints that rider's main
    // words beside the golden slot the native list gives it, every frame.
    void trace(size_t i, const std::vector<std::pair<int, uint32_t>>& native_slots) {
        static const char* who = std::getenv("RR_FIELD_TRACE");
        if (!who) return;
        const int want = std::atoi(who);
        const auto& f = g.frames.at(i);
        for (size_t k = 0; k < native_slots.size(); ++k) {
            if (native_slots[k].first != want || native_slots[k].second != 4) continue;
            const Rider& r = race().riders()[size_t(want)];
            const std::string p = "rider." + std::to_string(k) + ".";
            auto gv = [&](const std::string& key) {
                const auto it = f.find(p + key);
                return it == f.end() ? int64_t(-999999) : it->second;
            };
            std::cout << "trace tick " << tick0 + int64_t(i) << " slot " << k << " pos " << r.bike.i32(0x1c)
                      << "/" << gv("position") << " lat " << r.bike.i32(0x28) << "/" << gv("lateral")
                      << " spd " << r.bike.i32(0xec) << "/" << gv("speed") << " dt " << r.bike.i32(0x18)
                      << "/" << gv("bike_18") << " last " << r.sched_last << "/" << gv("bike_10") << " st "
                      << r.brain.state << "/" << gv("brain_160") << " tx " << r.brain.target_x << "/"
                      << gv("brain_194") << " rp " << r.brain.replan_ticks << "/" << gv("brain_188") << " fd "
                      << r.finish_distance << "/" << gv("raw_1f8") << " steer " << r.bike.i32(0x288) << "/"
                      << gv("steer") << " half-last " << r.half_sched_last << " sample " << r.sample << '\n';
        }
    }
    // Compare one golden frame against the native state after its tick.
    bool frame(size_t i) {
        const auto& f = g.frames.at(i);
        diff.clear();
        const char* ped_trace = std::getenv("RR_FIELD_TRACE_PEDS");
        if (ped_trace && tick0 + int64_t(i) >= std::atoi(ped_trace) && setup.instance.pedestrians)
            for (const auto& p : setup.instance.pedestrians->pool())
                if (p.active) {
                    const Bike& b = p.body.bike;
                    std::cout << "ped tick " << tick0 + int64_t(i) << " serial " << p.serial << " type "
                              << p.type << " state " << p.state << " pos " << b.i32(0x1c) << " anchor "
                              << p.anchor << " lat " << b.i32(0x28) << " spd " << b.i32(0xec) << " dt "
                              << b.i32(0x18) << " last " << p.last_tick << " next " << p.next_tick
                              << " heading " << b.i32(0x128) << " ground " << b.i32(0x160) << " accel "
                              << b.accel << " drag " << b.i32(0x15c) << " lat_accel " << b.i32(0x118) << " h "
                              << b.i32(0x20) << " v " << b.i32(0xf4) << " trigger " << p.trigger << " pace "
                              << p.pace << '\n';
                }
        const auto native_slots = slots();
        trace(i, native_slots);
        check(f, "riders", int64_t(native_slots.size()));
        for (size_t k = 0; k < native_slots.size() && (diff.empty() || all); ++k) {
            const std::string p = "rider." + std::to_string(k) + ".";
            const Rider& r = race().riders()[size_t(native_slots[k].first)];
            if (native_slots[k].second == 4)
                bike_slot(f, p, r, native_slots[k].first == 0);
            else
                half_slot(f, p, r);
            if (!diff.empty() && diff.find(" (native rider ") == std::string::npos)
                diff += " (native rider " + std::to_string(native_slots[k].first) + ")";
        }
        if (diff.empty()) cars(f);
        if (diff.empty()) nodes(f);
        if (diff.empty()) sched_and_ring(f);
        if (diff.empty()) return false;
        std::cout << "first divergence at frame " << i << " (tick " << tick0 + int64_t(i) << "): " << diff
                  << '\n';
        return true;
    }

    int run() {
        const auto& h = g.frames.at(0);
        // Spawn placement: every native bike sits on its captured node lateral.
        std::map<int64_t, int64_t> captured;
        for (const auto& [key, value] : h) {
            if (key.rfind("nodeaddr.", 0) != 0 || key.find('.', 9) != std::string::npos) continue;
            const auto t = h.find(key + ".type"), p = h.find(key + ".position"), l = h.find(key + ".lateral");
            if (t == h.end() || p == h.end()) continue;
            address_type[value] = t->second;
            if (t->second == 4 && l != h.end()) captured[p->second] = l->second;
        }
        for (const auto& r : race().riders()) {
            const auto c = captured.find(r.bike.i32(0x1c));
            if (c == captured.end() || c->second != r.bike.i32(0x28)) {
                std::cout << "first divergence at spawn: rider at " << r.bike.i32(0x1c)
                          << " lateral original "
                          << (c == captured.end() ? std::string("missing") : std::to_string(c->second))
                          << " native " << r.bike.i32(0x28) << '\n';
                return 1;
            }
        }
        identity = rider_node_identities(h, race().riders());
        // Frame 0 is post tick 1: run the build tick natively, then every
        // countdown tick, comparing each frame.
        race().set_ticks(int32_t(tick0 - 1));
        const uint32_t rng_before = race().rng_state();
        for (size_t i = 0; i < green; ++i) {
            race().countdown_step();
            if (frame(i)) return 1;
        }
        if (closure_factory_end && closure_anchor) {
            // Deferred closure: the countdown replay must land exactly on the
            // green pre-step anchor (its pre-step consumers are reproduced by
            // Race::countdown_step, e.g. the pedestrian re-trigger 0x00440dd0
            // at level 5). The header's ring.post_draws only covers the
            // factory-to-green window it could measure; the countdown's own
            // draws are the delta between rng_before and the replayed state.
            const uint32_t end = race().rng_state();
            if (end != *closure_anchor) {
                int64_t off = 0;
                uint32_t a = *closure_factory_end, b = *closure_anchor;
                for (int64_t n = 1; n <= 200000 && !off; ++n) {
                    a = a * 0x343fdu + 0x269ec3u;
                    b = b * 0x343fdu + 0x269ec3u;
                    if (a == *closure_anchor)
                        off = n; // countdown ends n draws early
                    else if (b == *closure_factory_end)
                        off = -n; // countdown ends n draws late
                }
                std::cout << "countdown does not close on the green anchor";
                if (off) std::cout << " (factory end is " << off << " draws from green)";
                std::cout << '\n';
                return 1;
            }
            std::cout << "countdown closes on the green anchor (pre-green countdown consumers reproduced)\n";
            race().set_rng_state(*closure_anchor);
        } else if (race().rng_state() != rng_before) {
            std::cout << "countdown consumed draws\n";
            return 1;
        }
        std::cout << "countdown: " << green << " native ticks equal\n";
        std::vector<std::pair<int64_t, uint32_t>> rng_history;
        for (size_t i = green; i < g.frames.size(); ++i) {
            const auto& f = g.frames[i];
            const auto fc = f.find("controls");
            if (fc == f.end()) throw std::runtime_error("frame lacks controls");
            race().step(demo ? 0 : uint32_t(fc->second));
            const auto ft = f.find("tick");
            if (ft == f.end() || ft->second != tick0 + int64_t(i)) {
                std::cout << "tick not contiguous at frame " << i << '\n';
                return 1;
            }
            const auto fr = f.find("rng");
            rng_history.push_back({fr == f.end() ? -1 : fr->second, race().rng_state()});
            if (rng_history.size() > 8) rng_history.erase(rng_history.begin());
            if (fr == f.end() || uint32_t(fr->second) != race().rng_state()) {
                if (all) {
                    std::cout << "rng mismatch original " << (fr == f.end() ? -1 : fr->second) << " native "
                              << race().rng_state() << '\n';
                    if (frame(i)) return 1; // diagnostic: expose the simultaneous state mismatch
                }
                std::cout << "first divergence at frame " << i << " (tick " << tick0 + int64_t(i)
                          << "): rng original " << (fr == f.end() ? -1 : fr->second) << " native "
                          << race().rng_state() << '\n';
                std::cout << "rng history (last " << rng_history.size() << " ticks, golden vs native):";
                for (const auto& e : rng_history) std::cout << " (" << e.first << "," << e.second << ")";
                std::cout << '\n';
                return 1;
            }
            if (frame(i)) return 1;
            if (cars_from_golden && !load_cars(f)) return 1;
            // Per-frame hook 0x00434980: the player's rpm (see the solo replay).
            // Frame end, or crashed on the ground (direct 0x004349c0).
            bool frame_ends = true;
            if (i + 1 < g.frames.size()) {
                const auto next44 = g.frames[i + 1].find("rider.0.bike_44");
                const auto pos = f.find("rider.0.position");
                if (next44 != g.frames[i + 1].end() && pos != f.end())
                    frame_ends = next44->second == pos->second;
            }
            const Bike& pbike = race().riders()[0].bike;
            if (frame_ends || (pbike.i32(0x2f4) != 0 && pbike.i32(0x20) < 0x65))
                update_engine_rpm(race().riders()[0].bike);
        }
        // Coverage: every golden key is compared or listed as non-state.
        static const std::set<std::string> exact = {"tick",
                                                    "rng",
                                                    "rng_calls",
                                                    "controls",
                                                    "riders",
                                                    "course",
                                                    "level",
                                                    "bike",
                                                    "character",
                                                    "seed",
                                                    "solo",
                                                    "probe.S",
                                                    "probe.end",
                                                    "probe.player",
                                                    "probe.frozen_words",
                                                    "player.demo_hold",
                                                    "ring.car_schema"};
        std::map<std::string, size_t> extras;
        for (const auto& [key, value] : g.frames.back()) {
            if (used.count(key) || exact.count(key)) continue;
            const std::string suffix = key.substr(key.rfind('.') + 1);
            if (key.rfind("rider.", 0) == 0 &&
                (suffix == "think" || suffix == "slice_ptr" || suffix == "bike_44"))
                continue;
            if (key.rfind("node.", 0) == 0 && suffix == "kind") continue;
            std::string folded;
            bool digits = false;
            for (char c : key) {
                if (c >= '0' && c <= '9') {
                    if (!digits) folded += 'N';
                    digits = true;
                } else {
                    folded += c;
                    digits = false;
                }
            }
            ++extras[folded];
        }
        size_t total = 0;
        for (const auto& [name, n] : extras) {
            total += n;
            if (coverage) std::cout << "golden-only " << name << ' ' << n << '\n';
        }
        if (!g.frames[0].count("ring.car_schema"))
            std::cout << "legacy car probe: kind/vehicle/lane/phase have invalid offsets; motion triples "
                         "compared\n";
        if (ignore_cars)
            std::cout << g.frames.size() << " ticks compared without free cars (diagnostic)\n";
        else if (total == 0 && rpm_timing_notes == 0 && slice_load_notes == 0)
            std::cout << g.frames.size() << " ticks identical\n";
        else
            std::cout << g.frames.size() << " ticks identical (" << total
                      << " golden-only fields not compared; " << rpm_timing_notes
                      << " rpm-timing notes on the nondeterministic channel; " << slice_load_notes
                      << " far-edge slice-load notes)\n";
        return 0;
    }
};
} // namespace rr::golden
