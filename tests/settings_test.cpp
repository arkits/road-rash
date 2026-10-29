#include "settings.hpp"
#include "view.hpp"
#include <cstdio>
#include <filesystem>
#include <iostream>
#include <unistd.h>

int main() {
    int failures = 0;
    auto check = [&](bool v, const char* what) {
        if (!v) {
            ++failures;
            std::cerr << "FAIL " << what << "\n";
        }
    };
    rr::Settings s;
    s.language = rr::Language::italian;
    s.metric = true;
    s.high_resolution = true;
    s.video_mode = 1;
    s.master_volume = 37;
    s.alias = "Rasher";
    s.alternate_artwork = true;
    const rr::Settings r = rr::parse_settings(rr::format_settings(s));
    check(r.language == s.language && r.metric && r.high_resolution && r.video_mode == 1 &&
              r.master_volume == 37 && r.alias == "Rasher" && r.alternate_artwork,
          "round trip");
    const rr::Settings g = rr::parse_settings(
        "video_mode=9\nmaster_volume=-1\nhires=yes\nnonsense\n=\nalias=" + std::string(40, 'x') + "\n");
    const rr::Settings d = rr::default_settings();
#ifdef __ANDROID__
    check(d.widescreen == rr::Widescreen::match, "Android defaults to display aspect");
#else
    check(d.widescreen == rr::Widescreen::off, "desktop retains original aspect default");
#endif
    check(rr::parse_settings("").widescreen == d.widescreen, "empty settings use platform aspect default");
    check(rr::parse_settings("widescreen=0\n").widescreen == rr::Widescreen::off,
          "saved explicit 4:3 choice is preserved");
    check(g.video_mode == d.video_mode && g.master_volume == d.master_volume &&
              g.high_resolution == d.high_resolution && g.alias.empty(),
          "invalid values ignored");
    const auto path =
        (std::filesystem::temp_directory_path() / ("rr-settings-test-" + std::to_string(::getpid()) + ".txt"))
            .string();
    check(rr::save_settings(path, s) && rr::load_settings(path).alias == "Rasher", "file round trip");
    std::remove(path.c_str());
    check(rr::load_settings(path).video_mode == d.video_mode, "missing file gives defaults");
    {
        // Plan 34 Task 7: lobby persistence round-trips; malformed seat/room
        // values are ignored (never a half token).
        rr::Settings n;
        n.net_server = "ws://127.0.0.1:18080/net";
        n.net_seat = "0123456789abcdef0123456789ABCDEF";
        n.net_room = "AB23CD";
        n.net_chat[0] = "Catch me!";
        n.net_chat[7] = "Last phrase";
        const rr::Settings m = rr::parse_settings(rr::format_settings(n));
        check(m.net_server == n.net_server && m.net_seat == n.net_seat && m.net_room == n.net_room &&
                  m.net_chat == n.net_chat,
              "lobby fields round trip");
        const rr::Settings bad = rr::parse_settings("net_seat=xyz\nnet_room=TOOLONG\nnet_server=\n");
        check(bad.net_seat.empty() && bad.net_room.empty() && bad.net_server == d.net_server,
              "lobby fields reject malformed values");
    }
    {
        rr::Settings n;
        n.display_mode = rr::DisplayMode::fullscreen;
        n.window_scale = 3;
        n.scale_mode = rr::ScaleMode::integer;
        n.filter = rr::FilterMode::smooth;
        n.vsync = false;
        n.show_fps = true;
        n.touch_opacity = 20;
        n.haptics = false;
        const rr::Settings m = rr::parse_settings(rr::format_settings(n));
        check(m.display_mode == rr::DisplayMode::fullscreen && m.window_scale == 3 &&
                  m.scale_mode == rr::ScaleMode::integer && m.filter == rr::FilterMode::smooth && !m.vsync &&
                  m.show_fps && m.touch_opacity == 20 && !m.haptics,
              "native fields round trip");
        const rr::Settings bad = rr::parse_settings(
            "window_scale=99\nscale_mode=7\nfilter=-1\ndisplay_mode=2\ntouch_opacity=101\nvsync=");
        check(bad.window_scale == d.window_scale && bad.scale_mode == d.scale_mode &&
                  bad.filter == d.filter && bad.display_mode == d.display_mode &&
                  bad.touch_opacity == d.touch_opacity && bad.vsync == d.vsync,
              "native fields reject out-of-range values");
        const rr::Settings old =
            rr::parse_settings("hires=1\nmaster_volume=40\n"); // a file written before this task
        check(old.high_resolution && old.master_volume == 40 && old.window_scale == d.window_scale,
              "old files still load");
        // Review Focus 2: hand-edited file with a truncated last line (no '=',
        // no trailing newline) keeps the good keys and ignores the bad line.
        const rr::Settings trunc = rr::parse_settings("show_fps=1\ntouch_opacity=20\nhaptics=1");
        check(trunc.show_fps && trunc.touch_opacity == 20 && trunc.haptics, "unterminated last line parses");
        const rr::Settings cut = rr::parse_settings("show_fps=1\ntouch_opacity=20\nvsync");
        check(cut.show_fps && cut.touch_opacity == 20 && cut.vsync == d.vsync, "truncated last line ignored");
    }
    {
        // Plan 18 Task 7 Step 2: bindings round-trip; an unknown code falls back
        // to that action's default only.
        rr::Settings b;
        b.controls = rr::default_keyboard_controls();
        b.controls[0] = 0x1a00;
        b.custom_controls = true;
        const rr::Settings r = rr::parse_settings(rr::format_settings(b));
        check(r.controls == b.controls && r.custom_controls, "bindings round trip");
        rr::Settings u = rr::parse_settings("controls=fffffffffffffffffffff"); // 21 chars
        check(!u.custom_controls, "malformed controls ignored");
        std::string unknown = "controls=";
        for (int i = 0; i < 11; ++i) unknown += i == 4 ? "00ff" : "0000";
        const rr::Settings f = rr::parse_settings(unknown);
        check(f.controls[4] == rr::default_keyboard_controls()[4], "unknown code falls back per action");
        check(f.controls[0] == 0 && f.controls[5] == 0, "known codes pass through");
    }
    {
        // Plan 18 Task 7: gamepad/touch page fields round-trip with clamping.
        rr::Settings p;
        p.swap_punch_kick = true;
        p.stick_deadzone = 10;
        p.left_handed = true;
        const rr::Settings q = rr::parse_settings(rr::format_settings(p));
        check(q.swap_punch_kick && q.stick_deadzone == 10 && q.left_handed, "page fields round trip");
        const rr::Settings c = rr::parse_settings("stick_deadzone=9\nstick_deadzone=41\n");
        check(c.stick_deadzone == 37, "dead zone rejects out-of-range values");
    }
    {
        // Plan 21 Task 1: enhancement options round-trip; out-of-range falls back.
        // (widescreen maps to the existing Widescreen enum from Task 8.)
        rr::Settings e;
        e.enhancements.interpolate = true;
        e.enhancements.filter = rr::TextureFilter::trilinear;
        e.enhancements.fog = 40;
        e.enhancements.grade = rr::Grade::dusk;
        e.enhancements.draw_slices = 64;
        e.widescreen = rr::Widescreen::mode16x9;
        const rr::Settings f = rr::parse_settings(rr::format_settings(e));
        check(f.enhancements.interpolate && f.enhancements.filter == rr::TextureFilter::trilinear &&
                  f.enhancements.fog == 40 && f.enhancements.grade == rr::Grade::dusk &&
                  f.enhancements.draw_slices == 64 && f.widescreen == rr::Widescreen::mode16x9,
              "enhancements round trip");
        const rr::Settings g2 =
            rr::parse_settings("enh_draw_slices=40\nenh_fog=101\nenh_filter=3\nenh_widescreen=9\n");
        check(g2.enhancements.draw_slices == 33 && g2.enhancements.fog == 0 &&
                  g2.enhancements.filter == rr::TextureFilter::nearest && g2.widescreen == d.widescreen,
              "only 33/48/64 slices and in-range values accepted");
        check(rr::parse_settings("enh_widescreen=2\n").widescreen == rr::Widescreen::mode16x9,
              "old enh_widescreen migrates");
        check(!rr::default_settings().enhancements.any(), "defaults are the original renderer");
        // Review 2026-10-13 item 1: widescreen is a display choice, independent
        // of Visual Style. Enhanced preset, then Display -> Off, stays 4:3.
        {
            rr::Settings s;
            s.enhancements = rr::enhanced_preset();
            s.widescreen = rr::Widescreen::off;
            check(rr::logical_width_for(s.widescreen) == 320, "Display Off wins over Enhanced preset");
            // Game::logical_width reads Settings::widescreen only (fix in
            // game_types.cpp); Enhancements has no widescreen member left.
            check(rr::logical_width_for(s.widescreen) == 320, "single setting drives the width");
        }
        // Plan 21 Task 9: measured preset (all-on except draw_slices/full_colour).
        const rr::Enhancements preset = rr::enhanced_preset();
        check(preset.interpolate && preset.perspective && preset.filter == rr::TextureFilter::trilinear &&
                  preset.fog == 40 && preset.grade == rr::Grade::dusk && preset.bloom && preset.vignette &&
                  preset.motion_blur && preset.draw_slices == 33 && !preset.full_colour,
              "enhanced preset matches the measured set");
    }
    {
        // Plan 19 Task 8: render_scale round-trips; 5 and -1 fall back to default.
        rr::Settings p;
        p.render_scale = 3;
        const rr::Settings q = rr::parse_settings(rr::format_settings(p));
        check(q.render_scale == 3, "render_scale round trips");
        const rr::Settings bad = rr::parse_settings("render_scale=5\nrender_scale=-1\n");
        check(bad.render_scale == rr::default_settings().render_scale,
              "render_scale rejects out-of-range values");
    }
    std::cout << failures << " failures\n";
    return failures ? 1 : 0;
}
