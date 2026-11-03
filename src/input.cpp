#include "input.hpp"
#include <algorithm>
#include <bit>
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
// Control bit per action row 0..10 (0x00476c00: bit 1 << row). Row 8 (dismount,
// 0x100) has no reader in the original or the port.
constexpr uint32_t kActionBits[11] = {
    control_accelerate, control_brake,    control_left,
    control_lean_down,  control_nitro,    control_punch,
    control_kick,       control_backhand, 0,
    control_right,      control_lean_up,
};
// Native default keyboard scancodes per action (current game feel; lean, nitro and
// dismount unbound).
constexpr int kDefaultKeys[11][2] = {
    {SDL_SCANCODE_UP, SDL_SCANCODE_W},
    {SDL_SCANCODE_DOWN, SDL_SCANCODE_S},
    {SDL_SCANCODE_LEFT, SDL_SCANCODE_A},
    {-1, -1},
    {-1, -1},
    {SDL_SCANCODE_Z, SDL_SCANCODE_LCTRL},
    {SDL_SCANCODE_X, SDL_SCANCODE_LALT},
    {SDL_SCANCODE_C, -1},
    {-1, -1},
    {SDL_SCANCODE_RIGHT, SDL_SCANCODE_D},
    {-1, -1},
};
bool pressed(const bool* down, int sc) {
    return sc > 0 && sc < SDL_SCANCODE_COUNT && down[sc];
}
// The US VK table 0x00465fe8 inverted: original code -> the scancodes of its VKs.
struct OriginalKey {
    uint8_t code;
    int scancodes[2];
};
constexpr OriginalKey kOriginalKeys[] = {
    {0x08, {SDL_SCANCODE_BACKSPACE, 0}},
    {0x09, {SDL_SCANCODE_TAB, 0}},
    {0x0d, {SDL_SCANCODE_RETURN, SDL_SCANCODE_KP_ENTER}},
    {0x1b, {SDL_SCANCODE_ESCAPE, 0}},
    {0x20, {SDL_SCANCODE_SPACE, 0}},
    {0x27, {SDL_SCANCODE_APOSTROPHE, 0}},
    {0x2a, {SDL_SCANCODE_KP_MULTIPLY, 0}},
    {0x2b, {SDL_SCANCODE_KP_PLUS, 0}},
    {0x2c, {SDL_SCANCODE_COMMA, 0}},
    {0x2d, {SDL_SCANCODE_MINUS, SDL_SCANCODE_KP_MINUS}},
    {0x2e, {SDL_SCANCODE_PERIOD, 0}},
    {0x2f, {SDL_SCANCODE_SLASH, SDL_SCANCODE_KP_DIVIDE}},
    {0x30, {SDL_SCANCODE_0, 0}},
    {0x31, {SDL_SCANCODE_1, 0}},
    {0x32, {SDL_SCANCODE_2, 0}},
    {0x33, {SDL_SCANCODE_3, 0}},
    {0x34, {SDL_SCANCODE_4, 0}},
    {0x35, {SDL_SCANCODE_5, 0}},
    {0x36, {SDL_SCANCODE_6, 0}},
    {0x37, {SDL_SCANCODE_7, 0}},
    {0x38, {SDL_SCANCODE_8, 0}},
    {0x39, {SDL_SCANCODE_9, 0}},
    {0x3b, {SDL_SCANCODE_SEMICOLON, 0}},
    {0x3d, {SDL_SCANCODE_EQUALS, 0}},
    {0x5b, {SDL_SCANCODE_LEFTBRACKET, 0}},
    {0x5c, {SDL_SCANCODE_BACKSLASH, 0}},
    {0x5d, {SDL_SCANCODE_RIGHTBRACKET, 0}},
    {0x60, {SDL_SCANCODE_GRAVE, 0}},
    {0x80, {SDL_SCANCODE_CLEAR, SDL_SCANCODE_KP_5}},
    {0x81, {SDL_SCANCODE_PAGEUP, SDL_SCANCODE_KP_9}},
    {0x82, {SDL_SCANCODE_PAGEDOWN, SDL_SCANCODE_KP_3}},
    {0x83, {SDL_SCANCODE_END, SDL_SCANCODE_KP_1}},
    {0x84, {SDL_SCANCODE_HOME, SDL_SCANCODE_KP_7}},
    {0x85, {SDL_SCANCODE_INSERT, SDL_SCANCODE_KP_0}},
    {0x86, {SDL_SCANCODE_DELETE, SDL_SCANCODE_KP_PERIOD}},
    {0x90, {SDL_SCANCODE_LSHIFT, SDL_SCANCODE_RSHIFT}},
    {0x91, {SDL_SCANCODE_LCTRL, SDL_SCANCODE_RCTRL}},
    {0x92, {SDL_SCANCODE_LALT, SDL_SCANCODE_RALT}},
    {0x93, {SDL_SCANCODE_LEFT, SDL_SCANCODE_KP_4}},
    {0x94, {SDL_SCANCODE_UP, SDL_SCANCODE_KP_8}},
    {0x95, {SDL_SCANCODE_RIGHT, SDL_SCANCODE_KP_6}},
    {0x96, {SDL_SCANCODE_DOWN, SDL_SCANCODE_KP_2}},
};
} // namespace

