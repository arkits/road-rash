// Plan 28 Task 1: pure race input actions (failing test).
#include "race_actions.hpp"
#include <iostream>

static SDL_Event key(SDL_Keycode k, bool repeat = false) {
    SDL_Event e{};
    e.type = SDL_EVENT_KEY_DOWN;
    e.key.key = k;
    e.key.repeat = repeat;
    return e;
}
static SDL_Event button(SDL_JoystickID id, Uint8 b) {
    SDL_Event e{};
    e.type = SDL_EVENT_GAMEPAD_BUTTON_DOWN;
    e.gbutton.which = id;
    e.gbutton.button = b;
    return e;
}
int main() {
    int failures = 0;
    auto check = [&](bool v, const char* what) {
        if (!v) {
            ++failures;
            std::cerr << "FAIL " << what << '\n';
        }
    };
    using A = rr::RaceAction;
    rr::RaceActionContext quick;
    quick.has_menu = true;
    quick.pad = 7;
    check(rr::race_action(key(SDLK_ESCAPE), quick) == A::open_pause, "quick Esc opens pause");
    check(rr::race_action(key(SDLK_ESCAPE, true), quick) == A::none, "key repeat ignored");
    check(rr::race_action(key(SDLK_RETURN), quick) == A::none, "Enter before finish does nothing");
    rr::RaceActionContext done = quick;
    done.finished = true;
    check(rr::race_action(key(SDLK_RETURN), done) == A::restart, "Enter after finish restarts (finding 1)");
    {
        SDL_Event alt_enter = key(SDLK_RETURN);
        alt_enter.key.mod = SDL_KMOD_LALT;
        check(rr::race_action(alt_enter, done) == A::toggle_fullscreen &&
                  rr::race_action(alt_enter, quick) == A::toggle_fullscreen,
              "Alt+Enter toggles fullscreen in a race instead of restarting (plan 33b)");
    }
    check(rr::race_action(button(7, SDL_GAMEPAD_BUTTON_SOUTH), done) == A::restart, "pad South restarts");
    check(rr::race_action(button(8, SDL_GAMEPAD_BUTTON_SOUTH), done) == A::none, "other pad ignored");
    SDL_Event tap{};
    tap.type = SDL_EVENT_FINGER_DOWN;
    check(rr::race_action(tap, done) == A::restart, "tap restarts");
    rr::RaceActionContext career = quick;
    career.career = true;
    check(rr::race_action(key(SDLK_ESCAPE), career) == A::request_quit, "career Esc asks to quit");
    career.quitting = true;
    check(rr::race_action(key(SDLK_ESCAPE), career) == A::confirm_quit, "second Esc confirms");
    check(rr::race_action(key(SDLK_A), career) == A::cancel_quit, "other key cancels");
    rr::RaceActionContext career_done = quick;
    career_done.career = true;
    career_done.finished = true;
    check(rr::race_action(key(SDLK_RETURN), career_done) == A::none, "career never restarts");
    check(rr::race_action(key(SDLK_Q), quick) == A::quit_app, "Q quits a quick race");
    rr::RaceActionContext demo;
    demo.demo = true;
    check(rr::race_action(key(SDLK_SPACE), demo) == A::demo_exit, "any key exits the demo");
    rr::RaceActionContext no_menu;
    no_menu.paused = true;
    check(rr::race_action(tap, no_menu) == A::resume, "tap resumes without a menu");
    rr::RaceActionContext menu_open = quick;
    menu_open.menu_open = true;
    check(rr::race_action(key(SDLK_ESCAPE), menu_open) == A::none, "menu owns input");
    // VK_PAUSE -> 0x004234f7 "Game paused." pump; any key / button / tap resumes.
    check(rr::race_action(key(SDLK_PAUSE), quick) == A::pause_message,
          "Pause key opens the original pause message");
    rr::RaceActionContext held = quick;
    held.pause_message = true;
    check(rr::race_action(key(SDLK_A), held) == A::resume_message, "any key resumes the pause message");
    check(rr::race_action(key(SDLK_ESCAPE), held) == A::resume_message, "Escape resumes (pump param 0)");
    check(rr::race_action(key(SDLK_F10), held) == A::none, "F10 is handled inside the pump");
    check(rr::race_action(button(7, SDL_GAMEPAD_BUTTON_SOUTH), held) == A::resume_message,
          "pad button resumes");
    check(rr::race_action(tap, held) == A::resume_message, "tap resumes");
    if (failures == 0) std::cout << "race_actions OK\n";
    return failures ? 1 : 0;
}
