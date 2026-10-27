#include "traffic.hpp"
#include <cstdlib>
#include "race.hpp"
#include "assets.hpp"
#include "course.hpp"
#include "course_scene.hpp"
#include <filesystem>
#include <algorithm>
#include <iostream>
#include <map>

// Synthetic tests always run; the CARS-container corpus part skips when the
// original disc is not mounted. Every rule is traced in src/traffic.cpp.

static int32_t div256(int32_t x) {
    return (x + ((x >> 31) & 0xff)) >> 8;
}

static rr::RouteSample lane_sample(int32_t left_q8, int32_t right_q8) {
    rr::RouteSample s;
    s.path.left_q8 = left_q8;
    s.path.right_q8 = right_q8;
    s.offset = 1000;
    return s;
}

static void test_configure() {
    rr::TrafficTimers t;
    traffic_configure(t, -25); // 0x0044dd00(-25): rate only
    if (t.rate != 2500) throw std::runtime_error("rate");
    traffic_configure(t, 6); // 0x0044dd00(6) + 0x0044db80 tail
    const int32_t reload = div256(2500 * 6 * 0x3c);
    if (t.oncoming_reload != reload || t.other_reload != reload) throw std::runtime_error("reload");
    if (t.oncoming != reload || t.behind != 0 || t.ahead != -reload) throw std::runtime_error("timer starts");
    traffic_configure(t, 0); // density 0 means 6 in the original
    if (t.density != 6) throw std::runtime_error("default density");
    // 0x0044dd00 from a zone event: reloads follow the density, the
    // oncoming timer restarts, behind/ahead keep running.
    t.behind = 123;
    t.ahead = -77;
    rr::traffic_set_density(t, 30);
    if (t.oncoming_reload != div256(2500 * 30 * 0x3c) || t.oncoming != t.oncoming_reload || t.behind != 123 ||
        t.ahead != -77)
        throw std::runtime_error("zone density");
    rr::traffic_set_density(t, -45); // rate and cruise only
    if (t.rate != 4500 || t.cruise != 45 || t.oncoming_reload != div256(2500 * 30 * 0x3c))
        throw std::runtime_error("zone rate");
}

static void test_lane_count() {
    if (rr::traffic_lane_count(0) != 0) throw std::runtime_error("zero lanes");
    if (rr::traffic_lane_count(0x300) != 3) throw std::runtime_error("three lanes");
    if (rr::traffic_lane_count(0x2ff) != 2) throw std::runtime_error("fractional floor");
    if (rr::traffic_lane_count(0x100) != 1) throw std::runtime_error("one lane");
    if (rr::traffic_lane_count(-0x200) != int8_t((-0x200 + 0xff) >> 8))
        throw std::runtime_error("negative lane word");
    if (rr::traffic_lane_count(-0x100) != int8_t((-0x100 + 0xff) >> 8))
        throw std::runtime_error("negative single");
}

// One synthetic vehicle: a valid three-frame RRAN and a cel whose CCB flags
// select only the same-direction speed class, packed into a CRSR container.
static std::vector<uint8_t> synthetic_container(uint32_t cel_flags) {
    std::vector<uint8_t> anim;
    auto aw = [&](uint32_t v) {
        for (int b = 0; b < 4; ++b) anim.push_back(uint8_t(v >> (b * 8)));
    };
    const uint32_t info_off = 28 + 3 * 28, ccb_off = info_off + 20;
    aw(0x5252414e);
    aw(20);
    aw(info_off);
    aw(ccb_off);
    aw(0);
    aw(0x52524644);
    aw(28 * 3 + 8);
    for (int f = 0; f < 3; ++f)
        for (int i = 0; i < 7; ++i) aw(0); // three empty frames
    aw(0x414e494d);
    aw(20);
    aw(0);
    aw(0);
    aw(3); // 'MINA' info, count at +16
    aw(0x43434220);
    for (int i = 0; i < 19; ++i) aw(0); // 'CCB ' template
    std::vector<uint8_t> cel;
    auto cw = [&](uint32_t v) {
        for (int b = 0; b < 4; ++b) cel.push_back(uint8_t(v >> (b * 8)));
    };
    // 'SNAC' wrapper: size word, type 4, then the embedded sub-container at
    // +size whose own size word leads to the cel block (0x0040b5f0 chain).
    cw(0x43414e53);
    cw(12);
    cw(4);
    cw(0x5352434e);
    cw(0x18);
    cw(1);
    cw(0);
    cw(0); // sub-container header+name
    cel.resize(48, 0);
    cw(9);
    cw(cel_flags); // cel block: count, CCB flags
    std::vector<uint8_t> container;
    auto put = [&](const std::vector<uint8_t>& v) { container.insert(container.end(), v.begin(), v.end()); };
    auto kw = [&](uint32_t v) {
        for (int b = 0; b < 4; ++b) container.push_back(uint8_t(v >> (b * 8)));
    };
    auto tag = [&](const char* t) {
        for (int i = 0; i < 4; ++i) container.push_back(uint8_t(t[i]));
    };
    tag("CRSR");
    kw(0);
    kw(0);
    tag("LBTR");
    kw(24);
    kw(0); // 24-byte header, table at 24
    tag("LBTR");
    kw(0);
    kw(3);
    kw(0); // table: three entries
    // Two identical vehicles: the original's type loop excludes the last one,
    // so a single-vehicle library can never accept a spawn.
    const uint32_t anim_off = 24 + 16 + 3 * 32, cel_off = anim_off + uint32_t(anim.size()) * 2;
    tag("MINA");
    kw(1);
    kw(anim_off);
    kw(uint32_t(anim.size())); // displays as ANIM 1
    for (int i = 0; i < 16; ++i) container.push_back(0);
    tag("MINA");
    kw(2);
    kw(anim_off + uint32_t(anim.size()));
    kw(uint32_t(anim.size()));
    for (int i = 0; i < 16; ++i) container.push_back(0);
    tag("SNAC");
    kw(1);
    kw(cel_off);
    kw(uint32_t(cel.size())); // displays as CANS 1
    for (int i = 0; i < 16; ++i) container.push_back(0);
    put(anim);
    put(anim);
    put(cel);
    return container;
}

// Minimal i386 PE image with one section so read_exe_data can serve the
// traffic tables at their original virtual addresses.
static std::vector<uint8_t> synthetic_exe() {
    std::vector<uint8_t> exe(0x200 + 0x78000, 0);
    auto w16 = [&](size_t p, uint16_t v) {
        exe[p] = uint8_t(v);
        exe[p + 1] = uint8_t(v >> 8);
    };
    auto w32 = [&](size_t p, uint32_t v) {
        for (int b = 0; b < 4; ++b) exe[p + b] = uint8_t(v >> (b * 8));
    };
    w16(0, 0x5a4d);
    w32(60, 0x80); // MZ, PE header at 0x80
    w32(0x80, 0x4550);
    w16(0x84, 0x14c);
    w16(0x86, 1); // PE, i386, one section
    w16(0x80 + 20, 0xe0);
    w16(0x80 + 24, 0x10b);         // optional header size/magic
    w32(0x80 + 24 + 28, 0x400000); // image base
    const size_t table = 0x80 + 24 + 0xe0;
    w32(table + 8, 0x78000);
    w32(table + 12, 0x1000); // virtual size/address
    w32(table + 16, 0x78000);
    w32(table + 20, 0x200); // raw size/pointer
    auto data = [&](uint32_t va) -> size_t { return 0x200 + va - 0x1000; };
    for (int k = 0; k < 4; ++k)
        exe[data(0x75694 + k)] = uint8_t(k == 2 ? -1 : k == 0 ? 1 : 0); // signed drift bytes
    w32(data(0x75698 + 4), 16);
    w32(data(0x75698 + 8), 10);
    w32(data(0x75698 + 0x24), 50);
    w32(data(0x75698 + 0x2c), 111);
    exe[data(0x75698 + 0x28)] = 10;                                     // template size
    for (int k = 0; k < 4; ++k) w32(data(0x68f00 + 4 * k), 0x40u << k); // lookahead windows
    for (int i = 0; i < 1024; ++i) w32(data(0x75768 + 4 * i), uint32_t(i * 32));
    return exe;
}

