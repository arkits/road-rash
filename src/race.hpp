#pragma once
#include "bike.hpp"
#include "crash.hpp"
#include "actor_descriptors.hpp"
#include "road.hpp"
#include "police.hpp"
#include "sound_event.hpp"
#include "cow_vector.hpp"
#include <memory>
#include <array>
#include <vector>
#include <functional>
#include <optional>
#include <span>
#include <string>

namespace rr {
class TrafficSystem;
struct TrafficCar;
class PedestrianSystem;
struct Pedestrian;
struct DzhrActor;
class TrackRing;
struct RingActor;
struct RingStatics;
struct AnimationFrame;
// Original timing: 0x004163d0 steps the simulation at QueryPerformanceFrequency/0x3c,
// at most 0x14 steps per frame.
constexpr int ticks_per_second = 60;
constexpr int max_steps_per_frame = 20;
// Nitro configuration: six words the setup copies from the player's bike
// spec at +0x14c (0x0040b0e0: recharge, boost Q8 multiplier, max charge,
// charges, minimum activation, tail-rate seed). The EXE defaults at
// 0x00465a28 are {0x11, 0x180, 0x78, 0x64, 0x1e, 0xf}.
struct NitroConfig {
    int32_t recharge = 0x11, boost = 0x180, max = 0x78, charges = 0x64, minimum = 0x1e, tail = 0xf;
};
// Plan 26 Task 1: wiring report — which Race hooks the factory attached.
// Derived from the existing members; no new state except nitro_set_.
struct RaceWiring {
    bool terrain = false, traffic = false, pedestrians = false, ring = false, crash_library = false,
         ride_library = false, nitro = false, finish = false, personalities = false;
};
// Rider fight stats: rider table 0x00463c78 words 12..15 (+0x30 strength, +0x34 base stamina,
// +0x38 maximum, +0x3c current). Hits lower maximum by damage*4 (not below base/2) and current
// by damage*64; current regenerates one point per tick up to maximum (0x00407d40).
struct RiderStats {
    int32_t strength = 7, base = 2500, max = 2500, current = 2500;
};
// In-road roadside hazard (plan 16 Task 2): one BORR lateral slot built by
// 0x00451cb0 and instantiated by 0x00454240/0x00438930 when strictly inside
// the road edges. Lateral span in Q8, on one route sample; only spans from
// this table are solid (pictured trees outside the edges are never actors).
struct RoadHazard {
    uint32_t sample = 0;
    int32_t lo_q8 = 0, hi_q8 = 0;
};
// BORR span builder: the 0x00451cb0 lateral-slot walk over a section's BORR
// profiles, keeping only slots strictly inside the road edges (0x00454240)
// at sample fraction 0x80. sample_base is the route index of section local
// sample 0. Pure; consumes random() in the same order as the billboard walk
// for shared-stream parity.
std::vector<RoadHazard> road_hazard_spans(const CourseSection&, uint32_t sample_base,
                                          std::function<uint32_t()> random);
struct SceneryObject; // scenery.hpp (forward: race.hpp must not include it)
// DZHR solid roadside object (plan 16 Task 2 / plan 24): one placed non-actor
// billboard (0x004520d0 packs word0 bit31 clear with (word0>>8)&0x3f != 0x3f,
// at most 2 per slice; the same records place_dzhr turns into static-family
// scenery). Lateral span in Q8 absolute in the route frame; the half extent
// is the placed size-8 units (approximation: per-type template widths are
// not yet traced). type is the record low nibble (word0 & 0xf): 0/1/2 take
// the class-1 crash path, 3+ is the marking-only branch (no crash).
struct DzhrSolid {
    int32_t lo_q8 = 0, hi_q8 = 0;
    uint8_t type = 0;
};
// Pure per-sample helper: absolute Q8 spans for the placed DZHR billboards
// on one route sample (centre_q8 is the route-frame road centre). BORR
// roadside_objects (static_family false) are draw-only and skipped. Consumes
// no randomness: placement already ran once at course load.
std::vector<DzhrSolid> dzhr_solids_for_sample(int32_t centre_q8, const std::vector<SceneryObject>& objects);
// Weapons (rider +0x1d4): 0 fists, 1 club, 2 chain, 3 kick. Reach in units of 0x1000 Q8
// (0x00409c10) and damage multipliers (0x00465a00).
constexpr int32_t weapon_reach[4] = {5, 7, 8, 5};
constexpr int32_t weapon_damage[4] = {256, 320, 384, 64};

struct Attack {
    int timer = 0; // 0 idle; counts ticks while attacking
    int weapon = 0;
    bool right = false; // toward larger lateral (0x004095a0)
    bool backhand = false;
    bool fired = false; // descriptor frame-2 hit event consumed this swing (0x004096f0)
};
constexpr int attack_legacy_hit_tick = 7; // pre-descriptor stand-in, used only with no animation bank
constexpr int attack_ticks = 18;          // swing length cap without descriptor completion

// An AI opponent (bike + rider pair built by 0x00405990 with the AI tables).
struct Opponent {
    BikeSpec spec;
    int32_t rider_value;  // rider table +0xc / 2
    int base_speed;       // personality +0x1bc + level*4 (units of (v>>12)+(v>>7)+(v>>9))
    int cur2_speed;       // personality +0x1a8 + level*4
    int lane;             // 0x00405990 draws rand()%3 (stream-dependent); -1 = draw at build
    int slot;             // grid slot 1..15 (0x00464088/0x00464108 via 0x00405600)
    int rider = 0;        // rider index: 0x00463c78 row and 0x00464558 personality (0x00405990 param_6)
    int32_t grid_pos = 0; // table sub-slice selector: exact Q8 start position (0 = native two-row grid)
    int32_t grid_lat = 0; // table lateral (added to the road centre); see grid_pos
    bool frozen = false;  // solo oracle: think/move nulled, body present for scans
    int32_t throttle_percent = 100; // 0x004642a0[level]
    RiderStats stats{};
    int weapon = 0; // rider table word 6 + level
    // Plan 34: online human seat driving this rider (0..7), -1 for an AI
    // opponent. A human rider is built like the player (no AI throttle
    // scaling, +0x324 = 0) but keeps a full AI brain for a hand-over.
    int human = -1;
};

// The original opponents for a level: personalities 1..count (0x00464558, 0x204 bytes each),
// bikes from 0x004641c8[level + rider*5], rider values from 0x00463c78 (+0xc / 2), AI throttle
// scale 0x004642a0[level]; grid slots 1..count. Offline races build 14
// opponents (DAT_004642d4=0x10, player + fourteen, slots 1..14, 0x00405600).
std::vector<Opponent> original_opponents(const std::vector<uint8_t>& exe, const std::vector<BikeSpec>& specs,
                                         int level, int count);
// Stable race-setup factory shared by game + replay (plan 10 replay
// prerequisite). One explicit config, no hidden replay-only setup: the
// player bike/rider resolve through the career tables (0x00415fc0 character
// -> rider, 0x004641c8 level_bike, 0x00463c78 rider row), opponents through
// original_opponents, and the road_base anchor through the same Race ctor
// mechanism on both sides (the game passes 0 = relative route frame;
// replay measures per golden with race_road_base_q8; see the Race ctor).
// The game (game_main.cpp, owned by another slice) calls the same resolvers;
// the replay harness below must not hard-code SPEC ids, weights or stats.
struct RaceSetupRequest {
    std::string course = "NAPA"; // CRS stem, e.g. "NAPA"
    int level = 0;               // 0..4 (0x004b8a12)
    int character = 1;           // 1..8 front-end character (0x00415fc0)
    bool solo = true;            // scheduler freeze + nulled traffic pool (field isolation only)
    int32_t road_base_q8 = 0;    // absolute Q8 anchor (game: 0; replay: measured)
    int opponents = 7;           // full-field count when !solo (offline original: 14)
};
// 0x00405990 lateral anchor math: road_base = golden_frame0_lateral - route[grid].offset*256 - grid_word.
// Throws std::runtime_error on bad data. Pure; no RNG, no allocation.
int32_t race_road_base_q8(int32_t golden_lateral, int32_t route_offset, int32_t grid_word);
// Original rider personalities (0x00464558, 10 x 0x204 bytes). The AI brain is
// a copy of one record (0x00407990 selects 0x004c6b40 + rider*0x81 words).
struct Personality {
    int32_t cur2_levels[5] = {};             // +0x1a8: reset source for +0x1a0
    int32_t cur_levels[5] = {};              // +0x1bc: reset source for +0x1a4
    int32_t rating[30] = {};                 // +0x0: overtake rating currents (10 x 3)
    int32_t rating_tgt[30] = {};             // +0x78: rating targets
    int32_t ease[3] = {};                    // +0x12c/+0x130/+0x134: replan easing steps
    int overtake_index = 0;                  // +0x165: rating-table index used by 0x004092e0
    int32_t reach = 0;                       // +0x1d4: overtake distance scale (0x004092e0)
    int8_t kick_roll = 1, backhand_roll = 1; // personality +0x1d0/+0x1d1
    int32_t reaction[15] = {};               // +0xf0: five relationship events, three ratings each
    int32_t word_184 = 0; // +0x184: no instruction writes it; the copy keeps the record word
};
std::vector<Personality> read_personalities(const std::vector<uint8_t>& exe);

struct AiState {
    int32_t target_x = 0;        // +0x194 (world units * 256)
    int32_t target_position = 0; // +0x198 (Q8 samples)
    int32_t target_speed = 0;    // +0x19c
    int32_t current_speed = 0;   // +0x1a4
    int32_t current2 = 0;        // +0x1a0
    int32_t base_speed = 0;      // +0x1bc + level*4
    int32_t replan_ticks = 0;    // +0x188
    int lane = 0;                // +0x18c
    int32_t bias = 0;            // +0x190
    int state =
        0; // +0x160: dispatch via 0x00465980 (1 = racing); 0x00405c80 spawns at 0, the first 0x00408ad0 moves it
    int target_rider = -1;       // +0x168: riders_ index, -1 null
    int target_step = 0;         // +0x174: rated overtake step of the target
    int nbr_ahead = -1;          // +0x16c: replan scan pick, -1 null
    int nbr_behind = -1;         // +0x170: replan scan pick, -1 null
    int ov_ahead = 0;            // +0x178: overtake-step rating
    int ov_behind = 0;           // +0x17c: overtake-step rating
    int32_t rating[30] = {};     // brain +0x0: easing currents
    int32_t rating_tgt[30] = {}; // brain +0x78: easing targets
    int32_t ease[3] = {0, 0, 0}; // brain +0x12c/+0x130/+0x134
    int overtake_index = 0;      // brain +0x165
    int32_t spd_a[5] = {};       // brain +0x1a8 levels (current2 source)
    int32_t spd_b[5] = {};       // brain +0x1bc levels (current source)
    int overtake_latched = -1;   // brain +0x180: latched overtake node (riders_ index, -1 null or non-rider)
    uint32_t overtake_type = 0;  // its object type (0 when null)
    int32_t overtake_position = 0;
    int32_t reach = 0;                       // brain +0x1d4: overtake distance scale
    std::array<int32_t, 10> state_ticks{};   // brain +0x138 + 4*state: ticks in each state (0x00407bd0)
    uint8_t state_flag = 0;                  // brain +0x164: cleared on a state change (0x00408df0)
    int8_t kick_roll = 1, backhand_roll = 1; // personality +0x1d0/+0x1d1
    int32_t reaction[15] = {};               // +0xf0: five relationship events, three ratings each
    int32_t word_184 = 0;                    // brain +0x184: personality record word, never written
};

struct Rider {
    Bike bike;
    int route = 0;                          // which branch route (0 = half 0)
    int slot = 0;                           // grid slot 0..7 (0x00464218); 0 is the player
    int rider_idx = 0;                      // 0x00463c78 row / personality index (0x00405990 param_6)
    size_t sample = 0, previous_sample = 0; // bike +0x140 / +0x13c
    bool ai = false;
    bool cop = false;    // the police rider (DAT_004642dc, 0x00406b80); never in places or finish order
    bool frozen = false; // solo oracle: think/move nulled, scanned but never stepped
    // Frozen scheduler metadata (0x00437e60 head + LAB_00438108): last/next/dt
    // evolve on every scheduled run even with NULL think/move; only the
    // interval recompute is skipped. Hydrated once from sched.* capture keys;
    // sched_live stays false for legacy captures and live games, where the
    // build dt 1 is kept. Bike +0x18 mirrors the live dt word.
    int32_t sched_last = 0;     // bike +0x10
    int32_t sched_next = 0;     // bike +0x0c
    int32_t sched_interval = 1; // bike +0x14
    bool sched_live = false;
    // Rider half (type 8 scheduler object): rides with the bike, so its
    // position, lateral and speed follow the bike unless detached. Frozen
    // solo halves keep their captured state even when contact moves the bike.
    // Original scheduler scans reject NULL object+0x2c slices (0x00449ce0).
    // Frozen replay nodes may retain NULL even at an in-range position;
    // ordinary riders and legacy captures keep their existing valid slices.
    bool bike_slice_present = true, half_slice_present = true;
    bool half_detached = false;
    // 0x00437e60 window test (bikes/halves: within 0x1e00 of the ring front
    // or 0x800 of the ring back on a scheduled run) calls +0xe8 = 0x00406940,
    // which parks the rider: think/draw callbacks and both slices nulled.
    // Recorded for frozen solo riders; live field races park and wake through
    // Race::park/Race::wake (race_schedule.cpp).
    bool parked = false;
    // Live scheduler (Race::set_live_schedule, 0x00438170(0) -> 0x00437e60):
    // the rider half is its own node with its own cadence (+0x0c next,
    // +0x10 last, +0x14 interval, +0x18 dt of the half object).
    int32_t half_sched_last = 0, half_sched_next = 0, half_sched_interval = 1, half_dt = 1;
    // Parking state of the bike (0x00406940 / 0x004060b0): +0x300 live byte
    // (0x00431960) and +0x304/+0x308 as the section-local virtual position
    // (virtual_section is the route sample where that section starts). The
    // bike image keeps +0x30c live ticks and +0x310 live distance (summed
    // by the live branch), +0x314 parked pace (Q8 per tick) and +0x318
    // crash delay.
    bool live = true;
    int32_t virtual_local = 0;
    size_t virtual_section = 0;
    int32_t half_lateral = 0, half_position = 0, half_speed = 0;
    // Live rider-half mirror (the +0x2fc object of a stepped rider): after
    // the 0x00438170 scheduler pass and before the 0x004368f0 contacts the
    // half carries the bike's motion words (measured on sched-human-20261016:
    // equal every tick except where contacts then move the bike).
    struct HalfMirror {
        int32_t position = 0, lateral = 0, height = 0, velocity = 0, speed = 0, steer_rate = 0, lat_vel = 0,
                lat108 = 0, yaw = 0;
        size_t sample = 0;
        int route = 0;
    };
    HalfMirror half_mirror;
    // Rider-half object image (0x00405990: 0x004387a0 from table 0x00463c10).
    // Holds the half's own words; its motion words are in half_mirror, and
    // its weapon/standings words (+0x1c8..+0x1d4, +0x1f4..+0x1fc) are kept on
    // the Rider fields below, which the original writes in place.
    Bike half_object;
    // Rider-half standings words (0x004060b0 after the bike physics):
    // +0x1f8 Q8 distance still to the finish (0x00407410; finish minus
    // position once the finish is known), +0x1fc place and +0x1f4 the
    // scheduler tick of the next place refresh (0x004075f0/0x00407550).
    int32_t finish_distance = 0, place = 0, place_refresh = 0;
    AiState brain;
    bool finished = false;
    int32_t finish_tick = -1;
    CrashActor crash_actor;
    ActorAnimationState crash_bike_animation;
    bool crash_active = false;
    // Solo oracle freeze-hack mirror (sim_freeze_others): the proxy nulls the
    // think/move of every non-player node at the start of each step, sparing
    // only 0x00434660 halves within 256 of the player bike. Once the crash
    // body flies 256+ from the bike the player half is nulled for good
    // (split-left 2101: diff 254 spared; 2102: diff 268 nulled, brain/anim/
    // sched frozen, rider.1 rows gone, crash stays 1 with no remount).
    // Solo-only (freeze_actors_); the live game never runs this path.
    // Latched: the hack never restores a nulled node.
    bool half_frozen = false;
    int32_t crash_timer = 0;
    int colour = 0; // rider colour remap index (0x0046e158)
    RiderStats stamina;
    int weapon = 0;                     // carried weapon (+0x1c8)
    int32_t steal_until = 0;            // +0x1cc: no steal before this tick (0x004096f0)
    int32_t hit_until = 0;              // +0x1d0: no hit damage before this tick (0x004096f0)
    ActorAnimationState ride_animation; // riding/attack block +0x70, advanced in Race::step (0x0043ae90)
    Attack attack;                      // attack.weapon latches +0x1d4 at swing start (0x00434660)
    // Riding animation selected by contact 0x004052c0 reason 4 without a crash
    // (+0x80/+0x81 via 0x0040ac70); -1 when none pending.
    int8_t contact_animation = -1;
    // Nitro (player-global 0x0040b110/0x0040b170/0x0040b1a0/0x0040adb0):
    // per-rider copies of the charge accumulator (Q8) and throttle force.
    int32_t nitro_charge = 0;   // accumulator Q8 (DAT_004c5f10)
    int32_t nitro_throttle = 0; // +0x240 throttle force holding 0x0040adb0 output
    // Plan 34: online human seat (0..7) or -1. Rider 0 is seat 0 offline.
    // A departed human is handed to its AI brain (ai = true) at a sealed
    // tick; Race::human() is false from then on.
    int human = -1;
    NitroConfig nitro_cfg{}; // humans 1..7: from their own bike spec
    bool busted = false;     // arrested (0x00407f30); Race::busted() mirrors rider 0
    int32_t nitro_count = 0; // humans 1..7: charges left (rider 0 keeps Race::nitro_charges_)
};

// Live-scheduler rules (race_schedule.cpp), pure for unit tests.
// 0x00437e60 interval of a bike or rider half: 30 without a slice, else by
// distance from the ring camera DAT_004b6e5c, slow-object bonus before tick
// 300 (not for the player's bike or half), capped at 8.
int32_t live_interval(bool slice, int32_t position, int32_t ring_camera, int32_t speed, int32_t tick,
                      bool player_object);
// 0x00406940 pace: (+0x310 << 8) / +0x30c, times 95% ahead of the player or 105% behind.
int32_t parked_pace(int32_t live_distance, int32_t live_ticks, bool ahead_of_player);
// 0x00407610 body over parked paces indexed by place - 1: the three nearest
// ahead of the player lose 60/30/15 (floor 3000), the three nearest behind
// gain 60/30/15 (cap 30000).
void pace_band(std::array<std::optional<int32_t>, 16>& by_place, int player_place);

// Per-tick snapshot for the simulation oracle (plan 10). Rider fields are
// keyed "rider.<grid slot>.<field>" from tests/sim_fields.hpp; the tick and
// rng come first. Traffic cars are appended by TrafficSystem.
struct FieldValue {
    std::string name;
    int64_t value;
};
struct RaceSnapshot {
    int32_t tick;
    uint32_t rng;
    std::vector<FieldValue> fields;
};
// Riders on a course: bike physics (bike.hpp), movement along the route
// (0x004395e0/0x00439270/0x00439390/0x004394c0), ground height and vertical motion
// (0x00439b10 case 0, 0x0043a190), branch choice at splits, AI racing (0x00407bd0 state 1)
// and the chase camera (0x00437c30) on rider 0, the player.
class Race {
  public:
    // route_a/route_b: make_route(course,false/true). Identical up to the first split.
    // road_base_q8: absolute Q8 anchor of the route. The original sub-slice
    // +0xcc is absolute while RouteSample::offset accumulates from 0 at the
    // course root, so grid placement adds this. Napa measures 7 world units
    // from the solo golden frame 0 (18176 - 16384 = 1792, via
    // race_road_base_q8); traversal mechanism open, other courses
    // unmeasured. The game passes 0 (relative frame); replay passes the
    // measured anchor through the same ctor — no hidden replay-only setup.
    Race(std::vector<RouteSample> route_a, std::vector<RouteSample> route_b, const PhysicsTables&,
         const BikeSpec&, int32_t rider_value, std::vector<Opponent> opponents = {}, int level = 0,
         int32_t road_base_q8 = 0, const std::vector<Personality>& personalities = {},
         const std::vector<uint8_t>& exe_image = {},                // level 0..4 (0x004b8a12)
         int32_t player_grid_pos = 0, int32_t player_grid_lat = 0); // plan 34: seat 0 grid row (0 = legacy)
    void set_terrain_source(std::function<const TerrainSlice&(const RouteSample&)> source) {
        terrain_source_ = std::move(source);
        attach_terrain_type();
    }
    // Guard-rail height 0x0041c200 writes into sub-slice +0x3b when it draws a
    // CESR event-5 strip (category-6 group header); 0 when it has no group.
    void set_rail_height_source(std::function<int32_t(const RouteSample&)> source) {
        rail_height_source_ = std::move(source);
    }
    // Free traffic steps with the race on the player's route (0x0044b3b0 calls
    // 0x0044dd90 with the per-step camera advance). The random-call
    // interleaving against the original's scheduler is not verified.
    void set_traffic(TrafficSystem* t);
    const TrafficSystem* traffic() const { return traffic_; }
    // Tunnel flag for the player engine mix (0x00434a80): the byte at +0x3c of
    // the object at player bike +0x140 is 3 inside a tunnel. Natively that is
    // the terrain type of the player's route sample (course_scene assigns 3
    // to mountain tunnel samples). Fixtures without terrain read type 0.
    bool player_in_tunnel() const;
    // Nulled-pool solo: keep timer/density draws, never place (Task 5/7).
    void set_spawn_enabled(bool v);
    // Pedestrians (plan 24 Task 4, src/race_pedestrians.cpp): spawned as the
    // ring builds their slice, thought/moved on the scheduler cadence of
    // 0x00437e60, and hit by riders and cars through the 0x004368f0 pass.
    void set_pedestrians(PedestrianSystem* p) { pedestrians_ = p; }
    // Track ring (src/track_ring.hpp): attaching runs the 0x004552e0 setup
    // on the current RNG state with the player not yet built (0x0044b250
    // builds the ring before 0x00405600); each step then advances it at the
    // 0x00454120 point, and its DZHR actors replace the precomputed
    // pedestrian walk. Attach pedestrians first: their pool takes the
    // 0x004409e0 spawns. The ring must outlive the race.
    void set_track_ring(TrackRing* ring);
    // Plan 34 Task 2 C1: the ring window of human seat >= 1 (seat 0 uses
    // set_track_ring). Each window walks and draws on its own; a slice that
    // lies inside another seat's window when built fires no actors or
    // statics (the first window to build an area owns it). Setup runs now,
    // at the seat's camera, in seat order.
    void add_human_ring(int seat, TrackRing* ring);
    // True when route sample s lies inside a ring window other than `seat`'s.
    bool sample_in_other_window(int seat, int32_t sample) const;
    // 0x00405600 rider build draws, in the original order after the ring
    // setup (0x004552e0): 0x00405990 draws each pair's brain lane rand()%3
    // (+0x18c via 0x00407990) for the player and then every opponent in slot
    // order, offline; the tail draws rand()%100 for 0x00402e10, which picks
    // the rider-name set (42 and 69 select the two alternate sets).
    void build_draws();
    int name_roll() const { return name_roll_; }
    // Oracle solo captures (RR_ORACLE_SOLO) freeze every scheduler node but
    // the player's: pedestrians spawn from the ring but never think.
    void set_freeze_actors(bool v) { freeze_actors_ = v; }
    // First +0xc next of the pedestrians the 0x004552e0 ring setup spawns.
    // Measured 2 on the solo captures (unlinks on 2 + 60k) but 0 on
    // napa-field-l1-20261017, whose setup pedestrians already moved in tick 1
    // (+36 = one move with dt 1 + interval) while its intersection objects
    // keep TrafficSystem::setup_tick 2 (torn down on tick 392 = 2 + 13*30).
    void set_pedestrian_setup_tick(int32_t t) { setup_tick_ = t; }
    // Live rider scheduler (field races): every tick 0x00438170(0) walks the
    // position-sorted object list front to back and runs each bike and
    // rider-half node whose +0x0c next tick is due through 0x00437e60
    // (dt, think, move, interval LOD by camera distance, window parking).
    // Bikes move with their own dt; parked bikes advance virtually at their
    // pace and wake inside the ring trackers (0x004060b0). Off by default:
    // solo oracle replays keep the frozen field and every-tick stepping.
    // Enable before the first tick: the build words (+0x10 = -interval,
    // 0x00438930; +0x30c/+0x310, 0x00405c80) are set here.
    void set_live_schedule(bool v);
    bool live_schedule() const { return live_schedule_; }
    // Rider-half and bike nodes in the order 0x00438170 walks them (front to
    // back; a tie puts the half first). type 4 = bike, 8 = half.
    struct ScheduleNode {
        int rider;
        uint32_t type;
        int32_t position;
        int32_t sorted = 0;
    }; // sorted: +0x1c at the last 0x004383e0
    std::vector<ScheduleNode> schedule_order() const;
    void set_crash_library(const ActorAnimationLibrary&, const CrashConfig&);
    void set_ride_library(const ActorAnimationLibrary* library,
                          const std::array<std::array<std::array<int8_t, 4>, 2>, 2>* attacks);
    // Police (plan 24 Task 2 / plan 32 D): enables the cop constructor
    // 0x00406b80 for the periodic spawn in 0x00407f30 and the DZHR cop
    // records. The cop is appended as the last rider (at most one) and is
    // removed when it parks (0x00406940 -> 0x004073a0).
    void set_police(const PoliceConfig&, std::vector<BikeSpec> specs, const ActorAnimationLibrary* library);
    bool police_enabled() const { return bool(police_); }
    // 0x00406b80: builds the cop `slice_offset` ring slices from the player's
    // half (0x00449910 walk, clamped to the ring). False when a cop exists,
    // the police are not enabled or the race has no player yet.
    // anchor: the human the spawn is placed from (plan 34 C4); -1 = seat 0.
    bool spawn_cop(int32_t slice_offset, int anchor_rider = -1);
    int cop_index() const { return !riders_.empty() && riders_.back().cop ? int(riders_.size()) - 1 : -1; }
    // 0x00416010(4, 0xb4): the player was arrested (0x00407f30).
    bool busted() const { return busted_; }
    // Nitro setup 0x0040b0e0: copy six words from the player spec at +0x14c
    // and start the accumulator at maximum<<8. Call once after construction
    // (the game calls it from 0x00405600 right after the player build).
    void set_nitro(const NitroConfig& c) {
        nitro_ = c;
        nitro_set_ = true;
        nitro_charges_ = c.charges;
        for (auto& r : riders_) r.nitro_charge = c.max << 8;
    }
    RaceWiring wiring() const; // derived from the existing members; no new state except nitro_set_
    const NitroConfig& nitro() const { return nitro_; }
    int32_t nitro_charges() const { return nitro_charges_; }
    // Demo hold (DAT_004753c8): attract/demo races and oracle captures run
    // with this set; the player rider think takes the AI branch (0x00434660
    // head: stamina regen, then 0x00407bd0) and ignores the control word.
    // Human races leave it clear. The attract demo (plan 13) will set it.
    // Enabling also seeds the player AI brain like an opponent build
    // (0x00405990/0x00407990/0x00405c80): base/current speed, lane, bias.
    // The oracle demo player is character 1's rider 8 (base 95).
    // Rider params come from its 0x00463c78 row (value +0xc/2, weapon
    // +0x18+level*4, stats +0x30..+0x3c); rider 8: 91, 2, 7/3000/3000/3000.
    void set_demo_hold(bool v, int base_speed = 0, int lane = 0, int32_t bias = 0, int weapon = 0,
                       RiderStats stats = {});
    void step(uint32_t controls); // one simulation tick (one human)
    // Plan 34: one control word per human seat, in seat order; the offline
    // game passes one. A single word takes exactly the old path.
    void step(std::span<const uint32_t> controls);
    int humans() const { return int(human_riders_.size()); }
    // Rider index driven by a human seat (view helper; the simulation never
    // reads the view's choice).
    int human_rider(int seat) const {
        return seat >= 0 && seat < humans() ? human_riders_[size_t(seat)] : -1;
    }
    // A departed human is driven by its AI brain from this tick on (the
    // server flags it in the sealed frame); `back` returns it to its seat.
    void set_human_driven(int seat, bool driven);
    void set_human_nitro(int seat, const NitroConfig&);
    // Rider colour remaps from the player's bike index (0x00446190).
    void set_player_bike(int bike);
    // Plan 34: mano-a-mano rules inside the simulation (handicap 0x00442f60
    // every 0x39 ticks, applied in 0x004395e0). Off offline.
    void set_net(bool v) { net_ = v; }
    bool net() const { return net_; }
    // Plan 34 per-human outcome (original codes, 0x00416010): 1 racing,
    // 2 won (first place), 3 finished, 4 busted, 5 wrecked.
    struct HumanOutcome {
        int code = 1;
        int place = 0; // 1-based when finished
        int32_t tick = -1;
    };
    HumanOutcome human_outcome(int seat) const;
    bool all_humans_done() const;
    // Render/audio only: a detached copy with the followed rider in row zero.
    // Never step this copy or feed its state back to the authoritative race.
    Race presentation_copy(int seat) const;
    int8_t handicap(int seat) const { return seat >= 0 && seat < 8 ? handicap_[size_t(seat)] : int8_t(0); }
    // Pre-green countdown tick (0x0044b3b0 while DAT_004c5d7c != 1): the
    // rider-half thinks run their AI branch (0x00434660 -> 0x00407bd0) but
    // nothing moves along the road. No draws were measured in the
    // countdown frames of the solo captures.
    void countdown_step();
    const std::vector<Rider>& riders() const { return riders_; }
    std::vector<Rider>& riders() { return riders_; }
    const Bike& bike() const { return riders_[0].bike; }
    uint32_t position() const { return uint32_t(bike().i32(0x1c)); }
    int32_t lateral() const { return bike().i32(0x28) >> 8; }
    const std::vector<RouteSample>& route() const { return route_of(riders_[0]); }
    const std::vector<RouteSample>& route_of(const Rider& r) const { return *routes_[r.route]; }
    // Oracle diagnostics: the rider's own slice static-collision state
    // (0x004373f0 inputs): BORR count, first slot lateral, boxes loaded.
    struct StaticSliceState {
        bool built = false, boxes_ready = false;
        int borr_count = 0, borr0 = 0, billboards = 0;
        uint32_t ac = 0;                           // sub-slice +0xac (0x0044b630)
        std::optional<std::array<int32_t, 4>> box; // first loaded BORR box (+0x68)
    };
    StaticSliceState static_slice_state(const Rider& r) const {
        return static_slice_state(r.route, r.sample);
    }
    StaticSliceState static_slice_state(int route, size_t sample) const {
        StaticSliceState out;
        const auto& table = statics_[route];
        if (sample >= table.size() || !table[sample].built) return out;
        const auto& s = table[sample];
        out = {true,         s.boxes_ready,           s.borr_count, s.borr_count ? s.borr[0] : 0,
               s.billboards, slice_ac(route, sample), std::nullopt};
        if (s.boxes_ready && s.borr_frame && !s.borr_frame->boxes.empty()) out.box = s.borr_frame->boxes[0];
        return out;
    }
    const std::vector<RouteSample>& route_for(int road) const { return *routes_[road]; }
    // Finish line 0x00454240/0x00406890/0x00437800: nonzero global finish
    // DAT_004b6e58 (Q8 position) ends the race when the actor's +0x1c
    // reaches it (with the +0x2c nonzero scheduler gate and +0xc0 bit 0x10
    // latch). 0 keeps the legacy route-end rule for synthetic routes.
    void set_finish_q8(int32_t q8) { finish_q8_ = q8; }
    int32_t finish_q8() const { return finish_q8_; }
    // Course-load hazard table (plan 16 Task 2): one span list per route
    // sample per road (index by route_for(road)[sample]); empty by default.
    void set_hazards(std::vector<std::vector<RoadHazard>> road0, std::vector<std::vector<RoadHazard>> road1) {
        hazards_[0] = std::make_shared<const std::vector<std::vector<RoadHazard>>>(std::move(road0));
        hazards_[1] = std::make_shared<const std::vector<std::vector<RoadHazard>>>(std::move(road1));
    }
    // DZHR solid table (plan 16 Task 2): one span list per route sample per
    // road; empty by default. Wired by the race factory from the scene's
    // placed objects; tests fill it directly.
    using DzhrTable = std::vector<std::vector<DzhrSolid>>;
    void set_roadside(DzhrTable road0, DzhrTable road1) {
        dzhr_[0] = std::make_shared<const DzhrTable>(std::move(road0));
        dzhr_[1] = std::make_shared<const DzhrTable>(std::move(road1));
    }
    // Live ring statics (0x00454240 slice builds, plan 16 Task 2): BORR slots
    // and billboards of each built slice, swept by 0x004373f0 with the
    // collision boxes 0x0044b630 copies from the object's frame 2. The frame
    // source resolves a slice's family selector (BORR category 0, billboard
    // category 4 frame 0); attaching it sizes the per-sample tables and
    // replaces the legacy hazard/DZHR spans in the sweep.
    using StaticFrames = std::function<const AnimationFrame*(uint32_t section, uint32_t local,
                                                             uint8_t selector, bool billboard)>;
    void set_static_frames(StaticFrames frames);
    // Plan 34 Task 3: size the ring-terrain overlay (scene section lengths)
    // and record one ring slice's terrain for the simulation.
    void set_ground_layout(const std::vector<uint32_t>& section_sizes);
    void apply_ring_terrain(uint32_t section, uint32_t local, const PathSample&, const TerrainSlice&);
    void ring_statics(int32_t sample, uint32_t section, uint32_t local, const RingStatics&);
    // Wiring query for the factory disc tests: total solid spans on a road.
    size_t roadside_solids(int road) const {
        size_t n = 0;
        for (const auto& spans : *dzhr_[road < 0 || road > 1 ? 0 : size_t(road)]) n += spans.size();
        return n;
    }
    bool finished() const { return riders_[0].finished; }
    bool crashed() const { return bike().i32(0x2f4) != 0; }
    int32_t ticks() const { return ticks_; }
    uint32_t camera_position() const;
    int32_t horizon_scroll() const { return rr::horizon_scroll(camera_heading_); }
    int32_t camera_x() const { return camera_x_; } // 0x004b7ee8: camera lateral, world units
    Camera camera() const;                         // relative to the route's own road at camera_position()
    int32_t speed() const { return bike().i32(0xec); }
    int gear() const { return bike().i32(0x23c); }
    // Simulation oracle hooks (plan 10): read and write the MSVC rand state
    // (0x00455eb0) and dump every snapshotted field.
    uint32_t rng_state() const { return seed_; }
    void set_rng_state(uint32_t s) { seed_ = s; }
    void set_ticks(int32_t t) { ticks_ = t; } // oracle: align with the capture's absolute tick
    RaceSnapshot snapshot() const;
    int place(size_t rider) const;                   // 1-based race order (finished riders by finish tick)
    int last_target() const { return last_target_; } // rider most recently hit by the player
    // Sound events queued this tick (0x00436830 call sites that involve the player).
    std::vector<SoundEventRecord> take_sound_events() {
        auto e = std::move(sound_events_);
        sound_events_.clear();
        return e;
    }
    bool wrecked() const { return bike().i32(0x2f0) == 0; } // bike condition used up
    // Shared knock-off decision 0x004052c0 (plan 16 Task 1): public for the
    // contact unit tests; the tick path calls it from contact_car. No RNG
    // inside; grunt/crash draws stay upstream in the 0x004049c0 caller.
    bool knock_off(Rider& victim, int32_t stamina_damage, int32_t bike_damage, int32_t reason,
                   bool owner_local = true, int32_t owner_flag = 0,
                   std::optional<std::array<int32_t, 3>> rider_impulse = {},
                   std::optional<std::array<int32_t, 3>> bike_impulse = {});
    // Scenario entry for the oracle `RR_ORACLE_KNOCK`: the original crash entry
    // 0x00403d60 with reason 1 and the default vectors, without the damage
    // bookkeeping of 0x004052c0 (the proxy calls 0x00403d60 directly).
    // The proxy hook runs after the tick counter has incremented, so the entry
    // sees the next tick: ticks_ + 1 is used for the animation deadlines.
    void crash_now(Rider& victim) {
        ++ticks_;
        crash_reason(victim, 1, {-256, 0, 1792}, {0, 0, 1024});
        --ticks_;
    }

