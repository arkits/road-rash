#include "input.hpp"
#include "race_overlay.hpp"
#include <iostream>

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
    std::cout << failures << " failures\n";
    return failures ? 1 : 0;
}