// 0x0044e710 lane-validity (lines 122-215): a lane outside the slice's
// lane count steps toward validity on the linked slice, else the car
// stops (target 0). Verified against the L5e removal run (kind-0 lane 1
// with one right lane -> lane 0, speed 0). Kind 0 uses a non-strict
// bound (count <= lane is outside); kind 2 uses a strict bound
// (count < -lane is outside).
static void test_lane_validity_stop() {
    rr::VehicleLibrary library(synthetic_container(0));
    rr::TrafficSystem system(library, synthetic_exe());
    system.configure(6);
    system.set_spawn_enabled(false);
    auto random = [] { return 1u; };
    const int32_t back = 0x10000, front = back + 0x8000, camera = front - 0x5000;
    auto drive = [&](uint8_t kind, int8_t lane, int32_t left_q8, int32_t right_q8,
                     int32_t speed) -> rr::TrafficCar {
        std::vector<rr::RouteSample> route(800, lane_sample(left_q8, right_q8));
        system.begin_schedule(route, front, random, 1);
        auto& car = system.cars_mut()[0];
        car = rr::TrafficCar{};
        car.active = true;
        car.kind = kind;
        car.lane = lane;
        car.phase = 0x100;
        car.position = back + 0x4000;
        car.lateral = 0;
        car.speed = speed;
        car.motion.u8(0xc0) = 7;
        car.last_tick = 0;
        car.next_tick = 1;
        system.run_car(0, route, camera, front, camera, 0, random, 1, 8000);
        return car;
    };
    // L5e tick-2399 shape: kind-0 lane 1 with one right lane steps to
    // lane 0 and stops (approach clamps to target 0, drag is identity
    // on zeroed motion words). The block never sets lane_changed.
    {
        const auto car = drive(0, 1, 0x100, 0x100, 1000);
        if (car.lane != 0 || car.speed != 0 || car.lane_changed)
            throw std::runtime_error("kind-0 lane 1 of 1 must step to 0 and stop");
    }
    // Kind-0 negative lanes pull to lane 0 without stopping: lane -1
    // fails the first check, fails the step validation, then the
    // recheck (prev 0) passes and restores... lane 0 with cruise intact.
    {
        const auto car = drive(0, -1, 0x100, 0x100, 1000);
        if (car.lane != 0 || car.speed != 1036 || car.lane_changed)
            throw std::runtime_error("kind-0 lane -1 must pull to 0 without stopping");
    }
    // Kind-0 lane 0 with one right lane is valid: cruise approach only.
    {
        const auto car = drive(0, 0, 0x100, 0x100, 1000);
        if (car.lane != 0 || car.speed != 1036 || car.lane_changed)
            throw std::runtime_error("kind-0 lane 0 of 1 must hold cruise");
    }
    // Kind-2 lane -2 with one left lane steps to -1 and stops.
    {
        const auto car = drive(2, -2, 0x100, 0x100, 1000);
        if (car.lane != -1 || car.speed != 0 || car.lane_changed)
            throw std::runtime_error("kind-2 lane -2 of 1 must step to -1 and stop");
    }
    // Kind-2 lane -1 with one left lane is valid (strict bound).
    {
        const auto car = drive(2, -1, 0x100, 0x100, 1000);
        if (car.lane != -1 || car.speed != 1036 || car.lane_changed)
            throw std::runtime_error("kind-2 lane -1 of 1 must hold cruise");
    }
    // Kind-2 lane -2 with two left lanes is valid (L5-1995 probe pin).
    {
        const auto car = drive(2, -2, 0x200, 0x200, 1000);
        if (car.lane != -2 || car.speed != 1036 || car.lane_changed)
            throw std::runtime_error("kind-2 lane -2 of 2 must hold cruise");
    }
}

// Linked-slot reads use the newest same-residue sample at/under the
// ring front (mod-128 slots, verified against dumped proxy words on
// L5e). On a course that narrows to one lane, a behind-back car
// reads its slot sample, not its current sample.
static void test_lane_validity_linked_slots() {
    rr::VehicleLibrary library(synthetic_container(0));
    rr::TrafficSystem system(library, synthetic_exe());
    system.configure(6);
    system.set_spawn_enabled(false);
    auto random = [] { return 1u; };
    // One-lane stretch with a two-lane island: the slot sample and
    // the linked slot can disagree, exercising the revert path.
    std::vector<rr::RouteSample> route(400, lane_sample(0x200, 0x200));
    for (int s = 140; s <= 160; ++s) {
        if (s == 151) continue;
        route[size_t(s)].path.left_q8 = route[size_t(s)].path.right_q8 = 0x100;
    }
    const int32_t front = 200 * 256, back = front - 0x8000, camera = front - 0x5000;
    auto drive_at = [&](uint8_t kind, int8_t lane, int32_t sample, int32_t speed) {
        system.begin_schedule(route, front, random, 1);
        auto& car = system.cars_mut()[0];
        car = rr::TrafficCar{};
        car.active = true;
        car.kind = kind;
        car.lane = lane;
        car.phase = 0x100;
        car.position = sample * 256;
        car.lateral = 0;
        car.speed = speed;
        car.motion.u8(0xc0) = 7;
        car.last_tick = 0;
        car.next_tick = 1;
        system.run_car(0, route, camera, front, camera, 0, random, 1, 8000);
        return car;
    };
    // Slot sample 145 (1-lane), linked slot 146 (1-lane): step to 0,
    // recheck fails, stop. The current sample 17 is 2-lane, so the
    // static-route read would have skipped the block entirely.
    {
        const auto car = drive_at(0, 1, 17, 1000);
        if (car.lane != 0 || car.speed != 0 || car.lane_changed)
            throw std::runtime_error("linked 1-lane slot must stop the car");
    }
    // Slot sample 150 (1-lane), linked slot 151 (2-lane): the step to 0
    // applies but the recheck passes on the linked counts, so the lane
    // is restored and cruise continues (revert path).
    {
        const auto car = drive_at(0, 1, 22, 1000);
        if (car.lane != 1 || car.speed != 1036 || car.lane_changed)
            throw std::runtime_error("linked 2-lane slot must restore the lane");
    }
}