std::array<int, 2> original_key_scancodes(uint8_t code) {
    if (code >= 0x61 && code <= 0x7a) return {SDL_SCANCODE_A + (code - 0x61), 0}; // VK 'A'..'Z'
    for (const auto& k : kOriginalKeys)
        if (k.code == code) return {k.scancodes[0], k.scancodes[1]};
    return {0, 0};
}

uint8_t original_key_code(int scancode) {
    if (scancode >= SDL_SCANCODE_A && scancode <= SDL_SCANCODE_Z)
        return uint8_t(0x61 + scancode - SDL_SCANCODE_A);
    if (scancode <= 0) return 0;
    for (const auto& k : kOriginalKeys)
        if (k.scancodes[0] == scancode || k.scancodes[1] == scancode) return k.code;
    return 0;
}

namespace {
// Windows VK of a scancode, for the 0x0044fa80 scan order (numpad keys as
// their NumLock-on VKs; left/right modifier VKs 0xa0-0xa5 not modelled).
int scancode_vk(int sc) {
    if (sc >= SDL_SCANCODE_A && sc <= SDL_SCANCODE_Z) return 0x41 + sc - SDL_SCANCODE_A;
    if (sc >= SDL_SCANCODE_1 && sc <= SDL_SCANCODE_9) return 0x31 + sc - SDL_SCANCODE_1;
    if (sc >= SDL_SCANCODE_KP_1 && sc <= SDL_SCANCODE_KP_9) return 0x61 + sc - SDL_SCANCODE_KP_1;
    switch (sc) {
    case SDL_SCANCODE_0:
        return 0x30;
    case SDL_SCANCODE_KP_0:
        return 0x60;
    case SDL_SCANCODE_BACKSPACE:
        return 0x08;
    case SDL_SCANCODE_TAB:
        return 0x09;
    case SDL_SCANCODE_CLEAR:
        return 0x0c;
    case SDL_SCANCODE_RETURN:
    case SDL_SCANCODE_KP_ENTER:
        return 0x0d;
    case SDL_SCANCODE_LSHIFT:
    case SDL_SCANCODE_RSHIFT:
        return 0x10;
    case SDL_SCANCODE_LCTRL:
    case SDL_SCANCODE_RCTRL:
        return 0x11;
    case SDL_SCANCODE_LALT:
    case SDL_SCANCODE_RALT:
        return 0x12;
    case SDL_SCANCODE_ESCAPE:
        return 0x1b;
    case SDL_SCANCODE_SPACE:
        return 0x20;
    case SDL_SCANCODE_PAGEUP:
        return 0x21;
    case SDL_SCANCODE_PAGEDOWN:
        return 0x22;
    case SDL_SCANCODE_END:
        return 0x23;
    case SDL_SCANCODE_HOME:
        return 0x24;
    case SDL_SCANCODE_LEFT:
        return 0x25;
    case SDL_SCANCODE_UP:
        return 0x26;
    case SDL_SCANCODE_RIGHT:
        return 0x27;
    case SDL_SCANCODE_DOWN:
        return 0x28;
    case SDL_SCANCODE_INSERT:
        return 0x2d;
    case SDL_SCANCODE_DELETE:
        return 0x2e;
    case SDL_SCANCODE_KP_MULTIPLY:
        return 0x6a;
    case SDL_SCANCODE_KP_PLUS:
        return 0x6b;
    case SDL_SCANCODE_KP_MINUS:
        return 0x6d;
    case SDL_SCANCODE_KP_PERIOD:
        return 0x6e;
    case SDL_SCANCODE_KP_DIVIDE:
        return 0x6f;
    case SDL_SCANCODE_SEMICOLON:
        return 0xba;
    case SDL_SCANCODE_EQUALS:
        return 0xbb;
    case SDL_SCANCODE_COMMA:
        return 0xbc;
    case SDL_SCANCODE_MINUS:
        return 0xbd;
    case SDL_SCANCODE_PERIOD:
        return 0xbe;
    case SDL_SCANCODE_SLASH:
        return 0xbf;
    case SDL_SCANCODE_GRAVE:
        return 0xc0;
    case SDL_SCANCODE_LEFTBRACKET:
        return 0xdb;
    case SDL_SCANCODE_BACKSLASH:
        return 0xdc;
    case SDL_SCANCODE_RIGHTBRACKET:
        return 0xdd;
    case SDL_SCANCODE_APOSTROPHE:
        return 0xde;
    default:
        return 0x100;
    }
}
// 0x0044fa00: store an analog deflection for rows 0 (throttle) and 2/9 (steer).
void set_analog(JoystickAnalog& a, int row, int32_t v, int32_t top_throttle) {
    if (v < 0) v = 0;
    if (row == 0)
        a.throttle = int32_t(int64_t(top_throttle) * v / 0x7fff);
    else if (row == 2 || row == 9)
        a.steer = v / 800;
}
// 0x0044fa50: a key or button on rows 0/2/9 means full deflection.
void full_analog(JoystickAnalog& a, int row, int32_t top_throttle) {
    if (row == 0 || row == 2) set_analog(a, row, 0, top_throttle);
    if (row == 9) set_analog(a, row, 0x6400, top_throttle);
}
} // namespace

