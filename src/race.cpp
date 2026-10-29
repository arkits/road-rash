#include "race.hpp"
#include "course_scene.hpp"
#include "scenery.hpp"
#include "track_ring.hpp"
#include "animation.hpp"
#include "pedestrians.hpp"
#include "terrain_ground.hpp"
#include "traffic.hpp"
#include "pedestrians.hpp"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstdlib>
#include <stdexcept>

namespace rr {
namespace {
int32_t div_pow2(int64_t x, int n) {
    return int32_t(x / (int64_t(1) << n));
}
int32_t div256(int64_t x) {
    return div_pow2(x, 8);
}
int32_t wrap_angle(int32_t a) {
    return a > 0x7fffff ? a - 0x1000000 : a;
}
constexpr int32_t ground_wall = 999; // 0x00439b10 wall height
// Grid (0x00464218): slots 0..3 at sample 0x32, 4..7 at 0x33, four lateral offsets.
constexpr int32_t grid_lateral[4] = {16384, -3616, -23616, -43616};
int32_t grid_position(int slot) {
    return slot < 4 ? 0x3200 : 0x3300;
}
int32_t ai_speed_units(int32_t v) {
    return (v >> 12) + (v >> 7) + (v >> 9);
} // 0x00409f70
} // namespace

std::vector<Opponent> original_opponents(const std::vector<uint8_t>& exe, const std::vector<BikeSpec>& specs,
                                         int level, int count);
namespace {
// Read one little-endian i32 from the reference EXE by virtual address.
int32_t exe_word(const std::vector<uint8_t>& exe, uint32_t va) {
    constexpr uint32_t text_va = 0x00401000, text_raw = 0x400, text_size = 0x5fc00;
    constexpr uint32_t data_va = 0x00463000, data_raw = 0x61600, data_size = 0x16400;
    size_t raw;
    if (va >= text_va && va + 4 <= text_va + text_size)
        raw = text_raw + (va - text_va);
    else if (va >= data_va && va + 4 <= data_va + data_size)
        raw = data_raw + (va - data_va);
    else
        throw std::runtime_error("EXE word outside mapped sections");
    if (raw + 4 > exe.size()) throw std::runtime_error("EXE word outside file");
    return int32_t(uint32_t(exe[raw]) | uint32_t(exe[raw + 1]) << 8 | uint32_t(exe[raw + 2]) << 16 |
                   uint32_t(exe[raw + 3]) << 24);
}
} // namespace

std::vector<Personality> read_personalities(const std::vector<uint8_t>& exe) {
    // 0x00464558: 10 rider personalities, 0x204 bytes each (0x004078f0 copies
    // them verbatim to 0x004c6b40; the AI brain is one such copy).
    std::vector<Personality> out;
    for (int k = 0; k < 10; ++k) {
        const uint32_t base = 0x00464558 + 0x204u * uint32_t(k);
        Personality p;
        for (int i = 0; i < 5; ++i) {
            p.cur2_levels[i] = exe_word(exe, base + 0x1a8 + 4u * uint32_t(i));
            p.cur_levels[i] = exe_word(exe, base + 0x1bc + 4u * uint32_t(i));
        }
        for (int i = 0; i < 30; ++i) {
            p.rating[i] = exe_word(exe, base + 4u * uint32_t(i));
            p.rating_tgt[i] = exe_word(exe, base + 0x78 + 4u * uint32_t(i));
        }
        for (int i = 0; i < 3; ++i) p.ease[i] = exe_word(exe, base + 0x12c + 4u * uint32_t(i));
        const uint32_t b165 = base + 0x165;
        constexpr uint32_t data_va = 0x00463000, data_raw = 0x61600;
        p.overtake_index = int(exe[data_raw + (b165 - data_va)]);
        p.kick_roll = int8_t(exe[data_raw + base + 0x1d0 - data_va]);
        p.backhand_roll = int8_t(exe[data_raw + base + 0x1d1 - data_va]);
        for (int i = 0; i < 15; ++i) p.reaction[i] = exe_word(exe, base + 0xf0 + 4u * uint32_t(i));
        p.reach = exe_word(exe, base + 0x1d4);
        p.word_184 = exe_word(exe, base + 0x184);
        out.push_back(p);
    }
    return out;
}

// Full-field grid tables (0x00405600): level < 4 uses 0x00464088, else 0x00464108.
// 16 slots of (lateral, sub-slice selector); position equals the selector.
void read_grid_slot(const std::vector<uint8_t>& exe, int level, int slot, int32_t& lateral, int32_t& pos) {
    if (slot < 0 || slot > 15) throw std::runtime_error("Grid slot out of range");
    const uint32_t base = (level < 4 ? 0x00464088u : 0x00464108u) + 8u * uint32_t(slot);
    lateral = exe_word(exe, base);
    pos = exe_word(exe, base + 4);
}
std::vector<Opponent> original_opponents(const std::vector<uint8_t>& exe, const std::vector<BikeSpec>& specs,
                                         int level, int count) {
    if (level < 0 || level > 4 || count < 0 || count > 14)
        throw std::runtime_error("Invalid level or opponent count");
    constexpr uint32_t data_va = 0x00463000, data_raw = 0x61600, data_size = 0x16400;
    auto at = [&](uint32_t va) {
        if (va < data_va || va + 4 > data_va + data_size || exe.size() < data_raw + (va - data_va) + 4)
            throw std::runtime_error("Opponent table outside .data");
        return data_raw + (va - data_va);
    };
    auto word = [&](uint32_t va) {
        const size_t p = at(va);
        return int32_t(uint32_t(exe[p]) | uint32_t(exe[p + 1]) << 8 | uint32_t(exe[p + 2]) << 16 |
                       uint32_t(exe[p + 3]) << 24);
    };
    auto byte = [&](uint32_t va) { return exe[at(va & ~3u) + (va & 3u)]; };
    std::vector<Opponent> out;
    for (int k = 1; k <= count; ++k) {
        // 0x00407990: slots 0..8 use personality copies 0x004c6b40 + slot
        // rows; 9..14 use 0x004c5f20 rows, which 0x004078f0 fills at career
        // start with row 0 and +0x1bc speeds jittered by rand()%13-6 (drawn
        // then, outside the race stream: not reproduced).
        const uint32_t personality = 0x00464558 + 0x204u * uint32_t(k < 9 ? k : 0);
        const size_t bike = byte(0x004641c8 + uint32_t(level + k * 5));
        if (bike >= specs.size()) throw std::runtime_error("Opponent bike index out of range");
        const uint32_t row = 0x00463c78 + 0x40u * uint32_t(k);
        // 0x00405600 builds slots 1..<DAT_004642d4-1 with the grid tables
        // 0x00464088 (level<4) / 0x00464108: position equals the selector,
        // lateral is the table lateral (0x00405990 adds road centre at
        // build). Slots 1..7 existed before; 8..14 complete the offline 15.
        int32_t grid_lat = 0, grid_pos = 0;
        read_grid_slot(exe, level, k, grid_lat, grid_pos);
        Opponent o{specs[bike],
                   word(row + 0xc) / 2,
                   word(personality + 0x1bc + 4u * uint32_t(level)),
                   word(personality + 0x1a8 + 4u * uint32_t(level)),
                   int(byte(personality + 0x18c)),
                   k,
                   k,
                   grid_pos,
                   grid_lat,
                   false,
                   word(0x004642a0 + 4u * uint32_t(level)),
                   {word(row + 0x30), word(row + 0x34), word(row + 0x38), word(row + 0x3c)},
                   word(row + 0x18 + 4u * uint32_t(level))};
        out.push_back(o);
    }
    return out;
}
int32_t race_road_base_q8(int32_t golden_lateral, int32_t route_offset, int32_t grid_word) {
    // 0x00405990: lateral = sub-slice +0xcc (absolute Q8 road centre) +
    // grid offset. RouteSample::offset is relative (world units), so the
    // absolute anchor is golden - relative*256 - grid. No RNG/allocation.
    return golden_lateral - route_offset * 256 - grid_word;
}

Race::Race(std::vector<RouteSample> a, std::vector<RouteSample> b, const PhysicsTables& t,
           const BikeSpec& spec, int32_t rider_value, std::vector<Opponent> opponents, int level,
           int32_t road_base_q8, const std::vector<Personality>& personalities,
           const std::vector<uint8_t>& exe_image, int32_t player_grid_pos, int32_t player_grid_lat)
    : routes_{std::make_shared<const std::vector<RouteSample>>(std::move(a)),
              std::make_shared<const std::vector<RouteSample>>(std::move(b))},
      tables_(t), level_(level), road_base_(road_base_q8) {
    if (!exe_image.empty()) {
        std::array<int32_t, 5> eager{};
        for (uint32_t i = 0; i < 5; ++i) eager[i] = exe_word(exe_image, 0x00465a10 + 4u * i);
        overtake_eagerness_ = eager;
        std::array<int8_t, 8> steal{};
        for (uint32_t i = 0; i < 8; ++i) steal[i] = int8_t(exe_word(exe_image, 0x00464538 + 4u * i));
        steal_codes_ = steal;
    }
    if (routes_[0]->size() < 64 || routes_[1]->size() < 64)
        throw std::runtime_error("Route too short for a race");
    auto brain_from = [&](AiState& ai, const Personality& p) {
        for (int i = 0; i < 5; ++i) {
            ai.spd_a[i] = p.cur2_levels[i];
            ai.spd_b[i] = p.cur_levels[i];
        }
        for (int i = 0; i < 30; ++i) {
            ai.rating[i] = p.rating[i];
            ai.rating_tgt[i] = p.rating_tgt[i];
        }
        for (int i = 0; i < 3; ++i) ai.ease[i] = p.ease[i];
        ai.overtake_index = p.overtake_index;
        ai.kick_roll = p.kick_roll;
        ai.backhand_roll = p.backhand_roll;
        std::copy_n(p.reaction, 15, ai.reaction);
        ai.reach = p.reach;
        ai.word_184 = p.word_184;
    };
    auto place_rider = [&](const BikeSpec& s, int32_t value, int slot, int32_t grid_pos, int32_t grid_lat) {
        Rider r;
        r.slot = slot;
        init_bike_object(r.bike, tables_);
        init_object(r.half_object, tables_.half_object);
        apply_spec(r.bike, s, value);
        r.bike.i32(0x18) = 1; // updated every tick
        // Table grid (0x00464088/0x00464108): position equals the selector;
        // lateral = road centre (Q8) + table lateral. grid_pos 0 keeps the
        // legacy two-row synthetic grid.
        const int32_t pos = grid_pos ? grid_pos : grid_position(slot);
        r.sample = r.previous_sample = size_t(pos >> 8);
        r.bike.i32(0x1c) = pos;
        r.bike.i32(0x138) = pos;
        // 0x00405990: bike +0x324 is the rider type (0 player, 1 opponent,
        // 2 cop; the cop is set in police.cpp). Native left all at 0;
        // set it here so the 0x0043a580/0x004060b0 walls gates see opponents
        // as opponents (+0x328 stays 0 for all for now, so the branch check
        // still always takes the walls path; +0x328 mapping via DAT_004c4fb0
        // stays open, plan-17).
        r.bike.i32(0x324) = (slot == 0 ? 0 : 1);
        // 0x00405990: lateral = sub-slice +0xcc (road centre, Q8) + grid offset.
        // RouteSample::offset is world units (edges/AI scale it by 256);
        // road_base_ anchors the relative route to the original absolute
        // frame (ctor param); the default 0 keeps synthetic routes exact.
        const int32_t lat = grid_pos ? grid_lat : grid_lateral[slot % 4];
        r.bike.i32(0x28) = (*routes_[0])[r.sample].offset * 256 + lat + road_base_;
        // 0x00405990 passes the same grid offset to 0x00407990 as the
        // brain's lane bias (+0x190), which 0x004091f0 adds to its target.
        r.brain.bias = lat;
        r.bike.i32(0x2f0) = r.bike.i32(0x2ec); // bike condition (0x00405c80)
        // 0x00438930: the build copies the five placed words 0x1c..0x2c to
        // the previous-position copies 0x30..0x40. move() re-syncs them every
        // tick, so this matters for tick 1 and for frozen riders, which never
        // move (their 0x3c feeds the 0x00436a40 lateral gate).
        for (int k = 0; k < 5; ++k) r.bike.i32(0x30 + 4 * k) = r.bike.i32(0x1c + 4 * k);
        return r;
    };
    Rider player = place_rider(spec, rider_value, 0, player_grid_pos, player_grid_lat);
    if (!personalities.empty()) brain_from(player.brain, personalities[0]);
    player.human = 0;
    riders_.push_back(player);
    for (const auto& o : opponents) {
        if (o.slot < 1 || o.slot > 15) throw std::runtime_error("Opponent grid slot must be 1..15");
        Rider r = place_rider(o.spec, o.rider_value, o.slot, o.grid_pos, o.grid_lat);
        r.ai = o.human < 0;
        r.human = o.human;
        r.rider_idx = o.rider;
        r.frozen = o.frozen;
        r.colour = o.slot; // 0x00446190 with player bike 0; see set_player_bike
        if (o.human >= 0) {
            // Plan 34: another human seat, built like the player (0x00405990
            // player tables, no AI scaling); 0x004317a0 puts seats on grid
            // rows by slot.
            r.bike.i32(0x324) = 0;
            if (size_t(o.human) >= human_riders_.size()) human_riders_.resize(size_t(o.human) + 1, -1);
            human_riders_[size_t(o.human)] = int(riders_.size());
        } else {
            // 0x00405990 for AI riders: level throttle scale and softer low gears.
            r.bike.i32(0x260) = (o.throttle_percent * r.bike.i32(0x260)) / 100;
            r.bike.i32(0x1d0) -= 0x19;
            r.bike.i32(0x1e0) -= 0xf;
            r.bike.i32(0x1f0) -= 8;
            r.bike.i32(0x200) -= 4;
        }
        r.brain.base_speed = r.brain.current_speed = o.base_speed;
        r.brain.current2 = o.cur2_speed;
        // Synthetic-test fallback: the traced replan resets speeds from the
        // per-level tables (spd_a/spd_b), which only read_personalities fills.
        // Without them the reset would zero the speed and stall the field.
        for (int i = 0; i < 5; ++i) {
            r.brain.spd_a[i] = o.cur2_speed;
            r.brain.spd_b[i] = o.base_speed;
        }
        // 0x00405990 draws the lane as rand()%3 at build; -1 requests the draw.
        r.brain.lane = o.lane >= 0 ? o.lane : int(random() % 3);
        if (!personalities.empty() && o.rider >= 0) {
            brain_from(r.brain, personalities[o.rider < 9 ? size_t(o.rider) : 0]);
            if (o.rider >= 9) r.brain.overtake_index = o.rider + 1; // 0x00407990: +0x165 = slot + 1
        }
        r.stamina = o.stats;
        r.weapon = o.weapon;
        riders_.push_back(r);
    }
    if (std::find(human_riders_.begin(), human_riders_.end(), -1) != human_riders_.end())
        throw std::runtime_error("Human seats must be 0..n-1 without gaps");
    riders_.reserve(riders_.size() + 1); // the cop is appended mid-race; references into riders_ stay valid
    // Nitro setup (0x0040b0e0): copy the six words from the player spec
    // at +0x14c when the spec carries them, else keep EXE defaults; the
    // accumulator starts at maximum<<8. Synthetic specs are zero there, so
    // the zero-guard keeps tests on the documented defaults.
    {
        bool spec_nitro = false;
        for (int k = 0; k < 6; ++k)
            if (spec.d[size_t(83 + k)] != 0) spec_nitro = true; // +0x14c = word 83
        if (spec_nitro) {
            nitro_.recharge = spec.d[83];
            nitro_.boost = spec.d[84];
            nitro_.max = spec.d[85];
            nitro_.charges = spec.d[86];
            nitro_.minimum = spec.d[87];
            nitro_.tail = spec.d[88];
        }
        nitro_charges_ = nitro_.charges;
        for (auto& rider : riders_) rider.nitro_charge = nitro_.max << 8;
    }
    // The halves mirror their bikes from birth (0x00405990 builds the pair
    // together): the camera reads the half image from the first tick.
    for (auto& r : riders_) {
        const Bike& b = r.bike;
        r.half_mirror = {b.i32(0x1c),  b.i32(0x28),  b.i32(0x20), b.i32(0xf4), b.i32(0xec), b.i32(0xf0),
                         b.i32(0x10c), b.i32(0x108), b.i32(0x60), r.sample,    r.route};
    }
    update_camera();
    previous_camera_position_ = camera_position();
}

void Race::set_crash_library(const ActorAnimationLibrary& library, const CrashConfig& config) {
    crash_library_ = &library;
    crash_config_ = config;
    for (auto& rider : riders_) init_crash_actor(rider.crash_actor, config);
}

RaceWiring Race::wiring() const {
    RaceWiring w;
    w.terrain = bool(terrain_source_);
    w.traffic = traffic_ != nullptr;
    w.pedestrians = pedestrians_ != nullptr;
    w.ring = ring_ != nullptr;
    w.crash_library = crash_library_ != nullptr;
    w.ride_library = ride_library_ && attack_codes_;
    w.nitro = nitro_set_;
    w.finish = finish_q8_ != 0;
    w.personalities = overtake_eagerness_.has_value();
    return w;
}

void Race::set_traffic(TrafficSystem* t) {
    traffic_ = t;
    attach_terrain_type();
}
void Race::set_spawn_enabled(bool v) {
    if (traffic_) traffic_->set_spawn_enabled(v);
}
void Race::set_demo_hold(bool v, int base_speed, int lane, int32_t bias, int weapon, RiderStats stats) {
    demo_hold_ = v;
    if (v && !riders_.empty()) {
        // Seed the player AI brain like an opponent build: replan starts at
        // zero (first reset 60 ticks after green) with base/current from the
        // rider's personality row. The demo rider's own value/weapon/stats
        // come from its rider-table row (0x00405990/0x00405c80).
        riders_[0].brain.base_speed = riders_[0].brain.current_speed = base_speed;
        // Scalar-only demo fixtures have no imported personality speed tables.
        // Match the opponent fallback; otherwise the first replan resets the
        // demo player's speed to zero. Imported tables remain authoritative.
        auto& brain = riders_[0].brain;
        if (std::all_of(brain.spd_b, brain.spd_b + 5, [](int32_t v) { return v == 0; })) {
            std::fill(brain.spd_b, brain.spd_b + 5, base_speed);
            std::fill(brain.spd_a, brain.spd_a + 5, base_speed);
        }
        riders_[0].brain.lane = lane;
        riders_[0].brain.bias = bias;
        riders_[0].stamina = stats;
        riders_[0].weapon = weapon;
    }
}

SliceEdges Race::edges(const Rider& r, size_t i) const {
    const auto& s = route_of(r)[i];
    // Imported terrain is attached by the game; standalone physics fixtures keep their original synthetic sides.
    return {s.offset - s.path.left,
            s.offset + s.path.right,
            s.offset - s.path.left - s.path.shoulder_left,
            s.offset + s.path.right + s.path.shoulder_right,
            terrain_source_ ? s.path.marking_flags : uint8_t(0),
            terrain_source_ ? ground(s).type : uint8_t(0)};
}

void Race::accumulate(Rider& r, uint32_t from, uint32_t to, size_t from_sample, size_t to_sample) {
    // 0x004394c0: bend and rise swept over the move, weighted by the Q8 distance in
    // each sample, and the heading word swept by the sub-slice curvature.
    const auto& route = route_of(r);
    Bike& b = r.bike;
    const int32_t bend_now = route[to_sample].path.bend, rise_now = route[to_sample].path.rise;
    // +0xb4 = atan(bend) << 14 (route build 0x00454240). 0x128 loses its low 12 bits
    // to the heading branch (0x00439920) and is scaled by half the swept distance;
    // bikes overwrite it again in 0x004395e0, so only route-following bodies (crash
    // halves, traffic, peds) keep the accumulated value.
    const auto curvature = [&](int32_t bend) { return (atan_ratio(tables_, bend) * (1 << 14)) >> 8; };
    if (((to ^ from) & 0xffffff00u) == 0) {
        const int32_t d = int32_t(to - from);
        b.i32(0x128) -= (curvature(bend_now) * d) / 2;
        b.i32(0x130) = -(bend_now * d);
        b.i32(0x12c) = rise_now * d;
        return;
    }
    int32_t in_new = int32_t(to & 0xff), in_old;
    if (int32_t(to) < int32_t(from)) {
        in_old = -int32_t(from & 0xff);
        in_new -= 0x100;
    } else
        in_old = 0x100 - int32_t(from & 0xff);
    const int32_t bend_old = route[from_sample].path.bend, rise_old = route[from_sample].path.rise;
    b.i32(0x128) -= (curvature(bend_old) * in_old + curvature(bend_now) * in_new) / 2;
    b.i32(0x130) = -(bend_now * in_new + bend_old * in_old);
    b.i32(0x12c) = rise_now * in_new + rise_old * in_old;
}

void Race::cross_sample(Rider& r, int32_t delta) {
    // 0x00439390 + 0x004394c0 for one step of at most one sample.
    Bike& b = r.bike;
    const uint32_t from = uint32_t(b.i32(0x1c));
    uint32_t to = from + uint32_t(delta);
    const size_t last = route_of(r).size() - 2;
    if (int32_t(to) < 0) to = 0;
    // 0x00406890: finish when the nonzero global DAT_004b6e58 is reached
    // (with the +0x2c scheduler gate and the +0xc0 bit-0x10 latch moving to
    // 0x00437800). finish_q8_ carries DAT_004b6e58; 0 keeps route-end
    // semantics for synthetic routes. +0x2c is nonzero for every inserted
    // scheduler actor (bike.cpp scheduler insert), so the gate passes here.
    if (finish_q8_ != 0 && (b.u8(0xc0) & 0x10) == 0 && int32_t(to) >= finish_q8_) {
        const size_t start_sample = r.sample;
        to = uint32_t(finish_q8_);
        b.i32(0x134) = int32_t(from);
        b.i32(0x1c) = int32_t(to);
        b.i32(0x138) = int32_t(to);
        r.previous_sample = start_sample;
        r.sample = size_t(to >> 8);
        accumulate(r, from, to, start_sample, r.sample);
        if (!r.finished && !r.cop) {
            r.finished = true;
            r.finish_tick = ticks_;
        }
        b.u8(0xc0) = uint8_t(b.u8(0xc0) | 0x10); // 0x00437800 rider+bike latch
        return;
    }
    if ((to >> 8) > last) {
        to = uint32_t(last << 8);
        if (!r.finished && !r.cop) {
            r.finished = true;
            r.finish_tick = ticks_;
        }
        b.u8(0xc0) = uint8_t(b.u8(0xc0) | 0x10); // 0x00437800 latch on the legacy path too
    }
    b.i32(0x134) = int32_t(from);
    b.i32(0x1c) = int32_t(to);
    b.i32(0x138) = int32_t(to);
    const size_t from_sample = r.sample;
    if (((to ^ from) & 0xffffff00u) != 0) {
        r.previous_sample = r.sample;
        r.sample = to >> 8;
        // Entering a split: the side of half 0's right edge decides the branch (0x00439390).
        const auto& a = (*routes_[0])[r.sample];
        if (r.route == 0 && int32_t(to) > int32_t(from) && a.event == RoadEvent::split) {
            const int32_t half0_right = a.offset + a.path.right;
            if ((b.i32(0x28) >> 8) >= half0_right) r.route = 1;
        }
    }
    accumulate(r, from, to, from_sample, r.sample);
}

void Race::move_along(Rider& r, int32_t distance, bool sweep) {
    // 0x00439270: split long moves into whole-sample steps and sum the swept bend/rise.
    // sweep=false is the third-argument-0 form used by collisions: position only.
    Bike& b = r.bike;
    if (distance == 0) {
        if (sweep) {
            b.i32(0x130) = 0;
            b.i32(0x12c) = 0;
        }
        return;
    }
    const int32_t saved_bend = b.i32(0x130), saved_rise = b.i32(0x12c), saved_heading = b.i32(0x128);
    const int32_t pos = b.i32(0x1c);
    int32_t steps = ((pos + distance) >> 8) - (pos >> 8);
    int32_t unit = 0x100;
    if (steps < 0) {
        steps = -steps;
        unit = -0x100;
    }
    if (steps < 2) {
        cross_sample(r, distance);
        if (!sweep) {
            b.i32(0x130) = saved_bend;
            b.i32(0x12c) = saved_rise;
            b.i32(0x128) = saved_heading;
        }
        return;
    }
    const size_t start_sample = r.sample;
    const int32_t magnitude = std::abs(distance);
    int32_t first = magnitude & 0xff;
    if (unit < 0) first = -first;
    cross_sample(r, first);
    int32_t bend = b.i32(0x130), rise = b.i32(0x12c);
    for (int32_t n = magnitude >> 8; n > 0 && !r.finished; --n) {
        cross_sample(r, unit);
        bend += b.i32(0x130);
        rise += b.i32(0x12c);
    }
    b.i32(0x134) = pos;
    r.previous_sample = start_sample;
    b.i32(0x130) = sweep ? bend : saved_bend;
    b.i32(0x12c) = sweep ? rise : saved_rise;
    if (!sweep) b.i32(0x128) = saved_heading; // 0x00439270 skips 0x004394c0 entirely
}

// 32-bit imul wraparound: 0x00436a40 multiplies depths and speeds by the
// partner mass, which is 0x1000000 for the 0x00437660 static object.
static int32_t wrap_mul(int32_t a, int32_t b) {
    return int32_t(uint32_t(a) * uint32_t(b));
}

void Race::separate(Rider& r1, uint32_t t1, Rider& r2, uint32_t t2) {
    // 0x00436a40, line for line. p1 is the walk's outer node, p2 its partner;
    // t1/t2 are their object types (+0xc4). 0x00439270 moves along the road.
    Bike& p1 = r1.bike;
    Bike& p2 = r2.bike;
    const int32_t m1 = p1.i32(0x194), m2 = p2.i32(0x194);
    p2.i32(400) = m1;
    p1.i32(400) = m2;
    const int32_t m = m1 + m2;
    int32_t rel = p1.i32(0x108) - p2.i32(0x108);
    rel = std::abs((rel >> 9) + (rel >> 12) + (rel >> 7));
    p1.u8(0x19c) = 0;
    p2.u8(0x19c) = 0;
    auto stack = [&](bool& raise1,
                     bool& any) { // height stacking when one object is under half the other's height
        if (p1.i32(0x17c) < p2.i32(0x17c) / 2 && p1.i32(0x20) == 0) {
            raise1 = false;
            any = true;
        } else if (p2.i32(0x17c) < p1.i32(0x17c) / 2 && p2.i32(0x20) == 0) {
            raise1 = true;
            any = true;
        } else {
            raise1 = false;
            any = false;
        }
    };
    if (p2.i32(0x178) + p1.i32(0x178) < std::abs(p1.i32(0x30) - p2.i32(0x30))) {
        bool raise1 = false, any = false;
        stack(raise1, any);
        int32_t x1 = p1.i32(0x28), h1 = p1.i32(0x174) / 2;
        if (p2.i32(0x28) - p2.i32(0x174) < x1 - h1 && h1 + x1 < p2.i32(0x28) + p2.i32(0x174) &&
            0x2000 < p2.i32(0x17c))
            p1.u8(0x19c) |= 0x40;
        const int32_t h2 = p2.i32(0x174) / 2;
        if (x1 - p1.i32(0x174) < p2.i32(0x28) - h2 && h2 + p2.i32(0x28) < x1 + p1.i32(0x174) &&
            0x2000 < p1.i32(0x17c))
            p2.u8(0x19c) |= 0x40;
        const int32_t depth = (p2.i32(0x178) - std::abs(p1.i32(0x1c) - p2.i32(0x1c))) + 8 + p1.i32(0x178);
        if (any) {
            const int32_t up =
                (p2.i32(0x17c) - std::abs(p1.i32(0x20) + ((p1.i32(0x17c) - p2.i32(0x20)) - p2.i32(0x17c)))) +
                0x800 + p1.i32(0x17c);
            if (raise1)
                p1.i32(0x20) = up + p1.i32(0x20) + 3;
            else
                p2.i32(0x20) = up + p2.i32(0x20) + 3;
        }
        if (p2.i32(0x30) < p1.i32(0x30)) {
            p1.u8(0x19c) |= 1;
            p2.u8(0x19c) |= 2;
            if (!any) {
                move_along(r1, wrap_mul(depth, m2) / m + 3, false);
                if (t1 != 4 || rel < 0x14) move_along(r2, -(wrap_mul(depth, m1) / m) - 3, false);
            }
        } else {
            p1.u8(0x19c) |= 2;
            p2.u8(0x19c) |= 1;
            if (!any) {
                if (t1 != 4 || rel < 0x14) move_along(r1, -(wrap_mul(depth, m2) / m) - 3, false);
                move_along(r2, wrap_mul(depth, m1) / m + 3, false);
            }
        }
        const int32_t imp = t2 == 1 ? (p2.i32(0x108) - p1.i32(0x108)) * ((m2 * 2) / m)
                                    : wrap_mul(wrap_mul(p2.i32(0x108) - p1.i32(0x108), m2), 2) / m;
        p1.i32(0x184) = imp;
        p2.i32(0x184) = -imp;
    } else {
        p1.i32(0x184) = 0;
        p2.i32(0x184) = 0;
    }
    if (p1.i32(0x174) + p2.i32(0x174) < std::abs(p1.i32(0x3c) - p2.i32(0x3c))) {
        const int32_t depth = (p2.i32(0x174) - std::abs(p1.i32(0x28) - p2.i32(0x28))) + 0x800 + p1.i32(0x174);
        if (p2.i32(0x3c) < p1.i32(0x3c)) {
            p1.u8(0x19c) |= 4;
            p2.u8(0x19c) |= 8;
            p1.i32(0x28) += wrap_mul(depth, m2) / m + 3;
            p2.i32(0x28) -= wrap_mul(depth, m1) / m + 3;
        } else {
            p1.u8(0x19c) |= 8;
            p2.u8(0x19c) |= 4;
            p1.i32(0x28) -= wrap_mul(depth, m2) / m + 3;
            p2.i32(0x28) += wrap_mul(depth, m1) / m + 3;
        }
        const int32_t imp = t2 == 1 ? (p2.i32(0x10c) - p1.i32(0x10c)) * ((m2 * 2) / m)
                                    : wrap_mul(wrap_mul(p2.i32(0x10c) - p1.i32(0x10c), m2), 2) / m;
        p1.i32(0x188) = imp;
        p2.i32(0x188) = -imp;
    } else {
        p1.i32(0x188) = 0;
        p2.i32(0x188) = 0;
    }
    {
        const int32_t a = p1.i32(0x17c), b = p2.i32(0x17c);
        int32_t imp = 0;
        if (a + b < std::abs(p1.i32(0x34) + ((a - p2.i32(0x34)) - b))) {
            const int32_t depth = (b - std::abs(((p1.i32(0x20) - p2.i32(0x20)) - b) + a)) + 0x800 + a;
            if (p2.i32(0x34) < p1.i32(0x34)) {
                p1.u8(0x19c) |= 0x20;
                p2.u8(0x19c) = uint8_t((p2.u8(0x19c) & 0xbf) | 0x10);
                p1.i32(0x20) += wrap_mul(depth, m2) / m + 3;
                p2.i32(0x20) = (p2.i32(0x20) - wrap_mul(depth, m1) / m) - 3;
                if (p2.i32(0x20) < 0) {
                    p1.i32(0x20) -= p2.i32(0x20);
                    p2.i32(0x20) = 0;
                }
            } else {
                p1.u8(0x19c) = uint8_t((p1.u8(0x19c) & 0xbf) | 0x10);
                p2.u8(0x19c) |= 0x20;
                p1.i32(0x20) -= wrap_mul(depth, m2) / m + 3;
                p2.i32(0x20) = p2.i32(0x20) + 3 + wrap_mul(depth, m1) / m;
                if (p1.i32(0x20) < 0) {
                    p2.i32(0x20) -= p1.i32(0x20);
                    p1.i32(0x20) = 0;
                }
            }
            imp = t2 == 1 ? (p2.i32(0x110) - p1.i32(0x110)) * ((m2 * 2) / m)
                          : wrap_mul(wrap_mul(p2.i32(0x110) - p1.i32(0x110), m2), 2) / m;
            p1.i32(0x18c) = imp;
            imp = -imp;
        } else {
            p1.i32(0x18c) = 0;
        }
        p2.i32(0x18c) = imp;
    }
    if (p1.u8(0x19c) == 0) {
        // Overlapping on every axis already.
        const int32_t b17c = p2.i32(0x17c);
        bool any = false, raise1 = false;
        if (p1.i32(0x17c) < b17c / 2 && p1.i32(0x20) == 0) {
            any = true;
            raise1 = false;
        } else if (b17c < p1.i32(0x17c) / 2 && p2.i32(0x20) == 0) {
            any = true;
            raise1 = true;
        }
        if (any) {
            const int32_t up = (b17c - std::abs(p1.i32(0x20) + ((p1.i32(0x17c) - p2.i32(0x20)) - b17c))) +
                               0x800 + p1.i32(0x17c);
            if (raise1)
                p1.i32(0x20) = up + p1.i32(0x20) + 3;
            else
                p2.i32(0x20) = up + p2.i32(0x20) + 3;
        } else {
            const int32_t fwd = t2 == 1 ? (p2.i32(0x108) - p1.i32(0x108)) * ((m2 * 2) / m)
                                        : wrap_mul(wrap_mul(p2.i32(0x108) - p1.i32(0x108), m2), 2) / m;
            p1.i32(0x184) = fwd;
            p2.i32(0x184) = -fwd;
            const int32_t lat = t2 == 1 ? (p2.i32(0x10c) - p1.i32(0x10c)) * ((m2 * 2) / m)
                                        : wrap_mul(wrap_mul(p2.i32(0x10c) - p1.i32(0x10c), m2), 2) / m;
            p1.i32(0x188) = lat;
            p2.i32(0x188) = -lat;
            if (std::abs(div_pow2(fwd, 2)) < std::abs(lat) || p1.i32(0x10c) <= p1.i32(0x108)) {
                const int32_t x1 = p1.i32(0x28);
                const int32_t depth = (p2.i32(0x174) - std::abs(x1 - p2.i32(0x28))) + 0x800 + p1.i32(0x174);
                if (t1 != 4) { // bikes are never pushed sideways here
                    if (lat < 0) {
                        p1.u8(0x19c) |= 8;
                        p1.i32(0x28) = (x1 - wrap_mul(depth, m2) / m) - 3;
                    } else {
                        p1.u8(0x19c) |= 4;
                        p1.i32(0x28) = wrap_mul(depth, m2) / m + x1 + 3;
                    }
                }
                if (t2 != 4) {
                    if (-lat < 1) {
                        p2.u8(0x19c) |= 8;
                        p2.i32(0x28) -= wrap_mul(depth, m1) / m + 3;
                    } else {
                        p2.u8(0x19c) |= 4;
                        p2.i32(0x28) += wrap_mul(depth, m1) / m + 3;
                    }
                }
            } else {
                const int32_t depth =
                    (p2.i32(0x178) - std::abs(p1.i32(0x1c) - p2.i32(0x1c))) + 8 + p1.i32(0x178);
                if (fwd < 0) {
                    p1.u8(0x19c) |= 2;
                    if (t1 != 4 || rel < 0x14) move_along(r1, -(wrap_mul(depth, m2) / m) - 3, false);
                } else {
                    p1.u8(0x19c) |= 1;
                    move_along(r1, wrap_mul(depth, m2) / m + 3, false);
                }
                if (-fwd < 1) {
                    p2.u8(0x19c) |= 2;
                    if (t2 != 4 || rel < 0x14) move_along(r2, -(wrap_mul(depth, m1) / m) - 3, false);
                } else {
                    p2.u8(0x19c) |= 1;
                    move_along(r2, wrap_mul(depth, m1) / m + 3, false);
                }
            }
        }
    }
    for (int k = 0; k < 5; ++k) {
        p1.i32(0x30 + 4 * k) = p1.i32(0x1c + 4 * k);
        p2.i32(0x30 + 4 * k) = p2.i32(0x1c + 4 * k);
    }
}

void Race::collide(Rider& ra, Rider& rb) {
    // 0x004368f0 pair of bikes (+0xc4 == 4): 0x00436a40, then each handler.
    separate(ra, 4, rb, 4);
    contact(ra, rb);
    contact(rb, ra);
}
// Rider-against-car pass: 0x004368f0 pair loop over the scheduler object list
// (cars are +0xc4 == 0x10 objects), 0x00436a40 separation, 0x004049c0 bike
// side for cars. Car dims via 0x004387a0 from template 0x00475698:
// +0x174 = 16384, +0x178 = 64, +0x17c = 8960 (see analysis/car-notes.md).
// The original runs this inside 0x004368f0 right after the rider-pair loop;
// the native call sits at the same point in Race::step (after collide pairs,
// before the camera update), for every rider, player and AI.
void Race::collide_traffic(Rider& r) {
    if (!traffic_ || r.finished) return;
    if (live_schedule_ && (r.bike.u8(0xc0) & 5) != 5) return; // parked: not a contact candidate
    // No crash gate here: 0x004368f0 checks only tick/flags/geometry, not
    // +0x2f4, so separation (0x00436a40) runs for crashed riders too, and
    // 0x004049c0 handles them (vertical +0xf4 bump, steer tail) without a
    // re-knock (crash_entry guards on crash_active; finish recompute in
    // contact_car is gated on uncrashed). Skipping crashed misses the
    // riderless-bike shove (e.g. L5d tick 2278: car 13e2038 vs crashed
    // rider 9 bike, lateral +12032 / steer -2).
    constexpr int32_t car_half_length = 64;   // 0x00475698 +0x48
    constexpr int32_t car_half_height = 8960; // 0x00475698 +0x4c
    Bike& s = r.bike;
    const int32_t half_length = s.i32(0x178), half_width = s.i32(0x174), half_height = s.i32(0x17c);
    const auto& route = route_of(r);
    for (auto& car : traffic_->cars_mut()) {
        if (!car.active || !car.collides) continue;
        const int32_t position = s.i32(0x1c), lateral = s.i32(0x28), height = s.i32(0x20);
        const auto& sample = route[r.sample];
        const int32_t car_half_width = TrafficSystem::car_half_width(car); // 0x00475700 + kind * 0x18
        // 0x004368f0 walks ascending position: only the outer (behind)
        // node must have run this tick. A non-due car behind a due rider
        // cannot contact it by scanning backward through the list.
        if (live_schedule_) {
            const bool rider_outer = position <= car.position;
            const bool due = rider_outer ? (r.sched_last == ticks_ && (s.u8(0xc0) & 6) == 6)
                                         : (car.last_tick == ticks_ && (car.motion.u8(0xc0) & 6) == 6);
            if (!due) continue;
        }
        // Split-roadway exclusion (0x004368f0 +0x36 & 3 shared-half test).
        if (sample.dual) {
            // Cars run on the player's route; riders on the other branch pass by.
            if (r.route != riders_[0].route) continue;
        }
        if (std::abs(position - car.position) > half_length + car_half_length) continue;
        if (std::abs(lateral - car.lateral) > half_width + car_half_width) continue;
        if (std::abs(height) > half_height + car_half_height) continue;
        if (live_schedule_) {
            // Cars now retain the original mover image, so use both objects'
            // previous positions/velocities and the common 0x00436a40 law.
            Rider other;
            other.bike = car.motion;
            other.route = riders_[0].route;
            other.sample = other.previous_sample = size_t(car.position >> 8);
            Bike& q = other.bike;
            if (q.i32(0xc4) != 0x10) {
                q.i32(0xc4) = 0x10;
                q.u8(0xc0) = 7;
                for (int k = 0; k < 5; ++k)
                    q.i32(0x30 + 4 * k) = k == 0 ? car.position : k == 3 ? car.lateral : 0;
                q.i32(0x108) = car.kind == 2 ? -car.speed : car.speed;
            }
            q.i32(0x1c) = car.position;
            q.i32(0x28) = car.lateral;
            q.i32(0xec) = car.speed;
            q.i32(0x174) = car_half_width;
            q.i32(0x178) = car_half_length;
            q.i32(0x17c) = car_half_height;
            q.i32(0x194) = 1800;
            const bool rider_outer = position <= car.position;
            if (rider_outer)
                separate(r, 4, other, 0x10);
            else
                separate(other, 0x10, r, 4);
            car.motion = q;
            car.position = q.i32(0x1c);
            car.lateral = q.i32(0x28);
            car.speed = q.i32(0xec);
            // 0x0044efa0: the second object's contact callback follows
            // the first on the ascending pair walk, including the horn draw.
            auto car_response = [&] {
                if (human(r) && random() % 10 == 1 && car.kind != 0) sound(SoundEvent::horn, car.motion);
                car.motion.u8(0x1f4) = 1;
                if ((car.kind & 1) && car.motion.i32(0x1f8) == 0)
                    car.motion.i32(0x1f8) = 0xf0; // partner is a bike (type &0xc)
            };
            if (!rider_outer) car_response();
            contact_car(r, car);
            if (rider_outer) car_response();
            if (s.i32(0x2f4) != 0) return;
            continue;
        }
        // Legacy unscheduled fixture path. Live races use the object image above.
        // Separate along the previously-clear axis (0x00436a40, mass-weighted;
        // car mass 1800 >> bike 256, so the rider takes nearly all of it).
        // The longitudinal share needs course-ring traversal (0x00439270);
        // lateral separation is direct. Previous-position copies stay in sync.
        const bool was_clear_forward = car_half_length + half_length < std::abs(s.i32(0x30) - car.position);
        const bool was_clear_lateral = car_half_width + half_width < std::abs(s.i32(0x3c) - car.lateral);
        constexpr int32_t bike_mass = 256, car_mass = 1800;
        constexpr int32_t total = bike_mass + car_mass;
        // The car keeps no previous-position copy (no TrafficCar member);
        // the axis-choice compares against its current position.
        if (was_clear_forward) {
            const int32_t depth = (car_half_length + half_length - std::abs(position - car.position)) + 8;
            const int32_t push = (depth * car_mass) / total + 3;
            if (car.position < s.i32(0x30))
                move_along(r, push, false);
            else if (r.bike.i32(0x108) - car.speed < 0x14)
                move_along(r, -push, false);
            // Car +0x108 (speed copy) has no TrafficCar member; car.speed
            // (+0xec) is the same value on a settled car (move() writes speed
            // directly, never staging through +0x108).
            s.i32(0x184) = ((car.speed - s.i32(0x108)) * car_mass * 2) / total;
        } else {
            s.i32(0x184) = 0;
        }
        if (was_clear_lateral) {
            const int32_t depth = (car_half_width + half_width - std::abs(lateral - car.lateral)) + 0x800;
            const int32_t push = (depth * car_mass) / total + 3;
            if (car.lateral < s.i32(0x3c)) {
                s.i32(0x28) += push;
                car.lateral -= (depth * bike_mass) / total + 3;
            } else {
                s.i32(0x28) -= push;
                car.lateral += (depth * bike_mass) / total + 3;
            }
            s.i32(0x188) = ((0 - s.i32(0x10c)) * car_mass * 2) / total;
        } else {
            s.i32(0x188) = 0;
        }
        s.i32(0x18c) = 0;
        // Contact flags 0x00436a40: bits 1/2 forward, 4/8 lateral (rider is
        // param_1). The car's own flags live on the scheduler object, which
        // has no native handle — only the rider side is observable.
        s.u8(0x19c) = uint8_t((was_clear_forward ? (car.position < s.i32(0x30) ? 1 : 2) : 0) |
                              (was_clear_lateral ? (car.lateral < s.i32(0x3c) ? 4 : 8) : 0));
        // Containment bit 0x40 (0x00436a40 prologue): the car's width contains
        // the rider when the rider's half-extent sits inside the car's span.
        if (car.lateral - car_half_width < lateral - half_width / 2 &&
            lateral + half_width / 2 < car.lateral + car_half_width)
            s.u8(0x19c) = uint8_t(s.u8(0x19c) | 0x40);
        for (int k = 0; k < 5; ++k) s.i32(0x30 + 4 * k) = s.i32(0x1c + 4 * k);
        contact_car(r, car);
        if (s.i32(0x2f4) != 0) return; // knocked off: stop scanning
    }
}
// Cars as outer nodes of 0x004368f0: a due car (+0x10 == tick, flags & 6)
// within its half-length (+0x178 = 64) of mid-slice sweeps its own slice's
// statics through 0x004373f0 like a rider (traffic-field slice capture tick
// 3834: crossing car 13e2038 takes the static's separation, +3 position,
// speed 1461 -> 1392). Runs before the rider-car pairs; both follow the
// ascending walk only approximately (cars are not in the native scan list).
void Race::collide_car_statics() {
    if (!traffic_ || !live_schedule_) return;
    constexpr int32_t car_half_length = 64;   // 0x00475698 +0x48
    constexpr int32_t car_half_height = 8960; // 0x00475698 +0x4c
    for (auto& car : traffic_->cars_mut()) {
        if (!car.active || !car.collides) continue;
        if (car.last_tick != ticks_ || (car.motion.u8(0xc0) & 6) != 6) continue;
        if (std::abs(int32_t(uint32_t(car.position) & 0xff) - 0x80) >= car_half_length) continue;
        Rider other;
        other.bike = car.motion;
        other.route = riders_[0].route;
        other.sample = other.previous_sample = size_t(car.position >> 8);
        Bike& q = other.bike;
        q.i32(0x1c) = car.position;
        q.i32(0x28) = car.lateral;
        q.i32(0xec) = car.speed;
        q.i32(0x174) = TrafficSystem::car_half_width(car);
        q.i32(0x178) = car_half_length;
        q.i32(0x17c) = car_half_height;
        q.i32(0x194) = 1800;
        static_car_ = &car;
        const bool hit = sweep_statics(other);
        static_car_ = nullptr;
        if (!hit) continue;
        car.motion = q;
        car.position = q.i32(0x1c);
        car.lateral = q.i32(0x28);
        car.speed = q.i32(0xec);
    }
}
// Crashed rider half (+0xc4 == 8) against cars. Crash entry sets the half's
// flags to 7 (0x00403d60 `+0xc0 |= 3`), so 0x004368f0 pairs it with cars
// like any other node: both +0x1a0 partner words, 0x00436a40 separation,
// then each object's +0x1a4 callback in walk order: 0x0044efa0 for the car
// (its horn draw needs the partner to be the player bike, which a half
// never is) and 0x004046b0 for the half, whose randomized 0x00439270 move
// draws rand() (traffic-field L5 tick 1303: car 13e1e28 vs rider 7 half).
void Race::collide_traffic_half(Rider& owner) {
    if (!traffic_ || !live_schedule_ || owner.finished || owner.frozen || !owner.crash_active ||
        owner.half_frozen)
        return;
    constexpr int32_t car_half_length = 64;   // 0x00475698 +0x48
    constexpr int32_t car_half_height = 8960; // 0x00475698 +0x4c
    for (auto& car : traffic_->cars_mut()) {
        if (!car.active || !car.collides) continue;
        Bike& body = owner.crash_actor.motion;
        if ((body.u8(0xc0) & 5) != 5) return; // not a contact candidate (flags 7 after entry)
        const int32_t position = body.i32(0x1c), lateral = body.i32(0x28), height = body.i32(0x20);
        const int32_t car_half_width = TrafficSystem::car_half_width(car);
        const bool half_outer = position <= car.position;
        const bool due = half_outer ? (owner.half_sched_last == ticks_ && (body.u8(0xc0) & 6) == 6)
                                    : (car.last_tick == ticks_ && (car.motion.u8(0xc0) & 6) == 6);
        if (!due) continue;
        const auto& route = *routes_[owner.crash_actor.route];
        if (owner.crash_actor.sample < route.size() && route[owner.crash_actor.sample].dual &&
            owner.crash_actor.route != riders_[0].route)
            continue; // cars run on the player's route (split-roadway half test)
        if (std::abs(position - car.position) > body.i32(0x178) + car_half_length) continue;
        if (std::abs(lateral - car.lateral) > body.i32(0x174) + car_half_width) continue;
        // 0x004368f0 height: |(h1 - h2) - half2 + half1| <= half1 + half2.
        const int32_t dz = (height - 0) - car_half_height + body.i32(0x17c);
        if (std::abs(dz) > body.i32(0x17c) + car_half_height) continue;
        Rider other;
        other.bike = car.motion;
        other.route = riders_[0].route;
        other.sample = other.previous_sample = size_t(car.position >> 8);
        Bike& q = other.bike;
        if (q.i32(0xc4) != 0x10) {
            q.i32(0xc4) = 0x10;
            q.u8(0xc0) = 7;
            for (int k = 0; k < 5; ++k)
                q.i32(0x30 + 4 * k) = k == 0 ? car.position : k == 3 ? car.lateral : 0;
            q.i32(0x108) = car.kind == 2 ? -car.speed : car.speed;
        }
        q.i32(0x1c) = car.position;
        q.i32(0x28) = car.lateral;
        q.i32(0xec) = car.speed;
        q.i32(0x174) = car_half_width;
        q.i32(0x178) = car_half_length;
        q.i32(0x17c) = car_half_height;
        q.i32(0x194) = 1800;
        Rider detached;
        detached.bike = body;
        detached.route = owner.crash_actor.route;
        detached.sample = owner.crash_actor.sample;
        detached.previous_sample = owner.crash_actor.previous_sample;
        if (half_outer)
            separate(detached, 8, other, 0x10);
        else
            separate(other, 0x10, detached, 8);
        car.motion = q;
        car.position = q.i32(0x1c);
        car.lateral = q.i32(0x28);
        car.speed = q.i32(0xec);
        auto car_response = [&] {
            car.motion.u8(0x1f4) = 1;
            if ((car.kind & 1) && car.motion.i32(0x1f8) == 0)
                car.motion.i32(0x1f8) = 0xf0; // partner type 8 & 0xc
        };
        if (!half_outer) car_response();
        contact_half_static(detached, owner, other);
        if (half_outer) car_response();
        owner.crash_actor.motion = detached.bike;
        owner.crash_actor.route = detached.route;
        owner.crash_actor.sample = detached.sample;
        owner.crash_actor.previous_sample = detached.previous_sample;
        for (int off = 0x180; off <= 0x1a0; off += 4) owner.half_object.i32(off) = detached.bike.i32(off);
        owner.ride_animation = owner.crash_actor.animation;
        sync_half_mirror(owner);
        if (finish_q8_) owner.finish_distance = finish_q8_ - detached.bike.i32(0x1c);
    }
}
// BORR span builder (plan 16 Task 2): the 0x00451cb0 lateral-slot walk over
// a section's BORR profiles, keeping only slots strictly inside the road
// edges (0x00454240 strictly-inside test) at sample fraction 0x80. The walk
// (record/stride/spacing) mirrors the billboard builder so both consume
// random() in the same order; the hazard half-width uses the same 224-unit
// inset the walk places slots at.
std::vector<RoadHazard> road_hazard_spans(const CourseSection& section, uint32_t sample_base,
                                          std::function<uint32_t()> random) {
    std::vector<RoadHazard> result;
    if (section.roadside_profiles.empty()) return result;
    size_t record = 0;
    int32_t start = 0, end = 0;
    auto period = [](uint32_t word) { return (word & 0x0f000000) ? int32_t((word >> 12) & 15) : 0; };
    auto duration = [&](uint32_t word) { return (period(word) + 1) * int32_t((word >> 24) & 15); };
    end = duration(section.roadside_profiles[0]);
    for (size_t j = 0; j < section.samples.size(); ++j) {
        if (j && int64_t(j) > end) {
            if (record + 1 >= section.roadside_profiles.size()) break;
            ++record;
            start = int32_t(j);
            end = start + duration(section.roadside_profiles[record]);
        }
        const uint32_t word = section.roadside_profiles[record];
        const uint8_t selector = uint8_t(word >> 16);
        const int32_t stride = period(word) + 1;
        if ((selector & 63) == 63 || (int32_t(j) - start) % stride) continue;
        bool left = (word & 0x10000000) != 0;
        if ((word & 0x80000000) && ((int32_t(j) - start) & 1) == 0) left = !left;
        int32_t spacing;
        if (word & 0x0f000000)
            spacing = (word & 0x20000000) ? int32_t(((word & 15) + 4) * 256) : int32_t((word & 15) * 64);
        else {
            const int32_t amount = int32_t(((word & 0xf000) >> 8) + (word & 15));
            spacing = (word & 0x20000000) ? (amount + 32) * 32 : amount * 8;
        }
        const auto& path = section.samples[j];
        int32_t x = left ? -path.left + 224 - spacing : path.right - 224 + spacing;
        const int count = int((word >> 8) & 7) + 1;
        for (int object = 0; object < count; ++object) {
            if (-path.left < x && x < path.right)
                result.push_back({sample_base + uint32_t(j), (x - 224) * 256, (x + 224) * 256});
            const int32_t step = (word & 0x800)
                                     ? spacing / 2 + int32_t(random() % uint32_t(spacing - spacing / 2 + 1))
                                     : spacing;
            x += left ? -step : step;
        }
    }
    return result;
}
std::vector<DzhrSolid> dzhr_solids_for_sample(int32_t centre_q8, const std::vector<SceneryObject>& objects) {
    // 0x004520d0 packing, inherited from the identical place_dzhr filters:
    // non-actor records (bit 31 clear, i.e. static_family billboards) with
    // (word0>>8)&0x3f != 0x3f enabled, at most 2 per slice. Laterals are
    // world units relative to the route-frame road centre (the same frame
    // the rider lateral in Race::place_rider uses: route offset + road
    // base). All nibble types are kept; the tick rule branches on them.
    std::vector<DzhrSolid> result;
    for (const auto& o : objects) {
        if (!o.static_family) continue; // BORR roadside_objects: draw-only
        const int32_t x = centre_q8 + o.lateral * 256;
        result.push_back({x - 0x800, x + 0x800, uint8_t(o.type & 0xf)});
    }
    return result;
}
// In-road BORR hazard pass (plan 16 Task 2). 0x00454240 instantiates only
// BORR slots strictly inside the road edges (0x00438930, pool DAT_00476ce8,
// template 0x00476cb8 class 1); 0x004373f0 sweeps the rider box
// (lateral +/- +0x174 at sample fraction 0x80) against each slot and
// 0x00437660 builds the static hazard actor and runs the shared 0x00436a40
// separator plus the rider contact handler. Hazards are immovable: the rider
// takes the full lateral push. The contact branch is class 1
// (0x004049c0: other +0xc4 == 1, short: the halved template +0x17c fits in
// half the bike's; reason 1 through knock_off, small-gap speed transfer,
// big-gap crash entry reason 1). No RNG, no allocation in the tick path.
void Race::collide_hazards(Rider& r) {
    if (r.finished) return;
    if (!statics_[r.route].empty()) {
        // 0x004373f0 sweep + 0x00437660 static: the original runs this for
        // crashed riders too (split-left 2056: the crashed bike takes the
        // +0x150 bump, nudge and kick via contact_static's crashed path).
        // The hazards_/dzhr_ tables below reject crashed (contact_hazard).
        sweep_statics(r);
        return;
    } // live ring slices
    if (r.bike.i32(0x2f4) != 0) return; // tables reject crashed; see contact_hazard
    Bike& s = r.bike;
    const auto& table = *hazards_[r.route];
    if (!table.empty() && r.sample < table.size())
        for (const auto& h : table[r.sample]) {
            const int32_t half = s.i32(0x174);
            if (s.i32(0x28) + half <= h.lo_q8 || h.hi_q8 <= s.i32(0x28) - half) continue;
            contact_hazard(r, h.lo_q8, h.hi_q8);
            if (s.i32(0x2f4) != 0) return; // knocked off: stop scanning
        }
    // DZHR solid billboards (0x00437750, same sweep point): low-nibble types
    // 0/1/2 build the static type-1 actor and take the class-1 crash path
    // above; types 3+ take the 0x004049c0 == 2 marking branch, which only
    // records on the (unported, nonexistent natively) static actor and never
    // crashes the rider, so they are skipped.
    const auto& dtable = *dzhr_[r.route];
    if (r.sample < dtable.size())
        for (const auto& o : dtable[r.sample]) {
            if ((o.type & 0xf) >= 3) continue;
            const int32_t half = s.i32(0x174);
            if (s.i32(0x28) + half <= o.lo_q8 || o.hi_q8 <= s.i32(0x28) - half) continue;
            contact_hazard(r, o.lo_q8, o.hi_q8);
            if (s.i32(0x2f4) != 0) return; // knocked off: stop scanning
        }
}
void Race::set_ground_layout(const std::vector<uint32_t>& sizes) {
    std::vector<uint32_t> base;
    uint32_t total = 0;
    for (uint32_t n : sizes) {
        base.push_back(total);
        total += n;
    }
    ground_base_ = std::make_shared<const std::vector<uint32_t>>(std::move(base));
    ground_.assign(total, RingGround{});
}

void Race::apply_ring_terrain(uint32_t section, uint32_t local, const PathSample& path,
                              const TerrainSlice& slice) {
    if (!ground_base_ || slice.type != 1 && slice.type != 4 || section >= ground_base_->size()) return;
    const size_t i = (*ground_base_)[section] + local;
    if (i >= ground_.size()) return;
    RingGround& g = ground_.mut(i);
    if (!g.set) {
        RouteSample s{};
        s.section = section;
        s.local = local;
        g.slice = terrain_source_ ? terrain_source_(s) : TerrainSlice{};
        g.set = true;
    }
    ring_terrain_onto(g.slice, path, slice);
}

const TerrainSlice& Race::ground(const RouteSample& s) const {
    if (ground_base_ && s.section < ground_base_->size()) {
        const size_t i = (*ground_base_)[s.section] + s.local;
        if (i < ground_.size() && ground_[i].set) return ground_[i].slice;
    }
    return terrain_source_(s);
}

void Race::set_static_frames(StaticFrames frames) {
    static_frames_ = std::move(frames);
    for (int road = 0; road < 2; ++road) statics_[road].assign(routes_[road]->size(), StaticSlice{});
}

void Race::ring_statics(int32_t sample, uint32_t section, uint32_t local, const RingStatics& in) {
    // A ring slice build (0x00454240) for this route sample. The ring walks
    // single roads, so the slice belongs to every road whose sample is the
    // same section sample. Frames resolve the slice's family selectors as
    // 0x0044b630 does when the slice is first drawn (+0x64 bit 4).
    if (!static_frames_ || sample < 0) return;
    for (int road = 0; road < 2; ++road) {
        auto& table = statics_[road];
        if (size_t(sample) >= table.size()) continue;
        const auto& rs = (*routes_[road])[size_t(sample)];
        if (rs.section != section || rs.local != local) continue;
        StaticSlice& out = table.mut(size_t(sample));
        out = StaticSlice{};
        out.built = true;
        out.borr_count = in.borr_count;
        for (size_t i = 0; i < in.borr_count && i < out.borr.size(); ++i)
            out.borr[i] = rs.offset + in.borr[i];
        if (in.borr_count) out.borr_frame = static_frames_(section, local, in.borr_selector, false);
        out.billboards = in.billboards;
        for (size_t i = 0; i < in.billboards && i < 2; ++i) {
            const auto& b = in.billboard[i];
            out.billboard[i] = {b.type, rs.offset + b.offset,
                                static_frames_(section, local, b.selector, true), b.selector};
        }
    }
}

void Race::prepare_static_boxes() {
    // 0x00438610 -> 0x0044b5f0/0x0044b630 load a slice's collision boxes on
    // its first draw. The frame drawn after this tick uses the ring camera
    // (DAT_004b6e5c, front - 0x5000) and reaches slices starting at most
    // 0x1e00 past it. Record it here; the next tick's contacts see it
    // (traffic-field slice capture: every rider slice with start <= previous
    // camera + 0x1e00 loaded, none beyond, none before the first frame).
    // Fixed simulation preparation, so render frequency cannot change contacts.
    box_draw_pending_ = true;
    box_draw_camera_ = ring_ ? ring_->front_q8() - 0x5000 : int32_t(camera_position());
}

void Race::apply_static_boxes() {
    if (!box_draw_pending_) return;
    box_draw_pending_ = false;
    const int64_t camera = std::max<int32_t>(box_draw_camera_, 0);
    const int64_t reach = camera + 0x1e00;
    for (auto& road : statics_)
        for (size_t i = size_t(camera >> 8); i < road.size() && int64_t(i) * 256 <= reach; ++i)
            if (road[i].built && !road[i].boxes_ready) road.mut(i).boxes_ready = true;
}

bool Race::sweep_statics(Rider& r, Rider* half_owner) {
    // 0x004373f0 over the rider's own slice (+0x140): BORR slots (+0x60
    // count, boxes +0x61/+0x68) first, then the billboards (+0x3d). The first
    // overlapping box runs its contact and ends the sweep.
    const auto& table = statics_[r.route];
    if (r.sample >= table.size() || !table[r.sample].built) return false;
    const StaticSlice& slice = table[r.sample];
    if (live_schedule_ && !slice.boxes_ready) return false;
    Bike& s = r.bike;
    const int32_t lo = s.i32(0x28) - s.i32(0x174), hi = s.i32(0x174) + s.i32(0x28);
    const int32_t bottom = s.i32(0x20) - s.i32(0x17c), top = s.i32(0x17c) + s.i32(0x20);
    auto overlaps = [&](const std::array<int32_t, 4>& b, int32_t base) {
        return b[0] + base <= hi && lo <= b[2] + base && b[1] <= top && bottom < b[3];
    };
    if (slice.borr_frame) {
        const auto& boxes = slice.borr_frame->boxes;
        for (size_t i = 0; i < slice.borr_count; ++i) {
            const int32_t units = slice.borr[i];
            const int32_t base = units * 0x100; // slice +0xcc frame, as pedestrians and terrain_height
            for (const auto& box : boxes) {
                std::array<int32_t, 4> b = box;
                // Single-box objects far off the centre line are three times
                // as wide (sub-slice +0xac bit 0x4000 never set by the ring).
                if (std::abs(units) >= 0x400 && boxes.size() == 1) {
                    b[0] *= 3;
                    b[2] *= 3;
                }
                if (!overlaps(b, base)) continue;
                hit_static(r, units, {b[0] + base, b[1], b[2] + base, b[3]}, half_owner);
                return true;
            }
        }
    }
    for (size_t n = 0; n < slice.billboards; ++n) {
        const auto& bb = slice.billboard[n];
        if (!bb.frame) continue;
        const int32_t base = bb.lateral * 0x100;
        for (const auto& box : bb.frame->boxes) {
            if (!overlaps(box, base)) continue;
            // 0x00437750: a car (+0xc4 & 0x10) meets only type-0 billboards;
            // the sweep still ends at the first overlap.
            if (static_car_ && bb.type != 0) return true;
            // 0x00437750: types 0..2 build the static; 1 and 2 lower its
            // bottom to 0x2000 / 0x3fff under the top. Types 3+ dispatch
            // the type-2 marking object directly, without separation.
            std::array<int32_t, 4> b{box[0] + base, box[1], box[2] + base, box[3]};
            if (bb.type < 3) {
                if (bb.type == 1)
                    b[1] = b[3] - 0x2000;
                else if (bb.type == 2)
                    b[1] = b[3] - 0x3fff;
                hit_static(r, bb.lateral, b, half_owner);
            } else {
                if (half_owner) return true; // 0x004046b0 ignores type-2 markings
                // 0x00437750 -> 0x004049c0 type2: decal/terrain marking.
                Bike& bike = r.bike;
                if (bike.i32(0x2f4) == 0 && int32_t(r.sample) != r.half_object.i32(0x1ec)) {
                    if (bb.type < 12) bike.i32(0x298) = (1 + (bb.type - 3) % 3) * 0x3c;
                    if (bike.i32(0xec) > 1000) {
                        r.half_object.u8(0x1e8) = bb.selector;
                        r.half_object.u8(0x1e9) = 1;
                        r.half_object.i32(0x1f0) = ticks_;
                        r.half_object.i32(0x1ec) = int32_t(r.sample);
                        bike.u8(0x80) = bb.selector;
                        bike.u8(0x81) = 1;
                    }
                    bike.i32(0xf0) = 0;
                    bike.i32(0x288) += bike.i32(0x288) / 2;
                }
                bike.i32(0x184) = bike.i32(0x188) = bike.i32(0x18c) = 0;
                bike.u8(0x19c) = 0;
                bike.i32(0xec) = std::clamp(int32_t(bike.i32(0xec)), -1000, 30000);
                bike.i32(0xf4) = std::clamp(int32_t(bike.i32(0xf4)), -10000, 0x600);
                if (live_schedule_ && bike.i32(0x2f4) == 0 && finish_q8_ != 0)
                    r.finish_distance = finish_q8_ - r.half_mirror.position;
            }
            return true;
        }
    }
    return false;
}

void Race::hit_static(Rider& r, int32_t lateral_units, const std::array<int32_t, 4>& box, Rider* half_owner) {
    // 0x00437660: the shared static object 0x004c3e90 at mid-sample with the
    // box extents, separated (0x00436a40) and handed to the rider's handler.
    Rider& o = static_actor_;
    o.route = r.route;
    o.sample = o.previous_sample = r.sample;
    Bike& b = o.bike;
    const int32_t position = int32_t((uint32_t(r.bike.i32(0x1c)) & 0xffffff00u) + 0x80);
    b.i32(0x1c) = position;
    b.i32(0x20) = 0;
    b.i32(0x28) = lateral_units * 0x100;
    for (int k = 0; k < 5; ++k) b.i32(0x30 + 4 * k) = b.i32(0x1c + 4 * k);
    b.i32(0xc4) = 1;
    b.i32(0xec) = 0;
    b.i32(0xf4) = 0;
    b.i32(0xf0) = 0;
    b.i32(0x108) = 0;
    b.i32(0x110) = 0;
    b.i32(0x10c) = 0;
    b.i32(0x134) = position;
    b.i32(0x138) = position;
    b.i32(0x174) = box[2] - box[0];
    b.i32(0x17c) = box[3] - box[1];
    b.i32(0x178) = 0;
    b.i32(0x180) = 0xc0;
    b.i32(0x194) = 0x1000000;
    b.u8(0xc0) = uint8_t(b.u8(0xc0) | 0x10);        // native guard: the static never latches the finish
    if (half_owner) r.bike.i32(0x1a0) = 0x004c3e90; // original fixed static-object identity
    if (static_car_) {
        // 0x0044efa0 with the static as partner: never the player bike, so
        // no horn draw; +0x1f8 = 0x3c (the static's +0xc4 = 1 has no 0xc bits).
        separate(r, 0x10, o, 1);
        r.bike.u8(0x1f4) = 1;
        if ((static_car_->kind & 1) && r.bike.i32(0x1f8) == 0) r.bike.i32(0x1f8) = 0x3c;
        return;
    }
    separate(r, half_owner ? 8 : 4, o, 1);
    if (half_owner)
        contact_half_static(r, *half_owner, o);
    else
        contact_static(r, o);
}

void Race::contact_half_static(Rider& detached, Rider& owner, Rider& other) {
    // 0x004046b0: the crashed half has a contact response independent of
    // its bike. The lateral nudge uses the owning bike's last dt; only the
    // final randomized longitudinal move sweeps the half's own route.
    Bike& s = detached.bike;
    const Bike& o = other.bike;
    auto& animation = owner.crash_actor.animation;
    const auto& library = *crash_library_;
    if (o.i32(0xc4) != 2) {
        if (human(owner)) sound(SoundEvent::grunt_b, s); // event 4
        if (owner.bike.i32(0x2f4) != 2 || o.i32(0xec) >= 1700) {
            if (o.i32(0xc4) & 0x15) s.i32(0x20) = o.i32(0x17c) * 2;
            if (s.i32(0x20) > 0x5000) s.i32(0x20) = 0x5000;
            const int32_t speed = std::abs(s.i32(0xec));
            s.i32(0xf4) = speed < 501 ? 0 : std::abs(s.i32(0xf4) / 2) + speed / 16;
            s.i32(0xec) -= s.i32(0xec) / 4;
            if (animation.animation != 33 && animation.animation != 29) {
                set_animation_with_fallback(animation, library.playback[0], 28, uint32_t(ticks_),
                                            library.fallback);
                queue_animation_with_fallback(animation, library.playback[0], 28, library.fallback);
            }
            if (s.i32(0xf4) > 0x150) s.i32(0xf4) = 0x150;
            if (owner.bike.i32(0x2f4) == 2 && o.i32(0xec) > 1700) {
                s.i32(0xec) += o.i32(0xec) / 4;
                owner.bike.i32(0x2f4) = 1;
                set_animation_with_fallback(animation, library.playback[0], 34, uint32_t(ticks_),
                                            library.fallback);
                queue_animation_with_fallback(animation, library.playback[0], 38, library.fallback);
            }
            if (human(owner) && s.i32(0xec) >= 2001)
                sound(SoundEvent((random() & 1) + (s.i32(0xec) >= 6001 ? 4 : 3)), s);
        }
        if (s.i32(0xf4) < 3000) {
            const int32_t step = (o.i32(0x174) + s.i32(0x174)) / 16 * owner.bike.i32(0x18);
            s.i32(0x28) += o.i32(0x28) < s.i32(0x28) ? step : -step;
            move_along(detached, int32_t(random() % 33) - 16);
        }
    }
    s.i32(0x184) = s.i32(0x188) = s.i32(0x18c) = 0;
    s.u8(0x19c) = 0;
    s.i32(0xec) = std::clamp(int32_t(s.i32(0xec)), -2000, 30000);
    s.i32(0xf4) = std::clamp(int32_t(s.i32(0xf4)), -10000, 10000);
}

void Race::contact_static(Rider& self, Rider& other) {
    // 0x004049c0 with a type-1 static (+0xc4 == 1) as the hit object.
    Bike& s = self.bike;
    Bike& o = other.bike;
    const int32_t raw184 = s.i32(0x184), raw188 = s.i32(0x188), raw18c = s.i32(0x18c);
    const uint32_t full =
        uint32_t(std::abs(raw184)) + uint32_t(std::abs(raw188)) + uint32_t(std::abs(raw18c));
    int32_t local20 = int32_t(full) >> 10;
    int iclass = 6;
    bool down = false;
    if (s.i32(0x2f4) != 0) {
        s.i32(0xf4) += 0x150;
    } else {
        o.i32(0x17c) = o.i32(0x17c) / 2; // short object (+0xc4 == 1)
        if (human(self)) {
            const int32_t hit = std::abs(raw184);
            if (hit < 0x1771) {
                if (hit >= 0x7d1 || std::abs(raw188) >= 0x3e9) {
                    sound(SoundEvent((random() & 1) + 3), s);
                    sound(SoundEvent(random() & 1), s);
                }
            } else {
                sound(SoundEvent((random() & 1) + 4), s);
                sound(SoundEvent((random() & 1) + 1), s);
            }
        }
        if (o.i32(0x17c) < 0x2000)
            iclass = 0;
        else if ((s.u8(0x19c) & 0x40) == 0)
            iclass = 3;
        else if (o.i32(0x17c) < 0x2800 && (o.i32(0xc4) & 0x10) == 0)
            iclass = 1;
        else
            iclass = 2;
        const int32_t elasticity = s.i32(0x180);
        s.i32(0x188) = div256(int64_t(raw188) * elasticity);
        s.i32(0x18c) = div256(int64_t(raw18c) * elasticity);
        s.i32(0x184) = div256(int64_t(raw184) * elasticity);
        const int32_t steer = s.i32(0xf0);
        if (iclass != 0) {
            down = knock_off(self, local20, local20, iclass); // (+0xc4 & 0xc) == 0: stamina and bike damage
            if (iclass == 1 || iclass == 2) {
                if (down) {
                    if (iclass == 2) s.i32(0xec) += s.i32(0x184);
                } else if (s.i32(0xec) - o.i32(0x108) < 0x1771) {
                    s.i32(0xec) += s.i32(0x184);
                } else {
                    crash_reason(self, iclass, kContactCrashRiderImpulse,
                                 kContactCrashBikeImpulse); // 0x404eeb immediates
                }
            } else if (down) {
                s.i32(0xf4) += div256(std::abs(s.i32(0x184)));
            } else {
                const int32_t shove = std::abs(s.i32(0x184));
                if (o.i32(0x28) < s.i32(0x28)) {
                    if (s.i32(0xf0) < 0) s.i32(0xf0) = 0;
                    s.i32(0xf0) += shove / 2;
                } else {
                    if (s.i32(0xf0) > 0) s.i32(0xf0) = 0;
                    s.i32(0xf0) -= shove / 2;
                }
                s.i32(0xec) -= div_pow2(shove, 2);
                if (s.i32(0xec) < 0) s.i32(0xec) = 0;
            }
            local20 = steer;
        } else {
            // Short object: slow riders are nudged aside, faster ones ride over.
            if (s.i32(0xec) < 300 && s.u8(0x19c) == 0x20) {
                move_along(self, s.i32(0x18) * 2, false);
                iclass = 6;
            } else if (s.i32(0xec) < 500)
                iclass = 6;
            local20 = steer;
            if (iclass == 0) {
                s.i32(0x20) += o.i32(0x17c) / 2;
                const int32_t t = wrap_mul(wrap_mul(o.i32(0x17c), std::abs(s.i32(0x184))), 2);
                const int32_t u = (t + ((t >> 31) & 0x3fff)) >> 14;
                s.i32(0xf4) += ((u + ((u >> 31) & 0xf)) >> 4) * 4;
            }
        }
    }
    if (iclass != 0) {
        // 0x00404f4d: step sideways away from the object.
        const int32_t w = s.i32(0x174) + o.i32(0x174);
        const int32_t step = div_pow2(w, 4) * s.i32(0x18);
        s.i32(0x28) += o.i32(0x28) < s.i32(0x28) ? step : -step;
    }
    // 0x00404f93 steering kick (skipped when knocked down or unchanged).
    if (!down && local20 != s.i32(0xf0)) {
        const uint32_t raw = uint32_t(s.i32(0x2d8)) * uint32_t(s.i32(0xf0) - local20) * 2u;
        const int32_t prod = int32_t(raw);
        const int32_t next = s.i32(0x288) + ((prod + ((prod >> 31) & 0xff)) >> 8);
        const int32_t lim = s.i32(0x29c);
        s.i32(0x288) = (next > lim || (-lim != -next && next <= -lim)) ? (next > lim ? lim : -lim) : next;
    }
    // 0x00404feb tail.
    if (s.i32(0xf4) > 0x600) s.i32(0xf4) = 0x600;
    s.i32(0x188) = 0;
    s.i32(0x18c) = 0;
    s.i32(0x184) = 0;
    s.u8(0x19c) = 0;
    if (s.i32(0xec) > 30000) s.i32(0xec) = 30000;
    if (s.i32(0xec) < -1000) s.i32(0xec) = -1000;
    if (live_schedule_ && s.i32(0x2f4) == 0 && finish_q8_ != 0)
        self.finish_distance = finish_q8_ - self.half_mirror.position;
}

void Race::contact_hazard(Rider& self, int32_t hazard_lo_q8, int32_t hazard_hi_q8) {
    Bike& s = self.bike;
    if (s.i32(0x2f4) != 0) return;
    const int32_t elasticity = s.i32(0x180);
    s.i32(0x188) = div256(int64_t(s.i32(0x188)) * elasticity);
    s.i32(0x184) = div256(int64_t(s.i32(0x184)) * elasticity);
    const int32_t hit = std::abs(s.i32(0x184));
    if (human(self)) {
        if (hit < 0x1771) {
            if (hit >= 0x7d1 || std::abs(s.i32(0x188)) >= 0x3e9) {
                sound(SoundEvent((random() & 1) + 3), s);
                sound(SoundEvent(random() & 1), s);
            }
        } else {
            sound(SoundEvent((random() & 1) + 4), s);
            sound(SoundEvent(random() & 1), s);
        }
    }
    const int32_t magnitude =
        (std::abs(s.i32(0x184)) + std::abs(s.i32(0x188)) + std::abs(s.i32(0x18c))) >> 10;
    // Class 1 (hazard short): knock_off reason 1; on survival the small-gap
    // branch transfers impulse to speed, the big-gap branch crashes reason 1
    // with the default 0x004052c0 vector pair (0x004049c0 local_18 ladder).
    const bool down = knock_off(self, magnitude, magnitude, 1);
    if (down) {
        return;
    } else if (s.i32(0xec) - 0 < 0x1771) {
        s.i32(0xec) += s.i32(0x184);
    } else {
        crash_reason(self, 1, {-256, 0, 1792}, {0, 0, 1024});
        return;
    }
    // Lateral separation against the immovable slot (0x00436a40 with the
    // static actor mass >> bike mass: the rider takes it all).
    const int32_t centre = (hazard_lo_q8 + hazard_hi_q8) / 2;
    const int32_t depth = (s.i32(0x174) - std::abs(s.i32(0x28) - centre)) + 0x800;
    s.i32(0x28) += s.i32(0x28) < centre ? -(depth + 3) : (depth + 3);
    s.i32(0x184) = 0;
    s.i32(0x188) = 0;
    s.i32(0x18c) = 0;
    s.u8(0x19c) = 0;
    if (s.i32(0xec) > 30000) s.i32(0xec) = 30000;
    if (s.i32(0xec) < -1000) s.i32(0xec) = 0;
}

// Shared knock-off decision 0x004052c0(victim, stamina_damage, bike_damage,
// reason). The grunt/crash sound draws stay upstream in the 0x004049c0 caller
// (shared stream position); this helper performs no RNG.
// Reason table: 0 zeroes both inputs; 1/4/5 keep both; 2 scales both by 7/4
// with C++ truncation toward zero (the assembly adds a sign-bias then shifts
// right arithmetically, which rounds negative dividends up, i.e. matches
// trunc(p*7/4) for p*7 != INT32_MIN); 3 divides both by 8 with the same
// truncation caveat; all other reasons including silent-hit 6 zero both.
// Stamina damage 0x004050c0: max -= 4*p floored at base/2, current -= 64*p
// floored at zero. Bike damage 0x004051e0: +0x2f0 -= p floored at zero.
// Owner/network bypasses: flag -1 (remote) or (flag 1 without local
// ownership) skip both applications and return the untouched +0x2f4 state,
// mirroring the assembly's early-return branches (network decisions from
// 0x00405990/0x004060b0 are not ported; offline play always passes flag 0).
// When either application returns zero, crash entry 0x00403d60 runs with the
// assembly's argument order: rider impulse (local_1c+3) then bike impulse
// (local_1c). Reason 4 passes {-80,0,0}/{0,0,0}; every other reason passes
// {-256,0,1792}/{0,0,1024}. Reason 4 without a crash selects riding
// animation 9 on the victim's +0x70 block (recorded on Rider for the render
// owner; the Rider object has no animation block of its own). Reason 5
// multiplies bike+0x188 by the scaled stamina damage; when that field is
// nonzero and the victim is still riding (+0x2f4 == 0), steer +0x288 gains
// trunc(+0x2d8 * +0x188 / 256) computed in original-width 32-bit wrap
// before truncation, then clamps to +-+0x29c.
bool Race::knock_off(Rider& victim, int32_t stamina_damage, int32_t bike_damage, int32_t reason,
                     bool owner_local, int32_t owner_flag,
                     std::optional<std::array<int32_t, 3>> rider_impulse,
                     std::optional<std::array<int32_t, 3>> bike_impulse) {
    int32_t p = stamina_damage, q = bike_damage;
    if (reason == 0) {
        p = 0;
        q = 0;
    } else if (reason == 1 || reason == 4 || reason == 5) { /* keep */
    } else if (reason == 2) {
        // 0x004052c0: (p*7 + ((p*7 >> 31) & 3)) >> 2, i.e. sar-rounded /4.
        const auto scale74 = [](int32_t v) {
            const uint32_t prod = uint32_t(v) * 7u;
            const int32_t p7 =
                prod <= uint32_t(INT32_MAX) ? int32_t(prod) : int32_t(int64_t(prod) - 0x100000000LL);
            return (p7 + ((p7 >> 31) & 3)) >> 2;
        };
        p = scale74(p);
        q = scale74(q);
    } else if (reason == 3) {
        // 0x004052c0: (p + ((p >> 31) & 7)) >> 3, i.e. sar-rounded /8.
        p = (p + ((p >> 31) & 7)) >> 3;
        q = (q + ((q >> 31) & 7)) >> 3;
    } else {
        p = 0;
        q = 0;
    }
    const bool bypass = owner_flag == -1 || (owner_flag == 1 && !owner_local);
    if (!bypass) {
        // 0x004050c0 (local/offline branch): products in int64 then narrowed,
        // matching the assembly's 32-bit imul before the add.
        RiderStats& st = victim.stamina;
        Bike& b = victim.bike;
        st.max = int32_t(int64_t(st.max) + int64_t(p) * -4);
        const int32_t floor = st.base / 2; // truncating divide, as original
        if (st.max < floor) st.max = floor;
        st.current = int32_t(int64_t(st.current) + int64_t(p) * -0x40);
        if (st.current < 1) st.current = 0;
        // 0x004051e0 (local/offline branch).
        b.i32(0x2f0) -= q;
        if (b.i32(0x2f0) < 1) b.i32(0x2f0) = 0;
    }
    if (victim.stamina.current == 0 || victim.bike.i32(0x2f0) == 0) {
        // 0x00403d60 with the reason-4 vectors or the default vectors,
        // assembly argument order preserved (rider impulse, then bike).
        if (!rider_impulse)
            rider_impulse =
                reason == 4 ? std::array<int32_t, 3>{-80, 0, 0} : std::array<int32_t, 3>{-256, 0, 1792};
        if (!bike_impulse)
            bike_impulse = reason == 4 ? std::array<int32_t, 3>{0, 0, 0} : std::array<int32_t, 3>{0, 0, 1024};
        crash_reason(victim, reason, *rider_impulse, *bike_impulse);
    }
    if (reason == 4 && victim.bike.i32(0x2f4) == 0) {
        if (live_schedule_ && ride_library_)
            set_animation_with_fallback(victim.ride_animation, ride_bank(victim), 9, uint32_t(ticks_),
                                        ride_library_->fallback);
        else
            victim.contact_animation = 9;
    } else if (reason == 5) {
        // 0x004052c0: original-width 32-bit wraparound product.
        const uint32_t prod = uint32_t(victim.bike.i32(0x188)) * uint32_t(p);
        victim.bike.i32(0x188) =
            prod <= uint32_t(INT32_MAX) ? int32_t(prod) : int32_t(int64_t(prod) - 0x100000000LL);
    }
    if (victim.bike.i32(0x188) != 0 && victim.bike.i32(0x2f4) == 0) {
        // Steering path shared by every reason (0x004052c0 tail): original
        // widths first (+0x2d8 * +0x188 wraps 32-bit), bias +sar-round /256.
        const uint32_t raw = uint32_t(victim.bike.i32(0x2d8)) * uint32_t(victim.bike.i32(0x188));
        const int32_t wide =
            raw <= uint32_t(INT32_MAX) ? int32_t(raw) : int32_t(int64_t(raw) - 0x100000000LL);
        const int32_t add = (wide + ((wide >> 31) & 0xff)) >> 8;
        const int32_t next = int32_t(uint32_t(victim.bike.i32(0x288)) + uint32_t(add));
        const int32_t lim = victim.bike.i32(0x29c);
        victim.bike.i32(0x288) =
            (lim < next || (-lim != -next && next <= -lim)) ? (lim < next ? lim : -lim) : next;
    }
    return victim.bike.i32(0x2f4) != 0;
}

// Bike side of the contact handler 0x004049c0 against a car (+0xc4 == 0x10).
// Cars are tall (+0x17c = 8960 >= 0x2000), so the hit is class 2 when the
// car's width contains the rider (+0x19c bit 0x40, set by collide_traffic
// from the 0x00436a40 prologue rule), otherwise class 3. Class 1 needs a
// short other object and never fires for cars. The knock-off decision below
// is the shared 0x004052c0 helper; the grunt/crash sound draws above stay
// upstream in this caller, preserving the shared-stream position.
void Race::contact_car(Rider& self, TrafficCar& car) {
    Bike& s = self.bike;
    // Crashed self (riderless bike, e.g. L5d tick 2278): 0x004049c0's crashed
    // path still runs the +0xf4 bump (line 219: +0x150). Mirror bike-bike
    // contact(), which does the same; knock_off/nudge/kick below then run
    // (knock_off guards re-entry via crash_active; finish recompute stays
    // gated on uncrashed).
    if (s.i32(0x2f4) != 0) s.i32(0xf4) += 0x150;
    // 0x004049c0 sounds, class routing and knock magnitudes read the
    // UNSCALED impulses; elasticity applies after (lines 89-93).
    const int32_t raw184 = s.i32(0x184), raw188 = s.i32(0x188), raw18c = s.i32(0x18c);
    const int32_t hit = std::abs(raw184);
    // Player sounds (shared with the bike-bike head): silent below both
    // thresholds (0x004049c0 jumps to LAB_00404c2d with iVar9 = 6, which the
    // geometry block below then recomputes). Silence only skips the sounds;
    // routing stays geometric.
    if (human(self)) {
        if (hit < 0x1771) {
            if (hit >= 0x7d1 || std::abs(raw188) >= 0x3e9) {
                sound(SoundEvent((random() & 1) + 3), s);
                sound(SoundEvent(random() & 1), s);
            }
        } else {
            sound(SoundEvent((random() & 1) + 4), s);
            sound(SoundEvent((random() & 1) + 1), s);
        }
    }
    const int32_t old_steer_rate = s.i32(0xf0);
    const bool contained = (s.u8(0x19c) & 0x40) != 0;
    const int32_t full = std::abs(raw184) + std::abs(raw188) + std::abs(raw18c);
    const int32_t magnitude = full >> 10;
    const int32_t elasticity = s.i32(0x180);
    s.i32(0x188) = div256(int64_t(raw188) * elasticity);
    s.i32(0x184) = div256(int64_t(raw184) * elasticity);
    s.i32(0x18c) = div256(int64_t(raw18c) * elasticity);
    const int32_t shit = std::abs(s.i32(0x184)); // scaled hit: the lateral branch below
    // Silent hits run the same geometric routing: the original recomputes
    // iVar9 from geometry after the sound gate (LAB_00404c2d).
    bool down = false; // 0x004049c0 local_21, returned by 0x004052c0
    // Crashed self (riderless bike): 0x004049c0 never calls 0x004052c0
    // (no re-knock; crash_entry guards on crash_active). Skip the decision
    // so down stays false (kick allowed) and no stamina/bike damage or
    // +shit/256 bump is applied; the +0x150 bump above is the only +0xf4
    // change. Mirrors bike-bike contact()'s crashed path.
    const bool crashed = s.i32(0x2f4) != 0;
    if (!contained) {
        // Class 3: glance off the car's side, with the 0x004052c0 decision.
        // 0x004049c0: other lateral < self lateral shoves +f0 (away from the
        // car), else -f0; speed loses |184|/4 either way. The car caller has
        // +0xc4 & 0xc == 0, so the assembly keeps stamina local_20 and bike
        // damage iVar10 both at the full magnitude.
        down = crashed ? false : knock_off(self, magnitude, magnitude, 3);
        if (down) {
            s.i32(0xf4) += div_pow2(shit, 8);
        } else if (car.lateral < s.i32(0x28)) {
            // Crashed: no +0xf0 shove and no speed bleed (0x004049c0 crashed
            // path has neither; the tail kick below uses magnitude vs +0xf0
            // to give the -2). Uncrashed shoves away from the car.
            if (!crashed) {
                if (s.i32(0xf0) < 0) s.i32(0xf0) = 0;
                s.i32(0xf0) += shit / 2;
                s.i32(0xec) = std::max(0, s.i32(0xec) - div_pow2(shit, 2));
            }
        } else {
            if (!crashed) {
                if (s.i32(0xf0) > 0) s.i32(0xf0) = 0;
                s.i32(0xf0) -= shit / 2;
                s.i32(0xec) = std::max(0, s.i32(0xec) - div_pow2(shit, 2));
            }
        }
    } else if ((down = crashed ? false : knock_off(self, magnitude, magnitude, 2))) {
        // Class 2 and the rider went down in the decision: the impulse still
        // transfers to speed (0x004049c0).
        s.i32(0xec) += s.i32(0x184);
    } else if (s.i32(0xec) - car.speed < 0x1771) {
        // Class 2, small speed gap: rear-end pushes speed over.
        s.i32(0xec) += s.i32(0x184);
    } else {
        // Class 2, big speed gap: knocked off with reason 2 (animation 29),
        // then the nudge and tail below. local_21 remains false here, as
        // in the original: it records the earlier knock-off decision.
        crash_car(self);
    }
    // Scenery-marking block (0x004049c0 iVar9 == 0 path): when local_18[3]
    // (speed at entry) exceeds 1000 and the rider's sample differs from the
    // car's +0x1ec sample, mark +0x298 by the car's +0x124 sub-kind, record
    // the sample/time/flags, zero the steer rate and grow +0x288 by half.
    // Cars never take this path in the golden (iVar9 is 2/3: the
    // else-if/else ladder above), so it is documented, not executed.
    // Nudge apart sideways (0x00404f4d).
    const int32_t push = div_pow2(s.i32(0x174) + 16384, 4) * s.i32(0x18);
    s.i32(0x28) += car.lateral < s.i32(0x28) ? push : -push;
    // Tail (0x00404feb): impulses and flags clear; speed/velocity clamps.
    s.i32(0x184) = 0;
    s.i32(0x188) = 0;
    s.i32(0x18c) = 0;
    s.u8(0x19c) = 0;
    if (s.i32(0xec) > 30000) s.i32(0xec) = 30000;
    if (s.i32(0xec) < -1000) s.i32(0xec) = -1000;
    if (s.i32(0xf4) > 0x600) s.i32(0xf4) = 0x600;
    if (s.i32(0xf4) < -10000) s.i32(0xf4) = -10000;
    // Steering kick (0x00404f93 tail). Uncrashed: kicked by the steer-rate
    // change (new - old). Crashed: 0x004049c0 never updated +0xf0 via shove,
    // so the tail compares local_20 (magnitude = full>>10) vs +0xf0;
    // e.g. L5d tick 2278: magnitude 2 vs +0xf0 0 gives kick -2.
    // local_21 (down) suppresses only this kick; a newly crashed rider still
    // receives the separation nudge and impulse/flag clear above.
    if (!down && (crashed ? magnitude : old_steer_rate) != s.i32(0xf0)) {
        const int32_t base = crashed ? magnitude : old_steer_rate;
        const int32_t kick = div256(int64_t(s.i32(0x2d8)) * (s.i32(0xf0) - base) * 2);
        const int32_t lim = s.i32(0x29c);
        const int32_t next = s.i32(0x288) + kick;
        s.i32(0x288) = std::max(-lim, std::min(lim, next));
    }
    if (live_schedule_ && s.i32(0x2f4) == 0 && finish_q8_ != 0)
        self.finish_distance = finish_q8_ - self.half_mirror.position;
}

// Crash entries for contact 0x00403d60. crash_car keeps the reason-2 car path;
// the impulse pair is the handler's own local_18 (rider {0,0,0x200}, bike
// {0,0,0x100} == kContactCrashRiderImpulse/kContactCrashBikeImpulse in
// crash.hpp), not the knock_off defaults. crash_reason carries any reason
// with the pair in the assembly's argument order (rider impulse, then bike).
//
// 0x00403d60 makes the half the crash body (iVar2 = *(bike+0x2fc)): the two
// stay the same object for the whole crash. Native keeps a separate crash
// body, so the half object image follows the body for the entry word set
// (0x1c..0x160 plus the zeroed block). Flags stay with the half: entry ORs
// +0xc0 |= 3 (line 112) onto the half's own template bits (4 -> 7); the
// body keeps its own object flags for the scan gates.
static void sync_crash_half(Rider& r) {
    for (int off : {0x1c,  0x20,  0x24,  0x28,  0x2c,  0x30,  0x34,  0x38,  0x3c,  0x40,  0x58,  0x5c,
                    0x60,  0xec,  0xf0,  0xf4,  0x104, 0x108, 0x10c, 0x110, 0x118, 0x11c, 0x114, 0x120,
                    0x124, 0x128, 0x12c, 0x130, 0x134, 0x138, 0x13c, 0x140, 0x158, 0x160, 0x198, 0x1a0})
        r.half_object.i32(off) = r.crash_actor.motion.i32(off);
    r.half_object.u8(0x164) = r.crash_actor.motion.u8(0x164);
    r.half_object.u8(0x19c) = r.crash_actor.motion.u8(0x19c);
}
void Race::crash_car(Rider& r) {
    crash_reason(r, 2, kContactCrashRiderImpulse, kContactCrashBikeImpulse);
}
void Race::crash_reason(Rider& r, int32_t reason, std::array<int32_t, 3> rider_impulse,
                        std::array<int32_t, 3> bike_impulse) {
    crash_entry(r, reason, rider_impulse, bike_impulse);
}
void Race::crash_entry(Rider& r, int32_t reason, std::optional<std::array<int32_t, 3>> rider_impulse,
                       std::optional<std::array<int32_t, 3>> bike_impulse) {
    if (crash_library_) {
        if (r.crash_active) return;
        r.bike.i32(0x2f4) = 0;
        // The crash body runs on the half object (iVar2): pre-fill the body
        // with the half's behavior-table words before begin_crash overwrites
        // the motion set (split-left 2056: body +0x16c/+0x140 spec words
        // were stale zeros, skewing the body vertical vs the original).
        r.crash_actor.motion = r.half_object;
        begin_crash(r.bike, r.crash_actor, crash_config_, crash_library_->playback[0],
                    crash_library_->fallback, uint32_t(ticks_), reason, rider_impulse, bike_impulse);
        // 0x00403d60 copies the bike words into the half (iVar2 =
        // *(bike+0x2fc): 0x1c..0x2c, 0x30..0x40, 0x58/0x5c/0x60, velocities
        // and 0x104..0x110, the zeroed 0x114..0x128/0x198/0x1a0 block,
        // 0x12c..0x160) and sets half +0xc0 |= 3 (line 112). begin_crash
        // makes the same copies into the crash body; the half object image
        // needs them too (split-left 2055: half ground 61 vs 37, flags 7
        // vs 4). Impulses already landed on the body, matching the half.
        sync_crash_half(r);
        r.half_object.u8(0xc0) |= 3;
        // 0x00403d60 writes the crash rider animation into the rider half's
        // +0x70 block (iVar2 = *(bike+0x2fc)) - the same block the riding
        // animation advances, so the field frame dump (half+0x80) reads the
        // crash code (29/33/35) right from the crash tick.
        r.ride_animation = r.crash_actor.animation;
        r.crash_active = true;
        r.crash_timer = 0;
        r.crash_actor.route = r.route;
        r.crash_actor.sample = r.sample;
        r.crash_actor.previous_sample = r.previous_sample;
        // The half is now the crash body (0x00403d60): its mirror reads the
        // copied body words even before the first body motion step.
        sync_half_mirror(r);
        // 0x00403d60 tail: thunk_FUN_00407410 on the crash body. With the
        // finish known and a live slice (+0x2c, i.e. bike_slice_present)
        // the original takes the direct path (finish - body position),
        // discarding any split-branch walk offset. That reset is what lets
        // a branch rider rejoining the trunk (e.g. L5d tick 2267) match.
        if (finish_q8_ != 0 && r.bike_slice_present)
            r.finish_distance = finish_q8_ - r.crash_actor.motion.i32(0x1c);
        return;
    }
    // Stand-in (no crash library): the shared helper already computed the
    // vector pair for the golden path; the reason lands in +0x2f8.
    (void)rider_impulse;
    (void)bike_impulse;
    r.bike.i32(0x2f4) = 1;
    r.bike.i32(0x2f8) = reason;
    r.bike.u8(0xc0) |= 0x10;
    r.crash_timer = 0;
}

// The rider half doubles as the crash body (0x00403d60): while the rider is
// crashed the ring node and the object scans read the body's own words, so
// the mirror is refreshed from it instead of the bike.
void Race::sync_half_mirror(Rider& r) {
    const Bike& m = r.crash_actor.motion;
    r.half_mirror = {m.i32(0x1c), m.i32(0x28),          m.i32(0x20),        m.i32(0xf4),
                     m.i32(0xec), m.i32(0xf0),          m.i32(0x10c),       m.i32(0x108),
                     m.i32(0x60), r.crash_actor.sample, r.crash_actor.route};
}

void Race::contact(Rider& self, Rider& other) {
    // Bike-against-bike branch of the contact handler 0x004049c0. The hit
    // object's class ladder (lines 77-88) reads the OTHER bike's height box:
    // a bike crashed to the ground (+0x17c < 0x2000) is class 0 and the rider
    // rides over it; a contained hit (+0x19c bit 0x40 width rule from
    // 0x00436a40) is class 1 against a short-ish unflagged bike, class 2
    // otherwise; an uncontained hit is class 3 (glance). The knock-off
    // decision 0x004052c0 runs for classes 1-3: the bike caller
    // (+0xc4 & 0xc != 0) zeroes the stamina param and passes the shifted
    // magnitude sum as the bike-damage param. A crashed self only gains
    // +0x150 velocity and joins the shared nudge and kick with the raw
    // magnitude (else branch, line 219).
    Bike& s = self.bike;
    const Bike& o = other.bike;
    const int32_t raw184 = s.i32(0x184), raw188 = s.i32(0x188), raw18c = s.i32(0x18c);
    const int32_t full = std::abs(raw184) + std::abs(raw188) + std::abs(raw18c);
    int32_t local20 = full >> 10; // 0x00404f93 kick target; the class branches
                                  // replace it with the old steer rate
    int iclass = 6;
    bool down = false; // 0x004052c0 local_21: the kick is skipped when the rider went down
    if (s.i32(0x2f4) != 0) {
        s.i32(0xf4) += 0x150;
    } else {
        self.half_object.u8(0x1e9) = 0; // line 37: contact clears the half's wobble
        if (human(self)) {
            // 0x004049c0: the player grunts and the contact makes a crash sound by its strength.
            const int32_t hit = std::abs(raw184);
            if (hit < 0x1771) {
                if (hit >= 0x7d1 || std::abs(raw188) >= 0x3e9) {
                    if (s.i32(0x2f4) == 0) sound(SoundEvent((random() & 1) + 3), s);
                    sound(SoundEvent(random() & 1), s);
                }
            } else {
                if (s.i32(0x2f4) == 0) sound(SoundEvent((random() & 1) + 4), s);
                sound(SoundEvent(random() & 1), s);
            }
        }
        if (o.i32(0x17c) < 0x2000)
            iclass = 0;
        else if ((s.u8(0x19c) & 0x40) == 0)
            iclass = 3;
        else if (o.i32(0x17c) < 0x2800 && (o.i32(0xc4) & 0x10) == 0)
            iclass = 1;
        else
            iclass = 2;
        // The sounds, class routing and knock magnitudes read the UNSCALED
        // impulses; elasticity applies after (lines 89-93).
        const int32_t elasticity = s.i32(0x180);
        s.i32(0x188) = div256(int64_t(raw188) * elasticity);
        s.i32(0x184) = div256(int64_t(raw184) * elasticity);
        s.i32(0x18c) = div256(int64_t(raw18c) * elasticity);
        const int32_t steer = s.i32(0xf0);
        if (iclass != 0) {
            const int32_t swing = (local20 + ((full >> 31) & 15)) >> 4;
            down = knock_off(self, 0, swing, iclass);
            local20 = steer; // every response branch lands on the old steer rate
            if (iclass == 1 || iclass == 2) {
                if (down) {
                    // Class 1 and down: speed is untouched (only class 2 transfers it).
                    if (iclass == 2) s.i32(0xec) += s.i32(0x184);
                } else if (s.i32(0xec) - o.i32(0x108) < 0x1771) {
                    // Small speed gap: rear-end pushes speed over.
                    s.i32(0xec) += s.i32(0x184);
                } else {
                    // Big speed gap: the handler's own local_18 pair, then the
                    // nudge and tail below.
                    crash_reason(self, iclass, kContactCrashRiderImpulse, kContactCrashBikeImpulse);
                }
            } else if (down) {
                s.i32(0xf4) += div256(std::abs(s.i32(0x184)));
            } else {
                // Class 3 (glance): the lateral branch (or the down vertical transfer).
                const int32_t shove = std::abs(s.i32(0x184));
                if (o.i32(0x28) < s.i32(0x28)) {
                    if (s.i32(0xf0) < 0) s.i32(0xf0) = 0;
                    s.i32(0xf0) += shove / 2;
                } else {
                    if (s.i32(0xf0) > 0) s.i32(0xf0) = 0;
                    s.i32(0xf0) -= shove / 2;
                }
                s.i32(0xec) -= div_pow2(shove, 2);
                if (s.i32(0xec) < 0) s.i32(0xec) = 0;
            }
        } else {
            // Short other (a bike on the ground): slow riders are nudged
            // aside, faster ones ride over it.
            if (s.i32(0xec) < 300 && s.u8(0x19c) == 0x20) {
                move_along(self, s.i32(0x18) * 2, false);
                iclass = 6;
            } else if (s.i32(0xec) < 500)
                iclass = 6;
            local20 = steer;
            if (iclass == 0) {
                s.i32(0x20) += o.i32(0x17c) / 2;
                const int32_t t = wrap_mul(wrap_mul(o.i32(0x17c), std::abs(s.i32(0x184))), 2);
                const int32_t u = (t + ((t >> 31) & 0x3fff)) >> 14;
                s.i32(0xf4) += ((u + ((u >> 31) & 0xf)) >> 4) * 4;
            }
        }
    }
    if (iclass != 0) {
        // Nudge apart sideways (0x00404f4d).
        const int32_t step = div_pow2(s.i32(0x174) + o.i32(0x174), 4) * s.i32(0x18);
        s.i32(0x28) += o.i32(0x28) < s.i32(0x28) ? step : -step;
    }
    // Steering kick (0x00404f93): a moved steer rate kicks +0x288 by
    // +0x2d8*(new-target)*2>>8 (sar-rounded), clamped to +-+0x29c. Skipped when
    // the rider went down (0x004052c0 local_21 true) or the rate is unchanged.
    if (!down && local20 != s.i32(0xf0)) {
        // Original-width 32-bit wraparound product (cf. knock_off reason 5).
        const uint32_t raw = uint32_t(s.i32(0x2d8)) * uint32_t(s.i32(0xf0) - local20) * 2u;
        const int32_t prod =
            raw <= uint32_t(INT32_MAX) ? int32_t(raw) : int32_t(int64_t(raw) - 0x100000000LL);
        const int32_t next = s.i32(0x288) + ((prod + ((prod >> 31) & 0xff)) >> 8);
        const int32_t lim = s.i32(0x29c);
        s.i32(0x288) = (next > lim || (-lim != -next && next <= -lim)) ? (next > lim ? lim : -lim) : next;
    }
    // Tail (0x00404feb): vertical clamp, impulses and flags clear, speed
    // clamp (the +10000 upper clamp is dead behind the +0x600 one).
    if (s.i32(0xf4) > 0x600) s.i32(0xf4) = 0x600;
    s.i32(0x188) = 0;
    s.i32(0x18c) = 0;
    s.i32(0x184) = 0;
    s.u8(0x19c) = 0;
    if (s.i32(0xec) > 30000) s.i32(0xec) = 30000;
    if (s.i32(0xec) < -1000) s.i32(0xec) = -1000;
    if (s.i32(0xf4) < -10000) s.i32(0xf4) = -10000;
    // An uncrashed rider's half recomputes +0x1f8 (0x00407410 from its own
    // position, which the half took at the bike's last move).
    if (live_schedule_ && s.i32(0x2f4) == 0 && finish_q8_ != 0)
        self.finish_distance = finish_q8_ - self.half_mirror.position;
}
void Race::move(Rider& r, bool player) {
    // 0x004395e0 (single player, no network ghost branch).
    Bike& b = r.bike;
    if (b.i32(0x160) > 0) b.i32(0x104) = b.i32(0xf0);
    b.i32(0x108) = b.i32(0xec);
    b.i32(0x10c) = b.i32(0x104);
    b.i32(0x110) = 0;
    // 0x004395e0:30-32 networked handicap: an uncrashed moving human with a
    // positive byte moves faster this tick (never slower).
    if (net_ && human(r) && b.i32(0x2f4) == 0 && b.i32(0xec) > 0) {
        const int32_t h = handicap_[size_t(r.human)];
        if (h > 0) b.i32(0x108) = ((h * b.i32(0xec)) >> 8) + b.i32(0xec);
    }
    int32_t effective = b.i32(0x108), lateral = b.i32(0x104), forward;
    if (effective < 0) {
        lateral = -lateral;
        forward = effective * -4;
    } else
        forward = effective * 4;
    int32_t yaw = wrap_angle(approx_atan2(forward, lateral) * 65536);
    const int32_t yaw_gain = (b.u8(0x278) & 4) ? b.i32(700) : b.i32(0x2b8);
    yaw = div256(yaw_gain) * yaw;
    if (b.u8(0x278) & 1) yaw = yaw < 1 ? -0x3f0000 : 0x3f0000;
    if (b.i32(0xec) < 4000) {
        if (yaw > 0x180000) yaw = 0x180000;
        if (yaw < -0x180000) yaw = -0x180000;
    }
    b.i32(0x5c) = yaw;
    const int32_t dt = b.i32(0x18);
    const int32_t distance = ((b.i32(0x108) * 2 - b.accel * dt) * dt) >> 1;
    for (int k = 0; k < 5; ++k) b.i32(0x30 + 4 * k) = b.i32(0x1c + 4 * k); // previous position/height copy
    move_along(r, div256(distance));
    int32_t heading = 0;
    const int32_t bend = b.i32(0x130);
    if (bend != 0) {
        int32_t push;
        if (!(b.u8(0x278) & 4) || int8_t(b.u8(0x164)) < 0x33)
            push = bend * 400 / dt;
        else if (player && !demo_hold_)
            push = bend * 800 / dt; // 0x004398c0: DAT_004753c8 demo gate
        else
            push = bend * 600 / dt;
        b.i32(0x10c) += div256(push);
        heading = approx_atan2(distance, div256(int64_t(bend) * 400)) * 65536;
    }
    if (heading > 0x7fffff) heading -= 0x1000000;
    b.i32(0x128) = heading;
    if (b.i32(0xec) > 300) b.i32(0x28) += dt * b.i32(0x10c);
}

Race::SubSliceView Race::sub_slice(const Rider& r, size_t sample) const {
    const auto& route = route_of(r);
    const auto& rs = route[std::min(sample, route.size() - 1)];
    const auto& path = rs.path;
    SubSliceView v;
    const TerrainSlice& t = ground(rs);
    v.type = t.type;
    v.flags = path.marking_flags;
    v.event_type = path.event_type;
    v.p3a = int8_t(path.event_parameters[1]);
    v.b3b = path.event_parameters[2];
    v.sel = {t.selectors[0], t.selectors[1], path.side_flags[0], path.side_flags[1]};
    v.e0 = rs.offset - path.left;
    v.e4 = rs.offset + path.right;
    const WallProfile w = wall_profile(t, path, t.type == 3);
    v.e8 = rs.offset + (t.type == 3 ? w.outer_left : -path.left - path.shoulder_left);
    v.ec = rs.offset + (t.type == 3 ? w.outer_right : path.right + path.shoulder_right);
    for (size_t side = 0; side < 2; ++side) {
        v.base[side] = rs.offset + w.base[side];
        v.top[side] = rs.offset + w.top[side];
        v.height[side] = w.height[side];
    }
    // 0x0041c200 stores the rail height in bits 2..7 when it draws the strip:
    // samples up to 31 ahead of the camera strip have been drawn.
    if (v.event_type == 5 && rail_height_source_ && int64_t(sample) <= int64_t(camera_position() >> 8) + 31) {
        const int32_t height = rail_height_source_(rs);
        if (height > 0) v.b3b = uint8_t(((height / 4) << 2) | (v.b3b & 3));
    }
    return v;
}

void Race::walls(Rider& r, const Bike* owner) {
    Bike& b = r.bike;
    if (!terrain_source_) {
        // Standalone physics fixtures keep their synthetic sides: stop at the
        // shoulder edge, crash when fast.
        const SliceEdges e = edges(r, r.sample);
        const int32_t x = b.i32(0x28) >> 8;
        if (x > e.left_shoulder && x < e.right_shoulder) return;
        b.i32(0x28) = (x >= e.right_shoulder ? e.right_shoulder - 1 : e.left_shoulder + 1) * 256;
        b.i32(0x10c) = 0;
        b.i32(0x104) = 0;
        b.i32(0xf0) = 0;
        b.i32(0x288) = 0;
        if (b.i32(0xec) > 4000) {
            if (human(r)) sound(SoundEvent::grunt_c, b);
            if (b.i32(0xc4) == 4)
                crash(r);
            else
                b.i32(0xec) = b.i32(0xec) * 3 / 4;
        } else
            b.i32(0xec) = b.i32(0xec) * 3 / 4;
        return;
    }
    // 0x0043a610. Laterals are absolute (sub-slice +0xcc included).
    auto trunc256 = [](int32_t v) { return (v + ((v >> 31) & 0xff)) >> 8; };
    auto interpolate = [&](int32_t value, int32_t next, int32_t fraction) {
        const int32_t d = next - value;
        return d ? value + trunc256(fraction * d) : value;
    };
    const auto& route = route_of(r);
    const size_t next_sample = std::min(r.sample + 1, route.size() - 1);
    const size_t previous_next = std::min(r.previous_sample + 1, route.size() - 1);
    const SubSliceView cur = sub_slice(r, r.sample), nxt = sub_slice(r, next_sample);
    const SubSliceView prev = sub_slice(r, r.previous_sample), prev_next = sub_slice(r, previous_next);
    const int32_t hw = b.i32(0x174);
    int32_t hi = (b.i32(0x28) + hw) >> 8;
    const int32_t lo = (b.i32(0x28) - hw) >> 8;
    if (cur.e0 < lo && hi < cur.e4) return;
    const int32_t c4 = b.i32(0xc4);
    const bool player =
        human(r) || (owner && std::any_of(riders_.begin(), riders_.end(),
                                          [&](const Rider& h) { return human(h) && owner == &h.bike; }));
    auto rail_height = [](uint8_t b3b) { return (b3b & 0xfc) ? int32_t(b3b & 0xfc) << 8 : 0x40; };
    uint32_t hit = 0;
    bool angled = false;
    int32_t lateral = b.i32(0x28);
    bool general = true;
    if (cur.event_type == 5 && (c4 == 4 || (c4 == 8 && owner && owner->i32(0x2f4) == 1))) {
        if (rail_height(cur.b3b) > b.i32(0x20)) {
            const int32_t fraction = int32_t(uint32_t(b.i32(0x1c)) & 0xff),
                          previous_fraction = int32_t(uint32_t(b.i32(0x30)) & 0xff);
            if (cur.b3b & 1) {
                const int32_t x = interpolate(cur.e8, nxt.e8, fraction) - cur.p3a;
                if (lo < x && x < hi) {
                    hit = 1;
                    const int32_t px = interpolate(prev.e8, prev_next.e8, previous_fraction) - prev.p3a;
                    lateral = b.i32(0x3c) < px * 0x100 ? (x - 4) * 0x100 - hw : (x + 4) * 0x100 + hw;
                }
            }
            if (!hit && (cur.b3b & 2)) {
                const int32_t x = interpolate(cur.ec, nxt.ec, fraction) + cur.p3a;
                if (lo < x && x < hi) {
                    hit = 1;
                    const int32_t px = interpolate(prev.ec, prev_next.ec, previous_fraction);
                    lateral =
                        (px + prev.p3a) * 0x100 < b.i32(0x3c) ? hw + (x + 4) * 0x100 : (x - 4) * 0x100 - hw;
                }
            }
            if (hit) {
                general = false;
                if (b.i32(0x160) == 0 && rail_height(cur.b3b) < b.i32(0x17c) + b.i32(0x20)) {
                    if (c4 == 4) {
                        if (b.i32(0x2f4) == 0) hit = 3;
                    } else if (c4 == 8)
                        hit = 0;
                }
            }
        }
    }
    if (general) {
        const int32_t widen = int32_t(((uint32_t(b.i32(0x1c)) & 0xff) * 0xfa) / 3) >> 8;
        int32_t depth = 0;
        switch (cur.type) {
        case 0: {
            if (cur.flags & 0x38) return;
            size_t side = 0;
            int32_t right = cur.ec;
            if (cur.sel[1] == 0xff) right += widen;
            if (right < hi) {
                hit = 1;
                lateral = right * 0x100 - hw;
                depth = hi - right;
                side = 1;
            } else {
                int32_t left = cur.e8;
                if (cur.sel[0] == 0xff) left -= widen;
                if (lo < left) {
                    hit = 1;
                    lateral = hw + left * 0x100;
                    depth = left - lo;
                    side = 0;
                }
            }
            if (!hit) return;
            // The comma in the original sets the angle flag before the last test.
            if (((prev.flags & 0x18) || (cur.sel[side] != 0xff && prev.sel[side] == 0xff)) &&
                ((hw + hw / 2) >> 8) < depth) {
                angled = true;
                if (depth < (hw * 2) >> 8) hit = 2;
            }
            break;
        }
        case 1:
            if (player) {
                if (hi <= cur.base[1]) {
                    int32_t left = cur.base[0];
                    if (lo < left) {
                        if (cur.height[0] < 1) left = cur.top[0];
                        if (lo < left) {
                            hit = 1;
                            lateral = hw + left * 0x100;
                        }
                    }
                } else if (cur.height[1] < 1) {
                    if (hi > cur.top[1]) {
                        hit = 1;
                        lateral = cur.top[1] * 0x100 - hw;
                    }
                } else {
                    hit = 1;
                    lateral = cur.base[1] * 0x100 - hw;
                }
            } else if (cur.ec < hi) {
                hit = 1;
                lateral = cur.ec * 0x100 - hw;
            } else if (lo < cur.e8) {
                hit = 1;
                lateral = hw + cur.e8 * 0x100;
            }
            break;
        case 3: {
            if (cur.ec < hi) {
                hit = 1;
                lateral = cur.ec * 0x100 - hw;
                depth = hi - cur.ec;
            } else if (lo < cur.e8) {
                hit = 1;
                lateral = hw + cur.e8 * 0x100;
                depth = cur.e8 - lo;
            }
            if (!hit) return;
            if (!(prev.flags & 0x40) && ((hw / 2) >> 8) < depth) {
                angled = true;
                if (depth < (hw / 2 + hw) >> 8) hit = 2;
            }
            break;
        }
        default:
            return;
        }
    }
    if (!hit) return;
    if (angled) {
        if (std::abs(b.i32(0xec)) < 0x9c4)
            angled = false;
        else if (hit != 2) {
            b.i32(0xec) = 0;
            move_along(r, b.i32(0x30) - b.i32(0x1c), false);
        }
    } else {
        if (hit < 3)
            b.i32(0xf0) = trunc256(b.i32(0xf0) * -100);
        else if (hit == 3 && c4 == 4) {
            crash_entry(r, 3, std::nullopt, std::nullopt);
            if (b.i32(0x17c) + b.i32(0x20) < rail_height(cur.b3b))
                b.i32(0xf0) = trunc256(b.i32(0xf0) * -100);
            else
                lateral = b.i32(0x28);
        }
        b.i32(0x60) = 0;
        if (c4 == 4) {
            if ((b.i32(0xf0) > 0 && b.i32(0x288) > 0) || (b.i32(0xf0) < 0 && b.i32(0x288) < 0))
                b.i32(0x288) = 0;
            b.i32(0x5c) = 0;
        } else if (c4 == 8)
            b.i32(0x5c) = b.i32(0x108) >= 0 ? 0 : 0x800000;
        else
            b.i32(0x5c) = 0;
    }
    if ((angled || (std::abs(b.i32(0xf0)) > 0x9c3 && hit != 3)) && c4 == 4) {
        if (angled && human(r)) sound(SoundEvent::grunt_c, b); // 0x00436830(5), local rider
        crash_reason(r, 6, std::array<int32_t, 3>{0, 0, 0x50}, std::array<int32_t, 3>{0, 0, 0x10});
    }
    b.i32(0x28) = lateral;
}

void Race::vertical(Rider& r) {
    // 0x0043a190 with imported terrain ground height from 0x00439b10.
    Bike& b = r.bike;
    const SliceEdges e = edges(r, r.sample);
    const auto& s = route_of(r)[r.sample].path;
    const int32_t x = b.i32(0x28) >> 8;
    int32_t ground = 0;
    if (x < e.left || x > e.right) {
        if (x > e.right)
            ground = x >= e.right_shoulder ? ground_wall : s.height_right;
        else
            ground = x <= e.left_shoulder ? ground_wall : s.height_left;
    }
    if (terrain_source_) {
        const auto& route = route_of(r);
        const auto& current = route[r.sample];
        const auto& next = route[std::min(r.sample + 1, route.size() - 1)];
        ground = terrain_height(this->ground(current), current.path, this->ground(next), next.path,
                                b.i32(0x28) - current.offset * 256, uint8_t(b.i32(0x1c)),
                                next.offset - current.offset, current.offset);
    }
    b.i32(0x24) = ground;
    // 0x0043a190 type-0x20 branch: pedestrians stay grounded, including
    // knocked-down actors, without the bike landing/bounce law.
    if (b.i32(0xc4) & 0x20) {
        b.i32(0xf4) = 0;
        b.i32(0x20) = 0;
        b.i32(0x160) = tables_.contact_base;
        return;
    }
    const int32_t dt = b.i32(0x18);
    int32_t ground_move = (ground - b.i32(0x38)) * 0x100 + b.i32(0x12c);
    const int32_t above = (b.i32(0xf4) * dt - ground_move) + b.i32(0x20);
    int32_t height_above = above * 100 / 99;
    const int32_t vy = b.i32(0x11c) * dt + b.i32(0xf4);
    const int32_t above_before = b.i32(0x34);
    b.i32(0xf4) = vy;
    const int32_t threshold = (dt * 4 - 3) * b.i32(0x16c);
    const int32_t rise_now = s.rise, rise_before = route_of(r)[r.previous_sample].path.rise;
    bool grounded = false;
    if (threshold < above) {
        b.u8(0x278) |= 0x20;
        if (threshold - above_before != 0 && above_before <= threshold) {
            const int32_t cap = b.i32(0x1b4) * b.i32(0x1b8);
            if (cap < vy) b.i32(0xf4) = cap;
        }
        b.i32(0x160) = 0;
        b.i32(0x11c) = -b.i32(0x1b4);
    } else if (threshold < above_before && vy < b.i32(0x168)) {
        // Landing.
        if (b.i32(0xc4) == 4) {
            int32_t wobble = ((std::abs(vy) * b.i32(0x288)) >> 12) + 1;
            const int32_t lim = b.i32(0x29c) * 2;
            if (wobble > lim)
                wobble = lim;
            else if (wobble < -lim)
                wobble = -lim;
            b.i32(0x288) = wobble;
        }
        const int32_t rebound = -div256(int64_t(b.i32(0x170)) * vy);
        const int32_t k = b.i32(0xec) < 0 ? -5 - rise_now : rise_now + 5;
        b.i32(0xf4) = k * (b.i32(0xec) >> 7) + rebound;
        b.i32(0x11c) = b.i32(0x1b4) * -2;
        b.i32(0xfc) = -0x1000;
        b.i32(0x160) = tables_.contact_base;
        height_above = 0;
        if (human(r)) sound(SoundEvent::bounce, b); // 0x0043a190
    } else {
        grounded = true;
    }
    if (grounded) {
        if (threshold < above_before) b.u8(0x164) = 1;
        if (dt != 1) ground_move = dt == 16 ? ground_move >> 4 : ground_move / dt;
        b.i32(0xf4) = ground_move;
        int32_t c = div256(int64_t(rise_now - rise_before) * b.i32(0xec));
        c = div_pow2(c, 3) + tables_.contact_base;
        b.i32(0x160) = c < 0 ? 0 : c;
        height_above = 0;
    }
    b.i32(0x20) = height_above;
    int32_t pitch = div_pow2(height_above - b.i32(0xfc), 6) + b.i32(0xfc);
    if (pitch > 0x2000) pitch = 0x2000;
    if (pitch < -0x2000) pitch = -0x2000;
    b.i32(0xfc) = pitch;
    b.i32(0x100) = pitch - height_above;
}

void Race::crash(Rider& r) {
    if (crash_library_) {
        if (r.crash_active) return;
        // traction currently raises the crash flag directly; entry applies once.
        r.bike.i32(0x2f4) = 0;
        // Crash body runs on the half object: pre-fill behavior words (see crash_entry).
        r.crash_actor.motion = r.half_object;
        begin_crash(r.bike, r.crash_actor, crash_config_, crash_library_->playback[0],
                    crash_library_->fallback, uint32_t(ticks_), 0);
        // 0x00403d60 half copies (see crash_entry).
        sync_crash_half(r);
        r.half_object.u8(0xc0) |= 3;
        r.ride_animation = r.crash_actor.animation; // 0x00403d60: the crash rider animation is the half block
        r.crash_active = true;
        r.crash_timer = 0;
        r.crash_actor.route = r.route;
        r.crash_actor.sample = r.sample;
        r.crash_actor.previous_sample = r.previous_sample;
        sync_half_mirror(r);
        // 0x00403d60 tail (same as crash_entry): recompute the half's
        // +0x1f8 on the crash body; direct when the finish is known.
        if (finish_q8_ != 0 && r.bike_slice_present)
            r.finish_distance = finish_q8_ - r.crash_actor.motion.i32(0x1c);
        return;
    }
    // Simplified stand-in for 0x00403d60: fall, slide to a stop, then remount on the road.
    r.bike.i32(0x2f4) = 1;
    r.bike.u8(0xc0) |= 0x10;
    r.crash_timer = 0;
}

// 0x00407f30 (the rider node's crash think) advances the crash animations
// and the +0x2f4 phase before any motion: the rider animation gate reads the
// body words left by the previous tick's body move.
void Race::crash_phases(Rider& r) {
    Bike& bike = r.bike;
    Bike& body = r.crash_actor.motion;
    const auto& library = *crash_library_;
    CrashRiderAnimationInput input;
    input.crash = bike.i32(0x2f4);
    input.speed = body.i32(0xec);
    input.height_q8 = body.i32(0x20);
    advance_crash_rider_animation(r.crash_actor.animation, library.playback[0], library.fallback, input,
                                  uint32_t(ticks_), [&] { return random(); });
    r.ride_animation = r.crash_actor.animation; // the crash rider animation runs on the rider half block
    if (bike.i32(0x2f4) != input.crash) r.crash_timer = 0;
    bike.i32(0x2f4) = input.crash;
    if (input.crash == 2) ++r.crash_timer;
}
// 0x0043a580: the riderless bike (the +0x300 path of 0x004060b0) has its
// own move - prep 0x00438c70, integrate 0x004390c0, gear shift 0x004391d0,
// road-relative move 0x004395e0 - while the detached rider body keeps the
// generic actor motion 0x0043adc0 on the half node.
void Race::crash_bike_animation(Rider& r) {
    Bike& bike = r.bike;
    const auto& library = *crash_library_;
    // 0x00407a90: outside states 1/2 (the state-3 pickup) the bike clears
    // its frame pointer +0x7c and skips the advance; rider animation 41
    // draws the bike being lifted. The disassembly (407abc-407b0b) then
    // refreshes the scenery-mark flash: when the half's +0x1e9 is set, it
    // survives only while the bike still sits on the marked sample
    // (bike +0x1c arithmetic >>8 == half +0x1ec) and the unsigned tick gap
    // (bike +0x10 - half +0x1f0) stays below 12, advancing as (gap>>2)+1;
    // bike +0x81 copies the half's byte. That copy already runs in the live
    // scheduler's bike think (run_bike_node); it runs here only for the
    // every-tick loop, where that think never runs. 0x00437e60 stores the
    // current tick into +0x10 just before think, so ticks_ is exact here.
    if (bike.i32(0x2f4) != 1 && bike.i32(0x2f4) != 2) {
        r.crash_bike_animation.selected.reset();
        if (!live_schedule_) {
            auto& half = r.half_object;
            if (half.u8(0x1e9)) {
                const int32_t pos_sample = bike.i32(0x1c) >> 8;
                const uint32_t elapsed = uint32_t(ticks_) - uint32_t(half.i32(0x1f0));
                half.u8(0x1e9) =
                    (pos_sample == half.i32(0x1ec) && elapsed < 12) ? uint8_t((elapsed >> 2) + 1) : 0;
            }
            bike.u8(0x81) = half.u8(0x1e9);
        }
        return;
    }
    advance_crash_bike_animation(r.crash_bike_animation, library.playback[0], library.fallback,
                                 bike.i32(0xec), uint32_t(ticks_), [&] { return random(); });
}
void Race::crash_bike_motion(Rider& r, bool player) {
    Bike& bike = r.bike;
    const int32_t steer = bike.i32(0x288);
    bike.i32(0x60) = wrap_angle(approx_atan2(74, div256(steer)) * 65536); // 0x00438c70 (exe 0x46dcd8 = 74)
    bike.i32(0x114) = 0;
    bike.i32(0x118) = 0;
    bike.i32(0xf0) = 0;
    bike.u8(0x164) = 0;
    const int surface = surface_class(edges(r, r.sample), bike.i32(0x28));
    integrate_speed(bike, tables_, surface); // 0x004390c0
    shift_gears(bike);                       // 0x004391d0
    move(r, player);                         // 0x004395e0: +0x108 := +0xec, then the advance
    walls(r, &r.bike);
    vertical(r);
}
// The crash body (0x00406890's crashed half callback -> 0x0043adc0): the
// detached rider half's own motion, then the mirror and standings refresh.
void Race::crash_body_motion(Rider& r, int32_t dt) {
    CrashActor& body = r.crash_actor;
    auto motion = [&](Rider& actor) {
        auto& b = actor.bike;
        if (!b.i32(0x160)) {
            b.i32(0x114) = b.i32(0x118) = 0;
            b.u8(0x164) = 0;
        }
        const int surface = surface_class(edges(actor, actor.sample), b.i32(0x28));
        integrate_speed(b, tables_, surface);
        advance_actor_motion(b, tables_, [&](int32_t distance) { move_along(actor, distance); });
        walls(actor, &r.bike);
        vertical(actor);
    };
    Rider detached;
    detached.bike = body.motion;
    detached.route = body.route;
    detached.sample = body.sample;
    detached.previous_sample = body.previous_sample;
    detached.bike.i32(0x18) = dt; // the half node's own dt in the live scheduler
    const int32_t body_before = detached.bike.i32(0x1c);
    motion(detached);
    body.motion = detached.bike;
    body.route = detached.route;
    body.sample = detached.sample;
    body.previous_sample = detached.previous_sample;
    // The half is the crash body (0x00403d60): it follows the body's words
    // every tick, not just at entry (split-left 2056: half ground stays 61
    // while the body goes to 0). Flags keep the entry OR (see sync_crash_half).
    sync_crash_half(r);
    // 0x00403d60 makes the half the crash body, so the ring node and the
    // scans read its live words: the mirror follows the body while crashed
    // (live_standings takes over again on the first uncrashed step).
    sync_half_mirror(r);
    // 0x00406890's crashed half callback runs with this motion: the body's
    // position delta feeds half +0x1f8, then the place refresh (0x004075f0).
    r.finish_distance += body_before - body.motion.i32(0x1c);
    if (r.place_refresh <= r.half_sched_last) refresh_place(r);
}
// The crash recovery (0x00407f30's tail plus the walk-back think states):
// phase timeouts, the remount and the off-screen bounds wreck. Runs on the
// half node after the body move, matching the original's think order.
void Race::crash_recovery(Rider& r, bool player, uint32_t controls) {
    Bike& bike = r.bike;
    CrashActor& body = r.crash_actor;
    const auto& library = *crash_library_;
    CrashRecoveryInput recovery;
    recovery.player = player;
    recovery.police_owner = r.cop;
    recovery.state_elapsed = r.crash_timer;
    // 0x0040a600 walk override: raw control bits 0x7800, player bike, human
    // branch (demo hold clear) and racing state (DAT_004c5d7c == 1, true for
    // every race step; the arrest switches it away with the busted flag).
    // Throttle/punch bits (0x1/0x21) never set 0x7800, so replayed inputs
    // always auto-walk (split-left 2377 walks back toward the bike); the
    // old accel/brake/left/right mapping invented bits the original never
    // reads there and sent the body the wrong way.
    if (player && !demo_hold_ && (controls & 0x7800) != 0 && !busted_)
        recovery.walk_controls = controls & 0x7800;
    // 0x0040a6d5: the walking player (rider animation 38, rider +0xc0 bit 0x10
    // clear) on a mountain sample (bike sub-slice type 1) is wrecked once the
    // bike or the rider lateral leaves [+0x14c, +0x158], the wall bases
    // (0x00416010(5, 0xb4), +0x2f0 = 0), and the walk is not stepped.
    bool bounds_wreck = false;
    if (player && terrain_source_ && body.animation.animation == 38 && !(body.motion.u8(0xc0) & 0x10)) {
        const SubSliceView own = sub_slice(r, r.sample);
        if (own.type == 1) {
            Rider walker;
            walker.route = body.route;
            walker.sample = body.sample;
            walker.previous_sample = body.previous_sample;
            const SubSliceView rider_slice = sub_slice(walker, body.sample);
            const int32_t bx = bike.i32(0x28) >> 8, rx = body.motion.i32(0x28) >> 8;
            bounds_wreck =
                own.base[0] > bx || own.base[1] < bx || rider_slice.base[0] > rx || rider_slice.base[1] < rx;
        }
    }
    const auto action = bounds_wreck ? CrashRecoveryAction::wrecked
                                     : recover_crash_actor(bike, body, library.playback[0], library.fallback,
                                                           uint32_t(ticks_), recovery);
    if (!bounds_wreck) r.crash_timer = recovery.state_elapsed;
    if (action == CrashRecoveryAction::remount) {
        remount_crash_actor(bike, r.crash_bike_animation, body, crash_config_, library.playback[0],
                            library.fallback, uint32_t(ticks_));
        r.ride_animation = body.animation; // one half block: the remount code continues into the ride advance
        // The half rejoins the bike: clear the crash-entry bits it gained
        // (0x00403d60 line 112), mirroring the body's &= 0xfc in
        // remount_crash_actor (contact 2724: golden half 4 vs native 7).
        r.half_object.u8(0xc0) &= 0xfc;
        r.stamina.current = r.stamina.max;
        r.crash_active = false;
        // 0x00404560 tail: thunk 0x00407410 on the half, which now sits at
        // the bike's position (0x004067d0 copy-back). Direct path with the
        // finish known (traffic-field-body tick 2575: +0x1f8 1496540 ->
        // 1496547, the 7-unit body-to-bike gap).
        if (finish_q8_ != 0 && r.bike_slice_present) r.finish_distance = finish_q8_ - body.motion.i32(0x1c);
        return;
    }
    if (action == CrashRecoveryAction::wrecked) bike.i32(0x2f0) = 0;
}
// The combined crash step for the every-tick rider loop (step_rider). The
// live scheduler runs the same parts split across the nodes: the half node
// owns the think phases (think_half) and the body move + recovery
// (run_half_node), the bike node owns the bike animation and the riderless
// bike move (move_bike_live) - matching 0x00407f30/0x00406890 on the half
// node against 0x00407a90/0x004060b0 on the bike node.
void Race::step_crash(Rider& r, bool player, uint32_t controls) {
    // Solo freeze-hack mirror: once the crash body is 256+ from the bike at
    // tick start the proxy has nulled the player half for good (see
    // Rider::half_frozen). The bike node keeps running (riderless-bike move);
    // the half node parts below stop: brain, phases, body motion, recovery.
    if (player && freeze_actors_ && r.crash_active && !r.half_frozen) {
        const int64_t gap = int64_t(r.crash_actor.motion.i32(0x1c)) - int64_t(r.bike.i32(0x1c));
        if (gap <= -256 || gap >= 256) r.half_frozen = true;
    }
    const bool half_live = !r.half_frozen;
    // 0x00407bd0 brain for crashed riders in the solo every-tick loop (the
    // live scheduler runs ai_think via think_half): transition, state clock,
    // replan and crash-gated stamina. Dispatch is a no-op for states 8/9.
    // Split-left 2056: native state 0/ticks 0, original state 9/ticks 1.
    // Half-think stops when the solo half is frozen (see above).
    if (half_live) {
        // 0x00434660 wrapper regen (see think_half): player +dt when crash != 1.
        if (player && r.bike.i32(0x2f4) != 1 && r.stamina.current < r.stamina.max)
            r.stamina.current = std::min(r.stamina.max, r.stamina.current + r.bike.i32(0x18));
        ai_transition(r);
        const int32_t brain_dt = r.bike.i32(0x18);
        if (r.brain.state >= 0 && r.brain.state < int(r.brain.state_ticks.size()))
            r.brain.state_ticks[size_t(r.brain.state)] += brain_dt;
        r.brain.replan_ticks += brain_dt;
        if (r.brain.replan_ticks > 0x3b) ai_replan(r);
        if (ride_library_) {
            const int crash_state = r.bike.i32(0x2f4);
            const int passes = crash_state == 1 ? 0 : crash_state == 0 ? 1 : 2;
            for (int i = 0; i < passes; ++i)
                r.stamina.current = std::min(r.stamina.max, r.stamina.current + r.bike.i32(0x18));
        }
    }
    crash_bike_animation(r);
    if (half_live) crash_phases(r);
    crash_bike_motion(r, player);
    if (half_live) crash_body_motion(r, r.bike.i32(0x18));
    if (half_live) crash_recovery(r, player, controls);
}

int32_t Race::curvature(const Rider& r, size_t sample, int samples) const {
    // 0x00449b40: average of sub-slice +0xb4 (atan(bend) << 14, 0x00454240) over the next samples.
    const auto& route = route_of(r);
    int64_t sum = 0;
    for (int k = 0; k < samples && sample + size_t(k) < route.size(); ++k)
        sum += wrap_angle(atan_ratio(tables_, route[sample + size_t(k)].path.bend) * (1 << 14));
    return int32_t(sum / samples);
}

// Ring scans for the AI replan (0x00408cd0 via 0x00449ce0). The original walks
// scheduler links in position order from the rider's own half node, skipping
// riders whose lateral sits outside their slice bounds. Returns a riders_
// index or -1. Only bike/half pairs participate (halves carry the scan state).
int Race::ai_scan_replan(const Rider& r, bool forward, int32_t cap) const {
    // 0x00408cd0: from the rider's half, walk rider halves (mask 8) with
    // 0x00449ce0 until one lies strictly inside its slice's road edges
    // (+0xe0/+0xe4); return that half's bike (+0x1c4). The police rider
    // (DAT_004642dc) skips the edge test.
    build_scan_list();
    int at = scan_find(int(&r - riders_.data()), 8);
    for (;;) {
        at = scan_next(at, 8, forward, cap);
        if (at < 0) return -1;
        const ScanObject& o = scan_[size_t(at)];
        const Rider& q = riders_[size_t(o.rider)];
        const SliceEdges e = edges(q, q.sample);
        const int32_t x = o.lateral >> 8;
        if (r.cop || (e.left < x && x < e.right)) return o.rider; // the police rider skips the edge test
    }
}

void Race::build_scan_list() const {
    // Rider bikes then halves (0x00405990 creates bike then half), riders in
    // build order (player first, 0x00405600). Pedestrians carry their own
    // creation order: ring-setup spawns precede the riders.
    scan_.clear();
    for (size_t i = 0; i < std::min(riders_.size(), built_riders_); ++i) {
        const Rider& r = riders_[i];
        ScanObject bike;
        bike.position = r.bike.i32(0x1c);
        bike.lateral = r.bike.i32(0x28);
        bike.speed = r.bike.i32(0x108);
        bike.type = 4;
        bike.order = 0x10000000u + uint32_t(i) * 2;
        bike.rider = int(i);
        bike.sample =
            r.bike_slice_present ? &route_of(r)[std::min(r.sample, route_of(r).size() - 1)] : nullptr;
        scan_.push_back(bike);
        ScanObject half = bike;
        half.lateral = half_x(r);
        if (live_schedule_) {
            // The half's own words, as its bike's last move copied them
            // (0x004067d0); contacts since then moved only the bike.
            const auto& road = route_of(r);
            half.position = r.half_mirror.position;
            half.lateral = r.half_mirror.lateral;
            half.speed = r.half_mirror.lat108;
            half.sample = &road[std::min(r.half_mirror.sample, road.size() - 1)];
        } else if (r.half_detached) {
            half.position = r.half_position;
            half.speed = r.half_speed;
            const auto& road = route_of(r);
            half.sample = &road[std::min(size_t(std::max(half.position, 0) >> 8), road.size() - 1)];
        }
        if (r.crash_active) {
            // A crashed rider's half object is the body (0x00403d60 detaches it
            // from the bike): it carries its own position, lateral and speed.
            const Bike& m = r.crash_actor.motion;
            const auto& road = *routes_[r.crash_actor.route];
            half.position = m.i32(0x1c);
            half.lateral = m.i32(0x28);
            half.speed = m.i32(0x108);
            half.sample = &road[std::min(size_t(std::max(half.position, 0) >> 8), road.size() - 1)];
        }
        if (!r.half_slice_present)
            half.sample = nullptr;
        else if (!half.sample)
            half.sample = &route_of(r)[std::min(r.sample, route_of(r).size() - 1)];
        half.type = 8;
        half.order = bike.order + 1;
        scan_.push_back(half);
    }
    if (pedestrians_) {
        const auto& pool = pedestrians_->pool();
        for (size_t i = 0; i < pool.size(); ++i) {
            const auto& p = pool[i];
            if (!p.active) continue;
            ScanObject o;
            o.position = p.body.bike.i32(0x1c);
            o.lateral = p.body.bike.i32(0x28);
            o.speed = p.body.bike.i32(0x108);
            o.type = 0x20;
            o.order = p.order;
            o.pedestrian = int(i);
            const auto& road = route_of(p.body);
            o.sample = &road[std::min(p.body.sample, road.size() - 1)];
            scan_.push_back(o);
        }
    }
    if (traffic_) {
        const auto& road = route_of(riders_[0]);
        const auto& cars = traffic_->cars();
        for (size_t i = 0; i < cars.size(); ++i) {
            const auto& c = cars[i];
            if (!c.active) continue;
            ScanObject o;
            o.position = c.position;
            o.lateral = c.lateral;
            o.speed = c.motion.i32(0x108); // signed road velocity, including oncoming cars
            o.type = 0x10;                 // +0xc4 of the free-car template 0x00475698
            o.order = 0x80000000u + c.serial;
            o.car = int(i);
            o.sample = &road[std::min(size_t(std::max(c.position, 0) >> 8), road.size() - 1)];
            scan_.push_back(o);
        }
    }
    // Live scheduler: rider nodes keep the list order of the last
    // 0x004383e0 sort (the scans walk links, which a pass does not reorder),
    // keyed by their position at that sort; other objects by position.
    for (auto& o : scan_) {
        if (live_schedule_ && o.car >= 0 && size_t(o.car) < car_scan_keys_.size())
            o.key = car_scan_keys_[size_t(o.car)];
        else if (live_schedule_ && o.pedestrian >= 0 && size_t(o.pedestrian) < ped_scan_keys_.size())
            o.key = ped_scan_keys_[size_t(o.pedestrian)];
        else
            o.key = o.position;
    }
    if (live_schedule_) {
        (void)schedule_order();
        const int count = int(schedule_.size());
        for (int k = 0; k < count; ++k) {
            const ScheduleNode& n = schedule_[size_t(k)];
            for (auto& o : scan_)
                if (o.rider == n.rider && o.type == n.type) {
                    o.key = n.sorted;
                    o.rank = count - 1 - k;
                }
        }
    }
    std::stable_sort(scan_.begin(), scan_.end(), [](const ScanObject& a, const ScanObject& b) {
        if (a.key != b.key) return a.key < b.key;
        if (a.rank >= 0 && b.rank >= 0) return a.rank < b.rank;
        return a.order < b.order;
    });
}

int Race::scan_find(int rider, uint32_t type) const {
    for (size_t k = 0; k < scan_.size(); ++k)
        if (scan_[k].rider == rider && scan_[k].type == type) return int(k);
    return -1;
}

int Race::scan_next(int from, uint32_t mask, bool forward, int32_t window) const {
    // 0x00449ce0: the next object along the list in that direction whose type
    // matches the mask; the walk stops (null) at the first object window or
    // more away from the starting object, matching or not. A NULL slice
    // rejects self or skips a candidate, even when its position is in range.
    // Single-road slices carry road flags 7, so the +0x36 half test passes.
    if (from < 0) return -1;
    if (window < 0) window = 0x20000000;
    const ScanObject& f = scan_[size_t(from)];
    if (!f.sample) return -1;
    if (forward) {
        for (size_t k = size_t(from) + 1; k < scan_.size(); ++k) {
            if (window <= scan_[k].position - f.position) return -1;
            if (scan_[k].sample && (scan_[k].type & mask)) return int(k);
        }
    } else {
        for (int k = from - 1; k >= 0; --k) {
            if (window <= f.position - scan_[size_t(k)].position) return -1;
            if (scan_[size_t(k)].sample && (scan_[size_t(k)].type & mask)) return k;
        }
    }
    return -1;
}

// 0x0044a110: object size by type.
static int32_t object_size(uint32_t type) {
    switch (type) {
    case 1:
    case 2:
        return 0x2000;
    case 4:
    case 8:
    case 0x20:
        return 0x3000;
    case 0x10:
        return 0x8000;
    default:
        return 0x1000;
    }
}

// 0x00409020: slow for the curvature 8 samples ahead (4-sample average).
int32_t Race::ai_curve_limit(const Rider& r) const {
    const size_t at = std::min(r.sample + 8, route_of(r).size() - 1);
    const int32_t c = std::abs(curvature(r, at, 4));
    const int32_t base = r.brain.current_speed;
    const int32_t q = base - div_pow2(base, 2);
    if (c > 0x30000) {
        const int32_t half = q / 2;
        return (q - (int32_t(((int64_t(half) << 8) / 0x600) * (c >> 8)) >> 8)) + half;
    }
    return base;
}

// 0x004082f0: every 60 ticks ease the rating table, pick neighbours, rate the
// overtake steps, keep the nearer target and reset the speed from the level
// tables with the rubber band / fixed cases.
void Race::ai_replan(Rider& r) {
    AiState& ai = r.brain;
    // Ease the rating table once per 60 ticks banked; the police rider also
    // decays its pursuit speed +0x1a4 by one each bank (reset below 0x32).
    auto ease = [](int32_t& cur, int32_t target, int32_t step) {
        if (cur == target) return;
        if (target < cur) {
            cur -= step;
            if (cur < target) cur = target;
        } else {
            cur += step;
            if (target < cur) cur = target;
        }
    };
    while (ai.replan_ticks > 0x3b) {
        if (r.cop) {
            if (--ai.current_speed < 0x32 && level_ >= 0 && level_ < 5)
                ai.current_speed = ai.spd_b[size_t(level_)];
        }
        for (int k = 0; k < 10; ++k)
            for (int j = 0; j < 3; ++j)
                ease(ai.rating[size_t(k * 3 + j)], ai.rating_tgt[size_t(k * 3 + j)], ai.ease[size_t(j)]);
        ai.replan_ticks -= 0x3c;
    }
    const int32_t cap = r.cop ? 0x5000 : 0xa00;
    const int ahead = ai_scan_replan(r, true, cap);
    ai.nbr_ahead = ahead;
    const int behind = ai_scan_replan(r, false, cap);
    ai.nbr_behind = behind;
    // Player half +0x1fc is the player's 1-based place; inside the top three
    // the overtake thresholds drop.
    // 0x004082f0 reads the player half's place word +0x1fc (0x00407550
    // refreshes it on its own cadence; 0 from the build until then).
    const int place = nearest_human(r.bike.i32(0x1c)).place; // plan 34: the nearest human's place
    const int32_t near = place < 4 ? 4 - place : 0;
    auto rate = [&](int nbr) -> int {
        if (nbr < 0) return 0;
        const int idx = riders_[size_t(nbr)].brain.overtake_index; // neighbour brain +0x165
        int32_t v = 0;
        if (idx < 10)
            v = (ai.rating[size_t(idx * 3 + 1)] - ai.rating[size_t(idx * 3)]) +
                ai.rating[size_t(idx * 3 + 2)];
        if (v < (0x10 - near) * 1000 - level_ * 0x5dc) return 1;
        return ((0x10 - near) * 2000 - level_ * 3000 <= v) + 2;
    };
    ai.ov_ahead = rate(ahead);
    ai.ov_behind = rate(behind);
    int pick = ahead, step = ai.ov_ahead;
    if (ai.ov_ahead <= ai.ov_behind) {
        pick = behind;
        step = ai.ov_behind;
        if (ai.ov_behind <= ai.ov_ahead) { // equal ratings: present one, else the nearer
            if (behind < 0) {
                pick = ahead;
                step = ai.ov_ahead;
                if (ahead < 0) {
                    pick = -1;
                    step = 0;
                }
            } else if (ahead >= 0) {
                const int32_t self = r.bike.i32(0x1c);
                const int32_t db = std::abs(self - riders_[size_t(behind)].bike.i32(0x1c));
                const int32_t da = std::abs(self - riders_[size_t(ahead)].bike.i32(0x1c));
                if (da <= db) {
                    pick = ahead;
                    step = ai.ov_ahead;
                }
            }
        }
    }
    // The cop only ever chases a live bike (0x00432500: +0xe4 callback set),
    // and drops the human player (+0x324 == 0) once the race outcome
    // DAT_004c5d7c is 4, which the arrest sets (0x00416010(4, 0xb4)).
    if (r.cop && pick == 0 && busted_) {
        pick = -1;
        step = 0;
    }
    ai.target_rider = pick;
    ai.target_step = step;
    if (r.cop) return; // 0x004082f0: the cop keeps its pursuit speed words
    if (level_ >= 0 && level_ < 5) {
        ai.current_speed = ai.spd_b[size_t(level_)];
        ai.current2 = ai.spd_a[size_t(level_)];
    }
    const Rider& player = nearest_human(r.bike.i32(0x1c)); // plan 34: the nearest human
    const int32_t self = r.bike.i32(0x1c);
    if (ahead == 0 || behind == 0) {
        int32_t d = (self - player.bike.i32(0x1c)) >> 8;
        if (d < -9)
            d = -9;
        else if (d > 9)
            d = 10;
        ai.current_speed = ((tables_.rubber_band[size_t(d + 9)] + 100) * ai.current_speed) / 100;
    } else if (pick < 0 || step != 3) {
        const int32_t v = ai.current_speed;
        switch ((ahead < 0 ? 0 : 2) | (behind >= 0 ? 1 : 0)) {
        case 0:
            if (self <= player.bike.i32(0x1c))
                ai.current_speed = div_pow2(v, 2) + v;
            else
                ai.current_speed = ((level_ + 0x2c) * v * 2) / 100;
            break;
        case 1:
            ai.current_speed = div_pow2(v, 4) + v;
            break;
        case 2:
            ai.current_speed = ((level_ + 0x5e) * v) / 100;
            break;
        default:
            if (self <= player.bike.i32(0x1c))
                ai.current_speed = div_pow2(v, 5) + v;
            else
                ai.current_speed = (((level_ - 6) / 2 + 100) * v) / 100;
            break;
        }
    } else {
        ai.current_speed = div_pow2(ai.current_speed, 3) + ai.current_speed;
    }
    if (player.bike.i32(0x2f4) != 0 && std::abs(player.bike.i32(0x1c) - self) > 0xa00) {
        if (level_ == 3)
            ai.current_speed = div_pow2(int64_t(ai.current_speed) * 3, 2);
        else if (level_ != 4)
            ai.current_speed /= 2;
    }
}

// 0x00409510: compare the current animation with all four weapon variants.
int Race::ai_attack_anim(const Rider& r) const {
    if (!attack_codes_) return r.attack.timer != 0;
    for (const auto& side : *attack_codes_)
        for (const auto& variant : side)
            if (variant[size_t(r.attack.weapon & 3)] == r.ride_animation.animation) return 1;
    return 0;
}

// 0x00408d50: relationship reaction, saturated independently per axis.
void Race::ai_relationship(Rider& self, const Rider& other, int event) {
    const int k = other.brain.overtake_index;
    if (std::getenv("RR_COMBAT_TRACE"))
        std::fprintf(stderr, "reaction %d self %td other %td k %d event %d\n", ticks_, &self - riders_.data(),
                     &other - riders_.data(), k, event);
    if (k < 0 || k >= 10) return;
    for (int j = 0; j < 3; ++j)
        self.brain.rating[k * 3 + j] = std::clamp(
            self.brain.rating[k * 3 + j] + self.brain.reaction[event * 3 + j], -0x100000, 0x100000);
}

// 0x004095d0: side from the target half, notify both brains, select animation.
void Race::ai_start_attack(Rider& r, bool backhand) {
    Rider* t = r.brain.target_rider < 0 ? nullptr : &riders_[size_t(r.brain.target_rider)];
    r.attack.backhand = backhand;
    r.attack.right =
        t ? r.bike.i32(0x28) < (live_schedule_ ? t->half_mirror.lateral : half_x(*t)) : r.bike.i32(0x60) >= 0;
    if (!ride_library_ || !attack_codes_) {
        r.attack.timer = 1;
        r.attack.fired = false;
        return;
    }
    const int8_t code = (*attack_codes_)[r.attack.right][backhand][r.attack.weapon & 3];
    if (t && r.ride_animation.animation != code) {
        const int k = r.brain.overtake_index;
        if (k >= 0 && k < 10) {
            const int i = k * 3;
            if (t->brain.rating_tgt[i + 1] - t->brain.rating_tgt[i] + t->brain.rating_tgt[i + 2] ==
                t->brain.rating[i + 1] - t->brain.rating[i] + t->brain.rating[i + 2])
                ai_relationship(*t, r, 1);
        }
        ai_relationship(*t, r, 0);
        ai_relationship(r, *t, 3);
    }
    set_animation_with_fallback(r.ride_animation, ride_bank(r), code, uint32_t(ticks_),
                                ride_library_->fallback);
    r.attack.fired = false;
}

// 0x004092b0: true while the rider is fully behind its target.
bool Race::ai_behind_target(const Rider& r) const {
    if (r.brain.target_rider < 0) return false;
    const Rider& t = riders_[size_t(r.brain.target_rider)];
    return r.bike.i32(0x1c) + r.bike.i32(0x178) < t.bike.i32(0x1c) - t.bike.i32(0x178);
}

// 0x004092e0: overtake eligibility against the target (position and lateral
// reach scaled by brain+0x1d4, then a level-scaled random draw).
bool Race::ai_can_overtake(Rider& r) {
    if (r.brain.target_rider < 0) return false;
    const Rider& t = riders_[size_t(r.brain.target_rider)];
    if (!(uint32_t(r.hit_until) < uint32_t(ticks_) && uint32_t(t.hit_until) < uint32_t(ticks_))) return false;
    const int32_t reach = r.brain.reach;
    // The target's half words (param_2 +0x2fc); the live scheduler keeps
    // them apart from its bike.
    const int32_t tspeed = live_schedule_ ? t.half_mirror.speed : t.bike.i32(0xec);
    const int32_t tpos = live_schedule_ ? t.half_mirror.position : t.bike.i32(0x1c);
    const int32_t tlat = live_schedule_ ? t.half_mirror.lateral : t.bike.i32(0x28);
    const int32_t dpos = ((r.bike.i32(0xec) - tspeed) >> 4) - tpos + r.bike.i32(0x1c);
    if (std::abs(dpos) >= reach * 0x10) return false;
    const int32_t dlat = r.bike.i32(0x28) - tlat;
    if (std::abs(dlat) >= reach * 0x2000) return false;
    // Level-scaled draw (0x00465a10[level] * dt vs rand&0xff). The +0x7c rating
    // comparison past this needs the cops target (open); without cops the
    // draw decides.
    int32_t table = 0;
    if (level_ >= 0 && level_ < 5 && overtake_eagerness_)
        table = (*overtake_eagerness_)[size_t(level_)] * r.bike.i32(0x18);
    const uint32_t draw = random() & 0xff;
    if (table - int32_t(draw) == 0 || int32_t(draw) > table) return false;
    if (!r.cop) return true;
    // 0x004092e0 cop tail: only with the half's latched weapon +0x1d4 == 1,
    // and then when the target's relationship target (+0x7c) is below the
    // current (+4) in the target's personality column.
    if (r.attack.weapon != 1) return false;
    const int k = t.brain.overtake_index;
    return k < 10 && r.brain.rating_tgt[size_t(k * 3 + 1)] < r.brain.rating[size_t(k * 3 + 1)];
}

// 0x0040a4a0: true when the target is within 0xf0 ahead (adjusted for lengths).
bool Race::ai_fight_range(const Rider& r) const {
    if (r.brain.target_rider < 0) return false;
    const Rider& t = riders_[size_t(r.brain.target_rider)];
    const int32_t ahead_t = t.bike.i32(0x1c) + t.bike.i32(0x178);
    const int32_t self_pos = r.bike.i32(0x1c) - r.bike.i32(0x178);
    if (ahead_t < self_pos) {
        const int32_t gap = self_pos - ahead_t;
        if (gap < 0xf0) {
            const int32_t dlat = r.bike.i32(0x28) - t.bike.i32(0x28);
            return (uint32_t)(dlat < 0 ? -dlat : dlat) >> 8 < 0x80;
        }
    }
    return false;
}

// 0x00408df0: latch a new AI state, resetting the skid byte, the per-state
// timer and the target object when the state actually changes.
static void ai_set_state(AiState& ai, int state, int target) {
    if (ai.state != state) {
        ai.state = state;
        ai.state_flag = 0;
        if (state >= 0 && state < int(ai.state_ticks.size())) ai.state_ticks[size_t(state)] = 0;
        ai.target_rider = target;
    }
}

// 0x00408ad0: per-tick AI state transitions from crash state, target presence,
// proximity and overtake eligibility.
void Race::ai_transition(Rider& r) {
    AiState& ai = r.brain;
    const int32_t crash = r.bike.i32(0x2f4);
    if (crash == 0) {
        if (ai.target_rider < 0 || !riders_[size_t(ai.target_rider)].bike_slice_present) {
            ai_set_state(ai, 1, -1);
            return;
        }
        if (ai_attack_anim(r) != 0) return;
        if (ai.target_step == 1) {
            ai_set_state(ai, ai_behind_target(r) ? 2 : 1, ai.target_rider);
            return;
        }
        if (ai.target_step == 2) {
            // Lane index of a rider within its slice: (lateral - centre) in
            // lane units of 0xfa, bounded by the slice lane count.
            auto lane_index = [&](const Rider& q, int32_t& idx) {
                const auto& route = route_of(q);
                const size_t at = std::min(q.sample, route.size() - 1);
                const int32_t centre = route[at].offset * 256;
                const int32_t v = q.bike.i32(0x28) - centre;
                idx = int32_t((v + ((v >> 31) & 0xff)) >> 8) / 0xfa - (v < 0 ? 1 : 0);
                if (idx < 0) return false;
                const int32_t lanes = route[at].path.right_q8 >> 8;
                return idx < lanes;
            };
            // Two independent tests: own lane on the road and 0x004092e0 -> 3;
            // else the target's lane on the road and 0x0040a4a0 -> 5; else 1.
            int32_t self_lane = 0, tgt_lane = 0;
            if (lane_index(r, self_lane) && ai_can_overtake(r)) {
                ai_set_state(ai, 3, ai.target_rider);
                return;
            }
            if (lane_index(riders_[size_t(ai.target_rider)], tgt_lane) && ai_fight_range(r)) {
                ai_set_state(ai, 5, ai.target_rider);
                return;
            }
            ai_set_state(ai, 1, ai.target_rider);
            return;
        }
        if (ai.target_step == 3) {

            if (ai_can_overtake(r))
                ai_set_state(ai, 3, ai.target_rider);
            else
                ai_set_state(ai, 4, ai.target_rider);
            return;
        }
        return;
    }
    if (crash != 2 && crash != 3)
        ai_set_state(ai, 9, -1);
    else
        ai_set_state(ai, 8, -1);
}
// 0x004091f0: straight target from the lane preference a slice ahead.
void Race::ai_straight_target(Rider& r) {
    Bike& b = r.bike;
    AiState& ai = r.brain;
    const auto& route = route_of(r);
    const size_t next = std::min(r.sample + 1, route.size() - 1);
    const int32_t lanes = route[next].path.right_q8 >> 8;
    int32_t lane = 0;
    if (ai.lane == 1)
        lane = (lanes - 1) / 2;
    else if (ai.lane == 2)
        lane = std::abs(lanes - 1);
    ai.target_position = b.i32(0x1c) + 0x3c0;
    ai.target_x = div_pow2(curvature(r, next, 8), 3) + (route[next].offset + lane * 0xfa) * 0x100 + ai.bias;
    ai.target_speed = ai_curve_limit(r);
}

// 0x004090a0: take the inside of the curve when committed to the current
// target (position within 0x3c0 of it); else fall back to the straight target.
void Race::ai_curve_target(Rider& r) {
    Bike& b = r.bike;
    AiState& ai = r.brain;
    if ((b.i32(0x1c) >> 8) - (ai.target_position >> 8) != -0x3c0) {
        const auto& route = route_of(r);
        const int32_t c = curvature(r, r.sample, 8);
        const uint32_t mag = uint32_t(c < 0 ? -c : c);
        if (mag < 0x30000) {
            ai_straight_target(r);
            return;
        }
        const int32_t shown = ai_speed_units(b.i32(0xec));
        const int32_t lim = ai_curve_limit(r);
        if (shown < lim / 2) {
            ai_straight_target(r);
            return;
        }
        const auto& s = route[r.sample];
        const int32_t left_edge = s.offset - s.path.left;
        const int32_t half = (s.path.right_q8 / 2) * 0x100;
        uint32_t m = mag > 0x60000 ? 0x60000 : mag;
        int32_t off = int32_t((int64_t((m << 8) / 0x60000) * half) >> 8);
        if (c < 0) off = -off;
        ai.target_position = b.i32(0x1c) + 0x3c0;
        ai.target_x = off + (left_edge + s.path.left_q8) * 0x100 + half;
        ai.target_speed = lim;
        const int32_t x = b.i32(0x28);
        if (c < 1) {
            if (x < ai.target_x) ai.target_x = x;
        } else if (ai.target_x < x)
            ai.target_x = x;
    }
}

// 0x00409f70: steering and speed control toward the target point.
void Race::ai_speed_steer(Rider& r) {
    Bike& b = r.bike;
    AiState& ai = r.brain;
    const int8_t skid = int8_t(b.u8(0x164));
    const int32_t k = skid < 41 ? 1 : 4;
    const int32_t lead = (-1 - div_pow2(b.i32(0x18), 3)) * k * b.i32(0x288) * 4;
    const int32_t error = (lead - b.i32(0x28)) + ai.target_x;
    uint32_t angle = uint32_t(approx_atan2(ai.target_position - b.i32(0x1c), div256(error)) & 0xff) * 0x10000;
    int32_t steer_angle = int32_t(angle);
    if (steer_angle > 0x7fffff) steer_angle -= 0x1000000;
    if (b.i32(0x160) != 0 || std::abs(b.i32(0x60)) > 0xd) steer(b, tables_, steer_angle * 8, 0);
    const int32_t shown = ai_speed_units(b.i32(0xec)), want = ai.target_speed;
    if (shown < want && (skid < 41 || shown < want / 2))
        drive_mode(b, 1);
    else if (shown <= want)
        drive_mode(b, 0);
    else
        drive_mode(b, 2);
}

// 0x00449e90: nearest rider closing on self within the lateral window,
// scored by time-to-meet. Returns a riders_ index or -1 with the gap.
// Candidates are scanned in lateral order off self's ring successor, exactly
// as the 0x00449da0 slice-order walk yields them; the caller (0x00408e40)
// passes 0x3f/0/0x96/0x960/0xc000.
int Race::ai_scan_overtake(const Rider& r, int32_t& gap) const {
    build_scan_list();
    const int me = int(&r - riders_.data());
    const int start = scan_find(me, 4);
    if (start < 0) return -1;
    const ScanObject self = scan_[size_t(start)];
    if (std::getenv("RR_SCAN_TRACE") && ticks_ == std::atoi(std::getenv("RR_SCAN_TRACE")))
        for (const auto& o : scan_)
            std::fprintf(
                stderr,
                "scan tick %d rider %d type %u rider %d car %d pos %d key %d lat %d spd %d self %d/%d/%d\n",
                ticks_, me, o.type, o.rider, o.car, o.position, o.key, o.lateral, o.speed, self.position,
                self.lateral, self.speed);
    const int32_t lo = self.lateral - 0xc000 / 2;
    int best = -1;
    int32_t best_t = 0x96 + 1;
    int at = start;
    for (;;) {
        do {
            at = scan_next(at, 0x3f, true, 0x960);
            if (at < 0) return best;
        } while (scan_[size_t(at)].rider == me && scan_[size_t(at)].type == 8); // own half
        const ScanObject& o = scan_[size_t(at)];
        const int32_t size = object_size(o.type);
        const int32_t left = o.lateral - size / 2;
        if (size + left <= lo) return best;   // wholly left of the window: stop
        if (lo + 0xc000 <= left) return best; // wholly right: stop
        uint32_t closing = uint32_t(self.speed);
        if (o.type & 0x3c) closing -= uint32_t(o.speed);
        const int32_t c = int32_t(closing);
        if (best < 0 || (c > 0 && self.position < o.position) || (c < 0 && o.position < self.position)) {
            if (c == 0) {
                gap = 0x7fffffff;
                best_t = 0x7fffffff;
                best = at;
            } else {
                const int32_t t = (std::abs(self.position - o.position) * 0x100) / std::abs(c);
                if (t <= 0x96 && t < best_t) {
                    gap = t;
                    best = at;
                    best_t = t;
                }
            }
        }
    }
}
// 0x00408e40: overtake gate. Latches the scanned rider and steers off it when
// closing; returns true when a target was latched (the caller then skips its
// own target choice). The size table is 0x0044a110 on the node type.
bool Race::ai_overtake(Rider& r) {
    Bike& b = r.bike;
    AiState& ai = r.brain;
    int32_t gap = 0;
    const int at = ai_scan_overtake(r, gap);
    ai.overtake_latched = -1;
    ai.overtake_type = 0;
    ai.overtake_position = 0;
    if (at < 0) return false;
    const ScanObject o = scan_[size_t(at)];
    if (std::getenv("RR_FIELD_BRAINS"))
        std::fprintf(stderr, "OVERTAKE by %td latch type %d rider %d ped %d car %d pos %d\n",
                     &r - riders_.data(), int(o.type), o.rider, o.pedestrian, o.car, o.position);
    // Latching the replan target itself (+0x168 is a bike) counts as none;
    // either way the low byte returned is 0.
    if (o.type == 4 && ai.target_rider >= 0 && o.rider == ai.target_rider) return false;
    ai.overtake_latched = o.rider;
    ai.overtake_type = o.type;
    ai.overtake_position = o.position;
    // Pass on the side away from self, half the summed sizes plus 0x5000 off
    // the object; flip when that point leaves the object's slice lanes.
    int32_t side = (object_size(o.type) + object_size(4)) / 2 + 0x5000;
    if (b.i32(0x28) <= o.lateral) side = -side;
    const RouteSample& s = *o.sample;
    const int32_t v = o.lateral - s.offset * 0x100 + side;
    const int32_t lane = int8_t(int8_t(div_pow2(v, 8) / 0xfa) - (v < 0 ? 1 : 0));
    const int32_t right = div_pow2(s.path.right_q8, 8), left = div_pow2(s.path.left_q8, 8);
    if (right <= lane || (-lane != left && lane <= -left)) side = -side;
    ai.target_position = o.position + 0x3c0;
    ai.target_x = o.lateral + side;
    if (gap > 0x24 && std::abs(b.i32(0x1c) - o.position) > 799) {
        const int32_t lim = ai_curve_limit(r);
        ai.target_speed = gap < 0x4b ? lim - div_pow2(lim, 2) : lim;
        return true;
    }
    // 0x00449fe0: heading to the object, (atan & 0xff) << 16, wrapped.
    uint32_t angle =
        uint32_t(approx_atan2(o.position - b.i32(0x1c), div_pow2(o.lateral - b.i32(0x28), 8)) & 0xff) *
        0x10000;
    if (angle > 0x7fffff) angle -= 0x1000000;
    if (std::abs(int32_t(angle)) < 0xf) ai.target_speed = ai_curve_limit(r) / 2;
    return true;
}

void Race::ai_think(Rider& r) {
    // 0x00407bd0: transitions (0x00408ad0), state dispatch through 0x00465980,
    // the state's tick counter, then the replan accumulator (0x004082f0 once
    // it passes 0x3b). The 0x0040a410 lookahead is not ported.
    if (std::getenv("RR_SIM_TRACE_TICK") && ticks_ == std::atoi(std::getenv("RR_SIM_TRACE_TICK"))) {
        std::fprintf(stderr,
                     "ai %td state %d target %d anim %d frame %d queued %d deadline %u weapon %d until %d "
                     "pos %d lat %d spd %d\n",
                     &r - riders_.data(), r.brain.state, r.brain.target_rider, r.ride_animation.animation,
                     r.ride_animation.frame, r.ride_animation.queued, r.ride_animation.deadline,
                     r.attack.weapon, r.hit_until, int32_t(r.bike.i32(0x1c)), int32_t(r.bike.i32(0x28)),
                     int32_t(r.bike.i32(0xec)));
    }
    // 0x00407bd0 head: the cop half posts the siren event with its position and
    // speed, or an empty record while it is crashed.
    if (r.cop) siren_event(r, r.bike.i32(0x2f4) == 0);
    ai_transition(r);
    switch (r.brain.state) {
    case 1:
    case 2:
        ai_state_racing(r);
        break; // 0x00409000 (2: thunk 0x004092d0)
    case 3: {  // 0x00409460: follow, attack selection/hit, then steer again.
        ai_state_follow(r);
        if (!ai_attack_anim(r)) {
            if (int(random()) % r.brain.kick_roll == 0 && !r.cop) {
                r.attack.weapon = 3;
                ai_start_attack(r, false);
            } else {
                r.attack.weapon = r.weapon;
                ai_start_attack(r, int(random()) % r.brain.backhand_roll == 0);
            }
        } else if (fight_hit_frame(r))
            resolve_hit(r, size_t(&r - riders_.data()));
        ai_speed_steer(r);
        break;
    }
    case 4:
        ai_state_follow(r);
        break; // 0x00409ca0
    case 5:
        ai_state_fight(r);
        break; // 0x0040a4f0
    case 6:
        ai_state_shadow(r);
        break; // 0x0040a560
    default:
        break; // 0: 0x00408e30 does nothing
    }
    AiState& ai = r.brain;
    // The state clock and replan accumulator add the half's own dt
    // (0x00407bd0 reads +0x18 of the rider half); the live scheduler keeps
    // it apart from the bike's, every-tick stepping shares the bike's.
    const int32_t dt = live_schedule_ ? r.half_dt : r.bike.i32(0x18);
    if (ai.state >= 0 && ai.state < int(ai.state_ticks.size())) ai.state_ticks[size_t(ai.state)] += dt;
    ai.replan_ticks += dt;
    if (ai.replan_ticks > 0x3b) ai_replan(r);
    // 0x00407bd0 regenerates after dispatch. The demo player's outer
    // 0x00434660 wrapper also regenerates before entering this function.
    if (ride_library_) {
        const int crash = r.bike.i32(0x2f4);
        const int passes = crash == 1 ? 0 : crash == 0 ? 1 : 2;
        for (int i = 0; i < passes; ++i)
            r.stamina.current = std::min(r.stamina.max, r.stamina.current + r.bike.i32(0x18));
    }
}

// Racing states 1 and 2 (0x00409000): overtake gate, then the inside-curve or
// straight target choice, then steering and speed control (0x00409f70).
void Race::ai_state_racing(Rider& r) {
    if (!ai_overtake(r)) ai_curve_target(r);
    ai_speed_steer(r);
}

// Follow state 4 (0x00409ca0; state 3 runs it first): close on the target's
// half alongside, matching its speed through 0x00449a80, unless it is far
// ahead (then race). The police rider uses a 500 window and the downed-target caps.
void Race::ai_state_follow(Rider& r) {
    Bike& b = r.bike;
    AiState& ai = r.brain;
    if (ai.target_rider < 0) {
        if (r.cop) siren_event(r, false); // 0x00409ca0: no target silences the siren
        ai_state_racing(r);
        return;
    }
    const Rider& t = riders_[size_t(ai.target_rider)];
    // 0x00409ca0 reads the target's half (+0x168 -> +0x2fc): position,
    // lateral, slice and speed; the live scheduler keeps them apart.
    const int32_t tpos = live_schedule_ ? t.half_mirror.position : t.bike.i32(0x1c);
    const int32_t tlat = live_schedule_ ? t.half_mirror.lateral : half_x(t);
    const size_t tsample = live_schedule_ ? t.half_mirror.sample : t.sample;
    const int32_t dpos = tpos - b.i32(0x1c);
    const int32_t dist = std::abs(dpos) >> 4;
    const int32_t window = r.cop ? 500 : 0xa0;
    if (dpos > 0 && window < dist) {
        ai_state_racing(r);
        return;
    }
    if (ai_overtake(r)) {
        ai_speed_steer(r);
        return;
    }
    // Target half speed: +0x108 for the player (+0x324 == 0), else +0xec.
    const int32_t tspeed = live_schedule_
                               ? (ai.target_rider == 0 ? t.half_mirror.lat108 : t.half_mirror.speed)
                           : ai.target_rider == 0 ? t.bike.i32(0x108)
                                                  : t.bike.i32(0xec);
    const bool alongside = (dist << 4) < b.i32(0x178) * 4;
    if (alongside) {
        int32_t off = std::min(dist * 0x1000 + 14000, 28000);
        const auto& road = route_of(t);
        const RouteSample& ts = road[std::min(tsample, road.size() - 1)];
        const int32_t v = off - ts.offset * 0x100 + tlat;
        if (tlat < b.i32(0x28)) {
            const int32_t lane = div_pow2(v, 8) / 0xfa - (v < 0 ? 1 : 0);
            if (lane >= 0 && lane < div_pow2(ts.path.right_q8, 8)) off = -off;
        }
        ai.target_position = tpos + 0x3c0;
        ai.target_x = tlat - off;
    } else {
        ai_curve_target(r);
    }
    const int32_t speed = b.i32(0xec);
    // 0x00449a80 mode 0: gap (Q8 >> 4) plus 1, clamped speeds, step 5.
    const int32_t shown = ai_speed_units(speed);
    int32_t d = ((tpos - b.i32(0x1c)) >> 4) + 1;
    int32_t want = ai.current_speed;
    if (d <= window) {
        want = ai.current2;
        if (-window == d || -d < window) {
            if (d < 0) d = div_pow2(d, 3);
            want = d + ai_speed_units(tspeed);
            if (ai.current_speed < want)
                want = ai.current_speed;
            else if (want < ai.current2)
                want = ai.current2;
        }
    }
    if (want - shown <= 5) {
        if (5 < shown - want) want = shown - 5;
    } else
        want = shown + 5;
    ai.target_speed = want;
    if (std::abs(tspeed - speed) < 2000 && alongside && speed > 4000)
        b.i32(0xec) = ai.target_speed * 100;
    else if (r.cop) {
        // 0x00409ca0: the chaser stops for a downed target and slows for one on foot.
        const int crash = t.bike.i32(0x2f4);
        if (crash == 1)
            ai.target_speed = 0;
        else if (crash != 0)
            ai.target_speed = std::min(ai.target_speed, 0x14);
    }
    ai_speed_steer(r);
}

// Shadow state 6 (0x0040a560): sit on the target at the current speed.
void Race::ai_state_shadow(Rider& r) {
    AiState& ai = r.brain;
    if (!ai_overtake(r) && ai.target_rider >= 0) {
        const Rider& t = riders_[size_t(ai.target_rider)];
        ai.target_speed = ai.current_speed;
        ai.target_position = t.bike.i32(0x1c);
        ai.target_x = t.bike.i32(0x28);
    }
    ai_speed_steer(r);
}

// Fight state 5 (0x0040a4f0): overtake gate, else straight target snapped to
// the target's lateral unless too far ahead, then steering and speed control.
void Race::ai_state_fight(Rider& r) {
    AiState& ai = r.brain;
    if (!ai_overtake(r)) {
        ai_straight_target(r);
        if (ai.target_rider >= 0) {
            const Rider& t = riders_[size_t(ai.target_rider)];
            const int32_t tlat = t.bike.i32(0x28);
            // ...unless the rider is the police chaser, which always snaps.
            if (std::abs(ai.target_x - tlat) > 0x8fff && !r.cop) {
                ai_state_racing(r);
                return;
            }
            ai.target_x = tlat;
        }
    }
    ai_speed_steer(r);
}

void Race::step_frozen_schedule(Rider& r) {
    // 0x00437e60 head + LAB_00438108: last/next/dt evolve on every scheduled
    // run even with NULL think/move; only the interval recompute is skipped.
    // Same gate shape as the traffic/pedestrian cadence. Runs only for riders
    // hydrated from sched.* capture keys; legacy captures and live games keep
    // sched_live false and the build dt 1. No RNG, no allocation.
    if (!r.sched_live || r.sched_next > ticks_) return;
    r.bike.i32(0x18) = ticks_ - r.sched_last;
    r.sched_last = ticks_;
    r.sched_next = ticks_ + r.sched_interval;
    // 0x00438108..0x00438159: the window test on the scheduled run (ring
    // cursors as of the previous advance) parks a rider/half outside it:
    // +0xc4 & 0xc (bikes) uses 0x1e00 front / 0x800 back, else 0/0 (unlink).
    // 0x00406940 backs up +0xdc/+0xe0 to +0x31c/+0x320 and nulls draw, think,
    // the +0x2c slices and +0xc0 & 0xfc on both the bike and its half.
    if (r.frozen && !r.parked && ring_) {
        if (outside_windows(r.bike.i32(0x1c), 0x1e00, 0x800)) {
            r.parked = true;
            r.bike_slice_present = r.half_slice_present = false;
            r.bike.u8(0xc0) &= 0xfc;
            r.half_object.u8(0xc0) &= 0xfc;
        }
    }
}
void Race::step_rider(Rider& r, uint32_t controls, bool player) {
    Bike& b = r.bike;
    if (r.frozen) return; // solo oracle: scheduler metadata steps post-loop, never think/move
    if (r.finished) {     // coast to a stop past the line
        drive_mode(b, 0);
        integrate_speed(b, tables_, 0);
        return;
    }
    if (crash_library_ && b.i32(0x2f4)) {
        if (!r.crash_active) crash(r);
        // 0x00407a90 calls 0x0040b1a0 for the player (DAT_004642d8 check at
        // 407a91-407aa7) before the crash-state branch, so a crashed player
        // still ticks nitro while AI riders never do.
        if (player) nitro_charge_tick(r);
        step_crash(r, player, controls);
        if (player) police_think(r);
        return;
    }
    // 0x00434660 head: demo hold (or race-not-started) takes the AI branch;
    // the control word is ignored. fight() with zero controls is exactly the
    // branch's stamina regen with no attacks started.
    const bool demo_player = demo_hold_ && player;
    // 0x00407a90 scenery-mark flash for the solo every-tick loop: the live
    // scheduler runs this in run_bike_node, and step_crash runs it for
    // crashed riders, but an uncrashed rider in solo otherwise never
    // refreshes +0x1e9 (split-left 1423: native stuck at 1, original 2).
    // Same disassembly form (407acd-407b0b) as crash_bike_animation.
    if (!live_schedule_ && b.i32(0x2f4) != 1 && b.i32(0x2f4) != 2) {
        auto& half = r.half_object;
        if (half.u8(0x1e9)) {
            const int32_t pos_sample = b.i32(0x1c) >> 8;
            const uint32_t elapsed = uint32_t(ticks_) - uint32_t(half.i32(0x1f0));
            half.u8(0x1e9) =
                (pos_sample == half.i32(0x1ec) && elapsed < 12) ? uint8_t((elapsed >> 2) + 1) : 0;
        }
        b.u8(0x81) = half.u8(0x1e9);
    }
    fight(r, demo_player ? 0 : controls, size_t(&r - riders_.data()));
    if (player) nitro_charge_tick(r);
    rider_think(r, controls, player);
    rider_move(r, player);
}
// The 0x00434660 / 0x00407bd0 think body after the fight step: AI or
// player controls, the crashed slide, then the riding animation.
void Race::rider_think(Rider& r, uint32_t controls, bool player) {
    Bike& b = r.bike;
    const bool demo_player = demo_hold_ && player;
    if (b.i32(0x2f4) == 0) {
        if (r.ai || demo_player)
            ai_think(r);
        else {
            // 0x00434660: nitro activate/stop, then drive (0x0040acd0) and
            // steer (0x0040ad30). Boost multiplies the throttle force
            // (0x0040adb0); nitro_throttle records the post-boost force.
            nitro_think(r, controls, player);
            if ((b.u8(0xc0) & 0x10) == 0) {
                if (controls & control_accelerate) drive_mode(b, 1);
                if (controls & control_brake)
                    drive_mode(b, 2);
                else if (!(controls & control_accelerate))
                    drive_mode(b, 0);
            } else {
                drive_mode(b, 2);
            }
            if ((b.u8(0xc0) & 0x20) != 0) {
                b.i32(0x114) = nitro_throttle_force(r, b.i32(0x114));
                r.nitro_throttle = b.i32(0x114);
            } else {
                r.nitro_throttle = b.i32(0x114);
            }
            const int32_t lean = (controls & control_lean_up) ? 1 : ((controls & control_lean_down) ? -1 : 0);
            const int mode = (controls & control_left) ? 1 : ((controls & control_right) ? 2 : 0);
            steer_input(b, tables_, mode, lean);
        }
    } else {
        // Crashed: slide to a stop, wait one second, remount at the nearest lane.
        b.i32(0x114) = 0;
        b.i32(0xf0) = 0;
        b.i32(0x288) = 0;
        const int32_t v = b.i32(0xec);
        b.i32(0xec) = v > 0 ? std::max(0, v - v / 32 - 20) : 0;
        if (b.i32(0xec) == 0 && ++r.crash_timer > ticks_per_second) {
            const SliceEdges e = edges(r, r.sample);
            int32_t x = b.i32(0x28) >> 8;
            if (x < e.left + 125) x = e.left + 125;
            if (x > e.right - 125) x = e.right - 125;
            b.i32(0x28) = x * 256;
            b.i32(0x2f4) = 0;
            b.u8(0xc0) &= uint8_t(~0x10);
            b.i32(0x240) = 0;
            b.i32(0x23c) = 0;
            b.i32(0x104) = 0;
            b.i32(0x10c) = 0;
            b.u8(0x278) = 0;
        }
    }
    // 0x00407f30 runs at the end of the think (0x00434660 / 0x00407bd0),
    // before the physics move: selection reads the previous tick's speed.
    if (player) police_think(r);
    advance_ride_animation(r, player);
}
// The bike physics move (0x0043a580 from 0x004060b0's live branch).
void Race::rider_move(Rider& r, bool player) {
    Bike& b = r.bike;
    const int surface = surface_class(edges(r, r.sample), b.i32(0x28));
    traction(b, tables_, surface, player);
    if (b.i32(0x2f4) != 0 && !(b.u8(0xc0) & 0x10)) crash(r); // skid crash raised by traction
    integrate_speed(b, tables_, surface);
    shift_gears(b);
    move(r, player);
    walls(r);
    vertical(r);
}

// 0x00436830(0x13, half +0x1c, half +0xec) or (0x13, NULL, NULL).
void Race::siren_event(const Rider& cop, bool emitting) {
    if (sound_events_.size() >= 16) return;
    if (!emitting) {
        sound_events_.push_back({SoundEvent::siren, 0, 0, 0, 0, 0, int32_t(&cop - riders_.data())});
        return;
    }
    const Bike& b = cop.bike;
    const HalfMirrorView h = live_schedule_
                                 ? HalfMirrorView{cop.half_mirror.lateral, cop.half_mirror.height,
                                                  cop.half_mirror.position, cop.half_mirror.speed}
                                 : HalfMirrorView{b.i32(0x28), b.i32(0x20), b.i32(0x1c), b.i32(0xec)};
    sound_events_.push_back(
        {SoundEvent::siren, h.lateral, h.height, h.position, h.speed, 0, int32_t(&cop - riders_.data())});
}

void Race::sound(SoundEvent e, const Bike& b) {
    if (sound_events_.size() >= 16) return;
    int32_t source = -1;
    for (size_t i = 0; i < riders_.size(); ++i)
        if (&riders_[i].bike == &b || &riders_[i].crash_actor.motion == &b) source = int32_t(i);
    if (source < 0 && pedestrians_)
        for (size_t i = 0; i < pedestrians_->pool().size(); ++i)
            if (&pedestrians_->pool()[i].body.bike == &b) source = 3000 + int32_t(i);
    sound_events_.push_back({e, b.i32(0x28), b.i32(0x20), b.i32(0x1c), b.i32(0xec), 0, source});
}

void Race::build_draws() {
    // Player first, then each opponent: 0x00405990 draws the lane, then its
    // 0x00405c80 resets the brain, places the bike on the straight target
    // and replans against the riders built so far.
    for (size_t i = 0; i < riders_.size(); ++i) {
        Rider& r = riders_[i];
        r.brain.lane = int(random() % 3);
        spawn_brain(r, i + 1);
    }
    name_roll_ = int(random() % 100);
}

// 0x00405c80 (brain and placement part): state/target words cleared, the
// 0x004091f0 straight target, bike lateral = target x, the wreck timers
// +0x30c/+0x310/+0x314, place word 0, then 0x004082f0. Stamina, weapon,
// condition and the standings words are set by the Race constructor.
void Race::spawn_brain(Rider& r, size_t built) {
    AiState& ai = r.brain;
    // The brain is a 0x004c6b40 personality copy (0x00407990): its speed
    // words +0x1a0/+0x1a4 are 0 in every record, so 0x004091f0 below sees
    // current 0 until 0x004082f0 resets it. The copies persist across races
    // in one process, so a later race starts from the previous race's words;
    // the next think recomputes the target, so only an unstepped (human)
    // brain keeps that difference, and the native rebuilds brains per race.
    ai.current_speed = 0;
    ai.current2 = 0;
    ai.state = 0;
    ai.state_flag = 0;
    ai.target_rider = ai.nbr_ahead = ai.nbr_behind = -1;
    ai.target_step = ai.ov_ahead = ai.ov_behind = 0;
    ai.overtake_latched = -1;
    ai.overtake_type = 0;
    ai.overtake_position = 0;
    // Base speed is the record's level word (+0x1bc + level*4), which the
    // copied personality already holds for the player as for opponents.
    if (level_ >= 0 && level_ < 5) ai.base_speed = ai.spd_b[size_t(level_)];
    // Half +0x1d4 (latched attack weapon) starts as the carried weapon +0x1c8.
    r.attack.weapon = r.weapon;
    ai_straight_target(r);
    r.bike.i32(0x28) = ai.target_x;
    r.bike.i32(0x30c) = 600;
    r.bike.i32(0x310) = (((level_ * 5 + 0x2d) * 200) >> 8) * 600;
    r.bike.i32(0x314) = 0;
    r.place = 0;
    built_riders_ = built;
    ai_replan(r);
    built_riders_ = SIZE_MAX;
}

void Race::set_police(const PoliceConfig& config, std::vector<BikeSpec> specs,
                      const ActorAnimationLibrary* library) {
    police_ = PoliceSetup{config, std::move(specs), library};
}

// 0x00406b80 (called with the ring slice from 0x00449910): the cop is built
// by 0x00405990 (templates 0x464310/0x464378, rider row 15, param_5 = 2),
// its brain is the police personality, and the player's relationship toward
// it is replaced from the player's place and the level.
bool Race::spawn_cop(int32_t slice_offset, int anchor_rider) {
    if (!police_ || !police_->library || riders_.empty() || ring_setup_ || cop_index() >= 0) return false;
    if (riders_.size() == riders_.capacity())
        riders_.reserve(riders_.size() + 1); // keeps outstanding references valid only when the ctor reserved
    // Plan 34 C4: the spawn is placed from its anchor human and clamped to
    // that human's ring window (one human: the player and the ring).
    const size_t anchor = anchor_rider >= 0 ? size_t(anchor_rider) : 0;
    const Rider& player = riders_[anchor];
    const TrackRing* window = player.human > 0 && !player.ai ? rings_[size_t(player.human)] : ring_;
    if (!window) window = ring_;
    const auto& road = route_of(player);
    // 0x00449910 from the player's half slice (+0x2c), clamped to the ring.
    int64_t slice = int64_t(live_schedule_ ? player.half_mirror.sample : player.sample) + slice_offset;
    const int64_t lo = window ? int64_t(window->back_q8() >> 8) : 0;
    const int64_t hi = std::min<int64_t>(
        window ? int64_t(window->front_q8() >> 8) - 1 : int64_t(road.size()) - 1, int64_t(road.size()) - 1);
    slice = std::clamp<int64_t>(slice, std::min(lo, hi), hi);
    PoliceSpawnInput in;
    in.level = level_;
    in.player_place = std::clamp(player.place, 0, 15);
    in.player_personality = std::clamp(player.brain.overtake_index, 0, 9);
    in.lane = int(random() % 3); // 0x00405990: the lane draw
    in.position = int32_t(slice) * 256;
    in.lateral_q8 = in.lane_target_q8 = road[size_t(slice)].offset * 256 + road_base_;
    in.tick = uint32_t(ticks_);
    PoliceActor a = make_police_actor(police_->config, tables_, police_->specs, *police_->library, in);
    Rider r;
    r.cop = true;
    r.ai = true;
    r.slot = 15;
    r.rider_idx = 15;
    r.route = player.route;
    r.sample = r.previous_sample = size_t(slice);
    r.bike = a.bike;
    r.half_object = a.rider;
    r.ride_animation = a.rider_animation;
    for (int k = 0; k < 5; ++k) r.bike.i32(0x30 + 4 * k) = r.bike.i32(0x1c + 4 * k);
    r.stamina = {a.stats[12], a.stats[13], a.stats[14], a.stats[15]};
    r.weapon = a.stats[size_t(6 + level_)];
    r.nitro_charge = nitro_.max << 8;
    if (crash_library_) init_crash_actor(r.crash_actor, crash_config_);
    // Brain: the 0x204-byte police personality (config words, relationship
    // columns reset and the player's column replaced by police_pursuit).
    const auto& w = a.pursuit.personality;
    auto byte_at = [&](size_t off) { return int((uint32_t(w[off / 4]) >> (8 * (off & 3))) & 0xff); };
    AiState& ai = r.brain;
    for (int i = 0; i < 5; ++i) {
        ai.spd_a[i] = w[0x1a8 / 4 + size_t(i)];
        ai.spd_b[i] = w[0x1bc / 4 + size_t(i)];
    }
    for (int i = 0; i < 30; ++i) {
        ai.rating[i] = w[size_t(i)];
        ai.rating_tgt[i] = w[0x78 / 4 + size_t(i)];
    }
    for (int i = 0; i < 3; ++i) ai.ease[i] = w[0x12c / 4 + size_t(i)];
    ai.overtake_index = byte_at(0x165);
    ai.kick_roll = int8_t(byte_at(0x1d0));
    ai.backhand_roll = int8_t(byte_at(0x1d1));
    for (int i = 0; i < 15; ++i) ai.reaction[i] = w[0xf0 / 4 + size_t(i)];
    ai.reach = w[0x1d4 / 4];
    ai.word_184 = w[0x184 / 4];
    ai.lane = in.lane;
    riders_.push_back(std::move(r));
    Rider& cop = riders_.back();
    const int index = int(riders_.size()) - 1;
    if (live_schedule_) init_live_rider(cop, 4); // template interval 4 (0x00464310 word 2)
    // 0x00406b80 builds the rider (0x00405990 -> 0x00405c80 -> 0x004082f0)
    // before it stores DAT_004642dc, so the spawn-time replan treats the
    // new rider as an ordinary opponent: scan cap 0xa00, road-edge test,
    // level speed words (traffic-field box capture tick 4096: a cop 39
    // slices behind the player finds no target and starts in state 1).
    cop.cop = false;
    spawn_brain(cop, riders_.size()); // 0x00405c80
    cop.cop = true;
    // 0x00406b80 tail: pursuit speed from +0x1bc[level], overwritten after the build.
    cop.brain.current_speed = w[0x1a4 / 4];
    cop.brain.current2 = w[0x1a0 / 4];
    cop.bike.i32(0xec) = 0;
    if (live_schedule_) live_standings(cop);
    // The player's relationship toward the cop (player brain +0x80/+8 columns).
    const int column = cop.brain.overtake_index;
    if (column >= 0 && column < 10) {
        for (int seat = 0; seat < humans(); ++seat) { // plan 34 C4: every human's row
            Rider& p0 = riders_[size_t(human_rider(seat))];
            if (seat != 0 && !human(p0)) continue;
            p0.brain.rating_tgt[size_t(column * 3 + 2)] = a.player_relationship;
            p0.brain.rating[size_t(column * 3 + 2)] = a.player_relationship;
        }
    }
    schedule_add(index);
    return true;
}

// 0x00407860: from the player's half, scan rider halves backward within
// 0xa00 then forward within 0x500 for the cop's half; an uncrashed cop is
// returned unless the player's bike is flagged 0x10.
int Race::find_cop(const Rider& player) const {
    const int cop = cop_index();
    if (cop < 0) return -1;
    build_scan_list();
    const int from = scan_find(int(&player - riders_.data()), 8);
    for (int pass = 0; pass < 2; ++pass) {
        int at = from;
        for (;;) {
            at = scan_next(at, 8, pass == 1, pass == 0 ? 0xa00 : 0x500);
            if (at < 0) break;
            if (scan_[size_t(at)].rider != cop || riders_[size_t(cop)].bike.i32(0x2f4) != 0) continue;
            return (player.bike.u8(0xc0) & 0x10) == 0 ? cop : -1;
        }
    }
    return -1;
}

// 0x00407f30 (owner 0): the arrest decision, then the periodic cop spawn.
void Race::police_think(Rider& player) {
    if (!police_) return;
    const uint8_t half_flags = player.half_object.u8(0xc0);
    if (half_flags & 0x10) return;
    const Bike& b = player.bike;
    const int cop = find_cop(player);
    PoliceArrestInput in;
    in.owner = 0;
    in.crash = b.i32(0x2f4);
    in.bike_flags = b.u8(0xc0);
    in.rider_flags = half_flags;
    in.found_cop = cop >= 0;
    in.bike_speed = b.i32(0xec);
    // The walking rider (crash states 1/2) is the half object itself.
    in.rider_position = player.crash_active ? player.crash_actor.motion.i32(0x1c)
                        : live_schedule_    ? player.half_mirror.position
                                            : b.i32(0x1c);
    if (cop >= 0) {
        in.cop_speed = riders_[size_t(cop)].bike.i32(0xec);
        in.cop_position = riders_[size_t(cop)].bike.i32(0x1c);
    }
    if (police_arrest(in)) { // 0x00416010(4, 0xb4): the screen sees the outcome once
        player.busted = true;
        if (&player == &riders_[0]) busted_ = true;
    }
    // Plan 34 C4: every human's think runs the periodic check (one cop per
    // race: the first human whose check fires anchors it).
    if (police_spawn_due(uint32_t(ticks_), 0, half_flags, cop_index() >= 0))
        spawn_cop(-0x27, int(&player - riders_.data()));
}

// 0x004073a0: the cop is unlinked and freed. The cop is always the last
// rider, so removing it leaves every other index intact.
void Race::remove_cop() {
    // 0x00437df0 prepass: unlink the cop that 0x004073a0 marked last tick
    // (always the newest rider; riders built later never exist).
    cop_removal_pending_ = false;
    const int index = int(riders_.size()) - 1;
    schedule_.erase(std::remove_if(schedule_.begin(), schedule_.end(),
                                   [&](const ScheduleNode& n) { return n.rider == index; }),
                    schedule_.end());
    riders_.pop_back();
    for (auto& r : riders_) {
        for (int* v :
             {&r.brain.target_rider, &r.brain.nbr_ahead, &r.brain.nbr_behind, &r.brain.overtake_latched})
            if (*v == index) *v = -1;
    }
    if (last_target_ == index) last_target_ = -1;
}

uint32_t Race::random() {
    seed_ = seed_ * 0x343fdu + 0x269ec3u;
    return (seed_ >> 16) & 0x7fff;
}

// Plan 10: field order follows tests/sim_fields.hpp rider_fields. Values are
// read from the same members the game steps, keyed by grid slot.
RaceSnapshot Race::snapshot() const {
    RaceSnapshot out{ticks_, seed_, {}};
    for (const auto& r : riders_) {
        const std::string p = "rider." + std::to_string(r.slot) + ".";
        const Bike& b = r.bike;
        out.fields.push_back({p + "position", b.i32(0x1c)});
        out.fields.push_back({p + "lateral", b.i32(0x28)});
        out.fields.push_back({p + "height", b.i32(0x20)});
        out.fields.push_back({p + "velocity", b.i32(0xf4)});
        out.fields.push_back({p + "speed", b.i32(0xec)});
        out.fields.push_back({p + "gear", b.i32(0x23c)});
        out.fields.push_back({p + "condition", b.i32(0x2f0)});
        out.fields.push_back({p + "crash", b.i32(0x2f4)});
        out.fields.push_back({p + "slice", int64_t(r.sample)});
        out.fields.push_back({p + "flags", b.u8(0xc0)});
        out.fields.push_back({p + "steer", b.i32(0x288)});
        out.fields.push_back({p + "steer_rate", b.i32(0xf0)});
        out.fields.push_back({p + "lat_vel", b.i32(0x10c)});
        out.fields.push_back({p + "ground", b.i32(0x160)});
        out.fields.push_back({p + "stamina_current", r.stamina.current});
        out.fields.push_back({p + "stamina_max", r.stamina.max});
        out.fields.push_back({p + "weapon", r.weapon});
        out.fields.push_back({p + "ai_target_x", r.brain.target_x});
        out.fields.push_back({p + "ai_target_position", r.brain.target_position});
        out.fields.push_back({p + "ai_target_speed", r.brain.target_speed});
        out.fields.push_back({p + "ai_current_speed", r.brain.current_speed});
        out.fields.push_back({p + "ai_base_speed", r.brain.base_speed});
        out.fields.push_back({p + "ai_replan", r.brain.replan_ticks});
        out.fields.push_back({p + "ai_lane", r.brain.lane});
        out.fields.push_back({p + "ai_bias", r.brain.bias});
        out.fields.push_back({p + "anim", int64_t(r.ride_animation.animation)});
        out.fields.push_back({p + "anim_frame", int64_t(r.ride_animation.frame)});
        out.fields.push_back({p + "anim_queued", int64_t(r.ride_animation.queued)});
        out.fields.push_back({p + "anim_loops", int64_t(r.ride_animation.loops)});
        out.fields.push_back({p + "anim_deadline", int64_t(int32_t(r.ride_animation.deadline))});
        out.fields.push_back({p + "attack_weapon", int64_t(r.attack.weapon)});
        out.fields.push_back({p + "steal_until", int64_t(r.steal_until)});
        out.fields.push_back({p + "hit_until", int64_t(r.hit_until)});
        out.fields.push_back({p + "nitro_charge", int64_t(r.nitro_charge)});
        out.fields.push_back({p + "nitro_charges", int64_t(nitro_charges_)});
    }
    // Free-traffic rows (tests/sim_fields.hpp car_fields) for every pool
    // slot, active or not; the replay comparator requires the intended
    // set (missing field = FAIL). No RNG, no allocation in the tick path
    // beyond the field vector the snapshot already builds.
    if (traffic_) traffic_->append_snapshot(out.fields);
    return out;
}

void Race::fight(Rider& r, uint32_t controls, size_t index) {
    Bike& b = r.bike;
    if (r.ai && ride_library_) return; // AI combat and regeneration belong to 0x00407bd0.
    if (b.i32(0x2f4) != 0) {
        r.attack.timer = 0;
        return;
    }
    if (r.stamina.current < r.stamina.max)
        r.stamina.current = std::min(r.stamina.max, r.stamina.current + b.i32(0x18));
    // 0x00434660 / 0x00409510: descriptor-backed swings end when their
    // animation leaves the attack codes. The legacy 18-tick cap can restart
    // a held club swing before its first damage frame.
    if (ride_library_ && attack_codes_ && !ai_attack_anim(r)) r.attack.timer = 0;
    // 0x00434660: latch the attack weapon from the control word, then find
    // the target through 0x00409400/0x004095a0/0x00409c10 (native: nearest
    // rider on the chosen side). Descriptor-frame hits (0x004096f0) fire
    // from fight() via resolve_hit when the riding animation shows frame 2;
    // without a ride bank the legacy tick-7 stand-in preserves the old tests.
    if (r.attack.timer == 0) {
        bool start = false;
        if (!r.ai)
            start = (controls & (control_punch | control_kick | control_backhand)) != 0;
        else {
            // AI attack rule (stand-in for AI states 2..5): strike the player when alongside.
            const Rider& p = nearest_human(b.i32(0x1c)); // plan 34: the nearest human
            const int reach = weapon_reach[r.weapon] * 0x1000;
            start = p.bike.i32(0x2f4) == 0 && std::abs(p.bike.i32(0x1c) - b.i32(0x1c)) < 0x40 &&
                    std::abs(p.bike.i32(0x28) - b.i32(0x28)) < reach &&
                    (ticks_ + int32_t(index) * 7) % 45 == 0;
            controls = control_punch;
        }
        if (start) {
            // 0x00434660: kick uses weapon 3, otherwise the carried weapon.
            r.attack.weapon = (controls & control_kick) ? 3 : r.weapon;
            r.attack.backhand = (controls & control_backhand) != 0;
            // 0x00409400 acquire: nearest rider half within 0x1e0 positions
            // in either direction (nearer wins, behind wins ties), same road
            // half and on a slice; records the target in brain.+0x168
            // (rider index, -1 when nothing qualifies). 0x004095d0 takes the
            // swing side from the acquired target half when present (the
            // mask-0x38 fallback node only feeds side when the mask-8 scan
            // misses; fight keeps the yaw fallback for a total miss).
            int acquired = -1;
            if (!r.ai) {
                const int32_t hp = live_schedule_ ? r.half_mirror.position : r.bike.i32(0x1c);
                int ahead = -1, behind = -1, da = 0x1e0, db = 0x1e0;
                for (size_t oi = 0; oi < riders_.size(); ++oi) {
                    if (oi == index) continue;
                    const Rider& o = riders_[oi];
                    if (o.parked) continue;
                    if (route_of(o)[o.sample].dual && o.route != r.route) continue;
                    const int32_t op = live_schedule_ ? o.half_mirror.position : o.bike.i32(0x1c);
                    const int32_t d = op - hp;
                    if (d >= 0) {
                        if (d < da) {
                            da = d;
                            ahead = int(oi);
                        }
                    } else if (-d < db) {
                        db = -d;
                        behind = int(oi);
                    }
                }
                if (ahead >= 0 && behind >= 0)
                    acquired = (db <= da) ? behind : ahead;
                else
                    acquired = (ahead >= 0) ? ahead : behind;
                r.brain.target_rider = acquired;
            }
            if (acquired >= 0) {
                const Rider& t = riders_[size_t(acquired)];
                const int32_t tlat = live_schedule_ ? t.half_mirror.lateral : t.bike.i32(0x28);
                r.attack.right = b.i32(0x28) < tlat;
            } else if (!r.ai) {
                // 0x00409400 mask-0x38 fallback: nearest car/pedestrian
                // within 0x1e0 (same shape as above) feeds 0x004095d0's
                // side when the halves scan misses; brain.+0x168 stays
                // cleared. Total miss keeps the yaw fallback.
                int fahead = -1, fbehind = -1, fda = 0x1e0, fdb = 0x1e0;
                bool have_ahead = false, have_behind = false;
                int32_t flat_ahead = 0, flat_behind = 0;
                const int32_t hp = live_schedule_ ? r.half_mirror.position : r.bike.i32(0x1c);
                auto consider = [&](int32_t pos, int32_t lat) {
                    const int32_t d = pos - hp;
                    if (d >= 0) {
                        if (d < fda) {
                            fda = d;
                            have_ahead = true;
                            flat_ahead = lat;
                        }
                    } else if (-d < fdb) {
                        fdb = -d;
                        have_behind = true;
                        flat_behind = lat;
                    }
                };
                if (traffic_)
                    for (size_t ci = 0; ci < traffic_->cars().size(); ++ci) {
                        const auto& c = traffic_->cars()[ci];
                        if (!c.active) continue;
                        consider(c.position, c.lateral);
                    }
                if (pedestrians_)
                    for (size_t pi = 0; pi < pedestrians_->pool().size(); ++pi) {
                        const auto& p = pedestrians_->pool()[pi];
                        if (!p.active) continue;
                        consider(p.body.bike.i32(0x1c), p.body.bike.i32(0x28));
                    }
                int32_t flat = 0;
                bool have = false;
                if (have_ahead && have_behind) {
                    flat = (fdb <= fda) ? flat_behind : flat_ahead;
                    have = true;
                } else if (have_ahead) {
                    flat = flat_ahead;
                    have = true;
                } else if (have_behind) {
                    flat = flat_behind;
                    have = true;
                }
                if (have) {
                    r.attack.right = b.i32(0x28) < flat;
                } else {
                    const Rider* nearest = nullptr;
                    for (const auto& o : riders_)
                        if (&o != &r && std::abs(o.bike.i32(0x1c) - b.i32(0x1c)) < 0x100 &&
                            (!nearest || std::abs(o.bike.i32(0x28) - b.i32(0x28)) <
                                             std::abs(nearest->bike.i32(0x28) - b.i32(0x28))))
                            nearest = &o;
                    r.attack.right = nearest ? b.i32(0x28) < nearest->bike.i32(0x28) : b.i32(0x60) >= 0;
                }
            } else {
                const Rider* nearest = nullptr;
                for (const auto& o : riders_)
                    if (&o != &r && std::abs(o.bike.i32(0x1c) - b.i32(0x1c)) < 0x100 &&
                        (!nearest || std::abs(o.bike.i32(0x28) - b.i32(0x28)) <
                                         std::abs(nearest->bike.i32(0x28) - b.i32(0x28))))
                        nearest = &o;
                r.attack.right = nearest ? b.i32(0x28) < nearest->bike.i32(0x28) : b.i32(0x60) >= 0;
            }
            r.attack.timer = 1;
            r.attack.fired = false;
        }
        return;
    }
    ++r.attack.timer;
    if (fight_hit_frame(r)) resolve_hit(r, index);
    if ((!ride_library_ || !attack_codes_) && r.attack.timer > attack_ticks) {
        r.attack.timer = 0;
        r.attack.fired = false;
    }
}

// 0x004096f0: descriptor combat bits OR frame byte 2. The selected
// descriptor is the frame retained before the preceding animation advance.
bool Race::fight_hit_frame(const Rider& r) const {
    if (!ride_library_) return !r.attack.fired && r.attack.timer == attack_legacy_hit_tick;
    uint32_t flags = 0;
    const auto& selected = r.ride_animation.selected;
    if (selected) {
        const auto& seq = ride_bank(r).at(size_t((*selected)[0]));
        if (seq) flags = seq->at(size_t((*selected)[1])).flags;
    }
    return (flags & 0x600000) || r.ride_animation.frame == 2;
}

// 0x00409c10: side match ((attacker_lateral < target_lateral) == side), then
// |dpos| < 0x40, lateral reach by attack weapon (5/7/8 fists/club/chain,
// kick 5), and |dheight| < 0x3000.
bool Race::fight_reach(const Rider& self, const Rider& other, bool side) const {
    const int32_t lat = live_schedule_ ? other.half_mirror.lateral : other.bike.i32(0x28);
    const int32_t pos = live_schedule_ ? other.half_mirror.position : other.bike.i32(0x1c);
    const int32_t height = live_schedule_ ? other.half_mirror.height : other.bike.i32(0x20);
    if ((self.bike.i32(0x28) < lat) != side) return false;
    if (std::abs(self.bike.i32(0x1c) - pos) >= 0x40) return false;
    if (std::abs(self.bike.i32(0x28) - lat) >= weapon_reach[self.attack.weapon] * 0x1000) return false;
    if (std::abs(self.bike.i32(0x20) - height) >= 0x3000) return false;
    return true;
}

void Race::resolve_hit(Rider& r, size_t index) {
    // 0x004096f0 after the descriptor/frame gate: nearest rider on the
    // attacked side within fight_reach (0x00409400/0x004095a0/0x00409c10).
    // Steal path first (all gates before the rand()&3 draw): target steal
    // deadline +0x1cc expired, target frame < 2, attacker +0x1d4 == 0
    // (fists), target +0x1d4 neither 0 nor kick 3, target actively
    // attacking (0x00409510). Failed steal still hits. Hit deadline +0x1d0
    // (native: victim hit_until) suppresses damage until strictly expired;
    // a landed hit sets tick+(5-level)*6, then rand()%(6-level) decides a
    // repeat attack and (non-player only) rand()&3==2 queues animation 6.
    // 0x00409ad0 scales strength by stamina (floor half), multiplies the
    // per-weapon EXE table at 0x00465a00, then knock_off: kicks add
    // condition damage trunc(damage/4)+1 with reason 5, others reason 4.
    r.attack.fired = true;
    Bike& b = r.bike;
    Rider* target = nullptr;
    if (live_schedule_) {
        // 0x00409400 / 0x00449c80: nearest half along the road, backward
        // wins equal gaps. It becomes the brain target before the reach test.
        build_scan_list();
        const int self = scan_find(int(index), 8);
        if (self >= 0) {
            const int back = scan_next(self, 8, false, 0x1e0);
            const int ahead = scan_next(self, 8, true, 0x1e0);
            int at = ahead;
            if (back >= 0 &&
                (ahead < 0 || std::abs(scan_[size_t(back)].position - scan_[size_t(self)].position) <=
                                  std::abs(scan_[size_t(ahead)].position - scan_[size_t(self)].position)))
                at = back;
            r.brain.target_rider = at < 0 ? -1 : scan_[size_t(at)].rider;
            if (r.brain.target_rider >= 0) target = &riders_[size_t(r.brain.target_rider)];
        }
        if (attack_codes_) {
            const auto& codes = *attack_codes_;
            r.attack.right = r.ride_animation.animation == codes[1][0][r.attack.weapon & 3] ||
                             r.ride_animation.animation == codes[1][1][r.attack.weapon & 3];
            r.attack.backhand = r.ride_animation.animation == codes[0][1][r.attack.weapon & 3] ||
                                r.ride_animation.animation == codes[1][1][r.attack.weapon & 3];
        }
    } else
        for (auto& o : riders_) {
            if (&o == &r || o.bike.i32(0x2f4) != 0) continue;
            if (!fight_reach(r, o, r.attack.right)) continue;
            if (!target ||
                std::abs(o.bike.i32(0x28) - b.i32(0x28)) < std::abs(target->bike.i32(0x28) - b.i32(0x28)))
                target = &o;
        }
    const int w = r.attack.weapon;
    const bool involves_player = human(riders_[index]) || (target && human(*target));
    // 0x004096f0: weapon swish when the player swings or is swung at.
    if (involves_player) {
        if (w == 1)
            sound(SoundEvent::swish_club, b);
        else if (w == 2)
            sound(SoundEvent::swish_chain, b);
    }
    if (!target || target->ride_animation.animation == 9 || !fight_reach(r, *target, r.attack.right)) return;
    if (index == 0) last_target_ = int(target - riders_.data());
    // Steal: every gate above, then exactly one rand() draw.
    if (uint32_t(target->steal_until) < uint32_t(ticks_) && target->ride_animation.frame < 2 &&
        r.attack.weapon == 0 && target->attack.weapon != 0 && target->attack.weapon != 3 &&
        ai_attack_anim(*target) != 0 && (random() & 3) == 3) {
        r.weapon = target->weapon;
        r.attack.weapon = target->weapon;
        target->weapon = 0;
        target->attack.weapon = 0;
        r.steal_until = ticks_ + 300;
        if (ride_library_ && steal_codes_ && attack_codes_) {
            const auto& codes = *attack_codes_;
            const bool left = target->ride_animation.animation == codes[0][0][0] ||
                              target->ride_animation.animation == codes[0][1][0];
            const auto& bank = ride_bank(r);
            const auto& fallback = ride_library_->fallback;
            set_animation_with_fallback(r.ride_animation, bank,
                                        (*steal_codes_)[size_t(r.attack.weapon + left * 4)], uint32_t(ticks_),
                                        fallback);
            queue_animation_with_fallback(r.ride_animation, bank, 2, fallback);
            set_animation_with_fallback(target->ride_animation, bank, 2, uint32_t(ticks_), fallback);
            queue_animation_with_fallback(target->ride_animation, bank, 2, fallback);
        }
        return;
    }
    const bool landed = uint32_t(ticks_) > uint32_t(target->hit_until);
    if (landed) {
        // 0x00409ad0: impact sound by weapon (kick alternates kick/punch on the tick parity).
        if (involves_player) {
            static constexpr SoundEvent impact[3] = {SoundEvent::impact_punch, SoundEvent::impact_club,
                                                     SoundEvent::impact_chain};
            sound(w == 3 ? SoundEvent(10 + (ticks_ & 1)) : impact[w], b);
        }
        const RiderStats& s = r.stamina;
        int32_t damage = int32_t((int64_t((s.max << 8) / s.base) * s.strength) >> 8);
        if (damage < s.strength / 2) damage = s.strength / 2;
        damage =
            (int32_t(weapon_damage[w]) * damage + ((int32_t(weapon_damage[w]) * damage >> 31) & 0xff)) >> 8;
        const int32_t bike_damage = w == 3 ? (damage + ((damage >> 31) & 3)) / 4 + 1 : 0;
        const bool down = knock_off(*target, damage, bike_damage, bike_damage == 0 ? 4 : 5);
        (void)down;
        ai_relationship(*target, r, 2);
        ai_relationship(r, *target, 4);
        target->hit_until = ticks_ + (5 - level_) * 6;
        int8_t queued = -1;
        if ((random() % (6 - level_)) == 0)
            queued = r.ride_animation.animation;
        else if (index != 0 && (random() & 3) == 2)
            queued = 6;
        if (queued >= 0 && ride_library_)
            queue_animation_with_fallback(r.ride_animation, ride_bank(r), queued, ride_library_->fallback);
    }
    Bike& tb = target->bike;
    if (landed) {
        if (r.attack.backhand) {
            tb.i32(0xec) -= 300;
            b.i32(0xec) += 300;
        } // 0x00409bd0 shove
        tb.i32(0x288) += r.attack.right ? 3000 : -3000;
        b.i32(0x288) += r.attack.right ? -1000 : 1000;
    }
    if (w == 3) tb.i32(0x288) += r.attack.right ? 8000 : -8000;
    auto clamp_steer = [](Bike& k) {
        const int32_t lim = k.i32(0x29c);
        k.i32(0x288) = std::clamp(int32_t(k.i32(0x288)), -lim, lim);
    };
    clamp_steer(tb);
    clamp_steer(b);
}

// Nitro (player-global in the original; per-rider copies here):
// 0x0040b110 activates when nitro bit 0x10 and accelerate bit 1 are both
// held (checked in 0x00434660): boost flag +0xc0 bit 0x20 clear, charges > 0,
// accumulator/256 >= minimum. It decrements one charge, sets the flag and
// emits event 14. 0x0040b170 stops boost on accelerate release or finish
// (event 21). 0x0040b1a0 (called from 0x00407a90 for the player only) drains
// 256*dt while boosting, else recharges until the maximum test fails.
// 0x0040adb0 multiplies throttle force by the boost multiplier with an
// arithmetic >>8 shift.
void Race::nitro_think(Rider& r, uint32_t controls, bool player) {
    if (!player) return;
    Bike& b = r.bike;
    if ((b.u8(0xc0) & 0x10) == 0) {
        if ((controls & control_nitro) && (controls & control_accelerate)) nitro_think_activate(r);
        if (!(controls & control_accelerate)) nitro_think_stop(r);
    } else {
        nitro_think_stop(r);
    }
}
void Race::nitro_think_activate(Rider& r) {
    Bike& b = r.bike;
    if ((b.u8(0xc0) & 0x20) != 0) return;
    // Plan 34: seats 1..7 keep their own charges and spec words.
    int32_t& charges = r.human > 0 ? r.nitro_count : nitro_charges_;
    const NitroConfig& cfg = r.human > 0 ? r.nitro_cfg : nitro_;
    if (charges <= 0) return;
    if ((r.nitro_charge + ((r.nitro_charge >> 31) & 0xff)) / 256 < cfg.minimum) return;
    --charges;
    b.u8(0xc0) = uint8_t(b.u8(0xc0) | 0x20);
    sound(SoundEvent::nitro_on, b);
}
void Race::nitro_think_stop(Rider& r) {
    Bike& b = r.bike;
    if ((b.u8(0xc0) & 0x20) == 0) return;
    b.u8(0xc0) = uint8_t(b.u8(0xc0) & uint8_t(~0x20));
    sound(SoundEvent::nitro_off, b);
}
void Race::nitro_charge_tick(Rider& r) {
    Bike& b = r.bike;
    if ((b.u8(0xc0) & 0x20) == 0) {
        const NitroConfig& cfg = r.human > 0 ? r.nitro_cfg : nitro_;
        if ((r.nitro_charge + ((r.nitro_charge >> 31) & 0xff)) / 256 < cfg.max)
            r.nitro_charge += b.i32(0x18) * cfg.recharge;
    } else {
        r.nitro_charge += b.i32(0x18) * -0x100;
        if (r.nitro_charge < 1) nitro_think_stop(r);
    }
}
int32_t Race::nitro_throttle_force(Rider& r, int32_t force) {
    // 0x0040adb0 boost branch: force * multiplier, arithmetic >>8. int64
    // product then narrowed matches the 32-bit imul before the sar.
    const int64_t prod = int64_t(force) * int64_t((r.human > 0 ? r.nitro_cfg : nitro_).boost);
    return int32_t(prod >> 8);
}
// Riding animation advanced in the Race tick before snapshots (0x00407a90
// think part + 0x00407f30 selection + 0x0043ae90 advance). Without a ride
// bank there is nothing to advance; the render owner (Game::tick) keeps its
// own states. Contact animation 9 (knock_off reason 4) and attack swing
// codes (fight start) select here; crash states are owned by step_crash.
// Rider build 0x00405990 calls 0x00446060 on the bike and the half block:
// 0x0043ae10 selects animation 2, 0x0043ae60 queues 2, +0xc is cleared and
// 0x0043ae90 advances once. The riding selection in later thinks uses
// 0x0043ae10 only, so the queued 2 survives into the race (measured on
// sched-human-cov2-20261017: queued 2 on every frame). Nothing touches the
// block between the build and the attached bank, so it runs here.
void Race::set_ride_library(const ActorAnimationLibrary* library,
                            const std::array<std::array<std::array<int8_t, 4>, 2>, 2>* attacks) {
    ride_library_ = library;
    attack_codes_ = attacks;
    if (!ride_library_) return;
    for (auto& r : riders_) {
        const auto& bank = ride_bank(r);
        auto& st = r.ride_animation;
        set_animation(st, bank, 2, uint32_t(ticks_));
        queue_animation(st, bank, 2);
        st.selected.reset();
        advance_animation(st, bank, uint32_t(ticks_));
    }
}
void Race::advance_ride_animation(Rider& r, bool player) {
    if (r.frozen) return;
    // The pending contact selection (knock_off reason 4 -> animation 9) is
    // consumed in the tick even without a ride bank; selection itself needs
    // the bank. Crash states stay owned by step_crash.
    if (!ride_library_) {
        r.contact_animation = -1;
        return;
    }
    if (r.bike.i32(0x2f4) != 0) return;
    const auto& bank = ride_bank(r);
    const auto& fallback = ride_library_->fallback;
    auto& st = r.ride_animation;
    if (r.contact_animation >= 0) {
        set_animation_with_fallback(st, bank, r.contact_animation, uint32_t(ticks_), fallback);
        r.contact_animation = -1;
    }
    if (r.attack.timer == 1 && attack_codes_) {
        const auto& codes = *attack_codes_;
        set_animation_with_fallback(
            st, bank, codes[r.attack.right ? 1 : 0][r.attack.backhand ? 1 : 0][r.attack.weapon & 3],
            uint32_t(ticks_), fallback);
    }
    RidingAnimationInput input;
    input.speed = r.bike.i32(0xec);
    input.throttle = r.bike.i32(0x114);
    input.lateral_speed = r.bike.i32(0x100);
    input.boost = r.bike.i32(0xf4);
    input.flags = r.bike.u8(0x278);
    input.player = player;
    input.wrecked = (r.bike.u8(0xc0) & 0x10) != 0;
    // 0x00407f30 reads DAT_004b7ef4 before the driver updates the camera.
    input.camera_distance =
        std::abs(r.bike.i32(0x1c) - int32_t(live_schedule_ ? previous_camera_position_ : camera_position()));
    advance_riding_animation(st, bank, fallback, input, uint32_t(ticks_));
}

void Race::update_camera() {
    // 0x00437c30 (single road): trail the rider laterally, one sample behind.
    const Rider& p = riders_[0];
    const SliceEdges e = edges(p, p.sample);
    const int32_t centre = (e.left + e.right) / 2;
    const int32_t d = div256(p.bike.i32(0x28)) - centre;
    int32_t off = d - div_pow2(d, 2);
    if (d - off >= 0xc9)
        off = d - 200;
    else if (d - off < -200)
        off = d + 200;
    // 0x00437c30: the lift and pull-back ramp only while the player's bike
    // carries the 0x10 latch (finished/wrecked) or DAT_004753c4 is set (the arrest
    // sets it: the ring camera pulls back 3 per tick from the tick after it); a
    // plain crash (state 1..3) leaves the camera on the body.
    if ((p.bike.u8(0xc0) & 0x10) != 0 || busted_) { // DAT_004753c4 is set by the arrest (0x00416010(4, 0xb4))
        if (camera_lift_ < 300) ++camera_lift_;
        if (camera_back_ < 900) camera_back_ += 3;
    } else {
        camera_lift_ = camera_lift_ > 0 ? camera_lift_ - 1 : 0;
        camera_back_ = camera_back_ > 2 ? camera_back_ - 3 : 0;
    }
    camera_x_ = centre + off;
    camera_height_ = p.bike.i32(0x24) + camera_lift_;
    if (camera_height_ < 0x97) camera_height_ = 0x96;
    // Plan 34 C1: seats >= 1 keep their own ring-camera pull-back (the same
    // ramp), which places their ring windows.
    for (int seat = 1; seat < humans(); ++seat) {
        const Rider& h = riders_[size_t(human_rider(seat))];
        int32_t& back = seat_back_[size_t(seat)];
        if ((h.bike.u8(0xc0) & 0x10) != 0 || h.busted)
            back = back < 900 ? back + 3 : back;
        else
            back = back > 2 ? back - 3 : 0;
    }
}

void Race::update_traffic_windows(bool moving) {
    if (!traffic_ || humans() < 2) return; // one human: the classic single window
    std::vector<TrafficSystem::SeatWindow> windows(static_cast<size_t>(humans()),
                                                   TrafficSystem::SeatWindow{});
    for (int seat = 0; seat < humans(); ++seat) {
        const Rider& me = riders_[size_t(human_rider(seat))];
        TrafficSystem::SeatWindow& w = windows[size_t(seat)];
        w.me_q8 = me.bike.i32(0x1c);
        w.velocity = me.bike.i32(0x108);
        if (seat > 0) {
            const TrackRing* ring = rings_[size_t(seat)];
            w.camera_q8 = ring ? ring->front_q8() - 0x5000 : int32_t(seat_camera(seat)) + 0x100;
            w.front_q8 = w.camera_q8 + 0x5000;
            w.distance = moving ? seat_moved_[size_t(seat)] : 0;
        }
        // 0x0044f0d0: nearest other live human ahead within 0x4600 (distance
        // >= 0) or behind within 0x2d00, on a shared roadway.
        int32_t ahead = 0x4600, behind = 0x2d00;
        for (int other = 0; other < humans(); ++other) {
            if (other == seat) continue;
            const Rider& o = riders_[size_t(human_rider(other))];
            if (!human(o) || !o.live) continue;
            const auto& road = route_of(me);
            const bool shared = o.route == me.route || (me.sample < road.size() && !road[me.sample].dual);
            if (!shared) continue;
            const int32_t d = o.bike.i32(0x1c) - w.me_q8;
            if (d < 0) {
                if (-d < behind) behind = -d, w.behind = true;
            } else if (d < ahead) {
                ahead = d, w.ahead = true;
            }
        }
    }
    traffic_->set_seat_windows(windows);
}

uint32_t Race::seat_camera(int seat) const {
    if (seat <= 0) return camera_position();
    const int32_t p =
        riders_[size_t(human_rider(seat))].half_mirror.position - seat_back_[size_t(seat)] - 0x100;
    return uint32_t(p < 0 ? 0 : p);
}

bool Race::player_in_tunnel() const {
    // 0x00434a80: DAT_0046d97c = (*(player.bike + 0x140) + 0x3c == 3).
    if (riders_.empty()) return false;
    const Rider& player = riders_[0];
    const auto& road = route_of(player);
    if (player.sample >= road.size()) return false;
    return edges(player, player.sample).type == 3;
}

uint32_t
Race::camera_position() const { // 0x00437c30 reads the player's half (`*(player + 0x2fc) + 0x138`): while the
    // rider is down that half is the crash body, which the camera follows
    // (measured against napa-cops-crashed: ring camera == body +0x1c).
    // The half image trails the bike by the 0x004067d0 copy, which runs in
    // the move pass before contacts: a contact shove moves the bike's words
    // but not the half's until the next copy, so the camera (and the ring
    // driven from it) does not see contact pushes (contact 1469 lags the
    // bike by 7). half_mirror is the native image of that point: post-move,
    // pre-contact in both schedulers (live_standings / update_halves for
    // riding riders, crash-body sync while crashed).
    const int32_t follow = riders_[0].half_mirror.position;
    const int32_t p = follow - camera_back_ - 0x100;
    return uint32_t(p < 0 ? 0 : p);
}

Camera Race::camera() const {
    const size_t s = camera_position() >> 8;
    return {camera_x_ - route()[s].offset, camera_height_};
}

int Race::place(size_t index) const {
    const Rider& me = riders_.at(index);
    int place = 1;
    for (size_t i = 0; i < riders_.size(); ++i) {
        if (i == index || riders_[i].cop) continue; // the place table 0x004c7fb0 holds no cop
        const Rider& o = riders_[i];
        bool ahead;
        if (o.finished && me.finished)
            ahead = o.finish_tick < me.finish_tick;
        else if (o.finished != me.finished)
            ahead = o.finished;
        else
            ahead = o.bike.i32(0x1c) > me.bike.i32(0x1c);
        place += ahead;
    }
    return place;
}

void Race::update_halves() {
    // Rider halves mirror their bikes after the scheduler pass, before contacts.
    // Crashed riders keep the crash-body mirror (sync_half_mirror at entry
    // and after every body motion step): the half IS the body (0x00403d60),
    // so mirroring the riderless bike here would clobber it (split-left
    // 2056: half height 19903 vs bike 1543).
    for (auto& r : riders_) {
        if (r.frozen || r.bike.i32(0x2f4) != 0) continue;
        const Bike& b = r.bike;
        r.half_mirror = {b.i32(0x1c),  b.i32(0x28),  b.i32(0x20), b.i32(0xf4), b.i32(0xec), b.i32(0xf0),
                         b.i32(0x10c), b.i32(0x108), b.i32(0x60), r.sample,    r.route};
    }
    // 0x004060b0 tail for riding (uncrashed) riders: distance to the finish,
    // then 0x004075f0 refreshes the place when the half's scheduler tick
    // (+0x10) reaches +0x1f4. Frozen riders keep finish - position; crashed
    // riders keep the body-based distance (entry direct path + per-tick
    // body delta in crash_body_motion).
    for (auto& r : riders_)
        if (finish_q8_ != 0 && r.bike.i32(0x2f4) == 0) r.finish_distance = finish_q8_ - r.bike.i32(0x1c);
    for (size_t i = 0; i < riders_.size(); ++i) {
        Rider& r = riders_[i];
        // Frozen riders never move; crashed riders still refresh the place
        // (0x004075f0 runs on the un-frozen half node: split-left 2057 sets
        // place_refresh 2061 = 2057+4 while crashed, native stalled at 2057).
        // A solo-nulled half (half_frozen) stops refreshing (2105 stays).
        if (r.frozen || r.half_frozen) continue;
        const int32_t last = r.sched_live ? r.sched_last : ticks_;
        if (r.place_refresh > last) continue;
        int place = 1; // 0x00407550 over the other riders
        for (size_t j = 0; j < riders_.size(); ++j) {
            if (j == i || riders_[j].cop) continue;
            const Rider& o = riders_[j];
            const bool shared = o.bike_slice_present && r.bike_slice_present &&
                                !(route_of(r)[r.sample].dual && o.route != r.route);
            if (shared ? o.bike.i32(0x1c) > r.bike.i32(0x1c) : o.finish_distance < r.finish_distance) ++place;
        }
        r.place = place;
        r.place_refresh = last + (i == 0 ? 4 : 0x3c);
    }
}

void Race::countdown_step() {
    ++ticks_;
    apply_static_boxes();
    controls_.fill(0); // countdown thinks see no input (schedule_pass(0, ...))
    // 0x00438930 stores the player bike's last run as creation tick - 1
    // (template interval 1), so its first scheduled tick sees dt 2
    // (measured: solo captures, tick 1). Hydrated riders (sched_live) step
    // their captured 0x00437e60 cadence instead: frozen opponents hold
    // interval 4/lasts 1,5,..., the player interval 1. Legacy captures and
    // live games keep dt 1 (frozen) and the player 2-then-1 rule.
    countdown_started_ = true;
    if (live_schedule_) {
        schedule_pass(0, true);
        sort_schedule();
        parked_pace_band();
        prepare_static_boxes();
        update_traffic_windows(false);
        if (traffic_ && !riders_.empty())
            traffic_->countdown_step(
                route_of(riders_[0]), int32_t(camera_position()) + 0x5100, [this] { return random(); },
                uint32_t(ticks_));
        load_pedestrian_looks(); // the frame render after this step draws them
        return;
    }
    if (!riders_.empty()) {
        Rider& p = riders_[0];
        if (p.sched_live)
            step_frozen_schedule(p);
        else
            p.bike.i32(0x18) = ticks_ == 1 ? 2 : 1;
    }
    for (size_t i = 0; i < riders_.size(); ++i) {
        Rider& r = riders_[i];
        if (r.frozen) {
            step_frozen_schedule(r);
            continue;
        }
        if (r.finished || r.bike.i32(0x2f4) != 0) continue;
        if (r.ai || (demo_hold_ && i == 0)) ai_think(r);
    }
    update_halves();    // the bike move 0x004060b0 runs during the hold too
    step_pedestrians(); // the scheduler pass runs pedestrians during the hold too
    // 0x0044b3b0 calls 0x0044dd90 even while the camera is stationary:
    // at rate 2500 each hold tick banks +9 behind/rear and -9 oncoming.
    update_traffic_windows(false);
    if (traffic_ && !riders_.empty())
        traffic_->countdown_step(
            route_of(riders_[0]), int32_t(camera_position()) + 0x5100, [this] { return random(); },
            uint32_t(ticks_));
    load_pedestrian_looks();
}

void Race::collide_riders() {
    // 0x004368f0 over the position-sorted object list. Outer nodes: those the
    // scheduler ran this tick (+0x10 == tick) with flags & 6 == 6 (frozen
    // solo nodes never qualify). Each first meets the BORR hazards when its
    // fraction sits within its half-length of mid-slice (0x004373f0), then
    // walks the objects after it in list order with flags & 5 == 5, stopping
    // at the first whose position gap exceeds the summed half-lengths
    // (+0x178), and contacts those within the summed half-widths (+0x174),
    // on a shared road half, and within the height test on +0x17c.
    // Runtime flags are the template byte | 4: bikes and pedestrians (3)
    // take both roles, rider halves (0) neither. Free cars stay in
    // collide_traffic (not in this walk: their list order is approximate);
    // pedestrian pairs never contact: 0x004368f0 gates every pair on
    // (cat_a & cat_b & 3) != 0, and pedestrians are type 0x20 (& 3 == 0),
    // so two pedestrians pass through each other in the original too.
    build_scan_list();
    auto bike_of = [&](const ScanObject& o) -> Bike& {
        return o.rider >= 0 ? riders_[size_t(o.rider)].bike
                            : pedestrians_->pool()[size_t(o.pedestrian)].body.bike;
    };
    auto rider_or_ped = [](const ScanObject& o) {
        return (o.rider >= 0 && o.type == 4) || o.pedestrian >= 0;
    };
    for (size_t i = 0; i < scan_.size(); ++i) {
        const ScanObject a = scan_[i];
        if (a.rider >= 0 && a.type == 8) {
            Rider& owner = riders_[size_t(a.rider)];
            const Bike& body = owner.crash_actor.motion;
            if (!owner.crash_active || owner.frozen || owner.finished ||
                (live_schedule_ && (owner.half_sched_last != ticks_ || ((body.u8(0xc0) | 4) & 6) != 6)))
                continue;
            if (std::abs(int32_t(uint32_t(body.i32(0x1c)) & 0xff) - 0x80) < body.i32(0x178)) {
                Rider detached;
                detached.bike = body;
                detached.route = owner.crash_actor.route;
                detached.sample = owner.crash_actor.sample;
                detached.previous_sample = owner.crash_actor.previous_sample;
                if (sweep_statics(detached, &owner)) {
                    owner.crash_actor.motion = detached.bike;
                    owner.crash_actor.route = detached.route;
                    owner.crash_actor.sample = detached.sample;
                    owner.crash_actor.previous_sample = detached.previous_sample;
                    for (int off = 0x180; off <= 0x1a0; off += 4)
                        owner.half_object.i32(off) = detached.bike.i32(off);
                    owner.ride_animation = owner.crash_actor.animation;
                    sync_half_mirror(owner);
                    if (finish_q8_) owner.finish_distance = finish_q8_ - detached.bike.i32(0x1c);
                }
            }
            continue;
        }
        if (!rider_or_ped(a)) continue;
        if (a.rider >= 0) {
            const Rider& r = riders_[size_t(a.rider)];
            if (r.frozen || r.finished) continue;
            // 0x004368f0 outer: +0x10 == tick with flags & 6 == 6. Live
            // checks the scheduler image; solo keeps the frozen skip but
            // must still enforce the flags (e.g. split-left 845: the player
            // is 7, the overlapping frozen is 4 and must not contact).
            if ((r.bike.u8(0xc0) & 6) != 6) continue;
            if (live_schedule_ && r.sched_last != ticks_) continue;
        } else {
            const auto& p = pedestrians_->pool()[size_t(a.pedestrian)];
            if (freeze_actors_ || p.last_tick != ticks_ || (p.body.bike.u8(0xc0) & 6) != 6) continue;
        }
        Bike& ab = bike_of(a);
        if (a.rider >= 0 && std::abs(int32_t(uint32_t(ab.i32(0x1c)) & 0xff) - 0x80) < ab.i32(0x178))
            collide_hazards(riders_[size_t(a.rider)]);
        for (size_t j = i + 1; j < scan_.size(); ++j) {
            const ScanObject b = scan_[j];
            if (!rider_or_ped(b)) continue;
            Bike& bb = bike_of(b);
            // 0x004368f0 inner: flags & 5 == 5, unconditionally (solo and
            // live). The solo path previously skipped this, contacting
            // frozen c0=4 nodes the original ignores (split-left 845).
            if ((bb.u8(0xc0) & 5) != 5) continue;
            if (ab.i32(0x178) + bb.i32(0x178) < std::abs(ab.i32(0x1c) - bb.i32(0x1c))) break;
            if (std::abs(ab.i32(0x28) - bb.i32(0x28)) > ab.i32(0x174) + bb.i32(0x174)) continue;
            const Rider& ar =
                a.rider >= 0 ? riders_[size_t(a.rider)] : pedestrians_->pool()[size_t(a.pedestrian)].body;
            const Rider& br =
                b.rider >= 0 ? riders_[size_t(b.rider)] : pedestrians_->pool()[size_t(b.pedestrian)].body;
            if (route_of(ar)[ar.sample].dual && ar.route != br.route) continue;
            const int32_t dz = (ab.i32(0x20) - bb.i32(0x20)) - bb.i32(0x17c) + ab.i32(0x17c);
            if (std::abs(dz) > ab.i32(0x17c) + bb.i32(0x17c)) continue;
            if (a.rider >= 0 && b.rider >= 0) {
                collide(riders_[size_t(a.rider)], riders_[size_t(b.rider)]);
            } else if (a.rider >= 0 || b.rider >= 0) {
                Rider& r = riders_[size_t(a.rider >= 0 ? a.rider : b.rider)];
                Pedestrian& p = pedestrians_->pool()[size_t(a.rider >= 0 ? b.pedestrian : a.pedestrian)];
                if (!r.finished && r.bike.i32(0x2f4) == 0)
                    contact_rider_pedestrian(r, p, a.rider >= 0); // 0x004049c0 rejects crashed riders
            }
        }
    }
}

void Race::prepare_traffic_obstacles() {
    if (!traffic_) return;
    // Lane-scan obstacles for 0x0044a070 (mask 0x3f): bikes, riders on
    // foot and pedestrians on the cars' (camera) roadway.
    auto& obstacles = traffic_->obstacles();
    obstacles.clear();
    for (const auto& r : riders_) {
        if (route_of(r)[r.sample].dual && r.route != riders_[0].route) continue;
        const Bike& b = r.bike;
        obstacles.push_back({b.i32(0x1c), b.i32(0x28), b.i32(0x108), b.i32(0x10c), 4});
        if (r.crash_active) {
            const Bike& f = r.crash_actor.motion;
            obstacles.push_back({f.i32(0x1c), f.i32(0x28), f.i32(0x108), f.i32(0x10c), 8});
        }
    }
    if (pedestrians_)
        for (const auto& p : pedestrians_->pool()) {
            if (!p.active) continue;
            const Bike& b = p.body.bike;
            obstacles.push_back({b.i32(0x1c), b.i32(0x28), b.i32(0x108), b.i32(0x10c), 0x20});
        }
}

void Race::step(uint32_t controls) {
    const uint32_t one[1] = {controls};
    step(std::span<const uint32_t>(one));
}

void Race::set_player_bike(int bike) {
    // 0x00446190 (offline branch): the player draws remap = its rider-row
    // byte +5, which 0x00405990 fills with the bike index (0x405b3a..0x405b57:
    // level_bike in Thrash, else the career bike 0x004b8a14); opponent slot i
    // draws remap i, except the slot equal to that byte, which draws remap 0.
    // The network branch (bike +0x328) is not modelled.
    for (Rider& r : riders_) {
        if (r.cop) continue;
        if (r.slot == 0)
            r.colour = bike;
        else
            r.colour = r.slot == bike ? 0 : r.slot;
    }
}

void Race::set_human_driven(int seat, bool driven) {
    const int i = human_rider(seat);
    if (i < 0) return;
    riders_[size_t(i)].ai = !driven;
}

void Race::set_human_nitro(int seat, const NitroConfig& c) {
    const int i = human_rider(seat);
    if (i <= 0) return; // seat 0 uses set_nitro
    Rider& r = riders_[size_t(i)];
    r.nitro_cfg = c;
    r.nitro_count = c.charges;
    r.nitro_charge = c.max << 8;
}

const Rider& Race::nearest_human(int32_t position) const {
    const Rider* best = &riders_[0];
    if (human_riders_.size() < 2) return *best;
    int64_t best_d = -1;
    for (int i : human_riders_) {
        const Rider& h = riders_[size_t(i)];
        if (!human(h)) continue;
        const int64_t d = std::abs(int64_t(h.bike.i32(0x1c)) - position);
        if (best_d < 0 || d < best_d) best_d = d, best = &h;
    }
    return *best;
}

Race::HumanOutcome Race::human_outcome(int seat) const {
    HumanOutcome o;
    const int i = human_rider(seat);
    if (i < 0) return o;
    const Rider& r = riders_[size_t(i)];
    if (r.finished) {
        o.place = place(size_t(i));
        o.code = o.place == 1 ? 2 : 3; // networked: only first place wins (0x00416010)
        o.tick = r.finish_tick;
    } else if (r.busted) {
        o.code = 4;
    } else if (r.bike.i32(0x2f0) == 0) {
        o.code = 5;
    }
    return o;
}

bool Race::all_humans_done() const {
    for (int seat = 0; seat < humans(); ++seat) {
        const Rider& r = riders_[size_t(human_rider(seat))];
        if (r.ai) continue; // departed: AI-driven, not waited for
        if (human_outcome(seat).code == 1) return false;
    }
    return true;
}

Race Race::presentation_copy(int seat) const {
    Race view = *this;
    const int rider = human_rider(seat);
    if (rider <= 0) return view;
    std::swap(view.riders_[0], view.riders_[size_t(rider)]);
    const auto& local = view.riders_[0];
    view.busted_ = local.busted;
    view.camera_back_ = seat_back_[size_t(seat)];
    view.camera_lift_ = view.camera_back_ / 3;
    view.camera_heading_ = seat_heading_[size_t(seat)];
    const auto edges = view.edges(local, local.sample);
    const int32_t centre = (edges.left + edges.right) / 2;
    const int32_t d = div256(local.bike.i32(0x28)) - centre;
    int32_t off = d - div_pow2(d, 2);
    if (d - off >= 0xc9)
        off = d - 200;
    else if (d - off < -200)
        off = d + 200;
    view.camera_x_ = centre + off;
    view.camera_height_ = std::max(150, local.bike.i32(0x24) + view.camera_lift_);
    view.last_target_ = -1;
    return view;
}

void Race::update_handicap() {
    // 0x00442f60 (host branch), run every 0x39 ticks from shared state.
    if (!(handicap_tick_ < ticks_)) return;
    handicap_tick_ += 0x39;
    int64_t sum = 0;
    int count = 0;
    int32_t hi = 0, lo = 0;
    for (int seat = 0; seat < humans(); ++seat) {
        const Rider& h = riders_[size_t(human_rider(seat))];
        if (seat != 0 && !human(h)) continue; // rider 0 always counts (0x00442f60 seeds with it)
        const int32_t d = h.finish_distance;
        if (count == 0) hi = lo = d;
        hi = std::max(hi, d);
        lo = std::min(lo, d);
        sum += d;
        ++count;
    }
    if (sum == 0 || count == 0) return;
    const int32_t spread = hi - lo < 0x4201 ? (hi - lo) * 0x40 / 0x4200 : 0x40;
    const int32_t mean = int32_t(sum / count);
    if (lo == mean || hi == mean) {
        handicap_.fill(0); // the original clears 16 bytes; seats are 0..7
        return;
    }
    for (int seat = 0; seat < humans(); ++seat) {
        const Rider& h = riders_[size_t(human_rider(seat))];
        if (seat != 0 && !human(h)) continue;
        int32_t e = h.finish_distance - mean;
        if (e < 0) {
            e = spread * e / (mean - lo);
            if (e < -0x7f) e = -0x80;
        } else {
            e = spread * e / (hi - mean);
            if (e > 0x7e) e = 0x7f;
        }
        handicap_[size_t(seat)] = int8_t(e);
    }
}

void Race::step(std::span<const uint32_t> words) {
    std::array<uint32_t, 8> previous_view{};
    for (int seat = 1; seat < humans(); ++seat) previous_view[size_t(seat)] = seat_camera(seat);
    controls_.fill(0);
    for (size_t i = 0; i < words.size() && i < controls_.size(); ++i) controls_[i] = words[i];
    const uint32_t controls = controls_[0];
    if (riders_[0].finished &&
        std::all_of(riders_.begin(), riders_.end(), [](const Rider& r) { return r.cop || r.finished; }))
        return;
    ++ticks_;
    if (cop_removal_pending_) remove_cop(); // last tick's marked cop (0x00437df0 prepass)
    apply_static_boxes();
    const bool trace =
        std::getenv("RR_SIM_TRACE_TICK") && ticks_ == std::atoi(std::getenv("RR_SIM_TRACE_TICK"));
    auto phase = [&](const char* label) {
        if (trace) std::fprintf(stderr, "phase %d %s rng %u\n", ticks_, label, seed_);
    };
    phase("start");

    if (live_schedule_) {
        schedule_pass(controls, false); // 0x00438170(0): bikes and halves on their own cadences
        sort_schedule();                // 0x004383e0 after the pass
    } else {
        if (countdown_started_ && !riders_.empty() && !riders_[0].sched_live) riders_[0].bike.i32(0x18) = 1;
        for (size_t i = 0; i < riders_.size(); ++i)
            step_rider(riders_[i], word(riders_[i]), human(riders_[i]));
        // Scheduler metadata runs after drive/think and before contact: drive
        // consumes the previous run's dt (tick-2 launch ramps on dt 2), contact
        // the fresh one. Hydrated riders only; legacy/live keep prior behavior.
        for (size_t i = 0; i < riders_.size(); ++i) step_frozen_schedule(riders_[i]);
    }
    phase("schedule");
    if (!live_schedule_) step_pedestrians(); // live cadence interleaves them in schedule_pass
    if (!live_schedule_) update_halves();    // rider halves mirror their bikes before contacts
    phase("pedestrians");
    collide_riders();
    collide_car_statics();
    for (size_t i = 0; i < riders_.size(); ++i) {
        collide_traffic(riders_[i]);
        collide_traffic_half(riders_[i]);
    }
    collide_pedestrians(); // pedestrians against cars
    if (live_schedule_) {
        sort_schedule();    // 0x004383e0 again after the 0x004368f0 contacts
        parked_pace_band(); // 0x00407610 closes 0x00438170
    }
    // Riding animations advance inside each rider's think (step_rider).
    phase("contacts");
    update_camera();
    const auto& player = riders_[0];
    const auto& road = route_of(player);
    camera_heading_ = advance_heading(camera_heading_, previous_camera_position_, camera_position(),
                                      road[player.previous_sample].path.bend, road[player.sample].path.bend,
                                      (player.bike.u8(0xc0) & 16) != 0);
    for (int seat = 1; seat < humans(); ++seat) {
        const auto& rider = riders_[size_t(human_rider(seat))];
        const auto& route = route_of(rider);
        seat_heading_[size_t(seat)] =
            advance_heading(seat_heading_[size_t(seat)], previous_view[size_t(seat)], seat_camera(seat),
                            route[rider.previous_sample].path.bend, route[rider.sample].path.bend,
                            (rider.bike.u8(0xc0) & 16) != 0);
    }
    // 0x00454120 ring advance (terrain/BORR/DZHR draws) before 0x0044dd90.
    if (ring_) {
        ring_->advance(int32_t(camera_position()) - int32_t(previous_camera_position_), ring_random_,
                       ring_sink_);
        for (int seat = 1; seat < humans(); ++seat) { // plan 34 C1, seat order
            TrackRing* w = rings_[size_t(seat)];
            if (!w) continue;
            const int32_t now = int32_t(seat_camera(seat));
            seat_moved_[size_t(seat)] = now - seat_previous_[size_t(seat)];
            w->advance(seat_moved_[size_t(seat)], ring_random_, seat_sink_[size_t(seat)]);
            seat_previous_[size_t(seat)] = now;
        }
    } else
        build_pedestrian_slices();
    phase("ring");
    if (traffic_) {
        if (!live_schedule_) prepare_traffic_obstacles();
        update_traffic_windows(true);
        // 0x0044b3b0: distance moved this step feeds the spawn timers; the
        // ring front rides 0x5000 ahead of the ring camera, which runs
        // 0x100 ahead of the view camera (0x0044b250: 0x3200 vs 0x3100).
        const int32_t camera = int32_t(camera_position());
        traffic_->step(
            road, camera + 0x100, camera + 0x5100, camera - int32_t(previous_camera_position_),
            player.bike.i32(0x1c), player.bike.i32(0x28), [this] { return random(); }, uint32_t(ticks_),
            player.bike.i32(0x108));
    }
    phase("traffic");
    prepare_static_boxes();
    previous_camera_position_ = camera_position();
    load_pedestrian_looks();     // the frame render after this step draws them
    if (net_) update_handicap(); // 0x00442f60 runs on the networked tick path
}
} // namespace rr