  private:
    SliceEdges edges(const Rider&, size_t sample) const;
    void move_along(Rider&, int32_t distance, bool accumulate_sweep = true);
    void cross_sample(Rider&, int32_t delta);
    void accumulate(Rider&, uint32_t from, uint32_t to, size_t from_sample, size_t to_sample);
    void move(Rider&, bool player);
    void vertical(Rider&);
    // 0x0043a610 on the sub-slice values of a route sample (absolute laterals).
    struct SubSliceView {
        int32_t e0 = 0, e4 = 0, e8 = 0, ec = 0;
        uint8_t type = 0xff, flags = 0, event_type = 0, b3b = 0;
        int8_t p3a = 0;
        std::array<uint8_t, 4> sel{255, 255, 0, 0}; // +0x148..+0x14b
        std::array<int32_t, 2> base{}, top{}, height{};
    };
    SubSliceView sub_slice(const Rider&, size_t sample) const;
    void walls(Rider&, const Bike* owner = nullptr);
    void crash(Rider&);
    void step_crash(Rider&, bool player, uint32_t controls);
    // Crash step parts, split across the live scheduler nodes like the
    // original: half node = crash_phases (think) + crash_body_motion and
    // crash_recovery (move), bike node = crash_bike_animation +
    // crash_bike_motion.
    void crash_phases(Rider&);
    void crash_bike_animation(Rider&);
    void crash_bike_motion(Rider&, bool player);
    void crash_body_motion(Rider&, int32_t dt);
    void crash_recovery(Rider&, bool player, uint32_t controls);
    void step_rider(Rider&, uint32_t controls, bool player);
    void rider_think(Rider&, uint32_t controls, bool player); // think body after fight()
    void rider_move(Rider&, bool player);                     // physics move 0x0043a580
    void step_frozen_schedule(Rider&); // 0x00437e60 metadata cadence for hydrated frozen riders
    // Live scheduler (race_schedule.cpp).
    void schedule_pass(uint32_t controls, bool countdown); // 0x00438170(0)
    void prepare_traffic_obstacles();
    void run_bike_node(Rider&, uint32_t controls, bool countdown);  // 0x00437e60 on a bike
    void run_half_node(Rider&, uint32_t controls, bool countdown);  // 0x00437e60 on a rider half
    void think_half(Rider&, uint32_t controls, bool countdown);     // 0x00407bd0 / 0x00434660
    void move_bike_live(Rider&, uint32_t controls, bool countdown); // 0x004060b0 live branch
    void move_bike_parked(Rider&);                                  // 0x004060b0 parked branch
    void park(Rider&);                                              // 0x00406940 (edx 0)
    void wake(Rider&, size_t sample);                               // 0x004060b0 tail
    void live_standings(Rider&);                                    // 0x004067d0
    void refresh_place(Rider&);                                     // 0x004075f0 -> 0x00407550 place table
    void sync_half_mirror(Rider&);                                  // half mirror follows the crash body
    int32_t schedule_interval(const Rider&, bool half) const;       // 0x00437e60 LOD tail
    void sort_schedule();                                           // 0x004383e0
    int32_t node_position(const ScheduleNode&) const;
    void parked_pace_band();     // 0x00407610 (end of 0x00438170)
    int32_t pace_band_tick_ = 0; // DAT_004642e4 (0 at 0x00405600)
    bool live_schedule_ = false;
    mutable std::vector<ScheduleNode> schedule_;
    void update_halves();  // rider-half mirror and standings (0x004060b0 tail)
    void collide_riders(); // 0x004368f0 list walk: bikes, pedestrians, hazard gate
    void contact_rider_pedestrian(Rider&, Pedestrian&, bool rider_outer);
    void separate(Rider& outer, uint32_t outer_type, Rider& partner, uint32_t partner_type); // 0x00436a40
    // Position-ordered scheduler list (0x00438aa0 insert sort, ties in
    // creation order) for the AI ring scans: rider bikes (type 4) and halves
    // (8), pedestrians (0x20), free cars (0x10).
    struct ScanObject {
        int32_t position = 0, lateral = 0, speed = 0; // +0x1c, +0x28, +0x108
        uint32_t type = 0, order = 0;
        int32_t key = 0; // list key: +0x1c, or for live-scheduled riders the position at the last sort
        int rank = -1;   // live-scheduled riders: ascending list rank
        int rider = -1, pedestrian = -1, car = -1;
        const RouteSample* sample = nullptr; // the object's slice (+0x2c)
    };
    void build_scan_list() const;
    int scan_find(int rider, uint32_t type) const;
    int scan_next(int from, uint32_t mask, bool forward, int32_t window) const; // 0x00449ce0
    int32_t half_x(const Rider& r) const { return r.half_detached ? r.half_lateral : r.bike.i32(0x28); }
    void ai_state_follow(Rider&); // 0x00409ca0 (state 4; state 3 starts with it)
    void ai_state_shadow(Rider&); // 0x0040a560 (state 6)
    void ai_think(Rider&);
    void ai_replan(Rider&);       // 0x004082f0
    void ai_transition(Rider&);   // 0x00408ad0
    void ai_state_racing(Rider&); // 0x00409000
    void ai_state_fight(Rider&);  // 0x0040a4f0 (state 5)
    bool ai_overtake(Rider&);     // 0x00408e40: true when a target was latched
    int ai_scan_replan(const Rider&, bool forward, int32_t cap) const; // 0x00408cd0 via 0x00449ce0
    int ai_scan_overtake(const Rider&, int32_t& gap) const;            // 0x00449e90: nearest closing rider
    int32_t curvature(const Rider&, size_t sample, int samples) const;
    int32_t ai_curve_limit(const Rider&) const; // 0x00409020
    void ai_curve_target(Rider&);               // 0x004090a0: inside of the curve
    void ai_straight_target(Rider&);            // 0x004091f0
    void ai_speed_steer(Rider&);                // 0x00409f70: steering + speed control
    void ai_start_attack(Rider&, bool backhand);
    void ai_relationship(Rider&, const Rider&, int event);
    int ai_attack_anim(const Rider&) const;    // 0x00409510: nonzero while attacking
    bool ai_behind_target(const Rider&) const; // 0x004092b0
    bool ai_can_overtake(Rider&);              // 0x004092e0 (consumes rand on reach)
    bool ai_fight_range(const Rider&) const;   // 0x0040a4a0
    void collide(Rider&, Rider&);
    // Rider-against-car pass (0x004368f0 pair loop over the scheduler object
    // list, 0x004049c0 bike side for +0xc4 == 0x10 cars; see
    // analysis/car-notes.md). Runs at the traced point in Race::step for
    // every rider, player and AI. Enters crashes with reason 2 (animation
    // 29) through the crash module and queues the original sound events.
    void collide_traffic(Rider&);
    void collide_traffic_half(Rider& owner); // crashed half (type 8) against cars
    void contact_car(Rider&, TrafficCar&);
    void crash_car(Rider&); // 0x00403d60 reason 2 (car contact, animation 29)
    void crash_reason(Rider&, int32_t reason, std::array<int32_t, 3> rider_impulse,
                      std::array<int32_t, 3> bike_impulse);
    // 0x00403d60 with null impulse pointers (0x0043a610 reason 3).
    void crash_entry(Rider&, int32_t reason, std::optional<std::array<int32_t, 3>> rider_impulse,
                     std::optional<std::array<int32_t, 3>> bike_impulse); // reason-tagged crash entry
    void contact(Rider& self, Rider& other);
    void update_camera();
    // In-road BORR hazard pass (plan 16 Task 2): 0x00454240 instantiates only
    // BORR slots strictly inside the road edges via 0x00438930 (pool
    // DAT_00476ce8, template 0x00476cb8 class 1); 0x004373f0/0x00437660 sweep
    // the rider box against them and run the shared 0x00436a40 separator plus
    // the class-1 branch of 0x004049c0 (reason 1 through knock_off). Spans
    // are stored per route sample at course load; no allocation runs in the
    // tick path. Runs after collide_traffic, before advance_ride_animation.
    void collide_hazards(Rider&);
    void attach_terrain_type(); // traffic's 0x0041eca0 short-phase test reads the terrain type
    // Pedestrians (src/race_pedestrians.cpp).
    void build_pedestrian_slices(); // 0x00454120/0x004552e0 ring builds -> 0x004520d0
    void spawn_pedestrian(const RouteSample&, size_t sample, const DzhrActor&,
                          uint32_t jitter);             // 0x004520d0 actor record -> 0x004409e0
    void ring_actor(const RingActor&, uint32_t jitter); // TrackRing DZHR sink
    void step_pedestrians();                            // legacy cadence 0x00437e60 for type 0x20
    void run_ped_node(Pedestrian&);                     // one +0x20 node of 0x00437e60
    void load_pedestrian_looks();                       // 0x00440680 first-draw resource load
    void think_pedestrian(Pedestrian&);                 // 0x00440dd0
    void move_pedestrian(Pedestrian&);                  // 0x00440990
    int pedestrian_lane(const Pedestrian&) const;       // 0x0044a020(obj, 0)
    bool pedestrian_road_clear(const Pedestrian&, int factor, int delta) const; // 0x00441500
    bool pedestrian_arrived(const Pedestrian&) const;                           // 0x004414a0
    void hit_pedestrian(Pedestrian&, int32_t hitter_speed, bool hitter_attacking,
                        bool hitter_walking);      // 0x00440760
    void collide_pedestrians();                    // 0x004368f0 pairs: rider/car with pedestrian
    void contact_pedestrian(Rider&, Pedestrian&);  // 0x004049c0 short-object branch for the rider
    void pedestrian_voice(Pedestrian&, int which); // 0x00436830(0x16) with the MROF voice
    void contact_hazard(Rider&, int32_t hazard_lo_q8, int32_t hazard_hi_q8);
    void prepare_static_boxes(); // records this tick's frame draw (box loading), no simulation RNG
    void apply_static_boxes();   // the previous frame's draw loads its collision boxes
    bool box_draw_pending_ = false;
    int32_t box_draw_camera_ = 0;
    bool sweep_statics(Rider&, Rider* half_owner = nullptr); // 0x004373f0 against the live ring slice
    void collide_car_statics();                              // 0x004368f0 runs 0x004373f0 for due cars too
    TrafficCar* static_car_ = nullptr; // the car whose sweep is running (0x0044efa0 callback)
    void hit_static(Rider&, int32_t lateral_units, const std::array<int32_t, 4>& box,
                    Rider* half_owner = nullptr);                      // 0x00437660
    void contact_half_static(Rider& body, Rider& owner, Rider& other); // 0x004046b0 crashed-half contact
    void contact_static(Rider& self, Rider& other);                    // 0x004049c0 against a type-1 static
    void fight(Rider&, uint32_t controls, size_t index);
    bool fight_hit_frame(const Rider&) const; // descriptor flags + frame-2 gate (0x004096f0 head)
    bool fight_reach(const Rider&, const Rider&, bool side) const; // 0x00409c10
    void resolve_hit(Rider& attacker, size_t index);
    void nitro_think(Rider&, uint32_t controls, bool player); // 0x0040b110/0x0040b170 (player, 0x00434660)
    void nitro_think_activate(Rider&);                        // 0x0040b110 body
    void nitro_think_stop(Rider&);                            // 0x0040b170 body
    void nitro_charge_tick(Rider&);                           // 0x0040b1a0 (player, 0x00407a90)
    int32_t nitro_throttle_force(Rider&, int32_t target);     // 0x0040adb0 boost branch
    void advance_ride_animation(Rider&,
                                bool player); // riding block in Race::step (0x00407a90/0x00407f30/0x0043ae90)
    uint32_t random();
    int name_roll_ = -1;
    size_t built_riders_ = SIZE_MAX; // build_draws: riders 0x00405600 has built so far (scan limit)
    void spawn_brain(Rider& r, size_t built);
    struct PoliceSetup {
        PoliceConfig config;
        std::vector<BikeSpec> specs;
        const ActorAnimationLibrary* library = nullptr;
    };
    std::optional<PoliceSetup> police_;
    bool busted_ = false;
    bool cop_removal_pending_ = false;
    void police_think(Rider& player);        // 0x00407f30 arrest + periodic spawn (owner 0)
    int find_cop(const Rider& player) const; // 0x00407860
    struct HalfMirrorView {
        int32_t lateral, height, position, speed;
    };
    void siren_event(const Rider& cop, bool emitting); // 0x00436830(0x13, ...)
    void remove_cop();                                 // 0x004073a0
    void init_live_rider(Rider&, int32_t interval);    // 0x00438930 scheduler words
    void schedule_add(int rider);                      // 0x00438aa0 insert
    const ActorAnimationBank& ride_bank(const Rider& r) const {
        return ride_library_->playback[r.cop ? 1 : 0];
    }

