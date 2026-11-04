#include "race.hpp"
#include <iostream>

// Live rider scheduler rules (plan 10 Task 6). The expected numbers are the
// ones measured on napa-field-l1-20261017 (see docs/REVERSE_ENGINEERING.md,
// "Live rider scheduler"); the race checks use a synthetic road.
static std::vector<rr::RouteSample> straight(size_t n) {
    rr::RouteSample s{{0, 0, 500, 500, 512, 512}, 0, 0, 0};
    std::vector<rr::RouteSample> road(n, s);
    for (size_t i = 0; i < n; ++i) road[i].local = uint32_t(i);
    return road;
}

int main() {
    int failures = 0;
    auto check = [&](bool v, const char* what) {
        if (!v) {
            ++failures;
            std::cerr << "FAIL " << what << '\n';
        }
    };

    // 0x00437e60 interval at tick 1 (ring camera 12800, speed 0): the
    // captured grid gives 8, 7, 6 and 5 by distance; the player stays at 1.
    check(rr::live_interval(true, 25088, 12800, 0, 1, false) == 8, "far grid bike capped at 8");
    check(rr::live_interval(true, 14592, 12800, 0, 1, false) == 7, "7 samples ahead: 3 + 4 slow bonus");
    check(rr::live_interval(true, 14336, 12800, 0, 1, false) == 6, "6 samples ahead: 2 + 4");
    check(rr::live_interval(true, 13568, 12800, 0, 1, false) == 5, "3 samples ahead: 1 + 4");
    check(rr::live_interval(true, 12800, 12800, 0, 1, true) == 1, "player object: no slow bonus");
    check(rr::live_interval(false, 12800, 12800, 0, 1, false) == 30, "no slice: 30");
    check(rr::live_interval(true, 13568, 12800, 0, 300, false) == 1, "no slow bonus from tick 300");
    check(rr::live_interval(true, 13568, 12800, 0x1000, 1, false) == 2, "speed 0x1000..0x17ff: +1");
    check(rr::live_interval(true, 13568, 12800, 0x800, 1, false) == 3, "speed 0x800..0xfff: +2");
    check(rr::live_interval(true, 12800 - 10 * 256, 12800, 0x2000, 1, false) == 3,
          "10 behind: (10 - 2) / 4 + 1");
    check(rr::live_interval(true, 12800 + 64 * 256, 12800, 0x2000, 500, false) == 8, "far ahead capped at 8");

    // 0x00406940: the three front riders park on tick 1 with the build
    // words 600 + dt 5 ticks and 21000 distance: pace 8440 ahead of the
    // player (95%), 9329 behind (105%).
    check(rr::parked_pace(21000, 605, true) == 8440, "pace ahead of the player");
    check(rr::parked_pace(21000, 605, false) == 9329, "pace behind the player");

    // 0x00407610 on tick 1: parked places 1..3 ahead of the player in place
    // 12 lose 15/30/60 (nearest first), so their tick-9 advance (dt 8) is
    // 263/262/261, as captured.
    {
        std::array<std::optional<int32_t>, 16> by_place{};
        by_place[0] = by_place[1] = by_place[2] = 8440;
        rr::pace_band(by_place, 12);
        check(*by_place[2] == 8380 && *by_place[1] == 8410 && *by_place[0] == 8425,
              "nearest ahead lose 60/30/15");
        check((8 * *by_place[0] >> 8) == 263 && (8 * *by_place[1] >> 8) == 262 &&
                  (8 * *by_place[2] >> 8) == 261,
              "tick-9 parked advances");
        // Behind the player: +60/+30/+15, capped at 30000; the floor is 3000.
        std::array<std::optional<int32_t>, 16> back{};
        back[5] = 29990;
        back[7] = 1000;
        back[9] = 3010;
        rr::pace_band(back, 5);
        check(*back[5] == 30000 && *back[7] == 1030 && *back[9] == 3025, "behind gain 60/30/15 with the cap");
        std::array<std::optional<int32_t>, 16> front{};
        front[0] = 3010;
        rr::pace_band(front, 3);
        check(*front[0] == 3000, "ahead floor 3000");
    }

    // A live-scheduled race: tick 1 runs every node (next = build tick 0),
    // bikes and halves keep their own cadences, opponents far from the
    // camera wait their LOD interval, and the walk visits a half before its
    // bike and a later rider before an earlier one at equal positions.
    {
        const auto t = rr::PhysicsTables::synthetic();
        rr::BikeSpec spec{};
        spec.d[6] = 196;
        std::vector<rr::Opponent> opponents;
        opponents.push_back({spec, 90, 87, 70, 0, 1, 1, 0x3200 + 20 * 256, 0});
        opponents.push_back({spec, 90, 87, 70, 0, 2, 2, 0x3200 + 20 * 256, 0x8000});
        rr::Race race(straight(4000), straight(4000), t, spec, 90, opponents);
        race.set_live_schedule(true);
        const auto order = race.schedule_order();
        check(order.size() == 6, "two nodes per rider");
        check(order[0].rider == 2 && order[0].type == 8 && order[1].rider == 2 && order[1].type == 4 &&
                  order[2].rider == 1 && order[2].type == 8,
              "tie order: later rider first, half before bike");
        const auto& p = race.riders()[0];
        const auto& o = race.riders()[1];
        check(p.sched_interval == 1 && p.sched_last == -1 && o.sched_interval == 4 && o.sched_last == -4,
              "build words: player interval 1, opponents 4, last = -interval");
        race.countdown_step();
        check(p.bike.i32(0x18) == 2 && o.bike.i32(0x18) == 5 && o.half_dt == 5,
              "tick 1 dt: 2 for the player, 5 for opponents");
        check(o.sched_interval == 8 && o.sched_next == 9,
              "20 samples ahead of the camera at rest: interval 8");
        check(p.sched_next == 2 && p.half_sched_next == 2, "the player runs every tick");
        for (int i = 0; i < 7; ++i) race.countdown_step();
        check(o.sched_last == 1 && o.half_sched_last == 1, "opponents wait for their next tick");
        race.countdown_step();
        check(o.sched_last == 9 && o.bike.i32(0x18) == 8 && o.half_dt == 8, "tick 9: dt 8 on bike and half");
        check(o.live && !o.parked, "no ring: no window parking");
    }

    // 0x00438170 tail overrides +0xc for both player nodes, without
    // changing their stored LOD interval. A missing slice gives interval
    // 30 and makes the override observable even on a synthetic road.
    for (bool demo : {false, true}) {
        rr::Race race(straight(4000), straight(4000), rr::PhysicsTables::synthetic(), rr::BikeSpec{}, 90, {});
        race.set_live_schedule(true);
        race.set_demo_hold(demo);
        auto& player = race.riders()[0];
        player.bike_slice_present = player.half_slice_present = false;
        race.countdown_step();
        check(player.sched_interval == 30 && player.half_sched_interval == 30,
              "player stores the no-slice LOD interval");
        check(player.sched_next == (demo ? 31 : 2) && player.half_sched_next == (demo ? 31 : 2),
              "only human play forces both player nodes due next tick");
        race.countdown_step();
        check(player.sched_last == (demo ? 1 : 2) && player.half_sched_last == (demo ? 1 : 2),
              "human player runs again while demo retains its cadence");
    }

    if (failures) return 1;
    std::cout << "race schedule ok\n";
    return 0;
}
