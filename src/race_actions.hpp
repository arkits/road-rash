// Plan 28 Task 1: pure event-to-action mapping for the race loop.
#pragma once
#include <SDL3/SDL.h>
#include <cstdint>

namespace rr {
enum class RaceAction {
    none,
    quit_app,
    open_pause,
    toggle_pause,
    resume,
    request_quit,
    confirm_quit,
    cancel_quit,
    restart,
    demo_exit,
    pause_message,
    resume_message,
    toggle_fullscreen // native: Alt+Enter / Cmd+F, as in the menus (plan 18 Task 3, plan 33b Task 3)
};
struct RaceActionContext {
    bool demo = false, career = false, finished = false, paused = false, quitting = false;
    bool has_menu = false, menu_open = false;
    bool pause_message = false; // original Pause-key message is up
    SDL_JoystickID pad = 0;     // 0 = no pad open
};
// One decision per event; mirrors run_race as of the plan-25 fixes.
RaceAction race_action(const SDL_Event&, const RaceActionContext&);
} // namespace rr
