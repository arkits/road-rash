#pragma once
#include "animation.hpp"
#include "actor_descriptors.hpp"
#include "cel.hpp"
#include "course.hpp"
#include "race.hpp"
#include "resource.hpp"
#include "road.hpp"
#include "sound_event.hpp"
#include "world_renderer.hpp"
#include <array>
#include <functional>
#include <optional>
#include <vector>

namespace rr {

// Vehicle library from a course's DATA/CARS/<name>.CAR container, following
// the original loader 0x004465f0: vehicle i pairs the i-th 'ANIM' (RRAN)
// animation with the i-th 'CANS' wrapper when present. The vehicle mask
// 0x004b8fe0[i] is the CCB flags word inside the CANS 'SNAC' wrapper, read
// through the 0x0040b5f0/0x0040b620 chain; vehicles without a CANS inherit the
// previous pair's wrapper, as measured against the original initializer.
struct Vehicle {
    Animation animation;
    uint32_t mask = 0;
    ActorGraphicsGroups views;
    ActorAnimationBank playback;
};
class VehicleLibrary {
  public:
    // Throws std::runtime_error on malformed container entries.
    explicit VehicleLibrary(const std::vector<uint8_t>& cars_container, const std::vector<uint8_t>& exe = {});
    const std::vector<Vehicle>& vehicles() const { return vehicles_; }
    // 0x0044df60 type choice: accepted vehicles are those whose mask selects
    // no speed class ((mask & 0x1c0000) == 0) or the class matching the kind
    // filter; the pick is rand() % accepted count. Returns -1 when none.
    int choose(uint8_t kind, uint32_t random) const;

