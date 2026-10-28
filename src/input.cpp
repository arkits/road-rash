#include "input.hpp"
#include <cstdlib>

namespace rr {
void MenuStickInput::suspend() {
    direction_ = Direction::none;
    next_repeat_ = 0;
    require_neutral_ = true;
}

MenuDirections MenuStickInput::update(int x, int y, uint64_t now_ms) {
    // SDL axis values are signed 16-bit; use int so abs(-32768) is defined.
    const int ax = std::abs(x), ay = std::abs(y);
    if (ax <= 8000 && ay <= 8000) {
        direction_ = Direction::none;
        require_neutral_ = false;
        return {};
    }
    if (require_neutral_) return {};
    const int threshold = direction_ == Direction::none ? steer_dead_zone : 8000;
    if (ax <= threshold && ay <= threshold) return {};
    const Direction next =
        ax >= ay ? (x < 0 ? Direction::left : Direction::right) : (y < 0 ? Direction::up : Direction::down);
    if (next != direction_) {
        direction_ = next;
        next_repeat_ = now_ms + 350;
    } else {
        if (now_ms < next_repeat_) return {};
        // At most one navigation step per frame, even after a long stall.
        next_repeat_ = now_ms + 100;
    }
    return {next == Direction::left, next == Direction::right, next == Direction::up,
            next == Direction::down};
}

uint32_t controls_from(const PlayerInput& in, int dead_zone) {
    uint32_t c = 0;
    if (in.accelerate) c |= control_accelerate;
    if (in.brake) c |= control_brake;
    if (in.punch) c |= control_punch;
    if (in.kick) c |= control_kick;
    if (in.backhand) c |= control_backhand;
    if (in.left || in.steer_axis < -dead_zone)
        c |= control_left;
    else if (in.right || in.steer_axis > dead_zone)
        c |= control_right;
    return c;
}

namespace {
// Native control bit per action row 0..10 (rows 7/8/10 have no native bit).
constexpr uint32_t kActionBits[11] = {
    control_accelerate,
    control_brake,
    control_left,
    control_right,
    control_punch,
    control_kick,
    control_backhand,
    0,
    0,
    control_nitro,
    0,
};
// Default keyboard scancodes per action (current game feel; row 8 dead, 7/9/10 unbound).
constexpr int kDefaultKeys[11][2] = {
    {SDL_SCANCODE_UP, SDL_SCANCODE_W},
    {SDL_SCANCODE_DOWN, SDL_SCANCODE_S},
    {SDL_SCANCODE_LEFT, SDL_SCANCODE_A},
    {SDL_SCANCODE_RIGHT, SDL_SCANCODE_D},
    {SDL_SCANCODE_Z, SDL_SCANCODE_LCTRL},
    {SDL_SCANCODE_X, SDL_SCANCODE_LALT},
    {SDL_SCANCODE_C, -1},
    {-1, -1},
    {-1, -1},
    {-1, -1},
    {-1, -1},
};
bool pressed(const bool* down, int sc) {
    return sc > 0 && sc < SDL_SCANCODE_COUNT && down[sc];
}
} // namespace

uint32_t bound_control_bits(const std::array<uint16_t, 11>& codes, bool custom, const bool* down) {
    uint32_t bits = 0;
    for (int i = 0; i < 11; ++i) {
        if (kActionBits[i] == 0) continue;
        if (!custom) {
            if (pressed(down, kDefaultKeys[i][0]) || pressed(down, kDefaultKeys[i][1]))
                bits |= kActionBits[i];
        } else {
            // Custom keyboard codes store low-byte-first (0x0040e760).
            if (pressed(down, codes[size_t(i)] & 0xff)) bits |= kActionBits[i];
        }
    }
    return bits;
}

PlayerInput touch_input(float x, float y, float width, float height, bool left_handed) {
    if (left_handed) x = width - x; // plan 18 Task 7: mirror zones for left thumbs
    PlayerInput in;
    if (y < height / 3) {
        in.punch = true;
        in.accelerate = true;
        return in;
    } // top third: punch
    if (x < width / 3)
        in.left = true;
    else if (x > width * 2 / 3)
        in.right = true;
    else if (y > height * 3 / 4)
        in.brake = true;
    in.accelerate = !in.brake;
    return in;
}

std::array<TouchButton, 8> phone_buttons(bool left_handed) {
    std::array<TouchButton, 8> buttons{{
        {TouchAction::left, 30, 345, 85, 65, "LEFT"},
        {TouchAction::right, 130, 345, 85, 65, "RIGHT"},
        {TouchAction::accelerate, 870, 345, 85, 65, "GAS"},
        {TouchAction::brake, 770, 345, 85, 65, "BRAKE"},
        {TouchAction::punch, 870, 265, 85, 65, "PUNCH"},
        {TouchAction::kick, 770, 265, 85, 65, "KICK"},
        {TouchAction::backhand, 670, 265, 85, 65, "BACKHAND"},
        {TouchAction::pause, 880, 20, 75, 40, "PAUSE"},
    }};
    if (left_handed)
        for (auto& b : buttons) b.x = 1000 - b.x - b.w;
    return buttons;
}

bool touch_contains(const TouchButton& b, float x, float y) {
    return x * 1000 >= b.x && x * 1000 < b.x + b.w && y * 450 >= b.y && y * 450 < b.y + b.h;
}

PlayerInput phone_touch_input(float x, float y, bool left_handed) {
    PlayerInput in;
    for (const auto& b : phone_buttons(left_handed)) {
        if (!touch_contains(b, x, y)) continue;
        switch (b.action) {
        case TouchAction::left:
            in.left = true;
            break;
        case TouchAction::right:
            in.right = true;
            break;
        case TouchAction::accelerate:
            in.accelerate = true;
            break;
        case TouchAction::brake:
            in.brake = true;
            break;
        case TouchAction::punch:
            in.punch = true;
            break;
        case TouchAction::kick:
            in.kick = true;
            break;
        case TouchAction::backhand:
            in.backhand = true;
            break;
        case TouchAction::pause:
            break;
        }
    }
    return in;
}

void TouchInput::handle(const SDL_Event& e) {
    const auto id = std::make_pair(e.tfinger.touchID, e.tfinger.fingerID);
    if (e.type == SDL_EVENT_FINGER_UP || e.type == SDL_EVENT_FINGER_CANCELED)
        fingers_.erase(id);
    else if (e.type == SDL_EVENT_FINGER_DOWN || e.type == SDL_EVENT_FINGER_MOTION)
        fingers_[id] = phone_controls_ ? phone_touch_input(e.tfinger.x, e.tfinger.y, left_handed_)
                                       : touch_input(e.tfinger.x, e.tfinger.y, 1.f, 1.f, left_handed_);
}

PlayerInput TouchInput::current() const {
    PlayerInput in;
    for (const auto& finger : fingers_) {
        const auto& f = finger.second;
        in.accelerate |= f.accelerate;
        in.brake |= f.brake;
        in.left |= f.left;
        in.right |= f.right;
        in.punch |= f.punch;
        in.kick |= f.kick;
        in.backhand |= f.backhand;
    }
    // A braking finger overrides auto-throttle from the steering/attack zones.
    if (in.brake) in.accelerate = false;
    return in;
}
} // namespace rr
