#include "session.hpp"
#include "race.hpp"
#include <algorithm>

namespace rr {
void SessionState::pause() {
    paused_ = true;
    reset_ = true;
}
void SessionState::resume() {
    if (background_ || !focused_) return;
    paused_ = false;
    reset_ = true;
}
void SessionState::handle(const SDL_Event& e) {
    switch (e.type) {
    case SDL_EVENT_WILL_ENTER_BACKGROUND:
    case SDL_EVENT_DID_ENTER_BACKGROUND:
        background_ = true;
        pause();
        break;
    case SDL_EVENT_WILL_ENTER_FOREGROUND:
    case SDL_EVENT_DID_ENTER_FOREGROUND:
        background_ = false;
        pause(); // Returning to the game always requires an explicit resume.
        break;
    case SDL_EVENT_WINDOW_FOCUS_LOST:
        focused_ = false;
        pause();
        break;
    case SDL_EVENT_WINDOW_FOCUS_GAINED:
        focused_ = true;
        reset_ = true;
        break;
    default:
        break;
    }
}

bool SessionState::frame(uint64_t now_ns, bool frozen) {
    const uint64_t last = last_frame_ns_;
    const bool was_paused = last_frame_paused_;
    last_frame_ns_ = now_ns;
    const bool jump = last != 0 && !was_paused && now_ns > last && now_ns - last > suspend_gap_ns;
    if (jump) pause();
    last_frame_paused_ = paused() || frozen;
    return jump;
}

int TickClock::advance(uint64_t elapsed_ns, bool paused) {
    if (paused) {
        reset();
        return 0;
    }
    constexpr uint64_t second = 1000000000ull;
    constexpr uint64_t cap = (second * max_steps_per_frame + ticks_per_second - 1) / ticks_per_second;
    remainder_ += std::min(elapsed_ns, cap) * ticks_per_second;
    const int steps = int(remainder_ / second);
    if (steps >= max_steps_per_frame) {
        reset();
        return max_steps_per_frame;
    }
    remainder_ %= second;
    return steps;
}

uint32_t TickClock::alpha_q16() const {
    constexpr uint64_t second = 1000000000ull;
    return uint32_t((remainder_ << 16) / second);
}
} // namespace rr
