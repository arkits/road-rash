#pragma once
#include <cstdint>

namespace rr {
struct FrontendData;
struct MenuItem;
// Builds the native screens and popups from original item geometry already in data.
void add_native_screens(FrontendData& data, bool mobile);
// Native popups, keyed like FrontendData::popups (fake addresses 0xF000_0000 + n, never valid EXE addresses).
namespace native_popup {
constexpr uint32_t display_mode = 0xF0000001, window_size = 0xF0000002, scaling = 0xF0000003,
                   filtering = 0xF0000004, on_off = 0xF0000005, touch = 0xF0000006, resolution = 0xF0000007,
                   widescreen = 0xF0000008, visual_style = 0xF0000009, tex_detail = 0xF000000D,
                   enh_filter = 0xF000000A, enh_grade = 0xF000000B, enh_fog = 0xF000000C, net_ai = 0xF0000020,
                   net_cars = 0xF0000021, net_public = 0xF0000022, net_level = 0xF0000023;
}
// Setter ids for native popup choices (negative: never an EXE setter).
namespace native_setter {
constexpr int display_mode = -1, window_size = -2, scaling = -3, filtering = -4, vsync = -5, fps = -6,
              touch_opacity = -7, haptics = -8, render_resolution = -9, widescreen = -10, visual_style = -11,
              enh_interp = -12, enh_persp = -13, enh_colour = -14, enh_filter = -15, enh_fog = -16,
              enh_grade = -17, enh_bloom = -18, enh_vig = -19, enh_mb = -20, enh_wide = -21, tex_detail = -22,
              net_level = -23, net_ai = -24, net_cars = -25, net_public = -26;
}
// Direct-action handler ids (fake addresses, never valid EXE handlers).
namespace native_action {
constexpr uint32_t toggle_mute = 0xF0000010, toggle_swap = 0xF0000011;
constexpr uint32_t toggle_pad_swap = 0xF0000012, toggle_left_handed = 0xF0000013;
constexpr uint32_t pause_resume = 0xF0000014, pause_quit = 0xF0000015;
// Plan 34 Task 7: lobby actions on the net_lobby/net_rooms screens.
constexpr uint32_t net_leave = 0xF0000020, net_copy = 0xF0000021, net_refresh = 0xF0000022,
                   net_code = 0xF0000023, net_spectate = 0xF0000024;
} // namespace native_action
} // namespace rr
