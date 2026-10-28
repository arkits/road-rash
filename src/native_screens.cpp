#include "native_screens.hpp"
#include "career.hpp"
#include "frontend.hpp"
#include "native_strings.hpp"
#include <algorithm>

namespace rr {
namespace {
// A native value row: caption + description slots, opens a native popup.
MenuItem value_item(const MenuItem& t, int y, int caption, int desc, uint32_t popup) {
    MenuItem m = t;
    m.y = y;
    m.target = int(popup); // >= 0xF0000000: Frontend::activate opens the popup
    m.caption = caption;
    m.handler = 0;
    m.desc = desc;
    m.format = t.format;
    // Review 2026-10-13 item 3: no title_desc — the focused row's long help
    // must not become the 72-pt title (original rows never set it either).
    m.flags = item_flag::description;
    m.enabled = true;
    return m;
}
// One popup choice row copied from the template choice. The nav chain is
// rewired for the new table: template rows carry the original popup's links.
MenuItem choice_item(const MenuItem& t, int y, int target, int caption, size_t index, size_t count) {
    MenuItem m = t;
    m.y = y;
    m.target = target; // 1-based choice for the 0x00445440 confirm handler
    m.caption = caption;
    m.handler = 0x00445440;
    m.enabled = true;
    m.nav[2] = uint8_t((index + count - 1) % count);
    m.nav[3] = uint8_t((index + 1) % count);
    m.nav[4] = uint8_t((index + 1) % count);
    m.nav[5] = uint8_t((index + count - 1) % count);
    return m;
}
std::vector<MenuItem> build_popup(const std::vector<MenuItem>& tpl, int header_caption,
                                  const std::vector<std::pair<int, int>>& choices) {
    std::vector<MenuItem> out;
    MenuItem header = tpl[0];
    header.caption = header_caption;
    header.y = 302;
    header.h = 28;
    out.push_back(header);
    for (size_t i = 0; i < choices.size(); ++i) {
        const MenuItem& t = tpl[1];
        MenuItem choice =
            choice_item(t, 334 + int(i) * 28, choices[i].first, choices[i].second, i + 1, choices.size() + 1);
        choice.h = 26;
        out.push_back(choice);
    }
    return out;
}
} // namespace

namespace {
// Review 2026-10-13 item 3: rows fit in 480 px. step shrinks to fit the row
// count; h shrinks with it (24-pt rows need ~28 px, like the restroom).
int fit_step(int tpl_h, int tpl_y, size_t rows) {
    if (rows == 0) return tpl_h + 4;
    const int room = (480 - tpl_y - 8) / int(rows);
    const int step = std::min(tpl_h + 4, room);
    return std::max(20, step);
}
// Original rows never carry title_desc (the screen title persists); strip it
// from native rows so long help text can't become the banner.
void strip_title_desc(MenuItem& m) {
    m.flags &= ~item_flag::title_desc;
}
} // namespace

void add_native_screens(FrontendData& data, bool mobile) {
    const MenuItem& tpl = data.screens[0x13].items.front();
    const int step = tpl.h + 4;
    // Display screen rows (desktop): mode, size, scaling, filtering, vsync, fps, back.
    // Mobile: scaling, filtering, fps, touch, haptics, back (no mode/size; compositor owns vsync).
    struct Row {
        int caption, desc;
        uint32_t popup;
    };
    std::vector<Row> rows;
    if (!mobile) {
        rows.push_back({ns::display_mode, ns::d_display_mode, native_popup::display_mode});
        rows.push_back({ns::window_size, ns::d_window_size, native_popup::window_size});
    }
    rows.push_back({ns::scaling, ns::d_scaling, native_popup::scaling});
    rows.push_back({ns::filtering, ns::d_filtering, native_popup::filtering});
    // Plan 19 Task 8: RENDER RESOLUTION row (both desktop and mobile).
    rows.push_back({ns::render_resolution, ns::d_render_resolution, native_popup::resolution});
    // Plan 21 Task 8: WIDESCREEN row (both desktop and mobile).
    rows.push_back({ns::widescreen, ns::d_widescreen, native_popup::widescreen});
    // Plan 21 Task 9: VISUAL STYLE row (Original / Enhanced / Custom).
    rows.push_back({ns::visual_style, ns::d_visual_style, native_popup::visual_style});
    // Plan 20 Task 3: TEXTURE DETAIL row (Original / Enhanced).
    rows.push_back({ns::tex_detail, ns::d_tex_detail, native_popup::tex_detail});
    if (!mobile) {
        rows.push_back({ns::vsync, ns::d_vsync, native_popup::on_off});
        rows.push_back({ns::fps, ns::d_fps, native_popup::on_off});
    } else {
        rows.push_back({ns::fps, ns::d_fps, native_popup::on_off});
        rows.push_back({ns::touch_opacity, ns::d_touch, native_popup::touch});
        rows.push_back({ns::haptics, ns::d_haptics, native_popup::on_off});
    }
    MenuScreen& screen = data.screens[screen_id::native_display];
    screen.title = ns::display_title;
    screen.title_format = data.screens[0x13].title_format;
    screen.items.clear();
    {
        // Review 2026-10-13 item 3: 11 rows fit in 480 px.
        const int dstep = fit_step(tpl.h, tpl.y, rows.size() + 1);
        const int dh = std::min(tpl.h, dstep - 2);
        for (size_t i = 0; i < rows.size(); ++i) {
            MenuItem m =
                value_item(tpl, tpl.y + int(i) * dstep, rows[i].caption, rows[i].desc, rows[i].popup);
            m.h = dh;
            screen.items.push_back(m);
        }
        MenuItem back = tpl;
        back.y = tpl.y + int(rows.size()) * dstep;
        back.h = dh;
        back.target = 0x13;
        back.caption = ns::back;
        back.handler = 0;
        back.desc = 0;
        back.flags = item_flag::escape;
        back.enabled = true;
        screen.items.push_back(back);
    }
    const size_t n = screen.items.size();
    for (size_t i = 0; i < n; ++i) { // vertical chain, wrapping
        strip_title_desc(screen.items[i]);
        screen.items[i].nav[2] = uint8_t((i + n - 1) % n);
        screen.items[i].nav[3] = uint8_t((i + 1) % n);
        screen.items[i].nav[4] = uint8_t((i + 1) % n);
        screen.items[i].nav[5] = uint8_t((i + n - 1) % n);
    }
    // Native popups from the resolution popup template (header + choice rows).
    // Reuse its styling with uniform, bounded rows (resolution needs five).
    const auto& rtpl = data.popups.at(0x00474240);
    data.popups[native_popup::display_mode] =
        build_popup(rtpl, ns::display_mode, {{1, ns::windowed}, {2, ns::fullscreen}});
    data.popups[native_popup::window_size] = build_popup(
        rtpl, ns::window_size, {{1, ns::scale_1x}, {2, ns::scale_2x}, {3, ns::scale_3x}, {4, ns::scale_4x}});
    data.popups[native_popup::scaling] =
        build_popup(rtpl, ns::scaling, {{1, ns::fit}, {2, ns::integer}, {3, ns::stretch}});
    data.popups[native_popup::filtering] =
        build_popup(rtpl, ns::filtering, {{1, ns::sharp}, {2, ns::smooth}});
    data.popups[native_popup::on_off] = build_popup(rtpl, ns::fps, {{1, ns::off}, {2, ns::on}});
    data.popups[native_popup::touch] = build_popup(
        rtpl, ns::touch_opacity,
        {{1, ns::touch_0}, {2, ns::touch_25}, {3, ns::touch_50}, {4, ns::touch_75}, {5, ns::touch_100}});
    // Plan 19 Task 8: match + 1x..4x. Choice index maps to render_scale - 1 (see the setter).
    data.popups[native_popup::resolution] = build_popup(
        rtpl, ns::render_resolution,
        {{1, ns::res_match}, {2, ns::scale_1x}, {3, ns::scale_2x}, {4, ns::scale_3x}, {5, ns::scale_4x}});
    // Plan 21 Task 8: 4:3 + 16:10/16:9/match.
    data.popups[native_popup::widescreen] =
        build_popup(rtpl, ns::widescreen,
                    {{1, ns::wide_off}, {2, ns::wide_16x10}, {3, ns::wide_16x9}, {4, ns::wide_match}});
    // Plan 20 Task 3: Original / Enhanced.
    data.popups[native_popup::tex_detail] =
        build_popup(rtpl, ns::tex_detail, {{1, ns::tex_original}, {2, ns::tex_enhanced}});
    // Plan 21 Task 9: visual style + per-effect popups.
    data.popups[native_popup::visual_style] = build_popup(
        rtpl, ns::visual_style, {{1, ns::style_original}, {2, ns::style_enhanced}, {3, ns::style_custom}});
    data.popups[native_popup::enh_filter] = build_popup(
        rtpl, ns::enh_filter, {{1, ns::filt_nearest}, {2, ns::filt_bilinear}, {3, ns::filt_trilinear}});
    data.popups[native_popup::enh_grade] =
        build_popup(rtpl, ns::enh_grade,
                    {{1, ns::grade_none}, {2, ns::grade_warm}, {3, ns::grade_vivid}, {4, ns::grade_dusk}});
    data.popups[native_popup::enh_fog] = build_popup(
        rtpl, ns::touch_opacity,
        {{1, ns::touch_0}, {2, ns::touch_25}, {3, ns::touch_50}, {4, ns::touch_75}, {5, ns::touch_100}});
    // Enhancements screen (plan 21 Task 9): one row per Enhancements field +
    // Back to the Display screen. Toggle rows use the on/off popup; filter and
    // grade have their own; fog reuses the 0-100 touch popup; widescreen reuses
    // its popup. Built like the Display screen (same template/column).
    {
        MenuScreen& enh = data.screens[screen_id::native_enhancements];
        enh.title = ns::enh_title;
        enh.title_format = data.screens[0x13].title_format;
        enh.items.clear();
        const std::pair<int, uint32_t> erows[] = {
            {ns::enh_interp, native_popup::on_off}, {ns::enh_persp, native_popup::on_off},
            {ns::enh_colour, native_popup::on_off}, {ns::enh_filter, native_popup::enh_filter},
            {ns::enh_fog, native_popup::enh_fog},   {ns::enh_grade, native_popup::enh_grade},
            {ns::enh_bloom, native_popup::on_off},  {ns::enh_vig, native_popup::on_off},
            {ns::enh_mb, native_popup::on_off},     {ns::enh_wide, native_popup::widescreen},
        };
        {
            // Review 2026-10-13 item 3: 11 rows fit in 480 px.
            const int estep = fit_step(tpl.h, tpl.y, 12);
            const int eh = std::min(tpl.h, estep - 2);
            for (size_t i = 0; i < 10; ++i) {
                MenuItem m =
                    value_item(tpl, tpl.y + int(i) * estep, erows[i].first, ns::d_enh, erows[i].second);
                m.h = eh;
                enh.items.push_back(m);
            }
            MenuItem back = tpl;
            back.y = tpl.y + 10 * estep;
            back.h = eh;
            back.target = screen_id::native_display;
            back.caption = ns::back;
            back.handler = 0;
            back.desc = 0;
            back.flags = item_flag::escape;
            back.enabled = true;
            enh.items.push_back(back);
        }
        const size_t n = enh.items.size();
        for (size_t i = 0; i < n; ++i) {
            strip_title_desc(enh.items[i]);
            enh.items[i].nav[2] = uint8_t((i + n - 1) % n);
            enh.items[i].nav[3] = uint8_t((i + 1) % n);
            enh.items[i].nav[4] = uint8_t((i + 1) % n);
            enh.items[i].nav[5] = uint8_t((i + n - 1) % n);
        }
    }
    // Sound screen (plan 18 Task 6): dialog-300 order — master + six channels as sliders,
    // mute + swap as direct toggles, Back to the restroom. Same button template/column.
    {
        const int caps[7] = {ns::master, ns::music,  ns::effects,  ns::engines,
                             ns::others, ns::sirens, ns::interface};
        MenuScreen& sound = data.screens[screen_id::native_sound];
        sound.title = ns::sound_title;
        sound.title_format = data.screens[0x13].title_format;
        sound.items.clear();
        // Review 2026-10-13 item 3: 10 rows fit in 480 px.
        const int sstep = fit_step(tpl.h, tpl.y, 10);
        const int sh = std::min(tpl.h, sstep - 2);
        for (int i = 0; i < 7; ++i) {
            MenuItem m = tpl;
            m.y = tpl.y + i * sstep;
            m.h = sh;
            m.caption = caps[i];
            m.handler = 0;
            m.target = 0;
            m.desc = 0;
            m.flags = item_flag::native_slider;
            m.enabled = true;
            sound.items.push_back(m);
        }
        const std::pair<int, uint32_t> toggles[2] = {{ns::mute, native_action::toggle_mute},
                                                     {ns::swap, native_action::toggle_swap}};
        for (int i = 0; i < 2; ++i) {
            MenuItem m = tpl;
            m.y = tpl.y + (7 + i) * sstep;
            m.h = sh;
            m.caption = toggles[i].first;
            m.handler = toggles[i].second;
            m.target = 0;
            m.desc = 0;
            m.flags = 0;
            m.enabled = true;
            sound.items.push_back(m);
        }
        MenuItem back = tpl;
        back.y = tpl.y + 9 * sstep;
        back.h = sh;
        back.target = 0x10;
        back.caption = ns::back;
        back.handler = 0;
        back.desc = 0;
        back.flags = item_flag::escape;
        back.enabled = true;
        sound.items.push_back(back);
        const size_t n = sound.items.size();
        for (size_t i = 0; i < n; ++i) {
            strip_title_desc(sound.items[i]);
            sound.items[i].nav[2] = uint8_t((i + n - 1) % n);
            sound.items[i].nav[3] = uint8_t((i + 1) % n);
            sound.items[i].nav[4] = uint8_t((i + 1) % n);
            sound.items[i].nav[5] = uint8_t((i + n - 1) % n);
        }
    }
    // Gamepad page (plan 18 Task 7): fixed-mapping info rows (disabled), swap toggle,
    // dead-zone slider, Back to Configure Input. Touch page: opacity popup, handedness, Back.
    auto column = [&](MenuScreen& screen, int title) {
        screen.title = title;
        screen.title_format = data.screens[0x13].title_format;
        screen.items.clear();
    };
    auto chain = [&](MenuScreen& screen) {
        const size_t n = screen.items.size();
        for (size_t i = 0; i < n; ++i) {
            strip_title_desc(screen.items[i]);
            screen.items[i].nav[2] = uint8_t((i + n - 1) % n);
            screen.items[i].nav[3] = uint8_t((i + 1) % n);
            screen.items[i].nav[4] = uint8_t((i + 1) % n);
            screen.items[i].nav[5] = uint8_t((i + n - 1) % n);
        }
    };
    auto button = [&](int y, int caption, int desc, uint32_t handler, int target, uint32_t flags) {
        MenuItem m = tpl;
        m.y = y;
        m.caption = caption;
        m.desc = desc;
        m.handler = handler;
        m.target = target;
        m.flags = flags & ~item_flag::title_desc; // item 3: screen title persists
        m.enabled = true;
        return m;
    };
    {
        MenuScreen& pad = data.screens[screen_id::native_gamepad];
        column(pad, ns::gamepad_title);
        const int maps[4] = {ns::pad_map1, ns::pad_map2, ns::pad_map3, ns::pad_map4};
        for (int i = 0; i < 4; ++i) {
            MenuItem m =
                button(tpl.y + i * step, maps[i], 0, 0, 0, item_flag::title_desc | item_flag::description);
            m.enabled = false; // info rows: visible, never focused
            pad.items.push_back(m);
        }
        pad.items.push_back(button(tpl.y + 4 * step, ns::pad_swap, ns::d_swap, native_action::toggle_pad_swap,
                                   0, item_flag::title_desc));
        MenuItem dz = button(tpl.y + 5 * step, ns::pad_deadzone, ns::d_deadzone, 0, 0,
                             item_flag::title_desc | item_flag::native_slider);
        dz.desc = ns::d_deadzone;
        pad.items.push_back(dz);
        pad.items.push_back(
            button(tpl.y + 6 * step, ns::back, 0, 0, 0x12, item_flag::title_desc | item_flag::escape));
        chain(pad);
    }
    {
        MenuScreen& touch = data.screens[screen_id::native_touch];
        column(touch, ns::touch_title);
        touch.items.push_back(button(tpl.y, ns::touch_opacity, ns::d_touch, 0, int(native_popup::touch),
                                     item_flag::title_desc | item_flag::description));
        touch.items.push_back(button(tpl.y + step, ns::touch_left, ns::d_lefthand,
                                     native_action::toggle_left_handed, 0, item_flag::title_desc));
        touch.items.push_back(
            button(tpl.y + 2 * step, ns::back, 0, 0, 0x12, item_flag::title_desc | item_flag::escape));
        chain(touch);
    }
    // Pause menu (plan 18 Task 8): RESUME, SOUND, DISPLAY, CONTROLS, QUIT RACE.
    // Resume carries the escape flag so Back/B/Escape resumes the race.
    {
        MenuScreen& pause = data.screens[screen_id::native_pause];
        column(pause, ns::pause_title);
        pause.items.push_back(button(tpl.y, ns::resume, 0, native_action::pause_resume, 0,
                                     item_flag::title_desc | item_flag::escape));
        pause.items.push_back(
            button(tpl.y + step, ns::sound, 0, 0, screen_id::native_sound, item_flag::title_desc));
        pause.items.push_back(
            button(tpl.y + 2 * step, ns::display, 0, 0, screen_id::native_display, item_flag::title_desc));
        pause.items.push_back(button(tpl.y + 3 * step, ns::controls, 0, 0, 0x12, item_flag::title_desc));
        pause.items.push_back(button(tpl.y + 4 * step, ns::quit_race, ns::d_quit_race,
                                     native_action::pause_quit, 0,
                                     item_flag::title_desc | item_flag::description));
        chain(pause);
    }
    // Configure Input links: appended after the label rows so label indices keep
    // working; nav extended from the last label row through the new rows.
    {
        auto& items = data.screens[0x12].items;
        MenuItem gp = tpl;
        gp.y = tpl.y + 8 * step;
        gp.caption = ns::gamepad_title;
        gp.handler = 0;
        gp.target = screen_id::native_gamepad;
        gp.desc = 0;
        gp.flags = item_flag::title_desc | item_flag::description;
        gp.enabled = true;
        gp.nav[2] = 21;
        gp.nav[5] = 21;
        MenuItem th = gp;
        th.y = tpl.y + 9 * step;
        th.caption = ns::touch_title;
        th.target = screen_id::native_touch;
        th.nav[2] = 22;
        th.nav[5] = 22;
        if (mobile) {
            gp.nav[3] = 23;
            gp.nav[4] = 23;
            th.nav[3] = 0;
            th.nav[4] = 0;
            items.push_back(gp);
            items.push_back(th);
        } else {
            gp.nav[3] = 0;
            gp.nav[4] = 0;
            items.push_back(gp);
        }
        items[21].nav[3] = 22;
        items[21].nav[4] = 22;
        items[0].nav[2] = uint8_t(items.size() - 1);
        items[0].nav[5] = uint8_t(items.size() - 1);
    }
    // nav chain already routes 1->2->3).
    MenuItem& link = data.screens[0x13].items[2];
    link.target = screen_id::native_display;
    link.caption = ns::display;
    link.handler = 0;
    link.desc = ns::d_display_mode;
    link.flags = item_flag::description;
    link.enabled = true;
    // Plan 34 Task 7: host option popups in the original popup style, opened
    // over screen 0x0b. Level takes the EXE level popup's captions and targets
    // (Level 1-5) with its own setter (the career setter also resets the
    // career; the host setter only stages the room options). The original
    // Host opened a transport popup there instead, so the EXE level popup's
    // own rows (y 300-474) would cover Chat Messages/Restroom/Disconnect.
    {
        std::vector<std::pair<int, int>> levels;
        for (size_t i = 1; i < data.popups.at(0x004736a0).size(); ++i)
            levels.emplace_back(data.popups.at(0x004736a0)[i].target, data.popups.at(0x004736a0)[i].caption);
        data.popups[native_popup::net_level] = build_popup(rtpl, 414, levels); // "Level" (room list column)
        data.popups[native_popup::net_ai] = build_popup(
            rtpl, ns::net_ai_bikers, {{1, ns::net_ai_none}, {2, ns::net_ai_limited}, {3, ns::net_ai_full}});
        data.popups[native_popup::net_cars] =
            build_popup(rtpl, ns::net_cars, {{1, ns::net_cars_on}, {2, ns::net_cars_off}});
        data.popups[native_popup::net_public] =
            build_popup(rtpl, ns::net_visibility, {{1, ns::net_public}, {2, ns::net_unlisted}});
        // Lift each popup so its last row ends above the first menu row of
        // screen 0x0b that it would overlap horizontally.
        for (uint32_t id : {native_popup::net_level, native_popup::net_ai, native_popup::net_cars,
                            native_popup::net_public}) {
            auto& popup = data.popups[id];
            int bottom = 480;
            for (const MenuItem& m : data.screens[0x0b].items)
                if (m.y > popup.front().y && m.x < popup.front().x + popup.front().w &&
                    m.x + m.w > popup.front().x)
                    bottom = std::min(bottom, m.y - 4);
            const int lift = std::max(0, popup.back().y + popup.back().h - bottom);
            for (MenuItem& row : popup) row.y -= lift;
        }
        for (MenuItem& m : data.screens[0x0b].items)
            if (m.handler == 0x0042fc40) m.desc = ns::d_net_host;
    }
    // Plan 34 Task 7: online lobby screens from the 0x13 template column.
    // Captions are scratch slots filled per entry (prepare_screen in
    // frontend.cpp); layout follows the Display screen so rows fit 640x480.
    {
        MenuScreen& lobby = data.screens[screen_id::net_lobby];
        lobby.title_format = data.screens[0x13].title_format;
        lobby.items.clear();
        const int lstep = fit_step(tpl.h, tpl.y, 12);
        const int lh = std::min(tpl.h, lstep - 2);
        for (int i = 0; i < 8; ++i) {
            MenuItem m = tpl;
            m.y = tpl.y + i * lstep;
            m.h = lh;
            m.caption = 1040 + i; // roster scratch, filled per entry
            m.handler = 0;
            m.target = 0;
            m.desc = 0;
            m.flags = item_flag::description;
            m.enabled = false; // info rows: visible, never focused
            lobby.items.push_back(m);
        }
        const std::pair<int, std::pair<uint32_t, int>> actions[4] = {
            {ns::net_vote, {0, 0x0c}},
            {ns::net_chat, {0, 0x0d}},
            {ns::net_copy_code, {native_action::net_copy, 0}},
            {ns::net_leave, {native_action::net_leave, 0}},
        };
        for (int i = 0; i < 4; ++i) {
            MenuItem m =
                button(tpl.y + (8 + i) * lstep, actions[i].first, ns::d_net_lobby, actions[i].second.first,
                       actions[i].second.second, item_flag::title_desc | item_flag::description);
            m.h = lh;
            if (i >= 2) m.target = 0;
            lobby.items.push_back(m);
        }
        chain(lobby);
    }
    {
        MenuScreen& rooms = data.screens[screen_id::net_rooms];
        rooms.title = ns::net_rooms_title;
        rooms.title_format = data.screens[0x13].title_format;
        rooms.items.clear();
        const int rstep = fit_step(tpl.h, tpl.y, 12);
        const int rh = std::min(tpl.h, rstep - 2);
        for (int i = 0; i < 8; ++i) {
            MenuItem m = tpl;
            m.y = tpl.y + i * rstep;
            m.h = rh;
            m.caption = 1008 + i;   // room-list scratch, filled per entry
            m.handler = 0x00445440; // choice confirm; screen_ routes to join
            m.target = i + 1;
            m.desc = 0;
            m.flags = item_flag::description;
            m.enabled = false; // enabled per entry when a room is shown
            rooms.items.push_back(m);
        }
        MenuItem code = button(tpl.y + 8 * rstep, ns::net_enter_code, ns::d_net_lobby,
                               native_action::net_code, 0, item_flag::title_desc | item_flag::description);
        code.h = rh;
        rooms.items.push_back(code);
        MenuItem spec =
            button(tpl.y + 9 * rstep, ns::net_spectate, ns::d_net_lobby, native_action::net_spectate, 0,
                   item_flag::title_desc | item_flag::description);
        spec.h = rh;
        rooms.items.push_back(spec);
        MenuItem refresh = button(tpl.y + 10 * rstep, ns::net_refresh, 0, native_action::net_refresh, 0, 0);
        refresh.h = rh;
        rooms.items.push_back(refresh);
        MenuItem back =
            button(tpl.y + 11 * rstep, ns::back, 0, 0, 0x0b, item_flag::title_desc | item_flag::escape);
        back.h = rh;
        rooms.items.push_back(back);
        chain(rooms);
    }
}
} // namespace rr