uint32_t bound_control_bits(const std::array<uint16_t, 11>& codes, const bool* down, JoystickAnalog& analog,
                            int32_t top_throttle) {
    // 0x0044fa80 walks VKs 0..255 and, per pressed VK, rows 0..n: order the
    // pressed (VK, row) pairs the same way so the last analog write matches.
    struct Hit {
        int vk, row;
    };
    std::array<Hit, 11 * 2> hits{};
    size_t n = 0;
    uint32_t bits = 0;
    for (int i = 0; i < 11; ++i) {
        const uint8_t key = uint8_t(codes[size_t(i)] >> 8);
        if (key == 0) continue;
        for (int sc : original_key_scancodes(key))
            if (pressed(down, sc)) {
                bits |= kActionBits[i];
                hits[n++] = {scancode_vk(sc), i};
            }
    }
    std::stable_sort(hits.begin(), hits.begin() + std::ptrdiff_t(n),
                     [](const Hit& a, const Hit& b) { return a.vk < b.vk; });
    for (size_t k = 0; k < n; ++k) full_analog(analog, hits[k].row, top_throttle);
    return bits;
}

JoystickState joystick_state(SDL_Gamepad* pad) {
    JoystickState s;
    if (!pad) return s;
    const auto axis = [&](SDL_GamepadAxis a) { return int(SDL_GetGamepadAxis(pad, a)); };
    s.x = uint32_t(std::clamp(axis(SDL_GAMEPAD_AXIS_LEFTX) + 32768, 0, 0xffff));
    s.y = uint32_t(std::clamp(axis(SDL_GAMEPAD_AXIS_LEFTY) + 32768, 0, 0xffff));
    s.r = uint32_t(std::clamp(
        0x7fff + axis(SDL_GAMEPAD_AXIS_LEFT_TRIGGER) - axis(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER), 0, 0xffff));
    static constexpr SDL_GamepadButton kButtons[] = {
        SDL_GAMEPAD_BUTTON_SOUTH,         SDL_GAMEPAD_BUTTON_EAST,          SDL_GAMEPAD_BUTTON_WEST,
        SDL_GAMEPAD_BUTTON_NORTH,         SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER,
        SDL_GAMEPAD_BUTTON_BACK,          SDL_GAMEPAD_BUTTON_START,         SDL_GAMEPAD_BUTTON_LEFT_STICK,
        SDL_GAMEPAD_BUTTON_RIGHT_STICK,   SDL_GAMEPAD_BUTTON_GUIDE,         SDL_GAMEPAD_BUTTON_MISC1,
        SDL_GAMEPAD_BUTTON_RIGHT_PADDLE1, SDL_GAMEPAD_BUTTON_LEFT_PADDLE1,  SDL_GAMEPAD_BUTTON_RIGHT_PADDLE2,
        SDL_GAMEPAD_BUTTON_LEFT_PADDLE2,  SDL_GAMEPAD_BUTTON_TOUCHPAD,
    };
    for (size_t i = 0; i < std::size(kButtons); ++i)
        if (SDL_GetGamepadButton(pad, kButtons[i])) s.buttons |= 1u << i;
    const bool up = SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_DPAD_UP);
    const bool down = SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_DPAD_DOWN);
    const bool left = SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_DPAD_LEFT);
    const bool right = SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_DPAD_RIGHT);
    const int dx = int(right) - int(left), dy = int(down) - int(up);
    if (dx || dy) {
        // Clockwise from up in hundredths of a degree, as winmm reports a hat.
        static constexpr uint32_t kPov[3][3] = {
            {31500, 0, 4500}, {27000, 0xffff, 9000}, {22500, 18000, 13500}};
        s.pov = kPov[dy + 1][dx + 1];
    }
    return s;
}

