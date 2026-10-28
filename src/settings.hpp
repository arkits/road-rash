#pragma once
#include "pe_strings.hpp"
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace rr {
// Values the original keeps under HKLM\SOFTWARE\Electronic Arts\RoadRash 95 (string 0x004633e8),
// stored natively as key=value text in the SDL preference directory.
enum class DisplayMode : uint8_t { windowed = 0, fullscreen = 1 };    // fullscreen = borderless desktop
enum class ScaleMode : uint8_t { fit = 0, integer = 1, stretch = 2 }; // letterbox, whole multiples, fill
enum class FilterMode : uint8_t { sharp = 0, smooth = 1 };            // nearest, linear
enum class Widescreen : uint8_t {
    off = 0,
    mode16x10 = 1,
    mode16x9 = 2,
    match = 3
}; // plan 21 Task 8: 0 = 4:3
enum class TextureFilter : uint8_t { nearest = 0, bilinear = 1, trilinear = 2 }; // plan 21 Task 1
enum class Grade : uint8_t { none = 0, warm = 1, vivid = 2, dusk = 3 };          // plan 21 Task 1
struct Enhancements {
    bool interpolate = false; // Task 2
    bool perspective = false; // Task 3
    bool full_colour = false; // Task 4 (implied by filter != nearest)
    TextureFilter filter = TextureFilter::nearest;
    int fog = 0;                                               // Task 5: 0..100 strength
    Grade grade = Grade::none;                                 // Task 5
    int draw_slices = 33;                                      // Task 6: 33 (original), 48, 64
    bool bloom = false, vignette = false, motion_blur = false; // Task 7
    // Review 2026-10-13 item 1: no widescreen copy here; Settings::widescreen
    // is the single source of truth (a display choice, not an effect).
    bool any() const;
};
bool operator==(const Enhancements&, const Enhancements&);
Enhancements enhanced_preset(); // the measured "Enhanced" preset (Task 9 fills it)
struct Settings {
    Language language = Language::english;
    bool metric = false;            // original: LOCALE_IMEASURE != "1" (0x0042f490)
    bool high_resolution = false;   // "HiRes"  0x00475380: 640x480 race view
    bool inset = false;             // "Inset"  0x00475390: 320x240 inset in a 640x480 display
    bool high_res_bikers = false;   // "HiResBikers" 0x00475384
    int video_mode = 2;             // "VideoMode" 0x0047538c: 0 none, 1 small, 2 large
    bool alternate_artwork = false; // "AlternateArtwork" 0x004753a4 (Sierra postcard variant, key A)
    int mini_dash = 0;              // "MiniDash" 0x00467a98: 0 full, 1 mini, 2 off (F10 cycles, 0x004431d0)
    bool opponent_list = false;     // "OpponentList" 0x004753a8 (menu `/` key toggles, 0x00423777)
    // Key rebinding table: 11 u16 action codes at 0x00465fb8, stored in the
    // 0x16-byte "Controls" registry block (0x0040e710 writes, 0x0040e5c0 reads).
    std::array<uint16_t, 11> controls{}; // empty = EXE defaults below
    bool custom_controls = false;
    int music_group = 0, music_track = 0; // "Group"/"Track" (jukebox)
    bool swap_punch_kick = false;         // plan 18 Task 7: native gamepad page
    int stick_deadzone = 37;              // plan 18 Task 7: percent, 10..40 (≈ legacy 12000 units)
    bool left_handed = false;             // plan 18 Task 7: native touch page
    // "Mixer" block (0x00463030): master 100, other channels 80 by default.
    int master_volume = 100, music_volume = 80, effects_volume = 80, engine_volume = 80, others_volume = 80,
        siren_volume = 80, interface_volume = 80;
    bool mute = false, swap_speakers = false;
    std::string alias; // player name (0x004b8a1c)
    DisplayMode display_mode = DisplayMode::windowed;
    int window_scale = 2; // 1..4: window is 640x480 (or 640x510 in a race) times this
    ScaleMode scale_mode = ScaleMode::fit;
    FilterMode filter = FilterMode::sharp;
    bool vsync = true;
    bool show_fps = false;
    bool network_hud = false, smooth_remote = true; // plan 34 presentation options
    int render_scale = 1;                           // plan 19 Task 8: 0 = match display, 1..4 fixed
    Widescreen widescreen = Widescreen::off;        // plan 21 Task 8: Hor+ logical width
    int texture_detail = 0;                         // plan 20 Task 3: 0 original, 1 enhanced (MMPX)
    Enhancements enhancements;                      // plan 21 Task 1: all off = original renderer
    int touch_opacity = 50;                         // 0..100, Android touch overlay (plan 14 Task 3)
    bool haptics = true; // Android rumble on crash/hit where the device supports it
    // Plan 34 Task 7: online lobby persistence. net_server is the WebSocket
    // URL of rr-netd; net_seat is the 128-bit seat token (32 hex chars) kept
    // for the life of the room; net_room is the last room code.
    std::string net_server = "wss://road-rash.archit.xyz/net";
    std::string net_seat;
    std::string net_room;
    std::array<std::string, 8> net_chat{};
};
Settings default_settings();                      // language and units from the system locale
Settings parse_settings(const std::string& text); // unknown keys and malformed values are ignored
std::string format_settings(const Settings&);
Settings load_settings(const std::string& path); // defaults when missing or unreadable
bool save_settings(const std::string& path, const Settings&);
// Default Configure Input bindings (0x00466618 keyboard / 0x00466630 joystick):
// 11 rows x u16 at 0x00465fb8, persisted as the 0x16-byte "Controls" value
// (0x0040e710 writes, 0x0040e5c0 reads; values below are the reference EXE bytes).
std::array<uint16_t, 11> default_keyboard_controls();
std::array<uint16_t, 11> default_joystick_controls();
// Conflict rule of 0x0040fbf0: every other row holding the same 2-byte code is
// cleared (unbound), then the new code is stored at `action`. Throws on action >= 11.
void apply_rebind(std::array<uint16_t, 11>& table, size_t action, uint16_t code);
// Bounds-checked read of both default tables from the EXE; throws std::runtime_error.
void read_control_tables(const std::vector<uint8_t>& exe, std::array<uint16_t, 11>& keyboard,
                         std::array<uint16_t, 11>& joystick);
} // namespace rr
