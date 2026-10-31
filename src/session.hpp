#pragma once
#include <SDL3/SDL_events.h>
#include <atomic>
#include <cstdint>

namespace rr {
// Lifecycle events can arrive from SDL's Android event thread. Only atomic state
// is touched there; simulation and renderer work remain on the game thread.
class SessionState {
  public:
    void handle(const SDL_Event&);
    void pause();
    void resume();
    bool paused() const { return paused_.load() || background_.load() || !focused_.load(); }
    bool background() const { return background_.load(); }
    bool take_clock_reset() { return reset_.exchange(false); }
    // Plan 33a Task 5 Step 6: call once per frame of a real-time loop. A
    // wall-clock gap of more than 2 s since the previous call (system
    // suspend, a stopped process) counts as a focus loss: the session pauses
    // and needs an explicit resume. Returns true on such a jump, so the
    // caller can re-scan controllers. The first call only records the time,
    // and a gap that began while already paused, or while the caller was
    // frozen for its own reason (`frozen`: a pause menu or message), is not a
    // jump.
    static constexpr uint64_t suspend_gap_ns = 2'000'000'000ull;
    bool frame(uint64_t now_ns, bool frozen = false);

  private:
    std::atomic<bool> paused_{false}, background_{false}, focused_{true}, reset_{false};
    uint64_t last_frame_ns_ = 0;
    bool last_frame_paused_ = false;
};

// Rational 60 Hz accumulator: no nanosecond truncation drift, and no catch-up
// burst after pause/resume. Long stalls use the simulation's existing step cap.
class TickClock {
  public:
    int advance(uint64_t elapsed_ns, bool paused);
    void reset() { remainder_ = 0; }
    uint32_t alpha_q16() const; // plan 21 Task 2: sub-tick remainder in Q16
  private:
    uint64_t remainder_ = 0;
};
} // namespace rr