uint8_t joystick_capture_code(const JoystickState& s) {
    // DAT_00481e70 capability bits 2 (R axis) and 0x10 (POV): a mapped pad has both.
    if (s.x < 15000) return 0x28;
    if (s.x > 45000) return 0x26;
    if (s.y < 15000) return 0x25;
    if (s.y > 45000) return 0x27;
    if (s.r < 15000) return 0x2a;
    if (s.r > 45000) return 0x29;
    if (s.pov == 9000) return 0x22;
    if (s.pov == 0) return 0x21;
    if (s.pov == 18000) return 0x23;
    if (s.pov == 27000) return 0x24;
    // 0x0040f7a0: exactly one button bit names its button.
    if (s.buttons == 0 || (s.buttons & (s.buttons - 1)) != 0) return 0;
    return uint8_t(std::countr_zero(s.buttons) + 1);
}

bool joystick_bound(const std::array<uint16_t, 11>& codes) {
    for (uint16_t c : codes)
        if (c & 0xff) return true;
    return false;
}

uint32_t joystick_control_bits(const std::array<uint16_t, 11>& codes, const JoystickState& s,
                               JoystickAnalog& analog, int32_t top_throttle) {
    // 0x00476c04: rows 1 and 3..8 and 10 use the wide 0x3fff/0xbffd edges;
    // rows 0, 2 and 9 the 0x6fff/0x8fff edges and write the analog value.
    static constexpr bool kAnalogRow[11] = {true,  false, true,  false, false, false,
                                            false, false, false, true,  false};
    static constexpr uint32_t kPov[4] = {0, 9000, 18000, 27000}; // 0x00476ca0
    uint32_t bits = 0;
    for (int i = 0; i < 11; ++i) {
        const uint8_t code = uint8_t(codes[size_t(i)]);
        if (code == 0) continue;
        bool on = false;
        if (code < 0x21) {
            on = (s.buttons >> (code - 1)) & 1;
            if (on) full_analog(analog, i, top_throttle);
        } else if (code < 0x25) {
            on = s.pov == kPov[code - 0x21];
            if (on) full_analog(analog, i, top_throttle);
        } else {
            const bool a = kAnalogRow[i];
            const uint32_t lo = a ? 0x6fff : 0x3fff, hi = a ? 0x8fff : 0xbffd;
            int32_t v = 0;
            bool have = true;
            switch (code) {
            case 0x25:
                on = s.y <= lo, v = int32_t(s.y);
                break;
            case 0x26:
                on = s.x >= hi, v = int32_t(s.x) - 0x7fff;
                break;
            case 0x27:
                on = s.y >= hi, v = int32_t(s.y) - 0x7fff;
                break;
            case 0x28:
                on = s.x <= lo, v = int32_t(s.x);
                break;
            case 0x29:
                on = s.r >= hi, v = 0xffff - int32_t(s.r);
                break;
            case 0x2a:
                on = s.r <= lo, v = 0x7fff - int32_t(s.r);
                break;
            default:
                // The original reads an uninitialised deflection for codes
                // above 0x2a and always reports them pressed; the port keeps
                // the press and skips the analog write.
                on = true, have = false;
                break;
            }
            if (on && a && have) set_analog(analog, i, v, top_throttle);
        }
        if (on) bits |= kActionBits[i];
    }
    return bits;
}