    std::function<const TerrainSlice&(const RouteSample&)> terrain_source_; // base terrain (no ring writes)
    // Plan 34 Task 3: ring terrain the simulation reads (0x00451910 slices),
    // per course (section, local), copy-on-write so snapshots restore it.
    struct RingGround {
        bool set = false;
        TerrainSlice slice;
    };
    std::shared_ptr<const std::vector<uint32_t>> ground_base_; // section -> first index
    CowVector<RingGround, 64> ground_;
    const TerrainSlice& ground(const RouteSample& s) const;
    std::function<int32_t(const RouteSample&)> rail_height_source_;
    TrafficSystem* traffic_ = nullptr;
    PedestrianSystem* pedestrians_ = nullptr;
    std::vector<int32_t> car_scan_keys_; // list positions retained across this scheduler pass
    std::vector<int32_t> ped_scan_keys_;
    mutable std::vector<ScanObject> scan_; // reused by build_scan_list (no tick allocation after warm-up)
    uint32_t spawn_order_ = 0;             // creation counter for pedestrians
    bool countdown_started_ = false;       // first countdown tick seen (player dt 2)
    TrackRing* ring_ = nullptr;
    std::array<TrackRing*, 8> rings_{};                    // seat -> ring window (rings_[0] == ring_)
    std::array<int32_t, 8> seat_back_{}, seat_previous_{}; // seats >= 1: camera pull-back, last ring camera
    std::array<int32_t, 8> seat_heading_{};   // presentation headings, one independent view per human
    std::array<int32_t, 8> seat_moved_{};     // seats >= 1: camera advance this step
    int actor_seat_ = 0;                      // whose ring window is firing DZHR actors (plan 34 C4)
    void update_traffic_windows(bool moving); // plan 34 C3: seat windows + 0x0044f0d0 gates
    std::array<std::function<void(const RingActor&, uint32_t)>, 8> seat_sink_{};
    uint32_t seat_camera(int seat) const;                  // camera_position() for any seat
    const TrackRing* nearest_ring(int32_t position) const; // ring whose camera is nearest
    // 0x00438108 window test over every window: true when no window keeps
    // the position at least `front` from its front and `back` from its back.
    bool outside_windows(int32_t position, int32_t front, int32_t back) const;
    bool ring_setup_ = false; // inside 0x004552e0: no player object yet
    bool freeze_actors_ = false;
    int32_t setup_tick_ = 2; // set_pedestrian_setup_tick
    std::function<uint32_t()> ring_random_;
    std::function<void(const RingActor&, uint32_t)> ring_sink_;
    const ActorAnimationLibrary* crash_library_ = nullptr;
    const ActorAnimationLibrary* ride_library_ = nullptr;
    std::optional<std::array<int8_t, 8>> steal_codes_; // 0x00464538
    const std::array<std::array<std::array<int8_t, 4>, 2>, 2>* attack_codes_ = nullptr;
    CrashConfig crash_config_;
    NitroConfig nitro_;
    bool nitro_set_ = false;
    int32_t nitro_charges_ = 0;
    // Immutable after construction and shared between copies, so a race
    // snapshot (plan 34 Task 3) copies only mutable state.
    std::shared_ptr<const std::vector<RouteSample>> routes_[2];
    PhysicsTables tables_;
    std::shared_ptr<const std::vector<std::vector<RoadHazard>>> hazards_[2]{
        std::make_shared<const std::vector<std::vector<RoadHazard>>>(),
        std::make_shared<
            const std::vector<std::vector<RoadHazard>>>()}; // BORR spans per route sample per road
    std::shared_ptr<const DzhrTable> dzhr_[2]{std::make_shared<const DzhrTable>(),
                                              std::make_shared<const DzhrTable>()}; // DZHR solids per sample
    struct StaticSlice {
        bool built = false, boxes_ready = false;
        uint8_t borr_count = 0;
        std::array<int32_t, 8> borr{}; // absolute laterals, units (+0x6c)
        const AnimationFrame* borr_frame = nullptr;
        uint8_t billboards = 0;
        struct Billboard {
            uint8_t type = 0;
            int32_t lateral = 0;
            const AnimationFrame* frame = nullptr;
            uint8_t selector = 0;
        };
        std::array<Billboard, 2> billboard{};
    };
    // Sub-slice +0xac per ring slot (128 slots per road): 0x0044b630 loads
    // it with the boxes, and the slot rebuild (0x00451cb0) never clears it.
    std::array<std::array<uint32_t, 128>, 2> slot_ac_{};
    uint32_t slice_ac(int road, size_t sample) const { return slot_ac_[size_t(road)][sample & 127]; }
    CowVector<StaticSlice> statics_[2]; // mutable (ring builds), copy-on-write for snapshots
    StaticFrames static_frames_;
    Rider static_actor_{}; // 0x004c3e90 static hazard object (rebuilt per hit)
    std::vector<Rider> riders_;
    std::vector<int> human_riders_{0};   // seat -> rider index (plan 34)
    std::array<uint32_t, 8> controls_{}; // this tick's word per seat
    bool net_ = false;
    std::array<int8_t, 8> handicap_{}; // 0x004c1200[slot]
    int32_t handicap_tick_ = 0;        // DAT_0049bdd0
    void update_handicap();            // 0x00442f60
    bool human(const Rider& r) const { return r.human >= 0 && !r.ai; }
    // The word a rider's think receives: its own seat's, or seat 0's for AI
    // riders (they always received the player's word and ignore it).
    uint32_t word(const Rider& r) const { return controls_[size_t(r.human > 0 && !r.ai ? r.human : 0)]; }
    // The human an AI or the scheduler measures against: nearest by track
    // position, ties to the lower seat; rider 0 with one human (plan 34).
    const Rider& nearest_human(int32_t position) const;
    int32_t ticks_ = 0;
    int level_ = 0;
    int last_target_ = -1;
    bool demo_hold_ = false; // DAT_004753c8 (see set_demo_hold)
    int32_t road_base_ = 0;  // absolute Q8 anchor (ctor param)
    int32_t finish_q8_ = 0;  // global finish DAT_004b6e58 (Q8); 0 = route end (0x00454240/0x00406890)
    // 0x00465a10: per-level overtake eagerness, read once from the EXE; empty
    // image -> no table (the draw alone decides, as before).
    std::optional<std::array<int32_t, 5>> overtake_eagerness_;
    uint32_t seed_ = 1; // MSVC rand() state (0x00455eb0)
    std::vector<SoundEventRecord> sound_events_;
    void sound(SoundEvent, const Bike&);
    uint32_t previous_camera_position_ = 0;
    int32_t camera_heading_ = 0;
    int32_t camera_x_ = 0, camera_height_ = 150, camera_lift_ = 0, camera_back_ = 0;
};
} // namespace rr
