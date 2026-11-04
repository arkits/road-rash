#include "crash.hpp"
#include "actor_descriptors.hpp"
#include "assets.hpp"
#include "race.hpp"
#include "course.hpp"
#include "view_state.hpp"
#include <fstream>
#include <sstream>
#include <map>
#include <iostream>
// Plan 16 Task 1: synthetic 0x004052c0 reason-table coverage through the
// shared Race::knock_off helper (no crash library: stand-in crash path).
// A straight synthetic route keeps walls() out of the way; stamina base 2500
// matches the default RiderStats.
static std::vector<rr::RouteSample> knock_straight(size_t n) {
    rr::RouteSample s{{0, 0, 500, 500, 512, 512}, 0, 0, 0};
    s.path.shoulder_left = s.path.shoulder_right = 200;
    return std::vector<rr::RouteSample>(n, s);
}
static void test_knock_off_reasons();
int main(int argc, char** argv) {
    try {
        rr::Assets assets(RR_SOURCE_DIR "/../cdrom/ROADRASH");
        if (!assets.exists("DATA/GLOBAL.RSC")) {
            std::cout << "Imported crash corpus absent\n";
            return 0;
        }
        const auto exe = assets.read("ROADRASH.EXE");
        const auto config = rr::read_crash_config(exe);
        const rr::ActorAnimationLibrary library(exe, assets.read("DATA/GLOBAL.RSC"));
        rr::CrashActor initialized;
        rr::init_crash_actor(initialized, config);
        if (initialized.motion.i32(0xc4) != 8) throw std::runtime_error("Imported rider behavior type");
        std::ifstream file(argc > 1 ? argv[1]
                                    : RR_SOURCE_DIR "/analysis/oracle/napa-crash-entry-fixtures.tsv");
        if (!file) {
            std::cout << "Original crash fixture absent\n";
            return 0;
        }
        std::map<std::string, int64_t> values;
        std::string line, key;
        int64_t value;
        while (std::getline(file, line)) {
            std::istringstream row(line);
            if (row >> key >> value) values[key] = value;
        }
        std::ifstream walking_file(RR_SOURCE_DIR "/analysis/oracle/napa-crash-walking.tsv");
        if (walking_file) {
            std::map<std::string, int64_t> walking;
            while (std::getline(walking_file, line)) {
                std::istringstream row(line);
                if (row >> key >> value) walking[key] = value;
            }
            if (walking.at("crash.walk.count") != 256)
                throw std::runtime_error("Incomplete original walking fixture");
            const int bike_offsets[] = {0x1c, 0x28, 0x2f0, 0x2f4},
                      rider_offsets[] = {0x1c, 0x28, 0x5c, 0xec, 0x128};
            for (int c = 0; c < 256; ++c) {
                const auto root = "crash.walk." + std::to_string(c) + ".";
                rr::Bike bike;
                rr::CrashActor rider;
                rider.animation.animation = 38;
                rider.animation.deadline = UINT32_MAX;
                auto key_for = [&](const char* label, const char* who, int offset) {
                    std::ostringstream hex;
                    hex << std::hex << offset;
                    return root + label + who + hex.str();
                };
                for (int offset : bike_offsets)
                    bike.i32(offset) = int32_t(walking.at(key_for("input.", "bike.", offset)));
                for (int offset : rider_offsets)
                    rider.motion.i32(offset) = int32_t(walking.at(key_for("input.", "rider.", offset)));
                rr::CrashRecoveryInput input;
                input.player = true;
                input.walk_controls = uint32_t(walking.at(root + "controls"));
                input.state_elapsed = int32_t(walking.at(root + "elapsed"));
                rr::recover_crash_actor(bike, rider, library.playback[0], library.fallback, 100, input);
                for (int a = 0; a < 2; ++a)
                    for (int i = 0; i < (a ? 5 : 4); ++i) {
                        const int offset = (a ? rider_offsets : bike_offsets)[i];
                        const auto key = key_for("output.", a ? "rider." : "bike.", offset);
                        const auto actual = (a ? rider.motion : bike).i32(offset);
                        if (actual != walking.at(key))
                            throw std::runtime_error(key + " walking native=" + std::to_string(actual) +
                                                     " original=" + std::to_string(walking.at(key)));
                    }
                if (input.state_elapsed != walking.at(root + "output.elapsed") ||
                    rider.animation.animation != walking.at(root + "animation") ||
                    rider.animation.queued != walking.at(root + "queued"))
                    throw std::runtime_error(root + "walking state mismatch");
            }
            std::cout << "256 original player walking decisions match\n";
        }
        const int remount_offsets[] = {0x1c,  0x20,  0x24,  0x28,  0x2c,  0x30,  0x34,  0x38,  0x3c,
                                       0x40,  0x58,  0x5c,  0x60,  0xc0,  0xec,  0xf0,  0xf4,  0x104,
                                       0x108, 0x10c, 0x110, 0x114, 0x118, 0x134, 0x138, 0x13c, 0x140,
                                       0x154, 0x174, 0x178, 0x17c, 0x244, 0x288, 0x2f4};
        for (int c = 0; c < values.at("crash.remount.count"); ++c) {
            const auto root = "crash.remount." + std::to_string(c) + ".";
            rr::Bike bike;
            rr::CrashActor rider;
            rr::ActorAnimationState bike_animation;
            auto key_for = [&](const char* label, const char* actor, int offset) {
                std::ostringstream hex;
                hex << std::hex << offset;
                return root + label + actor + hex.str();
            };
            for (int offset : remount_offsets) {
                bike.i32(offset) = int32_t(values.at(key_for("input.", "bike.", offset)));
                rider.motion.i32(offset) = int32_t(values.at(key_for("input.", "rider.", offset)));
            }
            for (auto state : {&bike_animation, &rider.animation}) {
                state->animation = 41;
                state->loops = uint8_t(values.at(root + "input.loops"));
                state->deadline = UINT32_MAX;
            }
            rr::remount_crash_actor(bike, bike_animation, rider, config, library.playback[0],
                                    library.fallback, uint32_t(values.at(root + "tick")));
            for (int actor = 0; actor < 2; ++actor) {
                for (int offset : remount_offsets) {
                    const auto key = key_for("output.", actor ? "rider." : "bike.", offset);
                    const auto actual = (actor ? rider.motion : bike).i32(offset);
                    if (actual != values.at(key))
                        throw std::runtime_error(key + " remount native=" + std::to_string(actual) +
                                                 " original=" + std::to_string(values.at(key)));
                }
                const auto& state = actor ? rider.animation : bike_animation;
                const auto key = root + (actor ? "rider." : "bike.");
                if (state.animation != values.at(key + "animation") ||
                    state.frame != values.at(key + "frame") || state.queued != values.at(key + "queued") ||
                    state.loops != values.at(key + "loops") || state.deadline != values.at(key + "deadline"))
                    throw std::runtime_error(key + "remount animation mismatch");
            }
        }
        std::cout << values.at("crash.remount.count") << " original local remount resets match\n";
        const int recovery_bike[] = {0x1c, 0x28, 0x2ec, 0x2f0, 0x2f4},
                  recovery_rider[] = {0x1c, 0x28, 0x5c, 0xec, 0x128};
        for (int c = 0; c < values.at("crash.recovery.count"); ++c) {
            const auto root = "crash.recovery." + std::to_string(c) + ".";
            rr::Bike bike;
            rr::CrashActor rider;
            auto key_for = [&](const char* label, const char* actor, int offset) {
                std::ostringstream hex;
                hex << std::hex << offset;
                return root + label + actor + hex.str();
            };
            for (int offset : recovery_bike)
                bike.i32(offset) = int32_t(values.at(key_for("input.", "bike.", offset)));
            for (int offset : recovery_rider)
                rider.motion.i32(offset) = int32_t(values.at(key_for("input.", "rider.", offset)));
            rider.animation.animation = int8_t(values.at(root + "input.animation"));
            rider.animation.loops = uint8_t(values.at(root + "input.loops"));
            rider.animation.deadline = UINT32_MAX;
            rr::CrashRecoveryInput input;
            input.state_elapsed = int32_t(values.at(root + "elapsed"));
            rr::recover_crash_actor(bike, rider, library.playback[0], library.fallback,
                                    uint32_t(values.at(root + "tick")), input);
            for (int actor = 0; actor < 2; ++actor) {
                const auto offsets = actor ? recovery_rider : recovery_bike;
                for (int i = 0; i < 5; ++i) {
                    const auto key = key_for("output.", actor ? "rider." : "bike.", offsets[i]);
                    const auto actual = (actor ? rider.motion : bike).i32(offsets[i]);
                    if (actual != values.at(key))
                        throw std::runtime_error(key + " native=" + std::to_string(actual) +
                                                 " original=" + std::to_string(values.at(key)));
                }
            }
            const auto& state = rider.animation;
            if (state.animation != values.at(root + "animation") ||
                state.frame != values.at(root + "frame") || state.queued != values.at(root + "queued") ||
                state.loops != values.at(root + "loops") || state.deadline != values.at(root + "deadline"))
                throw std::runtime_error(root + "recovery animation mismatch");
        }
        std::cout << values.at("crash.recovery.count") << " original offline recovery decisions match\n";
        const auto tables = rr::read_physics_tables(exe);
        const auto course = rr::read_course(assets.read("DATA/COURSES/NAPA.CRS"));
        const auto specs = rr::read_bike_specs(assets.read("DATA/BIKESPEC.RSC"));
        rr::Race race(rr::make_route(course, false), rr::make_route(course, true), tables, specs.at(0), 90);
        race.set_crash_library(library, config);
        auto& live_bike = const_cast<rr::Bike&>(race.bike());
        live_bike.i32(0xec) = 6000;
        live_bike.i32(0x2f4) = 1;
        race.step(0);
        if (!race.riders()[0].crash_active || !race.riders()[0].crash_actor.animation.selected ||
            !race.riders()[0].crash_bike_animation.selected)
            throw std::runtime_error("Live crash did not start independent actors");
        bool separated = false, pickup = false, walked = false, was_pickup = false;
        int elapsed = 0;
        while (race.crashed() && elapsed++ < 4000) {
            race.step(0);
            const auto& rider = race.riders()[0];
            separated |= rider.bike.i32(0x1c) != rider.crash_actor.motion.i32(0x1c);
            // 0x00407a90: outside crash states 1/2 the bike think clears its
            // frame pointer (+0x7c), so only the rider's pickup animation 41
            // (which carries the bike) is drawn.
            // The half node enters state 3 after the bike node ran, so the
            // clear lands on the next bike think.
            const bool in_pickup = rider.crash_active && rider.bike.i32(0x2f4) == 3;
            if (in_pickup && was_pickup) {
                pickup = true;
                if (rider.crash_bike_animation.selected)
                    throw std::runtime_error("Riderless bike still drawn during pickup");
            }
            was_pickup = in_pickup;
            // The interpolated pose must carry the walking body's heading
            // (+0x5c), or the walk back draws facing away from the bike.
            if (rider.crash_active && rider.crash_actor.animation.animation == 38 &&
                rider.crash_actor.motion.i32(0x5c)) {
                rr::ViewState view;
                rr::capture_view(view, race, nullptr);
                for (const auto& pose : view.actors)
                    if (pose.id == 1000) {
                        walked = true;
                        if (pose.heading != rider.crash_actor.motion.i32(0x5c) ||
                            pose.roll != rider.crash_actor.motion.i32(0x60))
                            throw std::runtime_error("Crash body pose lost its heading/roll");
                    }
            }
        }
        if (!pickup || !walked) throw std::runtime_error("Live crash did not reach both walk and pickup");
        if (!separated || race.crashed() || race.riders()[0].crash_active)
            throw std::runtime_error(
                "Live crash did not separate and remount: elapsed=" + std::to_string(elapsed) + " state=" +
                std::to_string(race.bike().i32(0x2f4)) + " speed=" + std::to_string(race.bike().i32(0xec)));
        if (race.bike().i32(0x154) != 8 || race.bike().i32(0x174) != config.normal_dimensions[0])
            throw std::runtime_error("Live remount did not restore bike dimensions");
        std::cout << "Live Napa crash separated and remounted after " << elapsed << " ticks\n";
        // 0x00407a90 state-3 flash (407acd-407b0b): bike +0x81 mirrors the
        // half's +0x1e9 while the bike still sits on the marked sample and
        // the unsigned tick gap stays below 12, advancing as (gap>>2)+1.
        // A crashed player still ticks nitro (0x0040b1a0 before the crash
        // branch); expiry and sample mismatch clear the mark.
        {
            auto& me = race.riders()[0];
            me.crash_active = true;
            me.bike.i32(0x2f4) = 3;
            me.bike.i32(0x1c) = int32_t(me.sample) * 256;
            me.half_object.u8(0x1e9) = 1;
            me.half_object.i32(0x1ec) = int32_t(me.sample);
            me.half_object.i32(0x1f0) = race.ticks();
            me.bike.u8(0x81) = 0;
            me.nitro_charge = 0;
            me.crash_actor.motion.i32(0x1c) = me.bike.i32(0x1c) + 0x10000;
            me.crash_actor.motion.i32(0x28) = me.bike.i32(0x28) + 0x10000;
            race.step(0);
            if (me.bike.u8(0x81) != 1 || me.half_object.u8(0x1e9) != 1)
                throw std::runtime_error("Pickup flash did not mirror the mark (gap 1 -> 1)");
            if (me.nitro_charge <= 0) throw std::runtime_error("Crashed player did not tick nitro");
            me.crash_active = true;
            me.bike.i32(0x2f4) = 3;
            me.bike.i32(0x1c) = int32_t(me.sample) * 256;
            me.half_object.u8(0x1e9) = 5;
            me.half_object.i32(0x1ec) = int32_t(me.sample);
            me.half_object.i32(0x1f0) = race.ticks() - 20;
            me.crash_actor.motion.i32(0x1c) = me.bike.i32(0x1c) + 0x10000;
            race.step(0);
            if (me.bike.u8(0x81) != 0 || me.half_object.u8(0x1e9) != 0)
                throw std::runtime_error("Stale flash mark did not expire");
            me.crash_active = true;
            me.bike.i32(0x2f4) = 3;
            me.bike.i32(0x1c) = int32_t(me.sample) * 256;
            me.half_object.u8(0x1e9) = 2;
            me.half_object.i32(0x1ec) = int32_t(me.sample) + 1;
            me.half_object.i32(0x1f0) = race.ticks();
            me.crash_actor.motion.i32(0x1c) = me.bike.i32(0x1c) + 0x10000;
            race.step(0);
            if (me.bike.u8(0x81) != 0 || me.half_object.u8(0x1e9) != 0)
                throw std::runtime_error("Off-sample flash mark did not clear");
            std::cout << "Pickup flash mirrors, expires and clears on sample mismatch; crashed player ticks "
                         "nitro\n";
        }
        const int motion_offsets[] = {0x18,  0x1c,  0x20,  0x24,  0x28,  0x2c,  0x30,  0x34,  0x38,
                                      0x3c,  0x40,  0xec,  0xf0,  0xf4,  0x108, 0x10c, 0x128, 0x12c,
                                      0x130, 0x134, 0x138, 0x13c, 0x140, 0x184, 0x188, 0x18c, 0x19c};
        for (int c = 0; c < values.at("actor.motion.count"); ++c) {
            const auto root = "actor.motion." + std::to_string(c) + ".";
            rr::Bike actor;
            for (int offset : motion_offsets) {
                std::ostringstream hex;
                hex << std::hex << offset;
                actor.i32(offset) = int32_t(values.at(root + "input." + hex.str()));
            }
            actor.accel = int32_t(values.at(root + "accel"));
            rr::advance_actor_motion(actor, tables, [&](int32_t delta) {
                if (!delta) {
                    actor.i32(0x130) = actor.i32(0x12c) = 0;
                    return;
                }
                const int32_t old = actor.i32(0x1c);
                actor.i32(0x134) = old;
                actor.i32(0x1c) += delta;
                actor.i32(0x138) = actor.i32(0x1c);
                if ((uint32_t(old) ^ uint32_t(actor.i32(0x1c))) & 0xffffff00u)
                    actor.i32(0x13c) = actor.i32(0x140);
                actor.i32(0x130) = actor.i32(0x12c) = 0;
            });
            for (int offset : motion_offsets) {
                std::ostringstream hex;
                hex << std::hex << offset;
                const auto key = root + "output." + hex.str();
                if (actor.i32(offset) != values.at(key))
                    throw std::runtime_error(key +
                                             " motion mismatch native=" + std::to_string(actor.i32(offset)) +
                                             " original=" + std::to_string(values.at(key)));
            }
        }
        std::cout << values.at("actor.motion.count") << " original local actor motion cases match\n";
        const int offsets[] = {0x1c,  0x20,  0x24,  0x28,  0x2c,  0x30,  0x34,  0x38,  0x3c,  0x40,
                               0x58,  0x5c,  0x60,  0xbc,  0xc0,  0xec,  0xf0,  0xf4,  0x104, 0x108,
                               0x10c, 0x110, 0x114, 0x118, 0x11c, 0x120, 0x124, 0x128, 0x12c, 0x130,
                               0x134, 0x138, 0x13c, 0x140, 0x154, 0x158, 0x160, 0x164, 0x16c, 0x174,
                               0x178, 0x17c, 0x198, 0x19c, 0x1a0, 0x244, 0x288, 0x2f4, 0x2f8};
        for (int c = 0; c < values.at("crash.entry.count"); ++c) {
            const auto root = "crash.entry." + std::to_string(c) + ".";
            auto get = [&](const std::string& key) { return values.at(root + key); };
            rr::Bike bike;
            rr::CrashActor rider;
            rider.animation.animation = 2;
            rider.animation.deadline = UINT32_MAX;
            for (int offset : offsets) {
                std::ostringstream hex;
                hex << std::hex << offset;
                bike.i32(offset) = int32_t(get("input.bike." + hex.str()));
                rider.motion.i32(offset) = int32_t(get("input.rider." + hex.str()));
            }
            std::optional<std::array<int32_t, 3>> rider_impulse, bike_impulse;
            if (get("rider_impulse")) {
                rider_impulse.emplace();
                for (int i = 0; i < 3; ++i)
                    rider_impulse->at(i) = int32_t(get("rider_impulse." + std::to_string(i)));
            }
            if (get("bike_impulse")) {
                bike_impulse.emplace();
                for (int i = 0; i < 3; ++i)
                    bike_impulse->at(i) = int32_t(get("bike_impulse." + std::to_string(i)));
            }
            const bool accepted =
                rr::begin_crash(bike, rider, config, library.playback[0], library.fallback,
                                uint32_t(get("tick")), int32_t(get("reason")), rider_impulse, bike_impulse);
            if (accepted != (get("input.bike.2f4") == 0))
                throw std::runtime_error(root + "entry guard mismatch");
            for (int offset : offsets) {
                std::ostringstream hex;
                hex << std::hex << offset;
                for (int actor = 0; actor < 2; ++actor) {
                    const std::string key = "output." + std::string(actor ? "rider." : "bike.") + hex.str();
                    const int32_t actual = (actor ? rider.motion : bike).i32(offset);
                    if (actual != get(key))
                        throw std::runtime_error(root + key + " native=" + std::to_string(actual) +
                                                 " original=" + std::to_string(get(key)));
                }
            }
            const auto& state = rider.animation;
            if (state.animation != get("animation") || state.frame != get("frame") ||
                state.queued != get("queued") || state.loops != get("loops") ||
                state.deadline != get("deadline"))
                throw std::runtime_error(root + "entry animation mismatch");
        }
        std::cout << values.at("crash.entry.count")
                  << " original local crash entries match across 49 motion fields per actor\n";
        // 0x004049c0 local_18 at 0x404eeb (class-1 bike/bike and class-2
        // bike/car big-speed-gap crash): begin_crash must add the rider
        // impulse to the half's +0xf4 (vertical velocity) and the bike impulse
        // to the bike's +0xf4, never to +0xf0 (steer rate). This pins the
        // component order the contact handler depends on.
        {
            rr::Bike bike;
            rr::CrashActor rider;
            rider.animation.animation = 2;
            rider.animation.deadline = UINT32_MAX;
            bike.i32(0xec) = 1000;
            bike.i32(0xf0) = 7;
            bike.i32(0xf4) = 11;
            if (!rr::begin_crash(bike, rider, config, library.playback[0], library.fallback, 0, 1,
                                 rr::kContactCrashRiderImpulse, rr::kContactCrashBikeImpulse))
                throw std::runtime_error("contact impulse crash did not start");
            if (rider.motion.i32(0xec) != 1000 || bike.i32(0xec) != 1000)
                throw std::runtime_error("contact impulse moved speed");
            if (rider.motion.i32(0xf0) != 7 || bike.i32(0xf0) != 7)
                throw std::runtime_error("contact impulse leaked onto the steer rate");
            if (rider.motion.i32(0xf4) != 11 + 0x200)
                throw std::runtime_error("rider impulse did not land on +0xf4");
            if (bike.i32(0xf4) != 11 + 0x100) throw std::runtime_error("bike impulse did not land on +0xf4");
        }
        std::cout << "contact crash impulse pair lands on the vertical velocity (+0xf4)\n";
        // 0x00404560 tail calls 0x004067d0 on the remount: the half rejoins
        // the bike on the remount tick itself, not on the next bike move
        // (traffic-field 2279: original shadow 75067, native kept the stale
        // crash-body 75033). Only the half node runs this tick, so a missing
        // copy-back stays visible in the mirror.
        {
            std::vector<rr::Opponent> opponents;
            opponents.push_back({specs.at(0), 90, 87, 70, 0, 1, 1, 0x3200 + 20 * 256, 0x8000});
            rr::Race live(rr::make_route(course, false), rr::make_route(course, true), tables, specs.at(0),
                          90, opponents);
            live.set_crash_library(library, config);
            live.set_live_schedule(true);
            auto& ai = live.riders()[1];
            const int32_t bike_pos = 200 * 256, body_pos = 190 * 256;
            ai.sample = ai.previous_sample = 200;
            ai.bike.i32(0x1c) = bike_pos;
            ai.bike.i32(0x28) = 0x8000;
            ai.bike.i32(0x2f4) = 3;
            ai.crash_active = true;
            ai.crash_timer = 0;
            ai.crash_actor.motion.i32(0x1c) = body_pos;
            ai.crash_actor.motion.i32(0x28) = 0x8000;
            ai.crash_actor.sample = ai.crash_actor.previous_sample = 190;
            ai.crash_actor.animation.animation = 2; // past 41: recover remounts at once
            ai.sched_next = live.ticks() + 100;     // bike node not due: no 0x004060b0 copy
            ai.half_sched_next = live.ticks();      // half node due: think_half remounts
            live.step(0);
            if (ai.bike.i32(0x2f4) != 0 || ai.crash_active)
                throw std::runtime_error("Live remount did not clear on the half node");
            if (ai.half_mirror.position != bike_pos || ai.half_mirror.lateral != 0x8000)
                throw std::runtime_error("Live remount did not rejoin the half: mirror " +
                                         std::to_string(ai.half_mirror.position) + "/" +
                                         std::to_string(ai.half_mirror.lateral));
            std::cout << "Live remount rejoins the half on the remount tick\n";
        }
        test_knock_off_reasons();
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
static void test_knock_off_reasons() {
    auto fresh = []() {
        rr::Race race(knock_straight(200), knock_straight(200), rr::PhysicsTables::synthetic(),
                      rr::BikeSpec{}, 90);
        auto& r = race.riders()[0];
        r.stamina = {7, 2500, 2500, 2500};
        r.bike.i32(0x2f0) = 1000;
        r.bike.i32(0x188) = 0;
        r.bike.i32(0x2d8) = 0;
        r.bike.i32(0x288) = 0;
        r.bike.i32(0x29c) = 0x2e00;
        return std::move(race);
    };
    // Reason 0 zeroes both: no damage, no crash, returns false.
    {
        auto race = fresh();
        auto& r = race.riders()[0];
        if (race.knock_off(r, 10, 10, 0)) throw std::runtime_error("reason 0 crashed");
        if (r.stamina.max != 2500 || r.stamina.current != 2500 || r.bike.i32(0x2f0) != 1000)
            throw std::runtime_error("reason 0 damaged");
    }
    // Reason 1 keeps both: max -4*p floor base/2, current -64*p floor 0,
    // +0x2f0 -= p floor 0.
    {
        auto race = fresh();
        auto& r = race.riders()[0];
        if (race.knock_off(r, 10, 30, 1)) throw std::runtime_error("reason 1 crashed");
        if (r.stamina.max != 2500 - 40 || r.stamina.current != 2500 - 640 || r.bike.i32(0x2f0) != 970)
            throw std::runtime_error("reason 1 damage mismatch");
    }
    // Reason 2 scales by 7/4 trunc-toward-zero: p=8 -> 14.
    {
        auto race = fresh();
        auto& r = race.riders()[0];
        if (race.knock_off(r, 8, 8, 2)) throw std::runtime_error("reason 2 crashed");
        if (r.stamina.max != 2500 - 56 || r.stamina.current != 2500 - 896 || r.bike.i32(0x2f0) != 986)
            throw std::runtime_error("reason 2 scale mismatch");
    }
    // Reason 3 divides by 8 trunc-toward-zero: p=16 -> 2.
    {
        auto race = fresh();
        auto& r = race.riders()[0];
        if (race.knock_off(r, 16, 16, 3)) throw std::runtime_error("reason 3 crashed");
        if (r.stamina.max != 2500 - 8 || r.stamina.current != 2500 - 128 || r.bike.i32(0x2f0) != 998)
            throw std::runtime_error("reason 3 scale mismatch");
    }
    // Silent-hit 6 zeroes both like the default case.
    {
        auto race = fresh();
        auto& r = race.riders()[0];
        if (race.knock_off(r, 10, 10, 6)) throw std::runtime_error("reason 6 crashed");
        if (r.stamina.max != 2500 || r.stamina.current != 2500 || r.bike.i32(0x2f0) != 1000)
            throw std::runtime_error("reason 6 damaged");
    }
    // Depleted stamina enters the stand-in crash with the supplied reason.
    {
        auto race = fresh();
        auto& r = race.riders()[0];
        r.stamina.current = 10;
        if (!race.knock_off(r, 10, 0, 2)) throw std::runtime_error("depleted stamina did not crash");
        if (r.bike.i32(0x2f4) != 1 || r.bike.i32(0x2f8) != 2)
            throw std::runtime_error("crash reason not recorded");
    }
    // Network bypass: remote victims (flag -1) and unowned peers (flag 1,
    // owner_local false) take no damage even with lethal inputs.
    {
        auto race = fresh();
        auto& r = race.riders()[0];
        if (race.knock_off(r, 1000, 1000, 1, false, -1)) throw std::runtime_error("remote bypass crashed");
        if (r.stamina.current != 2500 || r.bike.i32(0x2f0) != 1000)
            throw std::runtime_error("remote bypass damaged");
    }
    {
        auto race = fresh();
        auto& r = race.riders()[0];
        if (race.knock_off(r, 1000, 1000, 1, false, 1)) throw std::runtime_error("peer bypass crashed");
        if (r.stamina.current != 2500 || r.bike.i32(0x2f0) != 1000)
            throw std::runtime_error("peer bypass damaged");
    }
    // Reason 4 without a crash records riding animation 9.
    {
        auto race = fresh();
        auto& r = race.riders()[0];
        if (race.knock_off(r, 1, 0, 4)) throw std::runtime_error("reason 4 crashed");
        if (r.contact_animation != 9) throw std::runtime_error("reason 4 animation not recorded");
    }
    // Reason 5: bike+0x188 *= scaled stamina damage; nonzero + riding steers
    // +0x288 += trunc(+0x2d8 * +0x188 / 256) clamped to +-+0x29c.
    {
        auto race = fresh();
        auto& r = race.riders()[0];
        r.bike.i32(0x188) = 3;
        r.bike.i32(0x2d8) = 256;
        if (race.knock_off(r, 5, 0, 5)) throw std::runtime_error("reason 5 crashed");
        if (r.bike.i32(0x188) != 15) throw std::runtime_error("reason 5 impulse mismatch");
        if (r.bike.i32(0x288) != 15) throw std::runtime_error("reason 5 steer mismatch");
    }
    // Reason-5 clamp: +0x29c bounds the steering gain.
    {
        auto race = fresh();
        auto& r = race.riders()[0];
        r.bike.i32(0x188) = 100;
        r.bike.i32(0x2d8) = 1 << 20;
        r.bike.i32(0x29c) = 100;
        if (race.knock_off(r, 1, 0, 5)) throw std::runtime_error("reason 5 clamp crashed");
        if (r.bike.i32(0x288) != 100) throw std::runtime_error("reason 5 steer not clamped");
    }
    // Negative inputs use the assembly sign-bias, not C++ truncation:
    // reason 2 p=-5 -> (-35+3)>>2 = -8; reason 3 p=-9 -> (-9+7)>>3 = -1.
    {
        auto race = fresh();
        auto& r = race.riders()[0];
        if (race.knock_off(r, -5, -5, 2)) throw std::runtime_error("reason 2 negative crashed");
        if (r.stamina.max != 2500 + 32 || r.stamina.current != 2500 + 512 || r.bike.i32(0x2f0) != 1008)
            throw std::runtime_error("reason 2 negative scale mismatch");
    }
    {
        auto race = fresh();
        auto& r = race.riders()[0];
        if (race.knock_off(r, -9, -9, 3)) throw std::runtime_error("reason 3 negative crashed");
        if (r.stamina.max != 2500 + 4 || r.stamina.current != 2500 + 64 || r.bike.i32(0x2f0) != 1001)
            throw std::runtime_error("reason 3 negative scale mismatch");
    }
    // Crash-impulse argument order (0x00403d60 param_2 rider, param_3 bike):
    // reason 4 enters {-80,0,0}/{0,0,0}, other reasons {-256,0,1792}/{0,0,1024}.
    {
        auto race = fresh();
        auto& r = race.riders()[0];
        r.stamina.current = 1;
        if (!race.knock_off(r, 10, 0, 4)) throw std::runtime_error("reason 4 impulse crashed");
        if (r.bike.i32(0x2f4) != 1 || r.bike.i32(0x2f8) != 4)
            throw std::runtime_error("reason 4 crash not recorded");
    }
    {
        auto race = fresh();
        auto& r = race.riders()[0];
        r.stamina.current = 1;
        if (!race.knock_off(r, 10, 0, 1)) throw std::runtime_error("reason 1 impulse crashed");
        if (r.bike.i32(0x2f4) != 1 || r.bike.i32(0x2f8) != 1)
            throw std::runtime_error("reason 1 crash not recorded");
    }
}
