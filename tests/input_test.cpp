#include "input.hpp"
#include "race_overlay.hpp"
#include <iostream>
#include <string>

int main() {
    int failures = 0;
    auto check = [&](bool v, const char* what) {
        if (!v) {
            ++failures;
            std::cerr << "FAIL " << what << '\n';
        }
    };
    rr::PlayerInput none{};
    check(rr::controls_from(none) == 0, "no input, no bits");
    rr::PlayerInput go{};
    go.accelerate = true;
    go.left = true;
    check(rr::controls_from(go) == (rr::control_accelerate | rr::control_left), "accelerate + left");
    rr::PlayerInput stop{};
    stop.brake = true;
    stop.right = true;
    check(rr::controls_from(stop) == (rr::control_brake | rr::control_right), "brake + right");
    // Analog stick past the threshold steers; inside the dead zone it does not.
    rr::PlayerInput stick{};
    stick.steer_axis = -20000;
    check(rr::controls_from(stick) == rr::control_left, "stick left");
    stick.steer_axis = 5000;
    check(rr::controls_from(stick) == 0, "stick dead zone");
    stick.steer_axis = 20000;
    check(rr::controls_from(stick) == rr::control_right, "stick right");
    // Touch zones on a 1000-wide screen: left third steers left, right third right, bottom-middle brakes;
    // any touch accelerates unless braking.
    check(rr::touch_input(100, 500, 1000, 800).left && rr::touch_input(100, 500, 1000, 800).accelerate,
          "touch left");
    check(rr::touch_input(900, 500, 1000, 800).right, "touch right");
    auto b = rr::touch_input(500, 750, 1000, 800);
    check(b.brake && !b.accelerate, "touch brake");
    rr::PlayerInput fight{};
    fight.punch = true;
    fight.kick = true;
    fight.backhand = true;
    check(rr::controls_from(fight) == (rr::control_punch | rr::control_kick | rr::control_backhand),
          "attack bits");
    check(rr::touch_input(500, 100, 1000, 800).punch, "touch top third punches");
    rr::TouchInput touches;
    auto finger = [&](Uint32 type, SDL_TouchID device, SDL_FingerID id, float x, float y) {
        SDL_Event e{};
        e.type = type;
        e.tfinger.touchID = device;
        e.tfinger.fingerID = id;
        e.tfinger.x = x;
        e.tfinger.y = y;
        touches.handle(e);
    };
    finger(SDL_EVENT_FINGER_DOWN, 1, 1, .1f, .6f);
    finger(SDL_EVENT_FINGER_DOWN, 1, 2, .5f, .1f);
    check(touches.current().left && touches.current().punch && touches.current().accelerate,
          "two fingers steer and attack together");
    finger(SDL_EVENT_FINGER_UP, 1, 2, .5f, .1f);
    check(touches.current().left && !touches.current().punch, "releasing attack preserves steering");
    finger(SDL_EVENT_FINGER_DOWN, 2, 1, .5f, .9f);
    check(touches.current().left && touches.current().brake && !touches.current().accelerate,
          "braking overrides touch throttle; identical finger IDs on different devices coexist");
    finger(SDL_EVENT_FINGER_CANCELED, 2, 1, .5f, .9f);
    check(!touches.current().brake && touches.current().left, "canceled finger released independently");
    finger(SDL_EVENT_FINGER_MOTION, 1, 1, .9f, .6f);
    check(touches.current().right && !touches.current().left, "motion updates same finger");
    touches.clear();
    check(rr::controls_from(touches.current()) == 0, "pause clears all touch controls");
    // Phone controls must work together without hidden auto-throttle, and the
    // visible layout must be the hit-test layout in either handedness.
    for (bool mirrored : {false, true}) {
        for (const auto& button : rr::phone_buttons(mirrored)) {
            auto intent = rr::phone_touch_input((button.x + button.w / 2) / 1000,
                                                (button.y + button.h / 2) / 450, mirrored);
            const auto bits = rr::controls_from(intent);
            uint32_t expected = 0;
            switch (button.action) {
            case rr::TouchAction::left:
                expected = rr::control_left;
                break;
            case rr::TouchAction::right:
                expected = rr::control_right;
                break;
            case rr::TouchAction::accelerate:
                expected = rr::control_accelerate;
                break;
            case rr::TouchAction::brake:
                expected = rr::control_brake;
                break;
            case rr::TouchAction::punch:
                expected = rr::control_punch;
                break;
            case rr::TouchAction::kick:
                expected = rr::control_kick;
                break;
            case rr::TouchAction::backhand:
                expected = rr::control_backhand;
                break;
            case rr::TouchAction::pause:
                break;
            }
            check(bits == expected, "phone button activates only its labeled action");
        }
    }
    check(rr::controls_from(rr::phone_touch_input(.5f, .5f, false)) == 0,
          "touch outside phone buttons does not accelerate or attack");
    touches.set_phone_controls(true);
    finger(SDL_EVENT_FINGER_DOWN, 1, 1, .91f, .84f); // gas
    finger(SDL_EVENT_FINGER_DOWN, 1, 2, .07f, .84f); // left
    finger(SDL_EVENT_FINGER_DOWN, 1, 3, .07f, .66f); // punch
    check(touches.current().accelerate && touches.current().left && touches.current().punch,
          "phone gas, steering and combat work simultaneously");
    finger(SDL_EVENT_FINGER_DOWN, 1, 4, .81f, .84f); // brake
    check(touches.current().brake && !touches.current().accelerate, "phone brake overrides held gas");
    finger(SDL_EVENT_FINGER_CANCELED, 1, 4, .81f, .84f);
    check(touches.current().accelerate, "canceling brake restores separately held gas");
    finger(SDL_EVENT_FINGER_MOTION, 1, 1, .5f, .5f);
    check(!touches.current().accelerate && touches.current().left && touches.current().punch,
          "dragging off gas releases it without releasing other fingers");
    touches.clear();
    check(rr::controls_from(touches.current()) == 0, "phone pause clears all held fingers");
    // The controls must remain visible in the side margins of a 4:3 scene,
    // without leaking their presentation/blend state into the next frame.
    auto* surface = SDL_CreateSurface(1000, 450, SDL_PIXELFORMAT_RGBA8888);
    auto* renderer = surface ? SDL_CreateSoftwareRenderer(surface) : nullptr;
    check(renderer != nullptr, "phone overlay software renderer");
    if (renderer) {
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderClear(renderer);
        SDL_SetRenderLogicalPresentation(renderer, 640, 480, SDL_LOGICAL_PRESENTATION_LETTERBOX);
        rr::draw_phone_controls(renderer, {}, false, 75);
        SDL_RenderPresent(renderer);
        Uint8 r = 0, g = 0, b = 0, a = 0;
        SDL_ReadSurfacePixel(surface, 30, 345, &r, &g, &b, &a);
        check(r > 0 && g > 0, "phone controls render outside the 4:3 scene viewport");
        int w = 0, h = 0;
        SDL_RendererLogicalPresentation mode;
        SDL_GetRenderLogicalPresentation(renderer, &w, &h, &mode);
        check(w == 640 && h == 480 && mode == SDL_LOGICAL_PRESENTATION_LETTERBOX,
              "phone overlay restores race presentation");
        SDL_DestroyRenderer(renderer);
    }
    SDL_DestroySurface(surface);
    rr::MenuStickInput menu;
    auto idle = [](const rr::MenuDirections& d) { return !d.left && !d.right && !d.up && !d.down; };
    check(idle(menu.update(12000, -12000, 0)), "menu stick activation dead zone");
    check(menu.update(-32768, 10000, 10).left, "menu stick signed minimum and dominant horizontal direction");
    check(idle(menu.update(-20000, 0, 359)), "menu stick initial repeat delay");
    check(menu.update(-20000, 0, 360).left, "menu stick repeats at 350ms");
    check(idle(menu.update(-20000, 0, 459)), "menu stick repeat interval");
    check(menu.update(-20000, 0, 460).left, "menu stick repeats at 100ms");
    check(menu.update(-10000, 0, 560).left, "menu stick release hysteresis");
    check(menu.update(20000, 0, 561).right, "menu stick reversal navigates immediately");
    auto vertical = menu.update(20000, -30000, 562);
    check(vertical.up && !vertical.right, "diagonal stick produces one dominant direction");
    check(menu.update(0, 32767, 563).down, "menu stick vertical reversal");
    check(menu.update(0, 32767, 100000).down, "long stall emits one repeat");
    check(idle(menu.update(0, 32767, 100000)), "long stall does not accumulate navigation debt");
    check(idle(menu.update(0, 8000, 100001)), "release threshold clears menu stick");
    check(idle(menu.update(0, 10000, 100002)), "release requires activation threshold again");
    menu.suspend();
    check(idle(menu.update(0, 30000, 200000)), "pause or nested race blocks held stick");
    check(idle(menu.update(0, 10000, 200001)), "interrupted stick must fully return to neutral");
    check(idle(menu.update(0, 0, 200002)), "neutral rearms interrupted menu stick");
    check(menu.update(0, -30000, 200003).up, "menu navigation works after explicit neutral");
    // Plan 18 Task 7 Step 2: defaults honor the current keys; a rebound key
    // produces its action's control bit, and the old key goes silent.
    {
        std::array<uint16_t, 11> defs{};
        bool down[SDL_SCANCODE_COUNT] = {};
        down[SDL_SCANCODE_UP] = true;
        check(rr::bound_control_bits(defs, false, down) == rr::control_accelerate,
              "Up accelerates by default");
        down[SDL_SCANCODE_UP] = false;
        down[SDL_SCANCODE_C] = true;
        check(rr::bound_control_bits(defs, false, down) == rr::control_backhand, "C backhands by default");
        // Rebind accelerate (action 0) to W: W yields accelerate, Up yields nothing.
        // Custom tables hold the original keyboard code in the high byte ('w').
        std::array<uint16_t, 11> codes{};
        codes[0] = 0x7700;
        down[SDL_SCANCODE_C] = false;
        down[SDL_SCANCODE_W] = true;
        check(rr::bound_control_bits(codes, true, down) == rr::control_accelerate,
              "rebound key produces its bit");
        down[SDL_SCANCODE_W] = false;
        down[SDL_SCANCODE_UP] = true;
        check(rr::bound_control_bits(codes, true, down) == 0, "old key silent after rebind");
    }
    // 0x00476c00: row i sets bit 1 << i; the original keyboard defaults
    // (0x00466618) drive the race through the US VK table 0x00465fe8.
    {
        const auto defs = rr::default_keyboard_controls();
        const auto bits_for = [&](SDL_Scancode sc) {
            bool down[SDL_SCANCODE_COUNT] = {};
            down[sc] = true;
            return rr::bound_control_bits(defs, true, down);
        };
        check(bits_for(SDL_SCANCODE_UP) == rr::control_accelerate, "default table: Up accelerates");
        check(bits_for(SDL_SCANCODE_KP_8) == rr::control_accelerate, "keypad 8 is Up's twin");
        check(bits_for(SDL_SCANCODE_DOWN) == rr::control_brake, "default table: Down brakes");
        check(bits_for(SDL_SCANCODE_LEFT) == rr::control_left, "row 2 steers left");
        check(bits_for(SDL_SCANCODE_RIGHT) == rr::control_right, "row 9 steers right");
        check(bits_for(SDL_SCANCODE_HOME) == rr::control_lean_down, "row 3 (Home) leans down");
        check(bits_for(SDL_SCANCODE_PAGEUP) == rr::control_lean_up, "row 10 (PgUp) leans up");
        check(bits_for(SDL_SCANCODE_N) == rr::control_nitro, "row 4 (n) is nitro");
        check(bits_for(SDL_SCANCODE_INSERT) == rr::control_punch, "row 5 (Insert) swings");
        check(bits_for(SDL_SCANCODE_RETURN) == rr::control_kick, "row 6 (Enter) kicks");
        check(bits_for(SDL_SCANCODE_SPACE) == rr::control_backhand, "row 7 (Space) backhands");
        check(bits_for(SDL_SCANCODE_D) == 0, "row 8 (dismount) has no reader");
        check(rr::original_key_code(SDL_SCANCODE_F1) == 0, "F1 has no original code");
        for (int code = 1; code < 256; ++code)
            for (int sc : rr::original_key_scancodes(uint8_t(code)))
                if (sc) check(rr::original_key_code(sc) == code, "code table round trips");
    }
    // Plan 18 Task 7 Steps 2/4: left-handed mirrors the touch zones.
    check(rr::touch_input(1.f / 6, 0.5f, 1.f, 1.f, false).left, "left third steers left by default");
    check(rr::touch_input(5.f / 6, 0.5f, 1.f, 1.f, true).left, "left-handed mirrors right to left");
    check(rr::touch_input(5.f / 6, 0.8f, 1.f, 1.f, false).accelerate, "steer zones accelerate by default");
    check(rr::touch_input(1.f / 6, 0.8f, 1.f, 1.f, true).accelerate, "left-handed mirrors accelerate across");
    // Plan 33b Task 4 Step 1: race keys are scancodes; labels follow the layout.
    {
        const auto azerty = [](SDL_Scancode sc, SDL_Keymod, bool) -> SDL_Keycode {
            switch (sc) {
            case SDL_SCANCODE_Q:
                return SDLK_A;
            case SDL_SCANCODE_A:
                return SDLK_Q;
            case SDL_SCANCODE_W:
                return SDLK_Z;
            case SDL_SCANCODE_Z:
                return SDLK_W;
            case SDL_SCANCODE_M:
                return SDLK_COMMA;
            default:
                return SDL_GetKeyFromScancode(sc, SDL_KMOD_NONE, false);
            }
        };
        const auto qwertz = [](SDL_Scancode sc, SDL_Keymod, bool) -> SDL_Keycode {
            if (sc == SDL_SCANCODE_Z) return SDLK_Y;
            if (sc == SDL_SCANCODE_Y) return SDLK_Z;
            return SDL_GetKeyFromScancode(sc, SDL_KMOD_NONE, false);
        };
        const auto dvorak = [](SDL_Scancode sc, SDL_Keymod, bool) -> SDL_Keycode {
            if (sc == SDL_SCANCODE_Z) return SDLK_SEMICOLON;
            if (sc == SDL_SCANCODE_X) return SDLK_Q;
            if (sc == SDL_SCANCODE_C) return SDLK_J;
            return SDL_GetKeyFromScancode(sc, SDL_KMOD_NONE, false);
        };
        // No video subsystem here, so SDL's lookup is the default US map.
        const rr::KeyFromScancode us = SDL_GetKeyFromScancode;
        check(rr::default_key_scancode(5) == SDL_SCANCODE_Z, "punch defaults to the Z position");
        check(rr::key_label(SDL_SCANCODE_Z, us) == "Z", "US punch label");
        check(rr::key_label(SDL_SCANCODE_Z, azerty) == "W", "AZERTY prints W on the punch key");
        check(rr::key_label(SDL_SCANCODE_Z, qwertz) == "Y", "QWERTZ prints Y on the punch key");
        check(rr::key_label(SDL_SCANCODE_X, dvorak) == "Q", "Dvorak prints Q on the kick key");
        check(rr::key_label(SDL_SCANCODE_UP, azerty) == "Up", "arrows keep their names");
        check(rr::key_label(rr::default_key_scancode(8), us) == "?", "unbound row");
        // The bound bit comes from the physical key whatever the layout prints.
        bool down[SDL_SCANCODE_COUNT] = {};
        down[SDL_SCANCODE_Z] = true;
        check(rr::bound_control_bits({}, false, down) == rr::control_punch, "default punch by position");
        std::array<uint16_t, 11> codes{};
        codes[5] = uint16_t(rr::original_key_code(SDL_SCANCODE_Q) << 8); // bound on AZERTY's A key
        down[SDL_SCANCODE_Z] = false;
        down[SDL_SCANCODE_Q] = true;
        check(rr::bound_control_bits(codes, true, down) == rr::control_punch, "custom punch by position");
        check(rr::key_label(rr::original_key_scancodes(uint8_t(codes[5] >> 8))[0], azerty) == "A",
              "custom label in the current layout");
    }
    // Plan 33b Task 4 Step 2: button names per pad family.
    {
        const auto xbox =
            rr::pad_labels(SDL_GAMEPAD_TYPE_XBOXONE, SDL_GAMEPAD_BUTTON_LABEL_A, SDL_GAMEPAD_BUTTON_LABEL_B,
                           SDL_GAMEPAD_BUTTON_LABEL_X, SDL_GAMEPAD_BUTTON_LABEL_Y);
        const auto rows = rr::gamepad_rows(xbox, false);
        check(rows[0] == "A/RT: gas  B/LT: brake  Pad: steer", "Xbox gas row");
        check(rows[1] == "X: punch  Y: kick  RB: backhand", "Xbox attack row");
        check(rows[2] == "Menu: pause  View: quit career race", "Xbox Start/Back names");
        check(rr::gamepad_rows(xbox, true)[1] == "Y: punch  X: kick  RB: backhand",
              "swap trades West and North");
        const auto ds4 = rr::pad_labels(SDL_GAMEPAD_TYPE_PS4, SDL_GAMEPAD_BUTTON_LABEL_CROSS,
                                        SDL_GAMEPAD_BUTTON_LABEL_CIRCLE, SDL_GAMEPAD_BUTTON_LABEL_SQUARE,
                                        SDL_GAMEPAD_BUTTON_LABEL_TRIANGLE);
        check(rr::gamepad_rows(ds4, false)[2] == "Options: pause  Share: quit career race", "DS4 names");
        // Nintendo layout: the South position is printed B.
        const auto pro = rr::pad_labels(SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_PRO, SDL_GAMEPAD_BUTTON_LABEL_B,
                                        SDL_GAMEPAD_BUTTON_LABEL_A, SDL_GAMEPAD_BUTTON_LABEL_Y,
                                        SDL_GAMEPAD_BUTTON_LABEL_X);
        check(rr::gamepad_rows(pro, false)[0] == "B/ZR: gas  A/ZL: brake  Pad: steer", "Switch Pro labels");
        const auto generic = rr::pad_labels(
            SDL_GAMEPAD_TYPE_STANDARD, SDL_GAMEPAD_BUTTON_LABEL_UNKNOWN, SDL_GAMEPAD_BUTTON_LABEL_UNKNOWN,
            SDL_GAMEPAD_BUTTON_LABEL_UNKNOWN, SDL_GAMEPAD_BUTTON_LABEL_UNKNOWN);
        check(rr::gamepad_rows(generic, false)[3] == "Menus: D-pad + South/East", "generic pad positions");
        check(rr::pad_labels(nullptr).south == "South", "no pad: positional names");
    }
    // Plan 33a Task 5 Step 5: pad-only name entry.
    {
        std::string name = "RASH";
        int cursor = -1;
        const rr::PadTextMove up{true, false, false, false}, down{false, true, false, false},
            left{false, false, true, false}, right{false, false, false, true};
        check(rr::pad_text_caption(name, cursor) == "RASH_", "keyboard editing shows the end cursor");
        rr::pad_text_edit(name, cursor, up, 19);
        check(name == "RASHA" && cursor == 4 && rr::pad_text_caption(name, cursor) == "RASH[A]",
              "up on the empty slot after the name starts at A");
        rr::pad_text_edit(name, cursor, down, 19);
        rr::pad_text_edit(name, cursor, down, 19);
        check(name == "RASH&", "down cycles backwards through space to the last symbol");
        rr::pad_text_edit(name, cursor, left, 19);
        rr::pad_text_edit(name, cursor, up, 19);
        check(name == "RASI&" && cursor == 3, "left moves onto H; up gives the next character");
        rr::pad_text_edit(name, cursor, right, 19);
        rr::pad_text_edit(name, cursor, right, 19);
        rr::pad_text_edit(name, cursor, right, 19);
        check(cursor == 5 && rr::pad_text_caption(name, cursor) == "RASI&_", "right stops at the empty slot");
        for (int i = 0; i < 6; ++i) rr::pad_text_edit(name, cursor, left, 19);
        check(cursor == 0, "left stops at the first character");
        std::string full(19, 'Z');
        int at = -1;
        rr::pad_text_edit(full, at, right, 19);
        check(at == 18 && full.size() == 19, "a full name keeps the cursor on its last character");
        rr::pad_text_edit(full, at, up, 19);
        check(full.size() == 19 && full.back() == 'a', "and never grows past the limit");
        std::string odd = "\xe9";
        int c0 = 0;
        rr::pad_text_edit(odd, c0, up, 19);
        check(odd == "A", "a character outside the cycle restarts it");
    }
    // Plan 33b Task 4 Step 2: hot-plug through SDL virtual gamepads.
    if (SDL_Init(SDL_INIT_GAMEPAD)) {
        const auto attach = [] {
            SDL_VirtualJoystickDesc desc;
            SDL_INIT_INTERFACE(&desc);
            desc.type = SDL_JOYSTICK_TYPE_GAMEPAD;
            desc.naxes = SDL_GAMEPAD_AXIS_COUNT;
            desc.nbuttons = SDL_GAMEPAD_BUTTON_COUNT;
            desc.name = "rr virtual pad";
            return SDL_AttachVirtualJoystick(&desc);
        };
        const auto pump = [](rr::GamepadPtr& pad, int& removed) {
            SDL_Event e;
            while (SDL_PollEvent(&e)) removed += rr::track_gamepad(pad, e) ? 1 : 0;
        };
        rr::GamepadPtr pad;
        int removed = 0;
        const SDL_JoystickID first = attach();
        pump(pad, removed);
        check(first && pad && SDL_GetGamepadID(pad.get()) == first, "added pad opens");
        const SDL_JoystickID second = attach();
        pump(pad, removed);
        check(pad && SDL_GetGamepadID(pad.get()) == first, "a second pad does not take over");
        SDL_DetachVirtualJoystick(first);
        pump(pad, removed);
        check(removed == 1, "removing the open pad reports it once (race pauses)");
        check(pad && SDL_GetGamepadID(pad.get()) == second, "the other connected pad takes over");
        SDL_DetachVirtualJoystick(second);
        pump(pad, removed);
        check(removed == 2 && !pad, "last pad removed");
        const SDL_JoystickID again = attach();
        pump(pad, removed);
        check(pad && SDL_GetGamepadID(pad.get()) == again && removed == 2, "reconnecting restores input");
        pad.reset();
        SDL_DetachVirtualJoystick(again);
        SDL_Quit();
    } else {
        check(false, SDL_GetError());
    }
    // Joystick codes (0x0044f880 race read, 0x0040f6d0 capture, 0x0044fa00 analog).
    {
        rr::JoystickState idle;
        check(rr::joystick_capture_code(idle) == 0, "idle stick captures nothing");
        rr::JoystickState j;
        j.x = 1000;
        check(rr::joystick_capture_code(j) == 0x28, "stick left captures 0x28");
        j = {};
        j.y = 60000;
        check(rr::joystick_capture_code(j) == 0x27, "stick down captures 0x27");
        j = {};
        j.r = 60000;
        check(rr::joystick_capture_code(j) == 0x29, "left pedal captures 0x29");
        j = {};
        j.pov = 27000;
        check(rr::joystick_capture_code(j) == 0x24, "hat left captures 0x24");
        j.pov = 4500;
        check(rr::joystick_capture_code(j) == 0, "diagonal hat captures nothing");
        j = {};
        j.buttons = 1u << 6;
        check(rr::joystick_capture_code(j) == 7, "button 7 captures 7");
        j.buttons = 3;
        check(rr::joystick_capture_code(j) == 0, "two buttons capture nothing");

        const auto joy = rr::default_joystick_controls();
        check(rr::joystick_bound(joy) && !rr::joystick_bound(rr::default_keyboard_controls()),
              "0x0044f7c0 joystick flag");
        rr::JoystickAnalog a;
        const int32_t top = 0x4000;
        j = {};
        j.buttons = 1; // row 5 button 1 punches
        check(rr::joystick_control_bits(joy, j, a, top) == rr::control_punch, "button 1 punches");
        j = {};
        j.y = 0x3000; // row 0 stick up, inside the 0x6fff edge
        a = {};
        check(rr::joystick_control_bits(joy, j, a, top) == rr::control_accelerate, "stick up accelerates");
        check(a.throttle == int32_t(int64_t(top) * 0x3000 / 0x7fff), "throttle cut from the deflection");
        j.y = 0x7000;
        check(rr::joystick_control_bits(joy, j, a, top) == 0, "past the 0x6fff edge: no accelerate");
        j = {};
        j.x = 0xa000; // row 9 stick right
        check(rr::joystick_control_bits(joy, j, a, top) == rr::control_right, "stick right steers");
        check(a.steer == (0xa000 - 0x7fff) / 800, "steer from the deflection");
        j.x = 0x1000; // row 2 stick left
        check(rr::joystick_control_bits(joy, j, a, top) == rr::control_left && a.steer == 0x1000 / 800,
              "stick left steers");
        // Row 1 (brake, stick down) uses the wide 0xbffd edge.
        j = {};
        j.y = 0xa000;
        check(rr::joystick_control_bits(joy, j, a, top) == 0, "brake needs 0xbffd");
        j.y = 0xc000;
        check(rr::joystick_control_bits(joy, j, a, top) == rr::control_brake, "brake past 0xbffd");
        // A bound key resets the analog values (0x0044fa50); the higher VK
        // writes last, so Right (VK 0x27) beats Left (0x25) for the steer value.
        bool down[SDL_SCANCODE_COUNT] = {};
        down[SDL_SCANCODE_LEFT] = true;
        down[SDL_SCANCODE_RIGHT] = true;
        a = {7, 7};
        const uint32_t bits = rr::bound_control_bits(rr::default_keyboard_controls(), down, a, top);
        check(bits == (rr::control_left | rr::control_right) && a.steer == 0x6400 / 800,
              "keys write full analog in VK order");
        down[SDL_SCANCODE_RIGHT] = false;
        down[SDL_SCANCODE_UP] = true;
        a = {7, 7};
        rr::bound_control_bits(rr::default_keyboard_controls(), down, a, top);
        check(a.steer == 0 && a.throttle == 0, "Left and Up keys reset steer and throttle");
        check(rr::joystick_state(nullptr).pov == 0xffff, "no pad reads idle");
    }
    std::cout << failures << " failures\n";
    return failures ? 1 : 0;
}
