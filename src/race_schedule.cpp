#include "race.hpp"
#include "pedestrians.hpp"
#include "track_ring.hpp"
#include "traffic.hpp"
#include <algorithm>
#include <array>
#include <optional>
#include <cstdio>
#include <cstdlib>

// Live rider scheduler (plan 10 Task 6): 0x00438170(0) walks the object list
// and 0x00437e60 runs each due node. Each function names the routine it
// ports. Known native differences (labelled where they occur):
// - pedestrians keep their own pass after riders/cars, and intersection
//   controllers run in a prepass; those nodes are not yet interleaved;
// - the ring trackers 0x00450d30/0x00450dc0 are taken at their measured
//   offsets from the ring back (14 and 91 samples, set up by 0x004552e0);
//   their split/merge hand-over is not modelled;
// - the parked virtual walk follows the rider's own route at splits, where
//   the original takes the split node's byte +0x18;
// - a crashed bike keeps the native crash path in its move slot; the
//   crashed branches of 0x004060b0 and 0x00406890 are not ported.
namespace rr {
namespace {
int32_t sar(int32_t x, int n) {
    return (x + ((x >> 31) & ((1 << n) - 1))) >> n;
} // signed division by 2^n
// Ring trackers (0x00450d30 from 0x004552e0): created on the back slice and
// stepped 14 (tracker 0, section +0x18) and 91 (tracker 1, section +0x1c)
// slices forward; 0x00454120 steps both with every slice crossing.
constexpr int32_t tracker_back = 14, tracker_front = 91;
size_t section_start(const std::vector<RouteSample>& road, size_t sample) {
    sample = std::min(sample, road.size() - 1);
    return sample >= road[sample].local ? sample - road[sample].local : 0;
}
int32_t section_length(const std::vector<RouteSample>& road, size_t start) {
    size_t s = start;
    while (s + 1 < road.size() && road[s + 1].section == road[start].section &&
           road[s + 1].local == road[s].local + 1)
        ++s;
    return int32_t(s - start + 1);
}
} // namespace

void Race::set_live_schedule(bool v) {
    live_schedule_ = v;
    if (!v) return;
    // 0x00438930: next = build tick, last = tick - template interval. The
    // player bike and half (0x00463c10) carry interval 1, opponent bikes
    // (0x00463ad8) and halves (0x00463b40) interval 4; dt starts at 0.
    for (size_t i = 0; i < riders_.size(); ++i) init_live_rider(riders_[i], i == 0 ? 1 : 4);
}

void Race::init_live_rider(Rider& r, int32_t interval) {
    r.sched_interval = interval;
    r.sched_next = ticks_;
    r.sched_last = ticks_ - interval;
    r.half_sched_interval = interval;
    r.half_sched_next = ticks_;
    r.half_sched_last = ticks_ - interval;
    r.bike.i32(0x18) = 0;
    r.half_dt = 0;
    r.sched_live = true;
    r.live = true;
    r.parked = false;
    const auto& road = route_of(r);
    r.virtual_section = section_start(road, r.sample);
    r.virtual_local = int32_t(r.sample - r.virtual_section) * 256; // 0x00405990: slice +4
    const Bike& b = r.bike;
    r.half_mirror = {b.i32(0x1c),  b.i32(0x28),  b.i32(0x20), b.i32(0xf4), b.i32(0xec), b.i32(0xf0),
                     b.i32(0x10c), b.i32(0x108), b.i32(0x60), r.sample,    r.route};
    // 0x00405c80 spawn words (spawn_brain repeats them for built riders).
    r.bike.i32(0x30c) = 600;
    r.bike.i32(0x310) = (((level_ * 5 + 0x2d) * 200) >> 8) * 600;
    r.bike.i32(0x314) = 0;
    r.bike.i32(0x318) = 0;
    // 0x00405c80: half +0x1f8 = 0x00407410, the distance to the finish.
    if (finish_q8_ != 0) r.finish_distance = finish_q8_ - b.i32(0x1c);
}

void Race::schedule_add(int rider) {
    // 0x00438aa0 for a rider created mid-race: the bike node, then the half
    // node in front of it, each ahead of existing nodes at an equal position.
    if (schedule_.empty()) return; // built lazily from riders_ by schedule_order
    for (uint32_t type : {4u, 8u}) {
        ScheduleNode n{rider, type, 0};
        n.position = n.sorted = node_position(n);
        auto at = std::find_if(schedule_.begin(), schedule_.end(),
                               [&](const ScheduleNode& o) { return o.position <= n.position; });
        schedule_.insert(at, n);
    }
}

std::vector<Race::ScheduleNode> Race::schedule_order() const {
    if (schedule_.empty()) {
        // Insert order (0x00438aa0): a node created at an equal position goes
        // after the existing ones on the ascending +0 chain, so the walk
        // (front to back) meets the later rider first and a half before its
        // bike.
        for (size_t i = 0; i < std::min(riders_.size(), built_riders_); ++i) {
            schedule_.push_back({int(i), 4, 0});
            schedule_.push_back({int(i), 8, 0});
        }
        for (auto& n : schedule_) n.position = n.sorted = node_position(n);
        std::stable_sort(schedule_.begin(), schedule_.end(),
                         [](const ScheduleNode& a, const ScheduleNode& b) {
                             if (a.position != b.position) return a.position > b.position;
                             if (a.rider != b.rider) return a.rider > b.rider;
                             return a.type > b.type;
                         });
    }
    for (auto& n : schedule_) n.position = node_position(n);
    return schedule_;
}

int32_t Race::node_position(const ScheduleNode& n) const {
    const Rider& r = riders_[size_t(n.rider)];
    if (n.type != 8) return r.bike.i32(0x1c);
    // A crashed rider's half node is the body (0x00403d60): the 0x004383e0
    // sort keys it by the body's own +0x1c.
    return r.crash_active ? r.crash_actor.motion.i32(0x1c) : r.half_mirror.position;
}

void Race::sort_schedule() {
    // 0x004383e0: insertion sort of the list by +0x1c; equal positions keep
    // their previous order.
    (void)schedule_order();
    std::stable_sort(schedule_.begin(), schedule_.end(),
                     [](const ScheduleNode& a, const ScheduleNode& b) { return a.position > b.position; });
    for (auto& n : schedule_) n.sorted = n.position;
}

int32_t live_interval(bool slice, int32_t position, int32_t ring_camera, int32_t speed, int32_t tick,
                      bool player_object) {
    // 0x00437e60 tail for +0xc4 & 0xc (bikes and halves): 30 with no slice,
    // else 1 + (distance ahead or behind the ring camera DAT_004b6e5c in
    // samples)/4, plus 1..4 for slow non-player objects before tick 300,
    // capped at 8.
    if (!slice) return 0x1e;
    int32_t d = sar(position - ring_camera, 8), v = 1;
    if (d < 4) {
        if (d < -2) v = sar(-2 - d, 2) + 1;
    } else {
        v = sar(d + 1, 2) + 1;
    }
    if (tick < 300 && speed < 0x1800 && !player_object) {
        v += speed < 0x1000 ? 2 : 1;
        if (speed < 0x800) v += 2;
    }
    return std::min(v, 8);
}

int32_t parked_pace(int32_t live_distance, int32_t live_ticks, bool ahead_of_player) {
    // 0x00406940: live distance per live tick (Q8), 95% for a rider ahead
    // of the player (DAT_00464304) or 105% behind (DAT_00464308).
    const int32_t pace = (live_distance << 8) / live_ticks;
    return ((ahead_of_player ? 95 : 105) * pace) / 100;
}

void pace_band(std::array<std::optional<int32_t>, 16>& by_place, int player_place) {
    // 0x00407610 body: by_place[k] is the pace +0x314 of the parked rider in
    // place k + 1 (empty if none).
    static constexpr int32_t ahead[3] = {-0x3c, -0x1e, -0xf}, behind[3] = {0x3c, 0x1e, 0xf};
    const int player = player_place - 1;
    int k = 0;
    for (int at = player - 1; at >= 0 && k < 3; --at) {
        if (at > 15 || !by_place[size_t(at)]) continue;
        *by_place[size_t(at)] = std::max(*by_place[size_t(at)] + ahead[k++], 3000);
    }
    k = 0;
    for (int at = player + 1; at <= 15 && k < 3; ++at) {
        if (at < 0 || !by_place[size_t(at)]) continue;
        *by_place[size_t(at)] = std::min(*by_place[size_t(at)] + behind[k++], 30000);
    }
}

int32_t Race::schedule_interval(const Rider& r, bool half) const {
    // Plan 34 C2: LOD distance from the nearest human's ring camera.
    const TrackRing* nearest = nearest_ring(half ? r.half_mirror.position : r.bike.i32(0x1c));
    const int32_t camera = nearest ? nearest->front_q8() - 0x5000 : int32_t(camera_position()) + 0x100;
    return live_interval(half ? r.half_slice_present : r.bike_slice_present,
                         half ? r.half_mirror.position : r.bike.i32(0x1c), camera,
                         half ? r.half_mirror.speed : r.bike.i32(0xec), ticks_, human(r));
}

void Race::schedule_pass(uint32_t controls, bool countdown) {
    (void)controls; // plan 34: each rider's word comes from controls_ via word()
    // 0x00438170(0): every node whose +0x0c is due runs 0x00437e60 with
    // param_3 = (+0x2c != 0) sampled before the call.
    std::vector<ScheduleNode> order = schedule_order();
    const int32_t camera = ring_ ? ring_->front_q8() - 0x5000 : int32_t(previous_camera_position_) + 0x100;
    std::vector<std::pair<int32_t, size_t>> cars;
    car_scan_keys_.clear();
    update_traffic_windows(false);
    if (traffic_ &&
        traffic_->begin_schedule(
            route_of(riders_[0]), camera + 0x5000, [this] { return random(); }, uint32_t(ticks_))) {
        const auto& pool = traffic_->cars();
        for (const auto& c : pool) car_scan_keys_.push_back(c.position);
        for (size_t i = 0; i < pool.size(); ++i)
            if (pool[i].active) cars.push_back({pool[i].position, i});
        std::stable_sort(cars.begin(), cars.end(),
                         [](const auto& a, const auto& b) { return a.first > b.first; });
    }
    size_t car = 0;
    auto run_cars = [&](int32_t position) {
        while (car < cars.size() && cars[car].first >= position) {
            prepare_traffic_obstacles();
            const auto& player = riders_[0];
            traffic_->run_car(
                cars[car++].second, route_of(player), camera, camera + 0x5000, player.bike.i32(0x1c),
                player.bike.i32(0x28), [this] { return random(); }, uint32_t(ticks_), player.bike.i32(0x108));
        }
    };
    // Pedestrian nodes (+0x20) sit in the same position-sorted ring: the walk
    // meets them at their +0x1c among the bikes (0x00438170 walks one list).
    // 0x00437df0 (the scheduler's prepass) removes last tick's marked nodes.
    std::vector<std::pair<int32_t, size_t>> peds;
    ped_scan_keys_.clear();
    if (pedestrians_) {
        for (auto& p : pedestrians_->pool())
            if (p.pending_remove) {
                p.active = false;
                p.pending_remove = false;
            }
        for (const auto& p : pedestrians_->pool()) {
            ped_scan_keys_.push_back(p.body.bike.i32(0x1c));
            if (p.active && p.next_tick <= ticks_)
                peds.push_back({p.body.bike.i32(0x1c), size_t(&p - pedestrians_->pool().data())});
        }
    }
    std::stable_sort(peds.begin(), peds.end(),
                     [](const auto& a, const auto& b) { return a.first > b.first; });
    size_t ped = 0;
    auto run_peds = [&](int32_t position) {
        while (ped < peds.size() && peds[ped].first >= position)
            run_ped_node(pedestrians_->pool()[peds[ped++].second]);
    };
    for (size_t at = 0; at < order.size(); ++at) {
        const ScheduleNode n = order[at];
        run_cars(n.position);
        run_peds(n.position);
        Rider& r = riders_[size_t(n.rider)];
        if (n.type == 8) {
            if (r.half_sched_next <= ticks_) run_half_node(r, word(r), countdown);
        } else if (r.sched_next <= ticks_)
            run_bike_node(r, word(r), countdown);
        if (schedule_.size() != order.size()) {
            // The original walks the live ring (0x00438170): a node inserted
            // mid-pass (the periodic cop, 0x00406b80) behind the cursor is met
            // in the same pass. Resume after the current node in the new list.
            order = schedule_;
            bool found = false;
            for (size_t k = 0; k < order.size() && !found; ++k)
                if (order[k].rider == n.rider && order[k].type == n.type) {
                    at = k;
                    found = true;
                }
            if (!found && at > 0) --at; // the current node was unlinked: the next one shifts into its slot
        }
    }
    run_cars(INT32_MIN);
    run_peds(INT32_MIN);
}

void Race::parked_pace_band() {
    // 0x00407610, every 60 ticks: parked riders nearest ahead of the player
    // in place order lose 60/30/15 of pace (DAT_004642e8, floor 3000), the
    // three nearest behind gain 60/30/15 (DAT_004642f8, cap 30000). A parked
    // rider qualifies unless its +0x304 node and the player's lie on the two
    // branches of one split (0x00454040 node flags 0x40/0x80); the branch
    // test is not modelled (every parked rider qualifies), which holds while
    // the field stays on the trunk.
    if (pace_band_tick_ > ticks_ || riders_.empty()) return;
    pace_band_tick_ = ticks_ + 0x3c;
    std::array<std::optional<int32_t>, 16> by_place{};
    std::array<Rider*, 16> who{};
    for (auto& r : riders_)
        if (!r.cop && !r.live && r.place > 0 && r.place < 0x11) {
            by_place[size_t(r.place - 1)] = r.bike.i32(0x314);
            who[size_t(r.place - 1)] = &r;
        }
    // Plan 34: the band forms around every human in seat order (one human:
    // the original band around the player).
    for (int seat = 0; seat < humans(); ++seat) {
        const Rider& h = riders_[size_t(human_rider(seat))];
        if (seat == 0 || human(h)) pace_band(by_place, h.place);
    }
    for (size_t k = 0; k < 16; ++k)
        if (who[k]) who[k]->bike.i32(0x314) = *by_place[k];
}

void Race::run_half_node(Rider& r, uint32_t controls, bool countdown) {
    // 0x00437e60 on a type-8 node: think 0x00407bd0 (0x00434660 for the
    // player) unless parked, move 0x00406890, interval, next. The window
    // and lateral calls reach 0x00406b40, which only clamps a live half's
    // lateral when called with edx 1.
    r.half_dt = ticks_ - r.half_sched_last;
    r.half_sched_last = ticks_;
    if (!r.parked) think_half(r, controls, countdown);
    // 0x00406890 (the half move): while the bike is crashed the half runs
    // the detached body's own motion (0x0043adc0) at the half node's dt,
    // then the +0x1f8 finish delta and the place refresh - the body words
    // and the ring node position stay live for the scans behind it.
    if (crash_library_ && r.bike.i32(0x2f4) && r.crash_active) crash_body_motion(r, r.half_dt);
    r.half_sched_interval = schedule_interval(r, true);
    if (r.live && (r.half_mirror.lateral < -0xc8000 || r.half_mirror.lateral > 0xc8000))
        r.half_mirror.lateral = std::clamp<int32_t>(r.half_mirror.lateral, -0xc8000, 0xc8000);
    // While crashed the half is the crash body, so the clamp lands on the
    // body's own +0x28 and the walk-back starts from the edge (traffic-field
    // slice capture tick 1923: original 819200 - 1002, native kept walking
    // from the unclamped body).
    if (r.live && r.crash_active) {
        Bike& body = r.crash_actor.motion;
        if (body.i32(0x28) < -0xc8000 || body.i32(0x28) > 0xc8000) {
            body.i32(0x28) = std::clamp<int32_t>(body.i32(0x28), -0xc8000, 0xc8000);
            r.half_object.i32(0x28) = body.i32(0x28);
        }
    }
    r.half_sched_next = ticks_ + r.half_sched_interval;
}

void Race::think_half(Rider& r, uint32_t controls, bool countdown) {
    const bool player = human(r);
    if (r.finished) return;
    // The countdown ticks run the whole think (0x00407f30 advances the
    // riding animation from tick 1 on napa-field-l1-20261017).
    (void)countdown;
    if (crash_library_ && r.bike.i32(0x2f4)) {
        // 0x00434660 routes every crashed rider to 0x00407bd0 (and the
        // crashed branch skips the player's input block): the transition
        // latches state 9 (state 8 for crash states 2/3), the state clock
        // and replan accumulator add the half dt, and the stamina regen
        // runs its crash-gated passes. The think tail 0x00407f30 then
        // advances the crash rider animation and the +0x2f4 phase off the
        // body words left by the previous half move; the walk-back and
        // remount states follow it on the same half block.
        if (!r.crash_active) crash(r); // entry applies once, on the first node to run
        ai_think(r);
        // 0x00434660 wrapper stamina regen (lines 15-24): the player half
        // regenerates +dt whenever crash != 1, on top of the 0x00407bd0
        // passes (fight 2239: crash 2 needs +3/tick, not the +2 the passes
        // alone give). Uncrashed this is fight()'s regen; crash 1 gets none.
        if (player && r.bike.i32(0x2f4) != 1 && r.stamina.current < r.stamina.max)
            r.stamina.current = std::min(r.stamina.max, r.stamina.current + r.bike.i32(0x18));
        crash_recovery(r, player, controls);
        if (player) police_think(r); // a rider on foot can be arrested
        if (r.crash_active)
            crash_phases(r);
        else
            advance_ride_animation(r, player);
        return;
    }
    const bool demo_player = demo_hold_ && player;
    fight(r, demo_player ? 0 : controls, size_t(&r - riders_.data()));
    rider_think(r, controls, player);
}

void Race::run_bike_node(Rider& r, uint32_t controls, bool countdown) {
    // 0x00437e60 on a type-4 node.
    Bike& b = r.bike;
    const bool slice = r.bike_slice_present;
    b.i32(0x18) = ticks_ - r.sched_last;
    r.sched_last = ticks_;
    // 0x00407a90: scenery-mark animation on the bike mirrors the half's
    // +0x1e9, advances once per four absolute ticks and expires after12.
    // Disassembly words (407acd-407b0b): sample check bike +0x1c>>8 vs half
    // +0x1ec, gap unsigned bike +0x10 minus half +0x1f0. Native keeps the
    // +0x10 image in Rider::sched_last (the raw Bike word +0x10 is never
    // stored; it must not be read), stored to ticks_ two lines above, and
    // r.sample equals bike +0x1c>>8 at every bike think (0 mismatches over
    // the 2400-tick field replay, October 25 probe), so these forms are
    // behavior-identical to the disassembly here.
    if (!r.parked && b.i32(0x2f4) != 1 && b.i32(0x2f4) != 2) {
        auto& half = r.half_object;
        if (half.u8(0x1e9)) {
            const uint32_t elapsed = uint32_t(r.sched_last) - uint32_t(half.i32(0x1f0));
            half.u8(0x1e9) =
                int32_t(r.sample) == half.i32(0x1ec) && elapsed < 12 ? uint8_t((elapsed >> 2) + 1) : 0;
        }
        b.u8(0x81) = half.u8(0x1e9);
    }
    // Think 0x00407a90 (nulled while parked): the player's nitro charge
    // 0x0040b1a0; its animation mirror is part of the native riding block.
    if (!r.parked && human(r) && !countdown) nitro_charge_tick(r);
    // Move 0x004060b0.
    if (r.live)
        move_bike_live(r, controls, countdown);
    else
        move_bike_parked(r);
    // 0x00437e60: a bike left on an end slice of the ring parks (+0xe8, edx 0).
    if (ring_ && r.bike_slice_present) {
        const int32_t s = b.i32(0x1c) >> 8;
        bool outside = true; // every window (plan 34 C2; one ring: the original test)
        for (const TrackRing* w : rings_)
            if (w && !(s < (w->back_q8() >> 8) || s > (w->front_q8() >> 8) - 1)) outside = false;
        if (outside) park(r);
    }
    r.sched_interval = schedule_interval(r, false);
    // Lateral beyond +-0xc8000: 0x00406940 with edx 1 clamps a live bike.
    if (r.live && (b.i32(0x28) < -0xc8000 || b.i32(0x28) > 0xc8000))
        b.i32(0x28) = std::clamp<int32_t>(b.i32(0x28), -0xc8000, 0xc8000);
    r.sched_next = ticks_ + r.sched_interval;
    // 0x00438108 window test on a run that began with a slice: within 0x1e00
    // of the ring front or 0x800 of the ring back parks the bike.
    if (slice && ring_ && outside_windows(b.i32(0x1c), 0x1e00, 0x800)) park(r);
}

void Race::move_bike_live(Rider& r, uint32_t controls, bool countdown) {
    // 0x004060b0 live branch: physics 0x0043a580, then (uncrashed) the half
    // copy 0x004067d0 and standings, and the +0x30c/+0x310 sums.
    Bike& b = r.bike;
    const bool player = human(r);
    const int32_t before = b.i32(0x1c);
    const auto& road = route_of(r);
    const uint32_t node_before = road[std::min(r.sample, road.size() - 1)].node;
    // The countdown runs the same physics move: with no target speed the
    // bike stays put, but its ground contact word settles (+0x160 37 -> 74
    // at tick 1 on napa-field-l1-20261017).
    (void)countdown;
    {
        if (r.finished) { // coast to a stop past the line
            drive_mode(b, 0);
            integrate_speed(b, tables_, 0);
        } else if (crash_library_ && b.i32(0x2f4)) {
            // 0x004060b0's +0x300 path: the riderless bike keeps its own
            // animation advance (the crashed branch of the bike think
            // 0x00407a90) and the 0x0043a580 move on the bike node; the
            // crash phases, the body motion and the recovery belong to the
            // half node (think_half / run_half_node).
            if (!r.crash_active) crash(r); // entry applies once, on the first node to run
            crash_bike_animation(r);
            crash_bike_motion(r, player);
            // 0x004060b0 lines 46-67: the crashed bike and its detached half
            // (the crash body, object +0x2fc) drift apart as the body keeps
            // sliding on its own drag. Once they sit more than 0x500 apart
            // along the road, the bike's speed eighths toward the half's: the
            // slower side gains (|half| - |bike|) / 8, or, when the half is
            // slower, the bike sheds its own speed / 8. The standings call
            // 0x0043c7e0 that follows inside that block is not ported here.
            const Bike& body = r.crash_actor.motion;
            if (std::abs(b.i32(0x1c) - body.i32(0x1c)) > 0x500) {
                const int32_t bike_speed = b.i32(0xec);
                const int32_t half_speed = body.i32(0xec);
                const int32_t bike_abs = std::abs(bike_speed);
                const int32_t half_abs = std::abs(half_speed);
                if (half_abs < bike_abs)
                    b.i32(0xec) = bike_speed - sar(bike_speed, 3);
                else
                    b.i32(0xec) = bike_speed + sar(half_abs - bike_abs, 3);
            }
        } else {
            rider_move(r, player);
        }
    }
    const int32_t after = b.i32(0x1c);
    if (b.i32(0x2f4) == 0) {
        live_standings(r);
        // Half +0x1f8: moved along the same node, subtract the move;
        // otherwise 0x00407410 recomputes it (finish - position once the
        // finish is known). Contact pushes between moves are not folded in.
        if (road[std::min(r.sample, road.size() - 1)].node == node_before)
            r.finish_distance += before - after;
        else if (finish_q8_ != 0)
            r.finish_distance = finish_q8_ - after;
        if (r.place_refresh <= r.half_sched_last) refresh_place(r); // 0x004075f0 -> 0x00407550
    }
    b.i32(0x310) += after - before;
    b.i32(0x30c) += b.i32(0x18);
}

void Race::refresh_place(Rider& r) {
    const bool player = human(r);
    const size_t i = size_t(&r - riders_.data());
    int place = 1;
    for (size_t j = 0; j < riders_.size(); ++j) {
        if (j == i || riders_[j].cop) continue; // the place table 0x004c7fb0 holds no cop
        const Rider& o = riders_[j];
        const bool shared = o.half_slice_present && r.half_slice_present &&
                            !(route_of(r)[r.sample].dual && o.route != r.route);
        if (shared ? o.half_mirror.position > r.half_mirror.position : o.finish_distance < r.finish_distance)
            ++place;
    }
    r.place = place;
    r.place_refresh = r.half_sched_last + (player ? 4 : 0x3c);
}

void Race::live_standings(Rider& r) {
    // 0x004067d0: an uncrashed bike copies its motion words and slice to
    // its half.
    const Bike& b = r.bike;
    r.half_mirror = {b.i32(0x1c),  b.i32(0x28),  b.i32(0x20), b.i32(0xf4), b.i32(0xec), b.i32(0xf0),
                     b.i32(0x10c), b.i32(0x108), b.i32(0x60), r.sample,    r.route};
    r.half_slice_present = r.bike_slice_present;
}

void Race::park(Rider& r) {
    // 0x00406940 with edx 0 on a live opponent (+0x324 != 0).
    if (!r.live || human(r)) return; // humans never park (0x00406940 skips +0x324 == 0)
    if (r.cop) {                     // single player: 0x004073a0 deletes the cop instead of parking it
        // 0x004073a0: siren off (event 0x13), DAT_004642dc = 0 at once, and
        // 0x00437c10 marks the half and bike (0x00437be0: +0xc0 &= ~4,
        // +0xc = +0x10 = tick + 100; then +0xc0 |= 8). The next scheduler
        // prepass (0x00437df0) unlinks them, so the rider is still dumped on
        // its deletion tick (traffic-field box capture tick 4104: bike_c =
        // bike_10 = 4204, gone on 4105).
        siren_event(r, false);
        for (Bike* b : {&r.bike, &r.half_object}) b->u8(0xc0) = uint8_t((b->u8(0xc0) & 0xfb) | 8);
        r.sched_next = r.sched_last = r.half_sched_next = r.half_sched_last = ticks_ + 100;
        r.cop = false;
        cop_removal_pending_ = true;
        return;
    }
    Bike& b = r.bike;
    b.i32(0x318) = 0;
    if (b.i32(0x2f4) != 0) {
        if (b.i32(0x2f4) == 1) {
            const int32_t v = r.half_mirror.speed;
            b.i32(0x318) = sar((v >> 9) + (v >> 12) + (v >> 7), 5) * 0x3c;
        }
        b.i32(0x318) += sar(std::abs(b.i32(0x1c) - r.half_mirror.position), 8) * 0x3c;
        // 0x00404560: crashed-rider cleanup remounts the rider, restores
        // stamina, copies bike words to the half (0x004067d0) and recomputes
        // finish distance (0x00407410).
        if (crash_library_) {
            remount_crash_actor(b, r.crash_bike_animation, r.crash_actor, crash_config_,
                                crash_library_->playback[0], crash_library_->fallback, uint32_t(ticks_));
            r.ride_animation = r.crash_actor.animation;
        } else {
            b.i32(0x2f4) = 0;
        }
        r.stamina.current = r.stamina.max;
        r.crash_active = false;
        live_standings(r);
        if (finish_q8_ != 0) r.finish_distance = finish_q8_ - r.half_mirror.position;
    }
    // Pace: live distance per live tick (Q8), 95% ahead of the player
    // (DAT_00464304) or 105% behind (DAT_00464308).
    // +0x304/+0x308 follow the bike's slice: 0x00439390 copies the node and
    // local offset (whole samples) at every sample crossing.
    const auto& road = route_of(r);
    r.virtual_section = section_start(road, r.sample);
    r.virtual_local = int32_t(r.sample - r.virtual_section) * 256;
    b.i32(0x314) =
        parked_pace(b.i32(0x310), b.i32(0x30c), nearest_human(b.i32(0x1c)).bike.i32(0x1c) < b.i32(0x1c));
    r.live = false;  // 0x00431960(bike, 0)
    r.parked = true; // +0xdc/+0xe0 of bike and half to +0x31c/+0x320 / +0x200/+0x204
    b.u8(0xc0) &= 0xfc;
    r.bike_slice_present = r.half_slice_present = false;
}

void Race::move_bike_parked(Rider& r) {
    // 0x004060b0 parked branch (+0x300 clear).
    Bike& b = r.bike;
    const auto& road = route_of(r);
    bool section_changed = false;
    if ((b.u8(0xc0) & 0x10) == 0) {
        int32_t step = 0;
        const int32_t dt = b.i32(0x18), pace = b.i32(0x314);
        if (b.i32(0x318) < 1) {
            if (nearest_human(b.i32(0x1c)).bike.i32(0x2f4) == 0)
                step = dt * pace >> 8;
            else if (level_ == 3)
                step = sar(pace * 3, 2) * dt >> 8;
            else if (level_ == 4)
                step = dt * pace >> 8;
            else
                step = (pace / 2) * dt >> 8;
        } else {
            b.i32(0x318) -= dt;
        }
        r.virtual_local += step;
        r.finish_distance -= step;
        b.i32(0x1c) += step;
        r.half_mirror.position = b.i32(0x1c);
        // Virtual finish: the finish section's +0x10 -> +0x2c finish sample.
        if (finish_q8_ != 0) {
            const size_t finish = size_t(finish_q8_ >> 8);
            if (finish < road.size() && section_start(road, finish) == r.virtual_section) {
                const int32_t local = int32_t(finish - r.virtual_section) * 256;
                if (local != 0 && r.finish_distance < 1) {
                    if ((b.u8(0xc0) & 0x10) == 0) { // 0x00437800
                        b.u8(0xc0) |= 0x10;
                        r.half_object.u8(0xc0) |= 0x10;
                        if (!r.finished && !r.cop) {
                            r.finished = true;
                            r.finish_tick = ticks_;
                        }
                    }
                    section_changed = true;
                    r.virtual_local = local + int32_t(random() % 0x701) + 0x100;
                }
            }
        }
        // Section walk over +0x304/+0x308 (the route's own branch at splits).
        int32_t length = section_length(road, r.virtual_section);
        while (length * 256 <= r.virtual_local) {
            section_changed = true;
            const size_t next = r.virtual_section + size_t(length);
            if (next >= road.size()) { // end node (link types 3/4)
                r.virtual_local = (length - 1) * 256;
                b.i32(0x314) = 0;
                break;
            }
            r.virtual_local -= length * 256;
            r.virtual_section = next;
            length = section_length(road, r.virtual_section);
        }
        if (section_changed && finish_q8_ != 0) r.finish_distance = finish_q8_ - b.i32(0x1c); // 0x00407410
    }
    // Wake test against the ring trackers in this section (TrackRing walks
    // both split branches in lockstep, so road[t0]/road[t1] are the tracker
    // positions on r's branch).
    // Plan 34 C1: every human's window, in seat order; the first that
    // reaches the rider wakes it (one ring: the original test).
    for (const TrackRing* w : rings_) {
        if (!w) continue;
        const int32_t back = w->back_q8() >> 8, front = w->front_q8() >> 8;
        const size_t t0 = size_t(back + tracker_back), t1 = size_t(back + tracker_front);
        if (t1 >= road.size()) continue;
        const uint32_t section = road[r.virtual_section].section;
        const bool has0 = road[t0].section == section, has1 = road[t1].section == section;
        if (!has0 && !has1) continue;
        const int32_t length = section_length(road, r.virtual_section);
        const int32_t lo = has0 ? int32_t(road[t0].local) * 256 : 0;
        const int32_t hi = has1 ? int32_t(road[t1].local) * 256 : length * 256 - 1;
        const int32_t v = r.virtual_local;
        if (v < lo || hi < v) continue;
        bool use0 = has0;
        if (has0 && has1) use0 = std::abs(v - lo) <= std::abs(v - hi);
        const int32_t slice = int32_t(use0 ? t0 : t1);
        // 0x00449910: step whole slices from the tracker's slice, clamped to the ring.
        int32_t steps = sar(v - (use0 ? lo : hi), 8);
        if (steps >= 1 && front <= steps + slice) steps = front - slice - 1;
        if (steps < 0 && steps + slice < back) steps = back - slice;
        wake(r, size_t(slice + steps));
        return;
    }
}

void Race::wake(Rider& r, size_t sample) {
    // 0x004060b0 tail: the bike lands mid-slice on the road centre, takes
    // its straight and overtake targets, rides at its pace, and becomes
    // live again with its callbacks restored.
    Bike& b = r.bike;
    const auto& road = route_of(r);
    sample = std::min(sample, road.size() - 1);
    const int32_t pos = int32_t(sample) * 256 + 0x80;
    b.i32(0x1c) = pos;
    b.i32(0x20) = 0;
    b.i32(0x24) = 0;
    b.i32(0x28) = road[sample].offset * 256 + road_base_; // sub-slice +0xcc
    r.sample = r.previous_sample = sample;
    b.i32(0x134) = b.i32(0x138) = pos;
    r.bike_slice_present = true;
    r.virtual_section = section_start(road, sample);
    r.virtual_local = int32_t(sample - r.virtual_section) * 256;
    ai_straight_target(r);
    b.i32(0x28) = r.brain.target_x;
    ai_overtake(r);
    b.i32(0x28) = r.brain.target_x;
    b.i32(0xf0) = 0;
    b.i32(0xf4) = 0;
    b.i32(0xec) = (b.u8(0xc0) & 0x10) == 0 ? b.i32(0x314) : 0;
    r.parked = false;
    b.u8(0xc0) |= 3;
    r.live = true;                            // 0x00431960(bike, 1)
    if (b.i32(0x2f4) == 0) live_standings(r); // 0x004067d0
    // 0x00407550: refresh the half's place now.
    const size_t i = size_t(&r - riders_.data());
    int place = 1;
    for (size_t j = 0; j < riders_.size(); ++j) {
        if (j == i || riders_[j].cop) continue;
        const Rider& o = riders_[j];
        const bool shared =
            o.half_slice_present && r.half_slice_present && !(road[r.sample].dual && o.route != r.route);
        if (shared ? o.half_mirror.position > r.half_mirror.position : o.finish_distance < r.finish_distance)
            ++place;
    }
    r.place = place;
    r.place_refresh = r.half_sched_last + 0x3c;
}
} // namespace rr