static void test_deferred_car_teardown() {
    rr::VehicleLibrary library(synthetic_container(0));
    rr::TrafficSystem traffic(library, synthetic_exe());
    traffic.set_spawn_enabled(false);
    std::vector<rr::RouteSample> road(800);
    for (auto& s : road) {
        s.path.left = s.path.right = 500;
        s.path.left_q8 = s.path.right_q8 = 0x200;
    }
    auto random = [] { return 1u; };
    const int32_t back = 0x10000, front = back + 0x8000, camera = front - 0x5000;
    traffic.begin_schedule(road, front, random, 1);
    auto& car = traffic.cars_mut()[0];
    car = rr::TrafficCar{};
    car.active = true;
    car.kind = 2;
    car.phase = 0x100;
    car.position = back - 1;
    car.lateral = -32000;
    car.speed = 1000;
    car.motion.u8(0xc0) = 7;
    car.last_tick = 0;
    car.next_tick = 1;
    traffic.run_car(0, road, camera, front, camera, 0, random, 1, 8000);
    if (!car.active || !car.pending_remove || car.last_tick != 101 || car.next_tick != 101 ||
        (car.motion.u8(0xc0) & 12) != 8)
        throw std::runtime_error("car retirement must mark the live node until next prepass");
    if (traffic.begin_schedule(road, front, random, 1) || !car.active)
        throw std::runtime_error("same-tick scheduler wrapper must retain marked car");
    traffic.begin_schedule(road, front, random, 2);
    if (car.active || car.pending_remove)
        throw std::runtime_error("next prepass must return retired car to pool");
}

static void test_library_choose() {
    rr::VehicleLibrary library(synthetic_container(0x100000));
    if (library.vehicles().size() != 2) throw std::runtime_error("synthetic vehicle count");
    if (library.vehicles()[0].animation.frames.size() != 3) throw std::runtime_error("synthetic frames");
    if (library.vehicles()[0].mask != 0x100000) throw std::runtime_error("synthetic mask");
    if (library.choose(0, 0) != 0) throw std::runtime_error("kind 0 accepted");
    if (library.choose(2, 0) != -1) throw std::runtime_error("kind 2 rejected by mask");
}

static void test_zone_events() {
    // Zone ends apply as their slice enters the ring front (and during the
    // initial fill of the 128 slices behind the first front).
    rr::VehicleLibrary library(synthetic_container(0));
    rr::TrafficSystem system(library, synthetic_exe());
    system.configure(6);
    std::vector<rr::RouteSample> route(400);
    for (auto& s : route) s = lane_sample(0, 0); // no lanes: nothing spawns
    auto zone = [&](size_t i, int8_t d) {
        route[i].path.zone_end = true;
        route[i].path.event_parameters[1] = uint8_t(d);
    };
    zone(20, 40);
    zone(200, -45);
    zone(260, 15);
    uint32_t seed = 1;
    auto random = [&]() -> uint32_t {
        seed = seed * 214013u + 2531011u;
        return (seed >> 16) & 0x7fffu;
    };
    int32_t camera = 0x3100;
    uint32_t tick = 0;
    auto step = [&](int32_t moved) {
        camera += moved;
        system.step(route, camera + 0x100, camera + 0x5100, moved, camera, 0, random, tick++);
    };
    step(0); // fill [front-128, front): sample 20 is inside
    if (system.zone_events() != 1 || system.timers().density != 40)
        throw std::runtime_error("initial fill zone");
    while (((camera + 0x5100) >> 8) <= 200) step(0x80);
    if (system.zone_events() != 2 || system.timers().rate != 4500)
        throw std::runtime_error("zone at ring front");
    step(-0x400);
    step(0x400); // back off and return: the front slice is rebuilt
    if (system.zone_events() != 3) throw std::runtime_error("rebuilt front slice");
    while (((camera + 0x5100) >> 8) <= 260) step(0x100);
    if (system.zone_events() != 4 || system.timers().density != 15 ||
        system.timers().oncoming_reload != div256(4500 * 15 * 0x3c))
        throw std::runtime_error("density after rate change");
}

static void test_countdown_timers() {
    rr::VehicleLibrary library(synthetic_container(0));
    rr::TrafficSystem system(library, synthetic_exe());
    system.configure(6);
    std::vector<rr::RouteSample> route(400, lane_sample(0x200, 0x300));
    for (auto& s : route) s.path.left = s.path.right = 750;
    rr::Race race(route, route, rr::PhysicsTables::synthetic(), rr::BikeSpec{}, 90);
    race.set_traffic(&system);
    const uint32_t position = race.position(), camera = race.camera_position(), seed = race.rng_state();
    const int32_t lateral = race.bike().i32(0x28), speed = race.bike().i32(0xec);
    for (int i = 0; i < 59; ++i) race.countdown_step();
    const auto& t = system.timers();
    if (t.oncoming != 3515 - 531 || t.behind != 531 || t.ahead != -3515 + 531)
        throw std::runtime_error("countdown banks 59 zero-distance timer ticks");
    if (race.rng_state() != seed || race.position() != position || race.camera_position() != camera ||
        race.bike().i32(0x28) != lateral || race.bike().i32(0xec) != speed)
        throw std::runtime_error("countdown changed player motion, camera or RNG");
    for (const auto& car : system.cars())
        if (car.active) throw std::runtime_error("short countdown spawned a car");
}

static void test_countdown_initial_fill() {
    rr::VehicleLibrary library(synthetic_container(0));
    rr::TrafficSystem system(library, synthetic_exe());
    system.configure(6);
    std::vector<rr::RouteSample> route(400, lane_sample(0, 0));
    route[20].path.zone_end = true;
    route[20].path.event_parameters[1] = 40;
    // The first ring is [2,130); both this zone and its changed density
    // must be visible before the first hold tick's spawn timers execute.
    constexpr int32_t camera = 0x3100, front = camera + 0x5100;
    uint32_t draws = 0;
    auto random = [&]() -> uint32_t {
        ++draws;
        return 0;
    };
    system.countdown_step(route, front, random, 1);
    if (system.zone_events() != 1 || system.timers().density != 40 ||
        system.timers().oncoming_reload != 23437 || system.timers().other_reload != 23437 ||
        system.timers().behind != 9 || system.timers().ahead != -3515 + 9 ||
        system.timers().oncoming != 23437 - 9)
        throw std::runtime_error("countdown initial density event/reload order");
    for (uint32_t tick = 2; tick <= 59; ++tick) system.countdown_step(route, front, random, tick);
    if (system.zone_events() != 1 || system.timers().oncoming != 23437 - 531)
        throw std::runtime_error("countdown repeated initial fill");
    system.step(route, camera + 0x100, front, 0, camera, 0, random, 60);
    if (system.zone_events() != 1 || system.timers().oncoming != 23437 - 540 || draws != 0)
        throw std::runtime_error("racing step replayed countdown ring fill");
}