  private:
    std::vector<Vehicle> vehicles_;
};

// Spawn timers, ports of 0x0044dd00 / 0x0044dd90. The rate is 0x19*100
// (0x0044dd00(-25)); the reloads scale with the density argument.
struct TrafficTimers {
    int32_t cruise = 25;                           // 0x004b7e8c: car cruise scale (0x0044e710)
    int32_t rate = 2500;                           // 0x004b7e84 / 0x004b7ea4
    int32_t oncoming_reload = 0, other_reload = 0; // 0x004b7e88 / 0x004b7e9c
    int32_t oncoming = 0, behind = 0, ahead = 0;   // 0x004b7e90 / 0x004b7e94 / 0x004b7e80
    int density = 6;                               // 0x004b7ea0
};
// 0x0044dd00. Negative: rate -d*100 and cruise -d, reloads untouched.
// Otherwise density (0 means 6) and both reloads from the current rate; the
// oncoming timer restarts at its reload, behind/ahead keep running.
void traffic_set_density(TrafficTimers&, int density, bool net = false); // net: x4 (0x0044dd00)
// 0x0044db80's sequence: 0x0044dd00(-25) then 0x0044dd00(density), then the
// behind/ahead timer starts. A negative density only sets the rate.
void traffic_configure(TrafficTimers&, int density, bool net = false);

// Lane count of a ring sub-slice lane word in 256ths, with the original's
// signed-char semantics: (int8_t)((value + (value < 0 ? 0xff : 0)) >> 8).
int traffic_lane_count(int32_t lanes_q8);

// One car object. Field comments name the original object offsets
// (0x004387a0 template copies and 0x0044df60 writes).
struct TrafficCar {
    uint8_t kind = 0;                            // +0x1cc
    uint8_t vehicle = 0;                         // +0x1fc
    int8_t lane = 0;                             // +0x1e8
    int32_t position = 0;                        // +0x1c: Q8 samples along the route
    int32_t lateral = 0;                         // +0x28: Q8 world lateral
    int32_t speed = 0;                           // +0xec
    int32_t phase = 0;                           // +0x1c4: (rand & 0x7f) + 0xc0, 192..319
    uint8_t jitter_a = 0;                        // +0x1c8
    uint8_t jitter_b = 0;                        // +0x1c9
    uint8_t jitter_c = 0;                        // +0x1ca
    uint32_t serial = 0;                         // +0x208 (DAT_004b7e98)
    bool active = false, pending_remove = false; // marked now, returned by next scheduler prepass
    int32_t last_tick = 0, next_tick = 0, interval = 10, dt = 0; // +0x10/+0xc/+0x14/+0x18
    Bike motion;         // 0x0043adc0 physics words, persistent per pool slot
    int32_t heading = 0; // +0x5c: 0 ahead, -0x800000 oncoming, +-0x400000 crossing (0x00449880)
    int signal = -1;     // +0x1ec: latched signal (signals() index), -1 none
    int32_t zone_start = 0, zone_end = 0; // +0x1d0/+0x1d4: controller +0x108/+0x10c (crossing cars)
    int zone_lanes = 0;                   // +0x1d9: controller +0x111
    bool collides = true;                 // +0xc0 & 3: crossing cars only near the ring camera (0x0044e710)
    int32_t steer_error = 0;              // +0x1dc: lateral error toward the lane centre (0x00449880 input)
    bool lane_changed = false;            // 0x0044e710 bVar2: this think moved the car past a slower obstacle
};
// A non-car scheduler object for the 0x0044a070 lane scans (mask 0x3f):
// bikes (4), rider halves on foot (8) and pedestrians (0x20). Race supplies
// these each tick; positions/laterals are Q8, velocities +0x108/+0x10c.
struct TrafficObstacle {
    int32_t position = 0, lateral = 0, velocity = 0, lateral_velocity = 0;
    uint32_t type = 4;
};
// Intersection controller (0x0041eca0; pool 0x00468ee4, 10 x 0x114, type
// 0x400, think 0x0041f040 every 0x1e ticks, teardown 0x0041f1f0) and its
// signals (0x0041ef60; pool 0x00468ee0, 40 x 0x108, type 0x40 << index,
// no think). Field comments name the original offsets.
struct IntersectionSignal {
    bool active = false;
    int index = 0;           // +0xf8: 0..3; odd indices spawn crossing cars; type 0x40 << index
    int side = 0;            // 0x0041ef60 param_2: 0 left shoulder, 1 right
    int32_t phase = 0;       // +0xf0: 0..2, starts (~index & 1) * 2
    int32_t accum = 0x30c;   // +0xf4
    int lane = 0;            // +0xfc byte: zone length - 2 (indices 0/2) or start lanes (1/3)
    int32_t position_q8 = 0; // +0x1c: corner slice * 0x100 + 0x50 / 0xb0
    int32_t lateral_q8 = 0;  // +0x28: middle of that side's shoulder
    int controller = -1;     // +0xec
    bool quiet = false;      // 0x0041eca0 bVar13: clears +0xdc (0x0041f220) when true
};
struct IntersectionController {
    bool active = false;
    int32_t durations[3] = {600, 180,
                            780};     // +0xfc..+0x104; {75, 0, 240} on a dual type-4 slice with flag byte 0
    int32_t start_q8 = 0, end_q8 = 0; // +0x108 / +0x10c: event start / end on the route
    int variant = 1;                  // +0x110: event type 1..3
    int lanes = 0;                    // +0x111: record byte +9, the cross road's lane span
    int32_t position_q8 = 0;          // +0x1c: zone-end slice, clamped into the ring window
    int32_t last_tick = 0, next_tick = 0; // +0x10 / +0xc scheduler words (interval 0x1e)
    int signals[4] = {-1, -1, -1, -1};    // +0xec/+0xf0/+0xf4/+0xf8: signal index 0, 1, 2, 3
    bool pending_remove = false;          // 0x0041f1f0 marked; unlinked by the next prepass
};
class TrafficSystem {
  public:
    static constexpr size_t pool_size = 16;       // 0x00438b70(0x210, 0x10): 16 objects
    static constexpr size_t controller_pool = 10; // 0x00468ee4: 10 x 0x114
    static constexpr size_t signal_pool = 40;     // 0x00468ee0: 40 x 0x108
    // DAT_004b7ecc while 0x004552e0 builds the first ring (objects created
    // there get +0xc next = 1, +0x10 last = 1 - interval); the player and
    // grid built later by 0x00405600 see 0. Setup controllers think on
    // 1 + 30k (police capture: 0x0041f040 at tick 751) and their teardown,
    // marked on a think, unlinks one tick later (2 + 30k on the solo
    // captures and napa-field-l1-20261017, tick 392). Earlier notes read the
    // unlink tick as the creation phase (2); corrected October 28, 2026.
    uint32_t setup_tick = 1;
    void set_setup_tick(uint32_t t) { setup_tick = t; }
    // Nulled-pool solo equivalent: the oracle zeroes the car pool count, so
    // 0x0044df60 returns on entry AFTER 0x0044dd90/0x0041f040 drew the lane
    // pick -- one stream draw per gated expiry, zero cars. Disabled spawns
    // reproduce exactly that (draw, no placement, no serial bump); live game
    // and field replays keep the default enabled. Car motion/horn never run
    // with an empty pool either way.
    void set_spawn_enabled(bool v) { spawn_enabled_ = v; }
    TrafficSystem(const VehicleLibrary&, std::vector<uint8_t> exe);
    void configure(int density) {
        configured_density_ = density;
        traffic_configure(timers_, density, net_);
    }
    // Plan 34 Task 2 C3: networked race. Density x4 (0x0044dd00), no odd
    // (crossing) kinds (0x0044df60), and the spawn gate below. Set before
    // configure().
    void set_net(bool v) { net_ = v; }
    // One human's traffic window for this tick. Seat 0's camera/front are
    // still the classic call arguments; its entry carries only the gate.
    // ahead/behind: another human within 0x4600 ahead / 0x2d00 behind on a
    // shared roadway (0x0044f0d0); spawns then stay at or behind / at or
    // ahead of this human (0x0044df60).
    struct SeatWindow {
        int32_t camera_q8 = 0, front_q8 = 0, distance = 0, me_q8 = 0, velocity = 0;
        bool ahead = false, behind = false;
    };
    // Seat order; [0] is seat 0. Seats >= 1 keep their own timers and slice
    // cursor; a slice inside another window when built places nothing.
    void set_seat_windows(const std::vector<SeatWindow>& w);
    // One simulation tick on the player's route. camera/front are the ring
    // cursors 0x004b6e5c (camera) and 0x004b6e50 (front, ring camera +
    // 0x5000). The ring camera starts at 0x3200 (0x0044b250) against the
    // view camera's 0x3100 and both advance by the same delta (0x0044b3b0),
    // so front = view camera + 0x5100 (static trace, not capture-checked).
    // Slices entering the ring front apply their zone events (see
    // PathSample::zone_end) before the spawn timers run, as 0x00454120
    // precedes 0x0044dd90; the first step also replays the initial ring
    // fill (0x004552e0, back to front over 128 samples).
    // 0x0041eca0 crossing traffic at intersections: controllers are placed
    // from type-1..3 zone ends in build_slice; think_intersections advances
    // phases and spawns crossing cars (kinds 1/3) before the car move pass.
    // 0x00438170 precedes the driver's ring advance and 0x0044dd90. Signal heads are
    // never drawn: DAT_00468ee8/ef0 have no writer anywhere in the
    // executable (REA xrefs: only the 0x0041f220 reads), so 0x0041f220's
    // draws are skipped and the port draws nothing, matching the original.
    // distance_moved is the per-step camera advance (0x0044b3b0). player_q8
    // and player_lateral are supplied by Race. tick is the global
    // tick (DAT_004b7ecc bit 3 gates crossing spawns). random is the
    // shared CRT rand stream; the original call order is preserved.
    void step(const std::vector<RouteSample>& route, int32_t camera_q8, int32_t front_q8,
              int32_t distance_moved, int32_t player_q8, int32_t player_lateral,
              const std::function<uint32_t()>& random, uint32_t tick, int32_t player_velocity = 0);
    // Scheduler phase before 0x004368f0 contacts. step() then runs only
    // the driver's ring/timer phase when this tick was already scheduled.
    void schedule(const std::vector<RouteSample>& route, int32_t camera_q8, int32_t front_q8,
                  int32_t player_q8, int32_t player_lateral, const std::function<uint32_t()>& random,
                  uint32_t tick, int32_t player_velocity = 0);
    // The live position-ordered walk dispatches each car between rider nodes.
    bool begin_schedule(const std::vector<RouteSample>& route, int32_t front_q8,
                        const std::function<uint32_t()>& random, uint32_t tick);
    void run_car(size_t index, const std::vector<RouteSample>& route, int32_t camera_q8, int32_t front_q8,
                 int32_t player_q8, int32_t player_lateral, const std::function<uint32_t()>& random,
                 uint32_t tick, int32_t player_velocity);
    // Hold tick: initialize ring zone events, then run 0x0044dd90 with
    // zero camera delta. Do not move cars or think intersection controllers.
    void countdown_step(const std::vector<RouteSample>& route, int32_t front_q8,
                        const std::function<uint32_t()>& random, uint32_t tick);
    const std::vector<TrafficCar>& cars() const { return cars_; }
    // 0x004368f0 car pass: the contact response writes the car's share of
    // the separation (lateral push; the original also runs 0x00436a40 on the
    // car object, which has no scratch equivalent — see analysis/car-notes.md).
    std::vector<TrafficCar>& cars_mut() { return cars_; }
    const TrafficTimers& timers() const { return timers_; }
    int32_t zone_events() const { return zone_events_; } // density changes applied so far
    const std::vector<IntersectionController>& controllers() const { return controllers_; }
    const std::vector<IntersectionSignal>& signals() const { return signals_; }
    // 0x004415b0: phase of the nearest active signal within 0x801 Q8 of a
    // position (0x00449ce0 ring scan for signal nodes), or -1. Pedestrians
    // cross on phase 2 without a road check; other phases hold them.
    // 0x004415b0 for a pedestrian at position on a slice with these marking
    // flags in this lane: only on crosswalk edge slices (flags & 0x18), the
    // nearer of the index-0 (lane >= 0) or index-2 (lane < 0) signals within
    // 0x1e0 ahead/behind (behind wins ties), when within 0x801; else -1.
    int pedestrian_signal(uint8_t marking_flags, int32_t position_q8, int lane) const;
    // Non-car obstacles for this tick's lane scans (set by Race before step).
    std::vector<TrafficObstacle>& obstacles() { return obstacles_; }
    // Terrain type of a route sample (0x00454240 +0x3c), for 0x0041eca0's
    // short-phase test; without it the long table is always used.
    void set_terrain_type(std::function<int(const RouteSample&)> source) {
        terrain_type_ = std::move(source);
    }
    const VehicleLibrary& library() const { return *library_; }
    // 0x00437e60 car LOD after the move, using the pre-driver ring camera.
    static int32_t schedule_interval(const TrafficCar&, int32_t camera_q8, int32_t player_velocity);
    // Contact half-width +0x174 by kind (0x00475700: 0x4000, crossing 0xa000).
    static int32_t car_half_width(const TrafficCar&);
    std::vector<SoundEventRecord> take_sound_events() {
        auto e = std::move(events_);
        events_.clear();
        return e;
    }
    // Sprite size class from the car template (0x00475698 +0x28).
    uint8_t sprite_size() const { return sprite_size_; }
    int32_t drift_rate(uint8_t kind) const { return drift_rates_[kind & 3]; }
    // Plan 10: append "car.<pool index>.<field>" rows (tests/sim_fields.hpp
    // car_fields) for every pool slot, active or not.
    void append_snapshot(std::vector<FieldValue>& out) const;