uint32_t bound_control_bits(const std::array<uint16_t, 11>& codes, bool custom, const bool* down) {
    uint32_t bits = 0;
    for (int i = 0; i < 11; ++i) {
        if (kActionBits[i] == 0 || custom) continue;
        {
            if (pressed(down, kDefaultKeys[i][0]) || pressed(down, kDefaultKeys[i][1]))
                bits |= kActionBits[i];
        }
    }
    if (custom) {
        JoystickAnalog unused;
        bits = bound_control_bits(codes, down, unused, 0);
    }
    return bits;
}

std::string key_label(int scancode, KeyFromScancode lookup) {
    if (scancode <= 0 || scancode >= SDL_SCANCODE_COUNT) return "?";
    const SDL_Keycode key = lookup(SDL_Scancode(scancode), SDL_KMOD_NONE, false);
    const char* name = key != SDLK_UNKNOWN ? SDL_GetKeyName(key) : nullptr;
    if (!name || !*name) name = SDL_GetScancodeName(SDL_Scancode(scancode));
    return name && *name ? name : "?";
}

int default_key_scancode(int action) {
    return action >= 0 && action < 11 ? kDefaultKeys[action][0] : -1;
}

PadLabels pad_labels(SDL_GamepadType type, SDL_GamepadButtonLabel south, SDL_GamepadButtonLabel east,
                     SDL_GamepadButtonLabel west, SDL_GamepadButtonLabel north) {
    PadLabels l;
    const auto face = [](SDL_GamepadButtonLabel label, const char* positional) -> std::string {
        switch (label) {
        case SDL_GAMEPAD_BUTTON_LABEL_A:
            return "A";
        case SDL_GAMEPAD_BUTTON_LABEL_B:
            return "B";
        case SDL_GAMEPAD_BUTTON_LABEL_X:
            return "X";
        case SDL_GAMEPAD_BUTTON_LABEL_Y:
            return "Y";
        case SDL_GAMEPAD_BUTTON_LABEL_CROSS:
            return "Cross";
        case SDL_GAMEPAD_BUTTON_LABEL_CIRCLE:
            return "Circle";
        case SDL_GAMEPAD_BUTTON_LABEL_SQUARE:
            return "Square";
        case SDL_GAMEPAD_BUTTON_LABEL_TRIANGLE:
            return "Triangle";
        default:
            return positional;
        }
    };
    l.south = face(south, "South");
    l.east = face(east, "East");
    l.west = face(west, "West");
    l.north = face(north, "North");
    switch (type) {
    case SDL_GAMEPAD_TYPE_XBOX360:
        l.start = "Start", l.back = "Back";
        break;
    case SDL_GAMEPAD_TYPE_XBOXONE:
        l.start = "Menu", l.back = "View";
        break;
    case SDL_GAMEPAD_TYPE_PS3:
        l.start = "Start", l.back = "Select";
        l.left_trigger = "L2", l.right_trigger = "R2", l.right_shoulder = "R1";
        break;
    case SDL_GAMEPAD_TYPE_PS4:
        l.start = "Options", l.back = "Share";
        l.left_trigger = "L2", l.right_trigger = "R2", l.right_shoulder = "R1";
        break;
    case SDL_GAMEPAD_TYPE_PS5:
        l.start = "Options", l.back = "Create";
        l.left_trigger = "L2", l.right_trigger = "R2", l.right_shoulder = "R1";
        break;
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_PRO:
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_PAIR:
        l.start = "+", l.back = "-";
        l.left_trigger = "ZL", l.right_trigger = "ZR", l.right_shoulder = "R";
        break;
    default:
        break;
    }
    return l;
}