static void test_countdown_disabled_spawn_expiry() {
    rr::VehicleLibrary library(synthetic_container(0));
    rr::TrafficSystem system(library, synthetic_exe());
    system.configure(6);
    system.set_spawn_enabled(false);
    const std::vector<rr::RouteSample> route(400, lane_sample(0x200, 0x300));
    uint32_t draws = 0;
    auto random = [&]() -> uint32_t {
        ++draws;
        return 0;
    };
    // 390 ticks leave oncoming/ahead at +/-5. Tick 391 expires both;
    // the nulled pool still consumes one lane draw for each attempted car.
    for (uint32_t tick = 1; tick <= 390; ++tick) system.countdown_step(route, 0x8200, random, tick);
    if (draws != 0 || system.timers().oncoming != 5 || system.timers().ahead != -5)
        throw std::runtime_error("countdown expired before timer threshold");
    system.countdown_step(route, 0x8200, random, 391);
    if (draws != 2 || system.timers().oncoming != 3511 || system.timers().ahead != -3511)
        throw std::runtime_error("countdown nulled-pool lane RNG/reload");
    for (uint32_t tick = 392; tick <= 400; ++tick) system.countdown_step(route, 0x8200, random, tick);
    if (draws != 2) throw std::runtime_error("countdown repeated expiry draws");
    for (const auto& car : system.cars())
        if (car.active || car.serial != 0) throw std::runtime_error("countdown disabled pool placed a car");
}

static void test_car_schedule_motion() {
    rr::VehicleLibrary library(synthetic_container(0));
    rr::TrafficSystem system(library, synthetic_exe());
    system.configure(6);
    system.set_spawn_enabled(false);
    std::vector<rr::RouteSample> route(300);
    for (auto& s : route) {
        s = lane_sample(0x200, 0x300);
        s.offset = 0;
    }
    auto& c = system.cars_mut()[0];
    c.active = true;
    c.position = 34816;
    c.lateral = 32000;
    c.phase = 224;
    c.last_tick = 216;
    c.next_tick = 226;
    c.motion.i32(0xc4) = 16;
    c.motion.i32(0x160) = 111;
    c.motion.i32(0x150) = 50;
    c.motion.i32(0x154) = 10;
    unsigned draws = 0;
    auto random = [&]() {
        ++draws;
        return 0u;
    };
    system.schedule(route, 0x4000, 0x9000, 0, 0, random, 227);
    system.step(route, 0x4000, 0x9000, 0, 0, 0, random, 227);
    if (c.dt != 11 || c.speed != 231 || c.position != 34828 || c.lateral != 32000 || c.next_tick != 287)
        throw std::runtime_error("car first scheduled think/drag/move (dt 11)");
    system.step(route, 0x4000, 0x9000, 0, 0, 0, random, 228);
    if (c.last_tick != 227 || c.speed != 231 || c.position != 34828 || draws != 0)
        throw std::runtime_error("car must sleep between due ticks without drawing RNG");
    // 0x00437e60 type-0x10 boundaries: relative velocity, slow speed,
    // near-camera cap, far-camera factor four and crossing-car branch.
    struct Case {
        int kind, d, speed, velocity, player, want;
    };
    const Case cases[] = {
        {0, 0, 2500, 2500, 4499, 1},  {0, 0, 2500, 2500, 4500, 2},  {0, 0, 1999, 1999, 1999, 2},
        {0, -3, 2500, 2500, 2500, 8}, {0, 37, 2500, 2500, 2500, 6}, {0, 38, 2500, 2500, 2500, 40},
        {0, 79, 0, 0, 0, 60},         {2, -5, 2500, 0, 0, 8},       {2, 9, 1999, 0, 0, 5},
        {1, -2, 0, 0, 0, 2},          {1, -3, 0, 0, 0, 3},          {3, 4, 0, 0, 0, 3}};
    for (const auto& x : cases) {
        rr::TrafficCar car;
        car.kind = uint8_t(x.kind);
        car.position = x.d * 256;
        car.speed = x.speed;
        car.motion.i32(0x108) = x.velocity;
        if (rr::TrafficSystem::schedule_interval(car, 0, x.player) != x.want)
            throw std::runtime_error("car scheduler LOD boundary");
    }
}

static void test_spawn_and_move() {
    rr::VehicleLibrary library(synthetic_container(0));
    std::vector<uint8_t> exe = synthetic_exe();
    rr::TrafficSystem system(library, exe);
    system.configure(6);
    if (system.drift_rate(0) != 800 || system.drift_rate(2) != -800)
        throw std::runtime_error("signed-byte drift table");
    std::vector<rr::RouteSample> route(300);
    for (auto& s : route) s = lane_sample(0x200, 0x300); // 2 left, 3 right lanes
    const int32_t camera = 64 * 256, front = camera + 0x5000;
    uint32_t seed = 1;
    auto random = [&]() -> uint32_t {
        seed = seed * 214013u + 2531011u;
        return (seed >> 16) & 0x7fffu;
    };
    // No movement: the oncoming timer decays by rate/256 = 9 per tick and
    // the ahead timer rises by 9; both cross their thresholds on the same
    // tick (their reloads are equal).
    const int32_t reload = system.timers().oncoming_reload;
    const int steps = reload / 9 + 1;
    for (int i = 0; i < steps; ++i) system.step(route, camera, front, 0, camera, 0, random, uint32_t(i));
    size_t oncoming = 0, active = 0;
    const rr::TrafficCar* spawned = nullptr;
    for (const auto& car : system.cars()) {
        if (!car.active) continue;
        ++active;
        if (car.kind == 2) {
            ++oncoming;
            spawned = &car;
        }
    }
    if (active == 0) throw std::runtime_error("no spawn after timer decay");
    if (oncoming != 1) throw std::runtime_error("oncoming spawn count");
    if (spawned->position != front - 0x100) throw std::runtime_error("spawn position");
    if (spawned->lane != -1 && spawned->lane != -2)
        throw std::runtime_error("oncoming lane"); // -1 - (rand % 2)
    if (spawned->lateral != (1000 + spawned->lane * 250 + 125) * 256)
        throw std::runtime_error("oncoming lateral");
    if (spawned->phase < 0xc0 || spawned->phase > 0x1df) throw std::runtime_error("phase range");
    if (spawned->serial != 1) throw std::runtime_error("first serial");
    const uint32_t serial = spawned->serial;
    const int32_t lane_centre = (1000 + spawned->lane * 250 + 125) * 256;
    // Movement with the player centred in the car's lane: the oncoming car
    // accelerates toward its cruise speed, drives backward through the horn
    // window and parks at the back cursor.
    int horns = 0;
    for (int i = 0; i < 6000; ++i) {
        system.step(route, camera, front, 0, camera, lane_centre, random, uint32_t(steps + i));
        for (const auto& e : system.take_sound_events()) {
            if (e.code != rr::SoundEvent::horn) throw std::runtime_error("unexpected traffic event");
            ++horns;
        }
    }
    for (const auto& car : system.cars())
        if (car.active && car.serial == serial) throw std::runtime_error("oncoming despawn at back cursor");
    if (horns != 0) throw std::runtime_error("movement must not invoke the contact horn callback");
    // Pool limit is the original 0x00438b70(0x210,0x10) allocation.
    if (rr::TrafficSystem::pool_size != 16) throw std::runtime_error("pool size");
}

