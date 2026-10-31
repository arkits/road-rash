#include "session.hpp"
#include "frame_profile.hpp"
#include "race.hpp"
#include <cmath>
#include <iostream>

int main() {
    int failures = 0;
    auto check = [&](bool ok, const char* message) {
        if (!ok) {
            ++failures;
            std::cerr << "FAIL " << message << '\n';
        }
    };
    rr::SessionState state;
    auto event = [&](Uint32 type) {
        SDL_Event e{};
        e.type = type;
        state.handle(e);
    };
    check(!state.paused(), "race starts active");
    event(SDL_EVENT_WILL_ENTER_BACKGROUND);
    check(state.paused() && state.background(), "background pauses immediately");
    state.resume();
    check(state.paused(), "cannot resume while backgrounded");
    event(SDL_EVENT_DID_ENTER_BACKGROUND);
    event(SDL_EVENT_WILL_ENTER_FOREGROUND);
    event(SDL_EVENT_DID_ENTER_FOREGROUND);
    check(state.paused() && !state.background(), "foreground requires explicit resume");
    check(state.take_clock_reset() && !state.take_clock_reset(), "clock reset consumed once");
    state.resume();
    check(!state.paused() && state.take_clock_reset(), "explicit resume resets clock");
    event(SDL_EVENT_WINDOW_FOCUS_LOST);
    state.resume();
    check(state.paused(), "cannot resume without focus");
    event(SDL_EVENT_WINDOW_FOCUS_GAINED);
    check(state.paused(), "focus gain does not resume");
    state.resume();
    check(!state.paused(), "focused window resumes");

    // Exact 10-second sequences at common presentation rates.
    for (uint64_t hz : {30, 60, 120, 144}) {
        rr::TickClock clock;
        int steps = 0;
        uint64_t previous = 0;
        for (uint64_t frame = 1; frame <= hz * 10; ++frame) {
            const uint64_t now = frame * 1000000000ull / hz;
            steps += clock.advance(now - previous, false);
            previous = now;
        }
        check(steps == 10 * rr::ticks_per_second, "fixed rate independent of presentation rate");
    }
    rr::TickClock clock;
    check(clock.advance(10000000, false) == 0, "partial tick retained");
    check(clock.advance(60000000000ull, true) == 0, "background time never advances simulation");
    check(clock.advance(10000000, false) == 0, "resume discards pre-pause partial tick");
    check(clock.advance(10000000, false) == 1, "normal ticks after resume");
    check(clock.advance(UINT64_MAX, false) == rr::max_steps_per_frame, "stall clamped before multiplication");
    {
        // Plan 21 Task 2: alpha_q16 reports the sub-tick remainder in Q16.
        rr::TickClock c2;
        constexpr uint64_t half = 1000000000ull / 60 / 2 + 1; // ceil(half a 60 Hz tick)
        check(c2.advance(half, false) == 0, "half tick takes no step");
        check(c2.alpha_q16() == 32767 || c2.alpha_q16() == 32768, "half-tick remainder reads half");
        check(c2.advance(half, false) == 1, "two halves complete the tick");
        check(c2.alpha_q16() == 0, "exact tick boundary reads zero");
    }
    check(clock.advance(0, false) == 0, "stall leaves no catch-up debt");

    // Plan 33b Task 4 Step 4: ten minutes at 60/144/165 Hz and NTSC 59.94 Hz,
    // vsync (exact period) and vsync off (deterministic +-3 ms jitter around a
    // 4 ms frame): the clock stays within one tick of wall time throughout,
    // and the interpolated time (ticks + alpha) never steps backwards.
    struct Rate {
        uint64_t num, den; // period = den / num seconds
        bool jitter;
        const char* what;
    };
    for (const Rate rate : {Rate{60, 1, false, "60 Hz vsync"}, Rate{144, 1, false, "144 Hz vsync"},
                            Rate{165, 1, false, "165 Hz vsync"}, Rate{60000, 1001, false, "59.94 Hz vsync"},
                            Rate{250, 1, true, "vsync off, jittered"}}) {
        rr::TickClock c;
        rr::FrameBench bench;
        bench.warmup = 0;
        bench.frames = long(rate.num * 600 / rate.den + 1);
        uint64_t ticks = 0, previous = 0, now = 0, lcg = 12345;
        double worst = 0, last_time = 0;
        bool monotonic = true;
        for (long frame = 1; frame < bench.frames; ++frame) {
            uint64_t next = uint64_t(frame) * 1000000000ull * rate.den / rate.num;
            if (rate.jitter) {
                lcg = lcg * 6364136223846793005ull + 1442695040888963407ull;
                next += (lcg >> 33) % 6000000; // later by 0..6 ms, never before the nominal start
                next = std::max(next, now + 1);
            }
            now = next;
            const int steps = c.advance(now - previous, false);
            previous = now;
            ticks += uint64_t(steps);
            bench.finish_frame(0, steps, now, c.alpha_q16());
            const double wall_ticks = double(now) * 60.0 / 1e9;
            worst = std::max(worst, std::fabs(double(ticks) - wall_ticks));
            const double time = double(ticks) + double(c.alpha_q16()) / 65536.0;
            monotonic = monotonic && time + 1e-9 >= last_time;
            last_time = time;
        }
        check(worst < 1.0, rate.what);
        check(monotonic, "interpolated time never steps back");
        check(ticks >= 35999 && ticks <= 36000, "ten minutes is 36000 ticks");
        check(std::fabs(bench.drift_ticks()) < 1.0 && bench.sim_ticks + 1 >= ticks,
              "bench pacing report agrees with the clock");
    }
    {
        // 144 Hz interpolation is even: every frame advances 60/144 tick +- 1/65536.
        rr::TickClock c;
        uint64_t ticks = 0, previous = 0;
        double last = 0;
        bool even = true;
        for (uint64_t frame = 1; frame <= 144 * 60; ++frame) {
            const uint64_t now = frame * 1000000000ull / 144;
            ticks += uint64_t(c.advance(now - previous, false));
            previous = now;
            const double time = double(ticks) + double(c.alpha_q16()) / 65536.0;
            if (frame > 1) even = even && std::fabs(time - last - 60.0 / 144.0) < 2.0 / 65536.0;
            last = time;
        }
        check(even, "144 Hz frames advance by an even 5/12 tick");
    }
    {
        rr::FrameBench bench;
        bench.warmup = 1;
        bench.frames = 3;
        bench.frames = 4;
        bench.finish_frame(0, 1, 1000);                         // warm-up frame records its start
        bench.finish_frame(0, 2, 1000 + 33333334);              // 2 ticks over 1/30 s
        bench.finish_frame(0, 0, 1000 + 33333334 + 1);          // nothing due
        bench.finish_frame(0, 0, 1000 + 2 * 33333334, 0, true); // resumed: interval left out
        const std::string json = bench.pacing_json();
        check(bench.step_frames[0] == 2 && bench.step_frames[2] == 1, "step histogram");
        check(bench.clock_resets == 1 && bench.intervals.size() == 2, "clock reset frame left out");
        check(std::fabs(bench.drift_ticks()) < 1e-3, "drift from intervals");
        check(json.find("\"drift_ticks\": -0.000") != std::string::npos ||
                  json.find("\"drift_ticks\": 0.000") != std::string::npos,
              "pacing JSON reports drift");
    }
    std::cout << failures << " failures\n";
    return failures ? 1 : 0;
}