  private:
    void build_slices(const std::vector<RouteSample>& route, int32_t front_q8, int32_t distance_moved,
                      uint32_t tick);
    void step_timers(const std::vector<RouteSample>& route, int32_t front_q8, int32_t back_q8,
                     int32_t distance_moved, const std::function<uint32_t()>& random);
    bool place(uint8_t kind, int8_t lane, const RouteSample& at, int32_t spawn_q8,
               const std::function<uint32_t()>& random);
    void move(TrafficCar&, const std::vector<RouteSample>& route, int32_t camera_q8, int32_t front_q8,
              int32_t back_q8, int32_t player_q8, int32_t player_lateral,
              const std::function<uint32_t()>& random);
    // 0x0044e710 speed target before the move: obstacle following, signal
    // latch and stop line. Returns the 0x00449880 target speed (x100) and
    // acceleration. front_q8 selects the ring slot the car reads its lane
    // words from (newest same-residue sample at/under the ring front).
    std::pair<int32_t, int32_t> plan_speed(TrafficCar&, const std::vector<RouteSample>& route,
                                           int32_t front_q8);
    const VehicleLibrary* library_; // pointer: copied into race snapshots (plan 34 Task 3)
    PhysicsTables physics_;
    std::array<int32_t, 26> car_template_{};
    uint32_t tick_ = 0;
    std::optional<uint32_t> scheduled_tick_;
    TrafficTimers timers_;
    std::array<int32_t, 4> drift_rates_{}; // 0x00475694[kind] signed byte * 800
    std::vector<TrafficCar> cars_{pool_size};
    std::vector<SoundEventRecord> events_;
    uint32_t serial_ = 0;       // DAT_004b7e98
    bool spawn_enabled_ = true; // false: nulled-pool solo (draw, never place)
    bool filled_ = false;       // initial ring fill replayed
    int32_t built_end_ = 0;     // ring front sample (exclusive) already built
    int32_t zone_events_ = 0;
    void build_slice(const std::vector<RouteSample>& route, int32_t sample, int32_t back_q8, int32_t front_q8,
                     uint32_t tick);
    // 0x0041eca0 placement from a type-1..3 zone end at route sample s.
    void place_intersection(const std::vector<RouteSample>& route, int32_t s, const PathSample& path,
                            int32_t back_q8, int32_t front_q8, uint32_t tick);
    int place_signal(const std::vector<RouteSample>& route, int controller, int index, int side,
                     int32_t fraction, int32_t slice, int lane); // 0x0041ef60; -1 when the pool is out
    // 0x0041f040 every 0x1e ticks, then the scheduler's ring-window teardown.
    void think_intersections(int32_t back_q8, int32_t front_q8, uint32_t tick,
                             const std::vector<RouteSample>& route, const std::function<uint32_t()>& random);
    void place_crossing(const IntersectionController& c, int signal, int kind, int lane,
                        const std::vector<RouteSample>& route, const std::function<uint32_t()>& random);
    std::vector<IntersectionController> controllers_{controller_pool};
    std::vector<IntersectionSignal> signals_{signal_pool};
    std::vector<TrafficObstacle> obstacles_;
    std::function<int(const RouteSample&)> terrain_type_;
    uint8_t sprite_size_ = 10;
    // Plan 34 C3.
    bool net_ = false;
    int configured_density_ = 6;
    struct SeatState {
        TrafficTimers timers;
        int32_t built_end = 0;
        bool filled = false;
    };
    std::vector<SeatWindow> windows_; // this tick, seat order
    std::vector<SeatState> seats_;    // seats >= 1 (index seat - 1)
    int32_t primary_front_ = 0;       // seat 0's front of the current call
    int32_t primary_built_end_ = 0;   // seat 0's cursor while a seat's state is swapped in
    bool primary_filled_ = false;
    int building_seat_ = 0; // whose slices/timers run now (gate + dedupe)
    bool in_window(int32_t q8, int32_t front_q8) const { return q8 >= front_q8 - 0x8000 && q8 < front_q8; }
    bool inside_any_window(int32_t q8, int32_t front_q8) const; // the call's window or any seat's
    bool slice_owned_elsewhere(int32_t sample) const;           // another window already built it
    int32_t nearest_camera(int32_t q8, int32_t camera_q8, int32_t& velocity) const;
    int32_t window_front_for(int32_t q8, int32_t front_q8) const; // front of the window holding q8
    void step_seat_windows(const std::vector<RouteSample>&, const std::function<uint32_t()>&, uint32_t tick,
                           bool hold);
};

// 0x004467e0 / 0x0044e710 selects one of three one-frame descriptor
// sequences by distance; 0x00446830 emits the actor billboard.
std::optional<DrawNode> traffic_car_node(const Vehicle&, const TrafficCar&, const RoadSlice&,
                                         const RoadSlice&, int road, const RouteSample&, const Camera&,
                                         const DepthTable&, int32_t camera_q8, int32_t ceiling,
                                         int32_t logical_width = 320, int32_t blend_position_q8 = 0,
                                         int32_t blend_lateral_q8 = 0, bool use_blend = false);

// Display nodes for the active cars, ordered far to near. The projection
// follows the verified scenery path (scenery_node) with the depth-table scale
// at each car's Q8 depth relative to the camera; frame cycling is a native
// approximation of the RRAN advancement 0x0043ae10/0x0043ae90, which is not
// ported.
std::vector<DrawNode> traffic_draw_nodes(const TrafficSystem&, const std::vector<RouteSample>& route,
                                         const std::vector<RoadSlice>& slices, const Camera&,
                                         const DepthTable&, int32_t camera_q8, uint32_t tick,
                                         int32_t logical_width = 320);

} // namespace rr