static void test_zone_corpus() {
    // Napa at a steady 0x60 Q8/tick: spawns with the course's zone events
    // against the old fixed density 6, over the same random stream.
    const std::string root = RR_SOURCE_DIR "/../cdrom/ROADRASH";
    if (!std::filesystem::exists(root + "/DATA/CARS/NAPA.CAR")) return;
    rr::Assets assets(root);
    rr::VehicleLibrary library(assets.read("DATA/CARS/NAPA.CAR"));
    const auto exe = assets.read("ROADRASH.EXE");
    const rr::Course course = rr::read_course(assets.read("DATA/COURSES/NAPA.CRS"));
    auto run = [&](bool zones, int32_t& events) {
        auto route = rr::make_route(course, false);
        if (!zones)
            for (auto& s : route) {
                s.path.zone_end = false;
                s.other.zone_end = false;
            }
        rr::TrafficSystem system(library, exe);
        system.configure(6);
        uint32_t seed = 1, spawned = 0, last = 0, tick = 0;
        auto random = [&]() -> uint32_t {
            seed = seed * 214013u + 2531011u;
            return (seed >> 16) & 0x7fffu;
        };
        for (int32_t camera = 0x3100; ((camera + 0x5100) >> 8) + 1 < int32_t(route.size()); camera += 0x60) {
            system.step(route, camera + 0x100, camera + 0x5100, 0x60, camera, 0, random, tick++);
            for (const auto& car : system.cars())
                if (car.active && car.serial > last) {
                    ++spawned;
                    last = std::max(last, car.serial);
                }
        }
        events = system.zone_events();
        return spawned;
    };
    int32_t with_events = 0, without_events = 0;
    const uint32_t with = run(true, with_events), without = run(false, without_events);
    std::cout << "Napa spawns at 0x60/tick: " << with << " with " << with_events << " zone events, "
              << without << " at fixed density 6\n";
    if (with_events == 0 || without_events != 0) throw std::runtime_error("Napa zone events");
    if (with >= without) throw std::runtime_error("Napa zones should thin traffic");
}

static void test_corpus() {
    const std::string root = RR_SOURCE_DIR "/../cdrom/ROADRASH";
    if (!std::filesystem::exists(root + "/DATA/CARS/NAPA.CAR")) {
        std::cout << "No CARS corpus\n";
        return;
    }
    rr::Assets assets(root);
    rr::VehicleLibrary library(assets.read("DATA/CARS/NAPA.CAR"));
    if (library.vehicles().size() != 10) throw std::runtime_error("Napa vehicle count");
    for (size_t i = 0; i < library.vehicles().size(); ++i) {
        const auto& v = library.vehicles()[i];
        if (v.animation.frames.empty()) throw std::runtime_error("vehicle animation frames");
        // The first six vehicles pair with CANS wrappers and carry real masks.
        if (i < 6 && v.mask == 0) throw std::runtime_error("vehicle mask missing");
    }
    for (uint8_t kind = 0; kind < 4; kind += 2)
        for (uint32_t r = 0; r < 64; ++r) {
            const int pick = library.choose(kind, r);
            if (pick < 0) throw std::runtime_error("no vehicle accepted");
            if (size_t(pick) + 1 >= library.vehicles().size()) throw std::runtime_error("choose range");
        }
    // Live route: spawn cars through the real course and render their nodes.
    rr::Course course = rr::read_course(assets.read("DATA/COURSES/NAPA.CRS"));
    const auto route = rr::make_route(course, false);
    std::vector<uint8_t> exe = assets.read("ROADRASH.EXE");
    rr::TrafficSystem system(library, exe);
    system.configure(6);
    uint32_t seed = 1;
    auto random = [&]() -> uint32_t {
        seed = seed * 214013u + 2531011u;
        return (seed >> 16) & 0x7fffu;
    };
    const int32_t camera = 12544, front = camera + 0x5000;
    for (int i = 0; i < 2000; ++i) {
        system.step(route, camera, front, i < 100 ? 300 : 0, camera, 0, random, uint32_t(i));
        if (std::any_of(system.cars().begin(), system.cars().end(), [](const auto& c) { return c.active; }))
            break;
    }
    size_t active = 0;
    for (const auto& car : system.cars())
        if (car.active) ++active;
    if (active == 0) throw std::runtime_error("no corpus traffic spawned");
    rr::DepthTable depths = rr::read_depth_table(exe);
    rr::Camera cam{0, 150};
    const auto first =
        std::find_if(system.cars().begin(), system.cars().end(), [](const auto& c) { return c.active; });
    const int32_t render_camera = first->position - 0x1000;
    const auto slices = rr::project_road(route, uint32_t(render_camera), cam, depths);
    const auto nodes = rr::traffic_draw_nodes(system, route, slices, cam, depths, render_camera, 300);
    if (nodes.empty()) throw std::runtime_error("no corpus traffic nodes");
    for (const auto& node : nodes)
        if (!node.texture) throw std::runtime_error("traffic node without texture");
}