PadLabels pad_labels(SDL_Gamepad* pad) {
    if (!pad) return {};
    return pad_labels(SDL_GetGamepadType(pad), SDL_GetGamepadButtonLabel(pad, SDL_GAMEPAD_BUTTON_SOUTH),
                      SDL_GetGamepadButtonLabel(pad, SDL_GAMEPAD_BUTTON_EAST),
                      SDL_GetGamepadButtonLabel(pad, SDL_GAMEPAD_BUTTON_WEST),
                      SDL_GetGamepadButtonLabel(pad, SDL_GAMEPAD_BUTTON_NORTH));
}

std::array<std::string, 4> gamepad_rows(const PadLabels& l, bool swap) {
    // race_screen: South/RT gas, East/LT brake, d-pad or stick steer, West
    // punch and North kick (traded by the swap), right shoulder backhand;
    // Start pauses, Back asks to quit a career race; menus take South/Start
    // to select and East/Back to go back.
    return {
        l.south + "/" + l.right_trigger + ": gas  " + l.east + "/" + l.left_trigger + ": brake  Pad: steer",
        (swap ? l.north : l.west) + ": punch  " + (swap ? l.west : l.north) + ": kick  " + l.right_shoulder +
            ": backhand",
        l.start + ": pause  " + l.back + ": quit career race", "Menus: D-pad + " + l.south + "/" + l.east};
}

bool track_gamepad(GamepadPtr& pad, const SDL_Event& e) {
    if (e.type == SDL_EVENT_GAMEPAD_ADDED && !pad) {
        pad.reset(SDL_OpenGamepad(e.gdevice.which));
        return false;
    }
    if (e.type != SDL_EVENT_GAMEPAD_REMOVED || !pad || SDL_GetGamepadID(pad.get()) != e.gdevice.which)
        return false;
    pad.reset();
    int count = 0;
    SDL_JoystickID* ids = SDL_GetGamepads(&count);
    for (int i = 0; ids && i < count && !pad; ++i)
        if (ids[i] != e.gdevice.which) pad.reset(SDL_OpenGamepad(ids[i]));
    SDL_free(ids);
    return true;
}

void pad_text_edit(std::string& buffer, int& cursor, PadTextMove move, size_t max_len) {
    // Space first, so a new slot starts at 'A' going up.
    static constexpr char cycle[] = " ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789.-'!?&";
    constexpr int count = int(sizeof cycle) - 1;
    const int last = int(max_len) - 1;
    if (cursor < 0) cursor = std::min(int(buffer.size()), last);
    const int size = int(buffer.size());
    if (move.left) cursor = std::max(0, cursor - 1);
    if (move.right) cursor = std::min({cursor + 1, size, last});
    if (move.up != move.down) {
        const bool fresh = cursor >= size;
        const char current = fresh ? ' ' : buffer[size_t(cursor)];
        const char* found = std::find(cycle, cycle + count, current);
        const int index = found == cycle + count ? 0 : int(found - cycle);
        const char next = cycle[(index + (move.up ? 1 : count - 1)) % count];
        if (fresh)
            buffer.push_back(next);
        else
            buffer[size_t(cursor)] = next;
    }
}

std::string pad_text_caption(const std::string& buffer, int cursor) {
    if (cursor < 0 || size_t(cursor) >= buffer.size()) return buffer + "_";
    const size_t at = size_t(cursor);
    return buffer.substr(0, at) + "[" + buffer[at] + "]" + buffer.substr(at + 1);
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
        {TouchAction::punch, 30, 265, 85, 65, "PUNCH"},
        {TouchAction::kick, 130, 265, 85, 65, "KICK"},
        {TouchAction::backhand, 230, 265, 85, 65, "BACKHAND"},
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
