#pragma once
#include "bike.hpp"
#include "sdl_handles.hpp"
#include <SDL3/SDL_events.h>
#include <array>
#include <map>
#include <string>

namespace rr {
// Device-independent player intent; mapped onto the original control bits (bike.hpp).
struct PlayerInput {
    bool accelerate = false, brake = false, left = false, right = false;
    bool punch = false, kick = false, backhand = false;
    int steer_axis = 0; // gamepad stick, -32768..32767
};
// Analog-control path as traced (0x0044f880 joystick branch): the original picks
// dead zones 0x3fff/0xbffd when the binding row's analog flag (0x00476c04) is set
// and 0x6fff/0x8fff otherwise, then scales the deflection via 0x0044fa00.
// Natively the stick is digital past one threshold: steer_dead_zone matches the
// non-analog 0x6fff edge scaled to SDL units (0x6fff/0x7fff*32768 ~ 12000...16384;
// 12000 keeps the existing game feel; see analysis/options2-notes.md).
constexpr int steer_dead_zone = 12000;
struct MenuDirections {
    bool left = false, right = false, up = false, down = false;
};
// Native menu convenience: one dominant stick direction, with bounded repeat.
// After interruption, require neutral before accepting a held stick again.
class MenuStickInput {
  public:
    MenuDirections update(int x, int y, uint64_t now_ms);
    void suspend();

  private:
    enum class Direction { none, left, right, up, down } direction_ = Direction::none;
    uint64_t next_repeat_ = 0;
    bool require_neutral_ = false;
};
// Dead-zone override in SDL axis units (plan 18 Task 7 stick setting);
// the default keeps current feel for existing callers.
uint32_t controls_from(const PlayerInput&, int dead_zone = steer_dead_zone);
// Rebindable keyboard map (plan 18 Task 7): native control bit per action row
// 0..10 (row 8 is dead in the original too, rows 7/10 have no native bit).
// Custom tables store the SDL scancode in the high byte (native capture format);
// the traced defaults keep their internal-id bytes and are honored through the
// default scancode sets below (current game feel, unchanged).
uint32_t bound_control_bits(const std::array<uint16_t, 11>& codes, bool custom, const bool* down);
// Plan 33b Task 4: race keys are physical positions (scancodes), so AZERTY,
// QWERTZ and Dvorak keep the same layout; the label is what the current
// layout prints on that key. `lookup` is SDL_GetKeyFromScancode except in tests.
using KeyFromScancode = SDL_Keycode (*)(SDL_Scancode, SDL_Keymod, bool);
std::string key_label(int scancode, KeyFromScancode lookup = SDL_GetKeyFromScancode);
// Primary default key of action row 0..10 (-1: unbound).
int default_key_scancode(int action);
// Plan 33b Task 4: button names on the connected pad (Xbox A/B/X/Y,
// PlayStation Cross/Circle/Square/Triangle, Nintendo by printed label),
// positional names when unknown. Face buttons follow SDL_GetGamepadButtonLabel.
struct PadLabels {
    std::string south = "South", east = "East", west = "West", north = "North";
    std::string start = "Start", back = "Back", left_trigger = "LT", right_trigger = "RT",
                right_shoulder = "RB";
};
PadLabels pad_labels(SDL_GamepadType, SDL_GamepadButtonLabel south, SDL_GamepadButtonLabel east,
                     SDL_GamepadButtonLabel west, SDL_GamepadButtonLabel north);
PadLabels pad_labels(SDL_Gamepad*); // null: positional names
// The four mapping rows of the Gamepad page, matching race_screen's mapping.
std::array<std::string, 4> gamepad_rows(const PadLabels&, bool swap_punch_kick);
// Hot-plug for every loop: opens a pad when one is added and none is open;
// when the open pad goes away, falls back to another connected pad. Returns
// true when the open pad was removed (races pause on that).
bool track_gamepad(GamepadPtr&, const SDL_Event&);
// One touch point on a width x height window (mirrored zones when left_handed).
PlayerInput touch_input(float x, float y, float width, float height, bool left_handed = false);
// Phone buttons use full-window normalized coordinates, including letterbox
// margins. Rendering and hit testing share this layout (1000 x 450 units).
enum class TouchAction { left, right, accelerate, brake, punch, kick, backhand, pause };
struct TouchButton {
    TouchAction action;
    float x, y, w, h;
    const char* label;
};
std::array<TouchButton, 8> phone_buttons(bool left_handed);
bool touch_contains(const TouchButton&, float normalized_x, float normalized_y);
PlayerInput phone_touch_input(float x, float y, bool left_handed);
// Each finger keeps its own intent; releasing one cannot release another.
class TouchInput {
  public:
    void handle(const SDL_Event&);
    void clear() { fingers_.clear(); }
    void set_left_handed(bool v) { left_handed_ = v; }
    void set_phone_controls(bool v) {
        phone_controls_ = v;
        clear();
    }
    PlayerInput current() const;

  private:
    std::map<std::pair<SDL_TouchID, SDL_FingerID>, PlayerInput> fingers_;
    bool left_handed_ = false;
    bool phone_controls_ = false;
};
} // namespace rr