// Plan 11a Task 1 Step 3: synthetic rider–car contact boundaries. A live
// Napa race with one seeded car, driven through the real Race::step path:
// - boundary: gap 128 still enters contact (0x004368f0 breaks only when the
//   summed half-lengths are STRICTLY below the gap) but both separation
//   axes are stale, so the hit is silent and only the 0x00404f4d nudge moves
//   the rider; gap 129 misses the gate entirely (no nudge);
// - crashed rider: no new crash is entered (0x004049c0 rejects).
static void test_car_contact_synthetic() {
    const std::string root = RR_SOURCE_DIR "/../cdrom/ROADRASH";
    if (!std::filesystem::exists(root + "/ROADRASH.EXE")) {
        std::cout << "No disc for contact tests\n";
        return;
    }
    rr::Assets assets(root);
    const auto exe = assets.read("ROADRASH.EXE");
    const auto tables = rr::read_physics_tables(exe);
    const auto course = rr::read_course(assets.read("DATA/COURSES/NAPA.CRS"));
    const auto specs = rr::read_bike_specs(assets.read("DATA/BIKESPEC.RSC"));
    const rr::VehicleLibrary library(assets.read("DATA/CARS/NAPA.CAR"), exe);
    rr::CourseScene scene(assets, exe, course, "NAPA", 1, 0);
    // Boundary: bike half-length 64 + car half-length 64 = 128. Probe 0
    // seeds the car exactly at the edge (gap 128: contact), probe 1 one
    // unit outside it (gap 129: no contact). Lateral/height aligned.
    // Detection (0x004368f0: longitudinal gate breaks only when
    // sum < gap, so equality contacts; port src/race.cpp:362) admits the
    // edge probe. The separation axis choice (0x00436a40 prev-copy
    // strict <, port src/race.cpp:369-372) then sees both axes stale:
    // Race::move (0x004395e0, port src/race.cpp:615) recopies the
    // previous-position words at step start, so prev == current and
    // 128 < 128 is false on both axes. Impulses stay 0 and the handler
    // (0x004049c0 LAB_00404c2d: hit 0 < 0x7d1 with lateral 0 < 0x3e9 is
    // silent, port src/race.cpp:448-455,473) leaves steer rate and speed
    // alone; the tail (0x00404feb, port src/race.cpp:522) clears the
    // impulses/flags. The ONLY observable is the unconditional
    // 0x00404f4d nudge ((7680 + 16384) >> 4 * dt = 1504, port
    // src/race.cpp:519-520): car lateral equals rider lateral, so the
    // else branch pushes the rider -1504 laterally.
    for (int probe = 0; probe < 2; ++probe) {
        rr::Race race(rr::make_route(course, false), rr::make_route(course, true), tables, specs.at(0), 90,
                      {}, 0);
        rr::TrafficSystem traffic(library, exe);
        traffic.configure(6);
        race.set_traffic(&traffic);
        race.set_terrain_source([&](const rr::RouteSample& s) -> const rr::TerrainSlice& {
            return scene.terrain(s.section, s.local);
        });
        auto& me = race.riders()[0];
        me.bike.i32(0x178) = 64;
        me.bike.i32(0x174) = 7680;
        me.bike.i32(0x17c) = 8960;
        // Pre-roll one step so walls()/vertical() settle the rider inside
        // the shoulder (the grid lateral starts outside it); the car is
        // seeded against the SETTLED position, which is what
        // collide_traffic sees. Speed words are zeroed after the pre-roll
        // so travel does not move the rider during the measured step.
        race.set_rng_state(1);
        race.step(0);
        const int32_t p0 = me.bike.i32(0x1c), l0 = me.bike.i32(0x28);
        auto& cars = traffic.cars_mut();
        for (auto& c : cars) c.active = false;
        cars[0].active = true;
        cars[0].position = p0 + 128 + probe; // probe 0: edge, probe 1: outside
        cars[0].lateral = l0;
        cars[0].speed = 0;
        me.bike.i32(0xec) = 0;
        me.bike.i32(0x108) = 0;
        me.bike.i32(0x10c) = 0;
        me.bike.i32(0xf0) = 0;
        const int32_t lat_before = me.bike.i32(0x28);
        const int32_t f0_before = me.bike.i32(0xf0);
        race.set_rng_state(1);
        race.step(0);
        const int32_t lat_after = me.bike.i32(0x28);
        if (probe == 0) {
            // Edge contact: silent hit, nudge only.
            if (lat_after != lat_before - 1504) throw std::runtime_error("edge nudge missed");
            if (me.bike.i32(0xf0) != f0_before) throw std::runtime_error("edge hit was not silent (f0)");
            if (me.bike.i32(0xec) != 0) throw std::runtime_error("edge hit was not silent (speed)");
            if (me.bike.i32(0x2f4) != 0) throw std::runtime_error("edge hit crashed the rider");
        } else {
            // Outside the gate: no contact, rider untouched.
            if (lat_after != lat_before) throw std::runtime_error("outside contact fired (lateral)");
            if (me.bike.i32(0xf0) != f0_before) throw std::runtime_error("outside contact fired (f0)");
            if (me.bike.i32(0xec) != 0) throw std::runtime_error("outside contact fired (speed)");
            if (me.bike.i32(0x2f4) != 0) throw std::runtime_error("outside contact fired (crash)");
        }
    }
    // Crashed rider: contact must not enter a new crash.
    {
        rr::Race race(rr::make_route(course, false), rr::make_route(course, true), tables, specs.at(0), 90,
                      {}, 0);
        rr::TrafficSystem traffic(library, exe);
        traffic.configure(6);
        race.set_traffic(&traffic);
        race.set_terrain_source([&](const rr::RouteSample& s) -> const rr::TerrainSlice& {
            return scene.terrain(s.section, s.local);
        });
        auto& me = race.riders()[0];
        me.bike.i32(0x178) = 64;
        me.bike.i32(0x174) = 7680;
        me.bike.i32(0x17c) = 8960;
        me.bike.i32(0x2f4) = 1; // already crashed
        const int32_t p0 = me.bike.i32(0x1c), l0 = me.bike.i32(0x28);
        auto& cars = traffic.cars_mut();
        for (auto& c : cars) c.active = false;
        cars[0].active = true;
        cars[0].position = p0;
        cars[0].lateral = l0;
        cars[0].speed = 0;
        race.set_rng_state(1);
        race.step(0);
        if (me.bike.i32(0x2f8) == 2) throw std::runtime_error("crashed rider took car crash");
    }
    // Split roadway: a rider on the other branch at the same position as
    // the car must not collide (0x004368f0 +0x36 & 3 shared-half test;
    // port src/race.cpp:358-361). LIMIT (Oct 12): this probe is NOT YET
    // PASSING — the parked AI rider's lateral still moves during the step,
    // which means either the exclusion misses (route/sample desync between
    // route_of(r) and the dual flag) or AI-adjacent motion (walls on route
    // documents the gap; do not claim split parity until it passes.
    {
        const auto route_a = rr::make_route(course, false);
        size_t dual_sample = 0;
        for (size_t i = 0; i < route_a.size(); ++i)
            if (route_a[i].dual) {
                dual_sample = i;
                break;
            }
        if (dual_sample == 0) throw std::runtime_error("no dual sample on Napa route 0");
        rr::Opponent opp{specs.at(0), 90, 40, 40, 0, 1};
        rr::Race race(rr::make_route(course, false), rr::make_route(course, true), tables, specs.at(0), 90,
                      {opp}, 0);
        rr::TrafficSystem traffic(library, exe);
        traffic.configure(6);
        race.set_traffic(&traffic);
        auto& ai = race.riders()[1];
        ai.bike.i32(0x178) = 64;
        ai.bike.i32(0x174) = 7680;
        ai.bike.i32(0x17c) = 8960;
        ai.route = 1; // other branch; player (riders_[0]) stays on route 0
        ai.sample = ai.previous_sample = dual_sample;
        ai.bike.i32(0x1c) = int32_t(dual_sample << 8);
        ai.bike.i32(0x138) = ai.bike.i32(0x1c);
        // Let walls() settle first: read the clamped lateral, THEN seed
        // the car dead-center on it. Otherwise the clamp moves the rider
        // after seeding and the test measures the clamp, not the exclusion.
        ai.bike.i32(0x114) = 0;
        ai.bike.i32(0x240) = 0;
        // Settle walls() BEFORE seeding the car: step once with no cars,
        // then seed dead-center on the clamped lateral.
        race.set_rng_state(1);
        race.step(0);
        const int32_t l0 = ai.bike.i32(0x28);
        auto& cars = traffic.cars_mut();
        for (auto& c : cars) c.active = false;
        cars[0].active = true;
        cars[0].position = ai.bike.i32(0x1c);
        cars[0].lateral = l0;
        cars[0].speed = 0;
        ai.bike.i32(0xec) = 0;
        ai.bike.i32(0x108) = 0;
        ai.bike.i32(0x10c) = 0;
        ai.bike.i32(0x114) = 0;
        ai.bike.i32(0x240) = 0; // no throttle: no travel
        ai.brain.current_speed = 0;
        ai.brain.target_speed = 0;
        ai.brain.base_speed = 0; // parked AI: no drive
        ai.ai = false;           // no think/step: rider is a static post; only the contact pass may move it
        ai.bike.i32(0xf0) = 0;
        const int32_t lat_before = ai.bike.i32(0x28);
        race.set_rng_state(1);
        race.step(0);
        if (ai.bike.i32(0x28) != lat_before)
            throw std::runtime_error("split-roadway contact fired (lateral)");
        if (ai.bike.i32(0xf0) != 0) throw std::runtime_error("split-roadway contact fired (f0)");
        if (ai.bike.i32(0x2f4) != 0) throw std::runtime_error("split-roadway contact fired (crash)");
    }
    std::cout << "car contact synthetic boundaries pass\n";
}

