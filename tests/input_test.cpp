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
        // Custom keyboard codes store low-byte-first (0x0040e760).
        std::array<uint16_t, 11> codes{};
        codes[0] = uint16_t(SDL_SCANCODE_W);
        down[SDL_SCANCODE_C] = false;
        down[SDL_SCANCODE_W] = true;
        check(rr::bound_control_bits(codes, true, down) == rr::control_accelerate,
              "rebound key produces its bit");
        down[SDL_SCANCODE_W] = false;
        down[SDL_SCANCODE_UP] = true;
        check(rr::bound_control_bits(codes, true, down) == 0, "old key silent after rebind");
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
        check(rr::default_key_scancode(4) == SDL_SCANCODE_Z, "punch defaults to the Z position");
        check(rr::key_label(SDL_SCANCODE_Z, us) == "Z", "US punch label");
        check(rr::key_label(SDL_SCANCODE_Z, azerty) == "W", "AZERTY prints W on the punch key");
        check(rr::key_label(SDL_SCANCODE_Z, qwertz) == "Y", "QWERTZ prints Y on the punch key");
        check(rr::key_label(SDL_SCANCODE_X, dvorak) == "Q", "Dvorak prints Q on the kick key");
        check(rr::key_label(SDL_SCANCODE_UP, azerty) == "Up", "arrows keep their names");
        check(rr::key_label(rr::default_key_scancode(7), us) == "?", "unbound row");
        // The bound bit comes from the physical key whatever the layout prints.
        bool down[SDL_SCANCODE_COUNT] = {};
        down[SDL_SCANCODE_Z] = true;
        check(rr::bound_control_bits({}, false, down) == rr::control_punch, "default punch by position");
        std::array<uint16_t, 11> codes{};
        codes[4] = uint16_t(SDL_SCANCODE_Q); // bound on AZERTY's A key
        down[SDL_SCANCODE_Z] = false;
        down[SDL_SCANCODE_Q] = true;
        check(rr::bound_control_bits(codes, true, down) == rr::control_punch, "custom punch by position");
        check(rr::key_label(codes[4] & 0xff, azerty) == "A", "custom label in the current layout");
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
    std::cout << failures << " failures\n";
    return failures ? 1 : 0;
}
