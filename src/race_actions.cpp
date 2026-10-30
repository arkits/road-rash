// Plan 28 Task 1: transcribe run_race's event loop branch by branch, in order.
#include "race_actions.hpp"

namespace rr {
RaceAction race_action(const SDL_Event& e, const RaceActionContext& c) {
    // Native window shortcut, in every race state (menus handle it in game_main).
    if (e.type == SDL_EVENT_KEY_DOWN && !e.key.repeat &&
        ((e.key.key == SDLK_RETURN && (e.key.mod & SDL_KMOD_ALT)) ||
         (e.key.key == SDLK_F && (e.key.mod & SDL_KMOD_GUI))))
        return RaceAction::toggle_fullscreen;
    // Demo: any input exits quietly.
    if (c.demo && ((e.type == SDL_EVENT_KEY_DOWN && !e.key.repeat) ||
                   e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN || e.type == SDL_EVENT_GAMEPAD_ADDED ||
                   e.type == SDL_EVENT_FINGER_DOWN || e.type == SDL_EVENT_MOUSE_BUTTON_DOWN))
        return RaceAction::demo_exit;
    // Menu owns all input while open.
    if (c.menu_open) return RaceAction::none;
    // 0x004431d0 pump with string 317: key-up of any key (other than F10 /
    // PrintScreen), a joystick button or (native) a tap resumes.
    if (c.pause_message) {
        if ((e.type == SDL_EVENT_KEY_DOWN && !e.key.repeat && e.key.key != SDLK_F10 &&
             e.key.key != SDLK_PRINTSCREEN) ||
            e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN || e.type == SDL_EVENT_FINGER_DOWN)
            return RaceAction::resume_message;
        return RaceAction::none;
    }
    if (e.type == SDL_EVENT_KEY_DOWN && !e.key.repeat) {
        if (c.quitting) {
            // Second Escape confirms the quit (career); any other key cancels.
            if (e.key.key == SDLK_ESCAPE) return RaceAction::confirm_quit;
            return RaceAction::cancel_quit;
        }
        if (e.key.key == SDLK_Q && !c.career) return RaceAction::quit_app;
        // VK_PAUSE (0x13) -> 0x004234f7: "Game paused." through 0x004431d0.
        if (e.key.key == SDLK_PAUSE && !c.paused) return RaceAction::pause_message;
        if (e.key.key == SDLK_AC_BACK) return c.has_menu ? RaceAction::open_pause : RaceAction::toggle_pause;
        if (e.key.key == SDLK_ESCAPE && c.career && !c.paused) return RaceAction::request_quit;
        if (e.key.key == SDLK_ESCAPE || e.key.key == SDLK_P)
            return c.has_menu ? RaceAction::open_pause : RaceAction::toggle_pause;
        if (e.key.key == SDLK_RETURN) {
            if (c.paused) return RaceAction::resume;
            if (c.finished && !c.career) return RaceAction::restart;
        }
        return RaceAction::none;
    }
    if (e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN && c.pad && e.gbutton.which == c.pad) {
        if (c.quitting) {
            if (e.gbutton.button == SDL_GAMEPAD_BUTTON_BACK) return RaceAction::confirm_quit;
            return RaceAction::cancel_quit;
        }
        if (e.gbutton.button == SDL_GAMEPAD_BUTTON_START)
            return c.has_menu ? RaceAction::open_pause : RaceAction::toggle_pause;
        if (e.gbutton.button == SDL_GAMEPAD_BUTTON_BACK && c.career && !c.paused)
            return RaceAction::request_quit;
        if (e.gbutton.button == SDL_GAMEPAD_BUTTON_SOUTH && !c.paused && c.finished && !c.career)
            return RaceAction::restart;
        return RaceAction::none;
    }
    if (e.type == SDL_EVENT_FINGER_DOWN || e.type == SDL_EVENT_FINGER_MOTION ||
        e.type == SDL_EVENT_FINGER_UP || e.type == SDL_EVENT_FINGER_CANCELED) {
        if (c.paused) {
            if (e.type == SDL_EVENT_FINGER_DOWN) return RaceAction::resume;
            return RaceAction::none;
        }
        if (e.type == SDL_EVENT_FINGER_DOWN && c.finished && !c.career) return RaceAction::restart;
        return RaceAction::none;
    }
    return RaceAction::none;
}
} // namespace rr