// Synthetic intersection: 3+3 lanes (750 world units a side), a type-1
// event over samples 90..100 with marking 8 / 0x20.. / 0x10 and the zone
// end at 100; record byte +9 (cross-road lanes) 4, density 6.
static std::vector<rr::RouteSample> intersection_route(int variant = 1) {
    std::vector<rr::RouteSample> route(400);
    for (auto& s : route) {
        s = lane_sample(0x300, 0x300);
        s.path.left = s.path.right = 750;
    }
    for (int j = 90; j <= 100; ++j) {
        auto& p = route[size_t(j)].path;
        p.event_type = uint8_t(variant);
        p.event_start = 90;
        p.event_end = 100;
        p.event_parameters = {4, 6, 0};
        p.marking_flags = j == 90 ? 8 : j == 99 ? 16 : j == 100 ? 0 : 0x20;
    }
    route[100].path.zone_end = true;
    return route;
}
static void test_intersections() {
    // 0x0041eca0 / 0x0041ef60 / 0x0041f040 / 0x0044df60 / 0x0044e710.
    rr::VehicleLibrary library(synthetic_container(0));
    uint32_t seed = 1;
    auto random = [&]() -> uint32_t {
        seed = seed * 214013u + 2531011u;
        return (seed >> 16) & 0x7fffu;
    };
    const int32_t camera = 0x3100; // ring [0x200, 0x8200): sample 100 is in the first fill
    // Layout.
    {
        auto route = intersection_route();
        rr::TrafficSystem system(library, synthetic_exe());
        system.configure(6);
        system.step(route, camera + 0x100, camera + 0x5100, 0, camera, 0, random, 100);
        size_t controllers = 0, signals = 0;
        for (const auto& c : system.controllers()) controllers += c.active;
        for (const auto& g : system.signals()) signals += g.active;
        if (controllers != 1 || signals != 4)
            throw std::runtime_error("type-1 zone: one controller, four signals");
        const auto& c = system.controllers()[0];
        if (c.start_q8 != 90 * 256 || c.end_q8 != 100 * 256)
            throw std::runtime_error("controller +0x108/+0x10c from the event interval");
        if (c.variant != 1 || c.lanes != 4)
            throw std::runtime_error("controller +0x110/+0x111 from the record");
        if (c.durations[0] != 600 || c.durations[1] != 180 || c.durations[2] != 780)
            throw std::runtime_error("phase table 600/180/780");
        const struct {
            int index, side;
            int32_t position;
            int lane;
        } want[4] = {{0, 1, 99 * 256 + 0x50, 8},
                     {1, 1, 90 * 256 + 0xb0, 6},
                     {2, 0, 90 * 256 + 0xb0, 8},
                     {3, 0, 99 * 256 + 0x50, 6}};
        for (int k = 0; k < 4; ++k) {
            const auto& g = system.signals()[size_t(c.signals[k])];
            if (g.index != want[k].index || g.side != want[k].side || g.position_q8 != want[k].position ||
                g.lane != want[k].lane)
                throw std::runtime_error("signal layout (slot order 0,1,2,3)");
            if (want[k].side ? g.lateral_q8 <= 1000 * 256 : g.lateral_q8 >= 1000 * 256)
                throw std::runtime_error("signal on its shoulder");
        }
        // First think at the placing tick with dt 0x1e: even signals 2 -> 0,
        // odd 0 -> 1 -> 2; the next think is 30 ticks later.
        if (system.signals()[size_t(c.signals[0])].phase != 0 ||
            system.signals()[size_t(c.signals[1])].phase != 2)
            throw std::runtime_error("first think phases");
        if (c.next_tick != 130) throw std::runtime_error("controller think interval 0x1e");
    }
    // Crossing cars: spawned on an odd signal's wrap to phase 0 on a tick
    // with bit 3, (lane + 1) samples into the zone, 0x78000 to the side,
    // heading across; they drive across while green.
    {
        auto route = intersection_route();
        rr::TrafficSystem system(library, synthetic_exe());
        system.configure(0x7f); // density 127: few free-traffic spawns
        size_t crossings = 0;
        int32_t moved = 0;
        std::map<uint32_t, int32_t> first_lateral;
        for (uint32_t tick = 100; tick < 100 + 3000; ++tick) {
            system.step(route, camera + 0x100, camera + 0x5100, 0, camera, 0, random, tick);
            for (const auto& car : system.cars()) {
                if (!car.active || !(car.kind & 1)) continue;
                if (!first_lateral.count(car.serial)) {
                    ++crossings;
                    first_lateral[car.serial] = car.lateral;
                    if (car.kind != 1 && car.kind != 3) throw std::runtime_error("crossing kind");
                    // Controller spawns can run later in the same scheduler
                    // pass: template dt 10, then dt-scaled acceleration/motion.
                    const int32_t start = (car.kind == 3 ? 0x78000 : -0x78000) + 1000,
                                  travel = car.kind == 1 ? car.lateral - start : start - car.lateral;
                    if (travel < 0 || travel > car.dt * car.dt * 0x2d)
                        throw std::runtime_error("crossing start lateral");
                    if ((car.position & 0xff) !=
                            ((0x80 + ((-car.motion.accel * car.dt * car.dt) >> 9)) & 0xff) ||
                        (car.position >> 8) < 91 || (car.position >> 8) > 94)
                        throw std::runtime_error("crossing sample");
                    if (car.heading != (car.kind == 1 ? 0x400000 : -0x400000) || car.signal < 0)
                        throw std::runtime_error("crossing heading/signal");
                    if (!(tick & 8)) throw std::runtime_error("crossing spawned on a tick without bit 3");
                }
                moved = std::max(moved, std::abs(car.lateral - first_lateral[car.serial]));
            }
        }
        if (crossings == 0) throw std::runtime_error("no crossing car in 3000 ticks");
        if (moved < 0x20000) throw std::runtime_error("crossing cars do not drive across");
    }
    // A kind-0 car in the near-camera cadence holds at red and goes on green.
    {
        const int32_t camera = 84 * 256;
        auto route = intersection_route();
        rr::TrafficSystem system(library, synthetic_exe());
        system.configure(0x7f);
        uint32_t tick = 100;
        auto red = [&] {
            const auto& c = system.controllers()[0];
            return system.signals()[size_t(c.signals[0])].phase == 2;
        };
        system.step(route, camera + 0x100, camera + 0x5100, 0, camera, 0, random, tick++);
        while (!red()) system.step(route, camera + 0x100, camera + 0x5100, 0, camera, 0, random, tick++);
        const int32_t stop = 90 * 256 + 0xa0;
        auto& probe = system.cars_mut()[15];
        probe = rr::TrafficCar{};
        probe.active = true;
        probe.kind = 0;
        probe.lane = 0;
        probe.phase = 0x100;
        probe.speed = 2500;
        probe.last_tick = int32_t(tick) - 10;
        probe.next_tick = int32_t(tick);
        probe.motion.i32(0xc4) = 16;
        probe.motion.i32(0x160) = 111;
        probe.motion.i32(0x150) = 50;
        probe.motion.i32(0x154) = 10;
        probe.position = stop - 0x600;
        probe.lateral = (1000 + 125) * 256;
        probe.serial = 999;
        int32_t held = probe.position;
        int still = 0;
        for (int i = 0; i < 500 && red(); ++i) {
            system.step(route, camera + 0x100, camera + 0x5100, 0, camera, 0, random, tick++);
            if (!probe.active) throw std::runtime_error("probe car vanished");
            if (probe.position > stop)
                throw std::runtime_error("car ran the red light tick " + std::to_string(tick - 1) + " pos " +
                                         std::to_string(probe.position) + " stop " + std::to_string(stop) +
                                         " speed " + std::to_string(probe.speed) + " dt " +
                                         std::to_string(probe.dt) + " signal " +
                                         std::to_string(probe.signal));
            still = probe.position == held ? still + 1 : 0;
            held = probe.position;
        }
        // Think, drag and the coarse scheduler settle the car at red.
        if (still < 100 || probe.speed > 0x100) throw std::runtime_error("car not stopped at red");
        for (int i = 0; i < 900 && probe.active && probe.position <= stop; ++i)
            system.step(route, camera + 0x100, camera + 0x5100, 0, camera, 0, random, tick++);
        if (probe.position <= stop) throw std::runtime_error("car does not go on green");
    }
    // Pool caps and teardown behind the ring back.
    {
        std::vector<rr::RouteSample> route(800);
        for (auto& s : route) {
            s = lane_sample(0x300, 0x300);
            s.path.left = s.path.right = 750;
        }
        for (int z = 0; z < 25; ++z)
            for (int j = 0; j <= 10; ++j) {
                auto& p = route[size_t(60 + z * 25 + j)].path;
                p.event_type = 1;
                p.event_start = 0;
                p.event_end = 10;
                p.event_parameters = {4, 6, 0};
                p.marking_flags = j == 10 ? 0 : 0x20;
                p.zone_end = j == 10;
            }
        rr::TrafficSystem big(library, synthetic_exe());
        big.configure(6);
        int32_t cam = 0x3100;
        uint32_t tick = 0;
        size_t most = 0;
        while (((cam + 0x5100) >> 8) < 790) {
            cam += 0x40;
            big.step(route, cam + 0x100, cam + 0x5100, 0x40, cam, 0, random, tick++);
            size_t bc = 0, bs = 0;
            for (const auto& c : big.controllers()) {
                if (!c.active) continue;
                ++bc;
                if (c.position_q8 < cam + 0x100 - 0x3000 - 0x1e * 0x40)
                    throw std::runtime_error("controller kept behind the ring");
            }
            for (const auto& g : big.signals()) bs += g.active;
            if (bc > 10 || bs > 40) throw std::runtime_error("intersection pool caps");
            most = std::max(most, bc);
        }
        if (most < 3) throw std::runtime_error("intersections not placed along the route");
    }
}
static void test_signal_phase() {
    // 0x004415b0: only on crosswalk edge slices (flags & 0x18); index 0 for
    // lanes >= 0, index 2 for negative lanes; within 0x1e0, behind wins ties.
    rr::VehicleLibrary library(synthetic_container(0));
    rr::TrafficSystem system(library, synthetic_exe());
    system.configure(6);
    if (system.pedestrian_signal(16, 99 * 256, 0) != -1) throw std::runtime_error("phase without signals");
    auto route = intersection_route();
    uint32_t seed = 1;
    auto random = [&]() -> uint32_t {
        seed = seed * 214013u + 2531011u;
        return (seed >> 16) & 0x7fffu;
    };
    system.step(route, 0x3200, 0x8200, 0, 0x3100, 0, random, 100);
    const auto& c = system.controllers()[0];
    const int index0 = system.signals()[size_t(c.signals[0])].phase,
              index2 = system.signals()[size_t(c.signals[2])].phase;
    if (system.pedestrian_signal(16, 99 * 256, 0) != index0)
        throw std::runtime_error("index-0 signal for lane >= 0");
    if (system.pedestrian_signal(8, 90 * 256, -1) != index2)
        throw std::runtime_error("index-2 signal for lane < 0");
    if (system.pedestrian_signal(0x20, 99 * 256, 0) != -1)
        throw std::runtime_error("signal outside the crosswalk edges");
    if (system.pedestrian_signal(16, 90 * 256, 0) != -1)
        throw std::runtime_error("index-0 signal beyond 0x1e0");
}

