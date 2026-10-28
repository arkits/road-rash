#include "atomic_file.hpp"
#include "settings.hpp"
#include "exe_tables.hpp"
#include <SDL3/SDL.h>
#include <fstream>
#include <sstream>
namespace rr {
namespace {
bool imperial_country(const char* c) {
    // Countries whose LOCALE_IMEASURE is "1" (U.S. system).
    return c && (std::string(c) == "US" || std::string(c) == "LR" || std::string(c) == "MM");
}
bool parse_int(const std::string& v, int lo, int hi, int& out) {
    try {
        size_t used = 0;
        const int x = std::stoi(v, &used);
        if (used != v.size() || x < lo || x > hi) return false;
        out = x;
        return true;
    } catch (...) {
        return false;
    }
}
} // namespace

bool operator==(const Enhancements& a, const Enhancements& b) {
    return a.interpolate == b.interpolate && a.perspective == b.perspective &&
           a.full_colour == b.full_colour && a.filter == b.filter && a.fog == b.fog && a.grade == b.grade &&
           a.draw_slices == b.draw_slices && a.bloom == b.bloom && a.vignette == b.vignette &&
           a.motion_blur == b.motion_blur;
}
bool Enhancements::any() const {
    return !(*this == Enhancements{});
}
Enhancements enhanced_preset() {
    // Plan 21 Task 9: measured on Apple M3 Pro at render scale 2 (all-on
    // 1.67 ms/tick vs 1.22 off; budget 8.3 ms). draw_slices stays 33 (Task 6
    // dropped); full_colour is implied by the filter.
    Enhancements e;
    e.interpolate = true;
    e.perspective = true;
    e.filter = TextureFilter::trilinear;
    e.fog = 40;
    e.grade = Grade::dusk;
    e.bloom = true;
    e.vignette = true;
    e.motion_blur = true;
    return e;
}

Settings default_settings() {
    Settings s;
    int count = 0;
    SDL_Locale** locales = SDL_GetPreferredLocales(&count);
    if (locales && count > 0 && locales[0]) {
        if (locales[0]->language) s.language = language_from_code(locales[0]->language);
        s.metric =
            locales[0]->country ? !imperial_country(locales[0]->country) : s.language != Language::english;
    }
    SDL_free(locales);
#ifdef __ANDROID__
    s.display_mode = DisplayMode::fullscreen;
#endif
    return s;
}

Settings parse_settings(const std::string& text) {
    Settings s = default_settings();
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string k = line.substr(0, eq), v = line.substr(eq + 1);
        int x = 0;
        auto flag = [&](bool& f) {
            if (parse_int(v, 0, 1, x)) f = x != 0;
        };
        if (k == "language") {
            if (parse_int(v, 0, 4, x)) s.language = Language(x);
        } else if (k == "metric")
            flag(s.metric);
        else if (k == "hires")
            flag(s.high_resolution);
        else if (k == "inset")
            flag(s.inset);
        else if (k == "hires_bikers")
            flag(s.high_res_bikers);
        else if (k == "video_mode") {
            if (parse_int(v, 0, 2, x)) s.video_mode = x;
        } else if (k == "alternate_artwork")
            flag(s.alternate_artwork);
        else if (k == "mini_dash") {
            if (parse_int(v, 0, 2, x)) s.mini_dash = x;
        } else if (k == "controls") {
            // 44 hex chars (full u16 per action, high byte first); legacy 22-char
            // files carry the low byte only. Malformed input ignored. Unknown codes
            // fall back to that action's keyboard default only: high byte 0 with a
            // low byte outside the joystick set means nothing pressable (plan 18
            // Task 7). Zero stays unbound.
            auto hex = [](char c) {
                return c >= '0' && c <= '9'   ? c - '0'
                       : c >= 'a' && c <= 'f' ? c - 'a' + 10
                       : c >= 'A' && c <= 'F' ? c - 'A' + 10
                                              : -1;
            };
            std::array<uint16_t, 11> t{};
            bool ok = false;
            if (v.size() == 44) {
                ok = true;
                for (size_t i = 0; i < 11 && ok; ++i) {
                    int h[4] = {hex(v[4 * i]), hex(v[4 * i + 1]), hex(v[4 * i + 2]), hex(v[4 * i + 3])};
                    if (h[0] < 0 || h[1] < 0 || h[2] < 0 || h[3] < 0)
                        ok = false;
                    else
                        t[i] = uint16_t(h[0] * 4096 + h[1] * 256 + h[2] * 16 + h[3]);
                }
            } else if (v.size() == 22) {
                ok = true;
                for (size_t i = 0; i < 11 && ok; ++i) {
                    int hi = hex(v[2 * i]), lo = hex(v[2 * i + 1]);
                    if (hi < 0 || lo < 0)
                        ok = false;
                    else
                        t[i] = uint16_t(hi * 16 + lo);
                }
            }
            if (ok) {
                static constexpr uint8_t kJoy[] = {0x00, 0x01, 0x02, 0x25, 0x26, 0x27, 0x28};
                const auto known_joy = [&](uint8_t b) {
                    for (uint8_t j : kJoy)
                        if (j == b) return true;
                    return false;
                };
                const auto kb = default_keyboard_controls();
                for (size_t i = 0; i < 11; ++i)
                    if (t[i] != 0 && (t[i] >> 8) == 0 && !known_joy(uint8_t(t[i]))) t[i] = kb[i];
                s.controls = t;
                s.custom_controls = true;
            }
        } else if (k == "music_group") {
            if (parse_int(v, 0, 31, x)) s.music_group = x;
        } else if (k == "music_track") {
            if (parse_int(v, 0, 31, x)) s.music_track = x;
        } else if (k == "master_volume") {
            if (parse_int(v, 0, 100, x)) s.master_volume = x;
        } else if (k == "music_volume") {
            if (parse_int(v, 0, 100, x)) s.music_volume = x;
        } else if (k == "effects_volume") {
            if (parse_int(v, 0, 100, x)) s.effects_volume = x;
        } else if (k == "engine_volume") {
            if (parse_int(v, 0, 100, x)) s.engine_volume = x;
        } else if (k == "others_volume") {
            if (parse_int(v, 0, 100, x)) s.others_volume = x;
        } else if (k == "siren_volume") {
            if (parse_int(v, 0, 100, x)) s.siren_volume = x;
        } else if (k == "interface_volume") {
            if (parse_int(v, 0, 100, x)) s.interface_volume = x;
        } else if (k == "mute")
            flag(s.mute);
        else if (k == "swap_speakers")
            flag(s.swap_speakers);
        else if (k == "display_mode") {
            if (parse_int(v, 0, 1, x)) s.display_mode = DisplayMode(x);
        } else if (k == "window_scale") {
            if (parse_int(v, 1, 4, x)) s.window_scale = x;
        } else if (k == "scale_mode") {
            if (parse_int(v, 0, 2, x)) s.scale_mode = ScaleMode(x);
        } else if (k == "filter") {
            if (parse_int(v, 0, 1, x)) s.filter = FilterMode(x);
        } else if (k == "vsync")
            flag(s.vsync);
        else if (k == "network_hud")
            flag(s.network_hud);
        else if (k == "smooth_remote")
            flag(s.smooth_remote);
        else if (k == "show_fps")
            flag(s.show_fps);
        else if (k == "render_scale") {
            if (parse_int(v, 0, 4, x)) s.render_scale = x;
        } else if (k == "widescreen") {
            if (parse_int(v, 0, 3, x)) s.widescreen = Widescreen(x);
        } else if (k == "texture_detail") {
            if (parse_int(v, 0, 1, x)) s.texture_detail = x;
        } else if (k == "enh_interpolate")
            flag(s.enhancements.interpolate);
        else if (k == "enh_perspective")
            flag(s.enhancements.perspective);
        else if (k == "enh_full_colour")
            flag(s.enhancements.full_colour);
        else if (k == "enh_filter") {
            if (parse_int(v, 0, 2, x)) s.enhancements.filter = TextureFilter(x);
        } else if (k == "enh_fog") {
            if (parse_int(v, 0, 100, x)) s.enhancements.fog = x;
        } else if (k == "enh_grade") {
            if (parse_int(v, 0, 3, x)) s.enhancements.grade = Grade(x);
        } else if (k == "enh_draw_slices") {
            if (v == "33" || v == "48" || v == "64") s.enhancements.draw_slices = std::stoi(v);
        } else if (k == "enh_bloom")
            flag(s.enhancements.bloom);
        else if (k == "enh_vignette")
            flag(s.enhancements.vignette);
        else if (k == "enh_motion_blur")
            flag(s.enhancements.motion_blur);
        else if (k == "enh_widescreen") {
            if (parse_int(v, 0, 3, x)) s.widescreen = Widescreen(x);
        } // migrated: single setting
        else if (k == "touch_opacity") {
            if (parse_int(v, 0, 100, x)) s.touch_opacity = x;
        } else if (k == "haptics")
            flag(s.haptics);
        else if (k == "swap_punch_kick")
            flag(s.swap_punch_kick);
        else if (k == "stick_deadzone") {
            if (parse_int(v, 10, 40, x)) s.stick_deadzone = x;
        } else if (k == "left_handed")
            flag(s.left_handed);
        else if (k == "alias") {
            if (v.size() < 20) s.alias = v;
        } else if (k.size() == 10 && k.substr(0, 9) == "net_chat_" && k.back() >= '1' && k.back() <= '8') {
            if (v.size() <= 60) s.net_chat[size_t(k.back() - '1')] = v;
        } else if (k == "net_server") {
            if (!v.empty() && v.size() < 128) s.net_server = v;
        } else if (k == "net_seat") {
            // 32 hex chars; anything else is ignored (never a half token).
            bool hex = v.size() == 32;
            for (char c : v)
                hex &= (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
            if (hex) s.net_seat = v;
        } else if (k == "net_room") {
            if (v.size() == 6) s.net_room = v;
        }
    }
    return s;
}

std::string format_settings(const Settings& s) {
    std::ostringstream o;
    o << "language=" << int(s.language) << "\nmetric=" << s.metric << "\nhires=" << s.high_resolution
      << "\ninset=" << s.inset << "\nhires_bikers=" << s.high_res_bikers << "\nvideo_mode=" << s.video_mode
      << "\nalternate_artwork=" << s.alternate_artwork << "\nmini_dash=" << s.mini_dash
      << "\nopponent_list=" << s.opponent_list << "\nmusic_group=" << s.music_group;
    if (s.custom_controls) {
        // Full u16 per action (4 hex chars, high byte first). The pre-Task-7
        // writer emitted only the low byte (22 chars); the reader still takes it.
        o << "\ncontrols=";
        for (uint16_t c : s.controls)
            o << "0123456789abcdef"[(c >> 12) & 15] << "0123456789abcdef"[(c >> 8) & 15]
              << "0123456789abcdef"[(c >> 4) & 15] << "0123456789abcdef"[c & 15];
    }
    o << "\nmusic_track=" << s.music_track << "\nmaster_volume=" << s.master_volume
      << "\nmusic_volume=" << s.music_volume << "\neffects_volume=" << s.effects_volume
      << "\nengine_volume=" << s.engine_volume << "\nothers_volume=" << s.others_volume
      << "\nsiren_volume=" << s.siren_volume << "\ninterface_volume=" << s.interface_volume
      << "\nmute=" << s.mute << "\nswap_speakers=" << s.swap_speakers
      << "\ndisplay_mode=" << int(s.display_mode) << "\nwindow_scale=" << s.window_scale
      << "\nscale_mode=" << int(s.scale_mode) << "\nfilter=" << int(s.filter) << "\nvsync=" << s.vsync
      << "\nnetwork_hud=" << s.network_hud << "\nsmooth_remote=" << s.smooth_remote
      << "\nshow_fps=" << s.show_fps << "\nrender_scale=" << s.render_scale
      << "\nwidescreen=" << int(s.widescreen) << "\ntexture_detail=" << s.texture_detail
      << "\nenh_interpolate=" << s.enhancements.interpolate
      << "\nenh_perspective=" << s.enhancements.perspective
      << "\nenh_full_colour=" << s.enhancements.full_colour << "\nenh_filter=" << int(s.enhancements.filter)
      << "\nenh_fog=" << s.enhancements.fog << "\nenh_grade=" << int(s.enhancements.grade)
      << "\nenh_draw_slices=" << s.enhancements.draw_slices << "\nenh_bloom=" << s.enhancements.bloom
      << "\nenh_vignette=" << s.enhancements.vignette << "\nenh_motion_blur=" << s.enhancements.motion_blur
      << "\ntouch_opacity=" << s.touch_opacity << "\nhaptics=" << s.haptics
      << "\nswap_punch_kick=" << s.swap_punch_kick << "\nstick_deadzone=" << s.stick_deadzone
      << "\nleft_handed=" << s.left_handed << "\nalias=" << s.alias << "\nnet_server=" << s.net_server
      << "\nnet_seat=" << s.net_seat << "\nnet_room=" << s.net_room << "\n";
    for (size_t i = 0; i < s.net_chat.size(); ++i) o << "net_chat_" << i + 1 << "=" << s.net_chat[i] << "\n";
    return o.str();
}

std::array<uint16_t, 11> default_keyboard_controls() {
    // 0x00466618 in the reference EXE, little-endian u16 words.
    return {0x9400, 0x9600, 0x9300, 0x8400, 0x6e00, 0x8500, 0x0d00, 0x2000, 0x6400, 0x9500, 0x8100};
}

std::array<uint16_t, 11> default_joystick_controls() {
    // 0x00466630 in the reference EXE (POV/axis codes 0x25-0x28, buttons 0x01/0x02).
    return {0x0025, 0x0027, 0x0028, 0x9300, 0x6e00, 0x0001, 0x0002, 0x2000, 0x9400, 0x0026, 0x9500};
}

void apply_rebind(std::array<uint16_t, 11>& table, size_t action, uint16_t code) {
    // 0x0040fbf0 scans all 11 rows; any other row holding the same 2-byte code
    // is zeroed (unbound, NOT swapped), then the new code is stored. Rows 9-10
    // and 2-3 get extra label-refresh side effects only; bindings behave the same.
    if (action >= table.size()) throw std::runtime_error("Rebind action out of range");
    for (size_t i = 0; i < table.size(); ++i)
        if (i != action && table[i] == code) table[i] = 0;
    table[action] = code;
}

void read_control_tables(const std::vector<uint8_t>& exe, std::array<uint16_t, 11>& keyboard,
                         std::array<uint16_t, 11>& joystick) {
    const auto kb = read_exe_data(exe, 0x00466618, 22);
    const auto joy = read_exe_data(exe, 0x00466630, 22);
    for (size_t i = 0; i < 11; ++i) {
        keyboard[i] = uint16_t(kb[2 * i] | kb[2 * i + 1] << 8);
        joystick[i] = uint16_t(joy[2 * i] | joy[2 * i + 1] << 8);
    }
}

Settings load_settings(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return default_settings();
    std::ostringstream text;
    text << in.rdbuf();
    return parse_settings(text.str());
}

bool save_settings(const std::string& path, const Settings& s) {
    const std::string text = format_settings(s);
    return write_file_atomic(path, text.data(), text.size());
}
} // namespace rr