static void test_intersections_all_courses() {
    // Plan 24 Task 5 intersections counterpart: every course with
    // type-1..3 zone ends places controllers through a full run, and the
    // pools never exceed the original sizes (10 controllers, 40 signals).
    const std::string root = RR_SOURCE_DIR "/../cdrom/ROADRASH";
    if (!std::filesystem::exists(root + "/ROADRASH.EXE")) {
        std::cout << "No disc for course sweep\n";
        return;
    }
    rr::Assets assets(root);
    const auto exe = assets.read("ROADRASH.EXE");
    for (const char* name : {"CITY", "HIWAY", "MEDLY", "NAPA", "CANYN"}) {
        const std::string course_name(name);
        const rr::Course course = rr::read_course(assets.read("DATA/COURSES/" + course_name + ".CRS"));
        const auto route = rr::make_route(course, false);
        rr::VehicleLibrary library(assets.read("DATA/CARS/" + course_name + ".CAR"));
        rr::TrafficSystem system(library, exe);
        system.configure(6);
        uint32_t seed = 1;
        auto random = [&]() -> uint32_t {
            seed = seed * 214013u + 2531011u;
            return (seed >> 16) & 0x7fffu;
        };
        bool ever_placed = false;
        uint32_t tick = 0;
        for (int32_t camera = 0x3100; ((camera + 0x5100) >> 8) + 1 < int32_t(route.size()); camera += 0x60) {
            system.step(route, camera + 0x100, camera + 0x5100, 0x60, camera, 0, random, tick++);
            size_t bc = 0, bs = 0;
            for (const auto& c : system.controllers())
                if (c.active) {
                    ++bc;
                    ever_placed = true;
                }
            for (const auto& s : system.signals())
                if (s.active) ++bs;
            if (bc > rr::TrafficSystem::controller_pool || bs > rr::TrafficSystem::signal_pool)
                throw std::runtime_error(std::string("pool overflow on ") + name);
        }
        if (!ever_placed) throw std::runtime_error(std::string("no intersections on ") + name);
        std::cout << name << ": intersections occur within pool caps\n";
    }
}
int main() {
    try {
        test_configure();
        test_lane_count();
        test_library_choose();
        test_deferred_car_teardown();
        test_countdown_timers();
        test_countdown_initial_fill();
        test_countdown_disabled_spawn_expiry();
        test_car_schedule_motion();
        test_lane_validity_stop();
        test_lane_validity_linked_slots();
        test_spawn_and_move();
        test_zone_events();
        test_intersections();
        test_signal_phase();
        test_intersections_all_courses();
        test_corpus();
        test_zone_corpus();
        test_car_contact_synthetic();
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "traffic: " << e.what() << "\n";
        return 1;
    }
}
