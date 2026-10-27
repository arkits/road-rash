#include "atomic_file.hpp"
#include <cstdlib>
#include "frontend.hpp"
#include "byte_reader.hpp"
#include <SDL3/SDL_scancode.h>
#include <SDL3/SDL_keyboard.h>
#include "assets.hpp"
#include "race_music.hpp"
#include "exe_tables.hpp"
#include "native_screens.hpp"
#include "native_strings.hpp"
#include "rri.hpp"
#include "view.hpp"
#include <algorithm>
#include <cctype>
#include <iterator>
#include <filesystem>
#include <fstream>
#include <stdexcept>

namespace rr {
// Slider range {min, max, step} per caption (plan 18 Task 7 dead zone differs).
static std::array<int, 3> slider_range(int caption);
namespace {

MenuItem read_item(const std::vector<uint8_t>& exe, uint32_t address) {
    const auto b = read_exe_data(exe, address, 0x38);
    MenuItem m;
    const ByteReader item(b);
    m.target = int32_t(item.le<uint32_t>(4));
    m.caption = int32_t(item.le<uint32_t>(8));
    m.x = int32_t(item.le<uint32_t>(0xc));
    m.y = int32_t(item.le<uint32_t>(0x10));
    m.w = int32_t(item.le<uint32_t>(0x14));
    m.h = int32_t(item.le<uint32_t>(0x18));
    std::copy(b.begin() + 0x1c, b.begin() + 0x24, m.nav.begin());
    m.flags = item.le<uint32_t>(0x24);
    m.enabled = b[0x28] != 0;
    m.handler = item.le<uint32_t>(0x2c);
    m.desc = int32_t(item.le<uint32_t>(0x30));
    m.format = item.le<uint32_t>(0x34);
    return m;
}

std::vector<MenuItem> read_items(const std::vector<uint8_t>& exe, uint32_t address, uint32_t count) {
    if (count > 64) throw std::runtime_error("Implausible menu item count");
    std::vector<MenuItem> items;
    for (uint32_t i = 0; i < count; ++i) items.push_back(read_item(exe, address + i * 0x38));
    return items;
}

// Drawing rectangles are in logical pixels, including the original shadow offset.
// 0x0041fa52..0x0041fab8: ordinary banners use bottom alignment, with a
// six-pixel bottom inset and a sixteen-pixel inset on the picture-facing side.
std::pair<TextRect, unsigned> title_text_layout(TextRect box, uint32_t format, int slot) {
    if (format & 4) return {box, dt::center | dt::bottom | dt::singleline};
    if (format & 1) return {box, dt::center | dt::vcenter | (slot == 0x178 ? dt::wordbreak : dt::singleline)};
    box.bottom -= 6;
    if (format & 2) {
        box.left += 16;
        return {box, dt::bottom | dt::singleline};
    }
    box.right -= 16;
    return {box, dt::right | dt::bottom | dt::singleline};
}

// 0x0042071d..0x00420795: only the fixed rectangle (flag 0x1000) forces
// centering. Other descriptions retain the caller's DrawText alignment.
std::pair<TextRect, unsigned> description_text_layout(TextRect box, uint32_t flags, unsigned format) {
    if (flags & 0x1000) return {box, dt::center | dt::wordbreak};
    const bool wide = (flags & 0x900) != 0;
    box.left += 10;
    box.top += wide ? 2 : 10;
    box.right -= 6;
    box.bottom += wide ? 2 : -6;
    if (format == 0x4852 && !(flags & 0x80)) format = 0x4850;
    if (wide) format |= dt::singleline;
    return {box, format};
}

// Popup tables passed to 0x004224b0 and the setter each popup applies (0x0049e0a8).
enum Setter {
    none = 0,
    set_mode = 1,
    setter_level = 2,
    set_resolution = 3,
    set_biker = 4,
    set_videos = 5,
    set_load = 6
};
constexpr uint32_t popup_mode = 0x004735c0, popup_level = 0x004736a0, popup_name = 0x004737f0,
                   popup_resolution = 0x00474240, popup_biker = 0x00474320, popup_videos = 0x004743c8,
                   popup_jukebox_video = 0x00473fa0;
// Jukebox track-list tables by menu caption 0xe1..0xe6 (0x00445600): address and row count.
constexpr uint32_t jukebox_tracks[6][2] = {{0x00473b38, 3}, {0x00473be0, 2}, {0x00473c50, 4},
                                           {0x00473d30, 5}, {0x00473e48, 3}, {0x00473ef0, 3}};

// Dialog kinds and the action that follows a confirmed "save before continuing?" (0x004450e0).
enum Then { then_none = 0, then_quit = 1, then_load = 2, then_mode = 3, then_characters = 4 };
enum DialogKind { dialog_message = 0, dialog_unsaved = 1, dialog_quit_race = 2 };
// Plan 34 Task 7: lobby dialogs (handled in finish_dialog).
enum DialogNet { dialog_net_error = 3, dialog_net_connecting = 4 };

constexpr uint32_t colour_light = 0xdefff0, colour_grey = 0xa0a0a0, colour_orange = 0xff6432,
                   colour_focus = 0xe1e180, colour_yellow = 0xffff00;
constexpr int dynamic_slot = 1;  // string slot 1 points at 0x00496a18 (0x004239a8)
constexpr int edit_slot = 184;   // name entry caption slot -> 0x004b8430
constexpr int label_slot = 1000; // native labels (save file names)

// Character file names for IMAGES/CHARS (not localised on the disc).
const char* const character_files[8] = {"Axle", "Bose", "Cydney", "Jon", "Pearl", "Rhonda", "Jim", "Mike"};
// 0x0041dbc0 / 0x0041d970 showroom and bike pictures, showroom slot order 0x80..0x8e.
const char* const bike_files[15] = {"Rat/Banzai",     "Rat/Corsair",   "Rat/Kamikaze",   "Rat/Perro",
                                    "Rat/Killer",     "Sport/Diablo",  "Sport/Kamikaze", "Sport/Stiletto",
                                    "Sport/DMG",      "Sport/Perro",   "Super/Banzai",   "Super/Diablo",
                                    "Super/Stiletto", "Super/Corsair", "Super/Kamikaze"};
// 0x004216e0: DrawText format chosen per showroom slot.
constexpr uint32_t showroom_format[15] = {0x4852, 0x4852, 0x4852, 0x4852, 0x4850, 0x4850, 0x4850, 0x4852,
                                          0x4852, 0x4850, 0x4850, 0x4852, 0x4852, 0x4852, 0x4852};

// printf subset used by the original's language strings (%s %d %c %% with width, '*' and '0').
struct Arg {
    bool text;
    long long n;
    std::string s;
};
Arg A(long long n) {
    return {false, n, {}};
}
Arg A(std::string s) {
    return {true, 0, std::move(s)};
}
std::string format_c(const std::string& f, const std::vector<Arg>& args) {
    std::string out;
    size_t next = 0;
    auto take = [&]() -> const Arg* { return next < args.size() ? &args[next++] : nullptr; };
    for (size_t i = 0; i < f.size(); ++i) {
        if (f[i] != '%') {
            out.push_back(f[i]);
            continue;
        }
        if (++i >= f.size()) break;
        if (f[i] == '%') {
            out.push_back('%');
            continue;
        }
        bool zero = false, left = false;
        for (; i < f.size() && (f[i] == '0' || f[i] == '-'); ++i) (f[i] == '0' ? zero : left) = true;
        int width = 0;
        if (i < f.size() && f[i] == '*') {
            const Arg* a = take();
            width = a && !a->text ? int(a->n) : 0;
            ++i;
        }
        for (; i < f.size() && f[i] >= '0' && f[i] <= '9'; ++i) width = width * 10 + (f[i] - '0');
        while (i < f.size() && (f[i] == 'l' || f[i] == 'h')) ++i;
        if (i >= f.size()) break;
        std::string piece;
        const Arg* a = take();
        switch (f[i]) {
        case 's':
            piece = a ? (a->text ? a->s : std::to_string(a->n)) : "";
            zero = false;
            break;
        case 'd':
        case 'i':
        case 'u':
            piece = a ? (a->text ? a->s : std::to_string(a->n)) : "0";
            break;
        case 'c':
            piece = a && !a->text ? std::string(1, char(a->n)) : "";
            zero = false;
            break;
        default:
            piece.clear();
            break;
        }
        const size_t w = size_t(std::max(0, std::min(width, 256)));
        if (piece.size() < w) {
            const std::string pad(w - piece.size(), zero ? '0' : ' ');
            if (left)
                piece += std::string(w - piece.size(), ' ');
            else if (zero && !piece.empty() && piece[0] == '-')
                piece = "-" + pad + piece.substr(1);
            else
                piece = pad + piece;
        }
        out += piece;
    }
    return out;
}

void fill_from(RgbImage& dst, const RgbImage& src, int x0, int y0, int w, int h) {
    for (int y = std::max(0, y0); y < std::min(dst.height, y0 + h); ++y)
        for (int x = std::max(0, x0); x < std::min(dst.width, x0 + w); ++x)
            dst.at(x, y) = src.contains(x, y) ? src.at(x, y) : 0;
}
} // namespace

std::vector<std::string> frontend_strings(const std::map<uint32_t, std::u16string>& resources) {
    std::vector<std::string> s = load_game_strings(resources);
    s.resize(4004);
    for (uint32_t id = 4000; id < 4004;
         ++id) { // height/weight formats loaded directly (0x0042f510/0x0042f4a0)
        const auto it = resources.find(id);
        if (it != resources.end()) s[id] = to_cp1252(it->second);
    }
    return s;
}

std::array<std::vector<std::string>, 2> credit_lines(const std::map<uint32_t, std::u16string>& resources) {
    std::array<std::vector<std::string>, 2> c;
    for (int i = 0; i < 2; ++i) {
        try {
            c[size_t(i)] = load_game_strings(resources, uint32_t(1000 * (i + 1)));
        } catch (const std::runtime_error&) {
        }
    }
    return c;
}

FrontendData read_frontend_data(const std::vector<uint8_t>& exe) {
    FrontendData d;
    // Counts the original fills in at start-up (0x00423881) for screens whose table count is 0.
    const std::map<int, uint32_t> counts = {{0x10, 0x00474668}, {0x11, 0x0047466c}, {0x12, 0x00465fb0},
                                            {0x13, 0x00474670}, {0x0b, 0x0046ca88}, {0x0d, 0x0046ce80}};
    for (int s = 0; s < 0x24; ++s) {
        const auto e = read_exe_data(exe, 0x0046a390 + uint32_t(s) * 0x14, 0x14);
        const ByteReader entry(e);
        uint32_t count = entry.le<uint32_t>(0);
        const uint32_t address = entry.le<uint32_t>(4);
        if (counts.count(s)) count = ByteReader(read_exe_data(exe, counts.at(s), 4)).le<uint32_t>(0);
        d.screens[size_t(s)].title = int32_t(entry.le<uint32_t>(8));
        d.screens[size_t(s)].title_format = entry.le<uint32_t>(12);
        if (address && count) d.screens[size_t(s)].items = read_items(exe, address, count);
    }
    for (const auto& [address, count] : std::vector<std::pair<uint32_t, uint32_t>>{{popup_mode, 4},
                                                                                   {popup_level, 6},
                                                                                   {popup_name, 2},
                                                                                   {popup_resolution, 4},
                                                                                   {popup_biker, 3},
                                                                                   {popup_videos, 4},
                                                                                   {popup_jukebox_video, 3}})
        d.popups[address] = read_items(exe, address, count);
    for (const auto& t : jukebox_tracks) d.popups[t[0]] = read_items(exe, t[0], t[1]);
    d.course_extra[0] = read_item(exe, 0x00469130); // Thrash: Restroom
    d.course_extra[1] = read_item(exe, 0x00469168); // mano-a-mano: Disconnect
    d.course_extra[2] = read_item(exe, 0x004690f8); // Big Game: Der Panzer Klub
    for (size_t i = 0; i < 8; ++i) {
        const uint32_t p = ByteReader(read_exe_data(exe, 0x00473598 + uint32_t(i) * 4, 4)).le<uint32_t>(0);
        const auto s = read_exe_data(exe, p, 8);
        d.restroom_patterns[i].assign(s.begin(), s.end());
    }
    const auto len = read_exe_data(exe, 0x00468f58, 4 * d.course_length.size());
    const ByteReader len_reader(len);
    for (size_t i = 0; i < d.course_length.size(); ++i) d.course_length[i] = len_reader.le<uint32_t>(4 * i);
    const auto sch = read_exe_data(exe, 0x0046a6e0, 40 * 28);
    for (size_t r = 0; r < 40; ++r)
        for (size_t k = 0; k < 14; ++k)
            d.schmooze[r][k] = uint16_t(sch[r * 28 + 2 * k] | sch[r * 28 + 2 * k + 1] << 8);
    const auto att = read_exe_data(exe, 0x0046ab44, 4);
    std::copy(att.begin(), att.end(), d.attitude.begin());
    const auto fmt = read_exe_data(exe, 0x0046a680, 4 * d.schmooze_format.size());
    const ByteReader fmt_reader(fmt);
    for (size_t i = 0; i < d.schmooze_format.size(); ++i)
        d.schmooze_format[i] = fmt_reader.le<uint32_t>(4 * i);
    for (size_t r = 0; r < 10; ++r) {
        const auto b = read_exe_data(exe, 0x00463c78 + uint32_t(r) * 0x40 + 8, 8);
        const ByteReader brow(b);
        d.body[r] = {int32_t(brow.le<uint32_t>(0)), int32_t(brow.le<uint32_t>(4))};
    }
    return d;
}

Frontend::Frontend(const Assets& assets, const TextRenderer& text, std::vector<std::string> strings,
                   std::array<std::vector<std::string>, 2> credits, FrontendData data, CareerTables tables,
                   Career& career, Settings& settings, std::string save_directory)
    : assets_(assets), text_(text), strings_(std::move(strings)), data_(std::move(data)), tables_(tables),
      career_(career), settings_(settings), save_dir_(std::move(save_directory)),
      credits_(std::move(credits)) {
    if (strings_.size() < 4004) strings_.resize(4004);
    chat_messages_ = settings_.net_chat;
    background_ = RgbImage(640, 480, 0);
    dark_ = background_;
    // The host enables online play after installing the compatibility/transport gate.
    for (auto& m : data_.screens[9].items)
        if (m.handler == 0x00430140) m.enabled = false;
}

uint32_t Frontend::next_random() {
    return career_random(career_); // the one CRT rand() stream (0x00455eb0)
}

const std::string& Frontend::str(int slot) const {
    static const std::string empty;
    if (slot >= native_slot_base) return native_string(slot);
    return slot > 0 && size_t(slot) < strings_.size() ? strings_[size_t(slot)] : empty;
}

std::string Frontend::caption_text(const MenuItem& m) const {
    if (m.caption == edit_slot && editing_name_) return edit_buffer_ + "_";
    // Plan 34 Task 7: Join code entry and F1-F8 slot editing show the buffer.
    if (m.caption == ns::net_enter_code && editing_code_) return net_code_buffer_ + "_";
    if (m.caption >= 1056 && m.caption < 1064)
        return "F" + std::to_string(m.caption - 1055) + " " + chat_messages_[size_t(m.caption - 1056)] +
               (editing_chat_ == m.caption - 1056 ? "_" : "");
    if (m.caption == ns::net_spectate) return str(m.caption) + (net_spectate_ ? " On" : " Off");
    if (m.flags & item_flag::native_slider) {
        // Plan 18 Task 6/7: caption plus ten segments plus the numeric value,
        // segments scaled to the slider's own range.
        const int v = slider_value(m.caption);
        const auto range = slider_range(m.caption);
        const int fill = range[1] == range[0] ? 10 : (v - range[0]) * 10 / (range[1] - range[0]);
        std::string segs;
        for (int i = 0; i < 10; ++i) segs += i < fill ? '#' : '-';
        return str(m.caption) + " " + segs + " " + std::to_string(v);
    }
    if ((m.handler == native_action::toggle_mute && settings_.mute) ||
        (m.handler == native_action::toggle_swap && settings_.swap_speakers) ||
        (m.handler == native_action::toggle_pad_swap && settings_.swap_punch_kick) ||
        (m.handler == native_action::toggle_left_handed && settings_.left_handed))
        return str(m.caption) + " On";
    if (m.handler == native_action::toggle_mute || m.handler == native_action::toggle_swap ||
        m.handler == native_action::toggle_pad_swap || m.handler == native_action::toggle_left_handed)
        return str(m.caption) + " Off";
    return str(m.caption);
}

std::string Frontend::player_name() const {
    const std::string alias(career_.state.name.data());
    if (career_.mode != GameMode::big_game && career_.mode != GameMode::mano) {
        if (!alias.empty()) return alias;
        return format_c("%s %d", {A(str(191)), A(career_.state.player + 1)});
    }
    const int c = career_.state.character;
    const std::string name = c >= 1 && c <= 8 ? str(357 + c) : "Thrash";
    return alias.empty() ? name : format_c("%s (%s %s)", {A(name), A(str(219)), A(alias)});
}

void Frontend::queue_video(const std::string& base, int variants) {
    if (settings_.video_mode == 0 || base.empty()) return;
    std::string name = base;
    if (variants > 0) name += std::to_string(next_random() % uint32_t(variants) + 1);
    std::string path = "VIDEO/";
    for (char c : name) path.push_back(char(std::toupper(static_cast<unsigned char>(c))));
    path += ".AVI";
    if (assets_.exists(path)) events_.push_back({FrontendEvent::Kind::play_video, path, 0});
}

void Frontend::start() {
    career_.mode = GameMode::attract;
    // 0x00449150 with the initial screen 7: studio logos (absent from the disc) then Intro.
    queue_video("Intro", 0);
    enter(7);
}

bool Frontend::poll(FrontendEvent& e) {
    if (events_.empty()) return false;
    e = events_.front();
    events_.pop_front();
    return true;
}

void Frontend::set_background(const std::string& name) {
    if (name.empty() || name == background_name_ || !menu_image_exists(assets_, name)) return;
    RgbImage img = load_menu_image(assets_, name);
    background_ = RgbImage(640, 480, 0);
    // 0x0041e240: a "Postcard/" or "Street/" picture starts from a copy of its
    // base picture and is decoded over it. The insets are partial: 397x480
    // postcards sit right of the 243-wide "Postcard" strip, 258x250 street
    // views at (146,230). Offsets are fixed by artwork continuity (seam and
    // pixel match against the base); the decoder's placement is not yet traced.
    int x = 0, y = 0;
    const bool postcard = name.rfind("Postcard/", 0) == 0, street = name.rfind("Street/", 0) == 0;
    if ((postcard || street) && menu_image_exists(assets_, postcard ? "Postcard" : "Street")) {
        const RgbImage base = load_menu_image(assets_, postcard ? "Postcard" : "Street");
        fill_from(background_, base, 0, 0, base.width, base.height);
        if (postcard)
            x = 640 - img.width;
        else {
            x = 146;
            y = 230;
        }
    }
    for (int row = 0; row < img.height; ++row)
        for (int col = 0; col < img.width; ++col)
            if (background_.contains(x + col, y + row)) background_.at(x + col, y + row) = img.at(col, row);
    dark_ = background_;
    for (auto& p : dark_.pixels) p = darken555(p);
    background_name_ = name;
    wide_bg_dirty_ = true;
}

// 0x0041d970 (+0x0041d7d0): picture for a screen.
std::string Frontend::background_for(int previous, int screen, int title) {
    auto numbered = [&](const char* base, int n) { return std::string(base) + std::to_string(n); };
    switch (screen) {
    case 7:
    case 0x23:
        return "Credits";
    case 9:
        return "Main";
    case 10:
    case 12:
    case 0x1b:
        return "Postcard";
    case screen_id::net_lobby:
    case screen_id::net_rooms:
    case 0x0b:
        return "Chars/ManoMano";
    case 0x0f:
        return "Street";
    case 0x14:
        return "Olleys";
    case 0x19:
        return "Panzer";
    case 0x18:
        return title >= 0x80 && title <= 0x8e ? std::string("Bikes/") + bike_files[title - 0x80] : "";
    case 0x1a:
        return {}; // chosen by prepare_schmooze
    case screen_id::native_display:
    case screen_id::native_sound:
    case screen_id::native_pause:
    case screen_id::native_gamepad:
    case screen_id::native_touch:
        return background_for(previous, 0x13, title); // native screens share the Graphics Options picture
    default:
        break;
    }
    // 0x0041d7d0 with a random picture number 1..6. The test override pins
    // rest_picture_ after the draw so the RNG stream still advances once.
    if (career_.mode == GameMode::mano) {
        net_video_rng_ = net_video_rng_ * 1664525u + 1013904223u;
        rest_picture_ = int(net_video_rng_ % 6) + 1;
    } else
        rest_picture_ = int(next_random() % 6) + 1;
    if (rest_picture_override_ >= 1 && rest_picture_override_ <= 6) rest_picture_ = rest_picture_override_;
    switch (screen) {
    case 8: {
        const int c = career_.state.character;
        return c >= 1 && c <= 8 ? std::string("Chars/") + character_files[c - 1] + "-F" : "";
    }
    case 0x10:
        return previous >= 0x11 && previous <= 0x13 ? "" : numbered("Restroom/Rest", rest_picture_);
    case 0x1c:
        return numbered("Results/Wreck", rest_picture_);
    case 0x1d:
        return numbered("Results/Bust", rest_picture_);
    case 0x1e:
        if (career_.mode != GameMode::mano && career_.state.level == 4 &&
            career_.state.qualified == all_qualified)
            return "Results/Finale";
        return numbered("Results/Win", rest_picture_);
    case 0x1f:
        return numbered("Results/Lose", rest_picture_);
    case 0x20:
        return numbered("Results/Level", career_.state.level);
    default:
        return {};
    }
}

// 0x0041dbc0: picture shown while an item has the focus.
std::string Frontend::focus_image(const MenuItem& m) const {
    const uint8_t q = career_.state.qualified;
    switch (m.caption) {
    case 0x0d:
        return screen_ == 12 ? "Postcard/Klub" : "";
    case 0x06:
        return screen_ == 10 ? (rest_picture_ & 1 ? "Postcard/RestA" : "Postcard/RestB") : "";
    case 0x08:
        return screen_ == 0x0f ? "Street/Home" : "";
    case 0x10:
        return q & 1 ? "Postcard/QCity" : "Postcard/City";
    case 0x11:
        return std::string(q & 2 ? "Postcard/QSierra" : "Postcard/Sierra") +
               (settings_.alternate_artwork ? "R" : "");
    case 0x12:
        return q & 4 ? "Postcard/QPenin" : "Postcard/Penin";
    case 0x13:
        return q & 8 ? "Postcard/QNapa" : "Postcard/Napa";
    case 0x14:
        return q & 0x10 ? "Postcard/QPCH" : "Postcard/PCH";
    case 0x30:
        return screen_ == 0x1b ? "Postcard/Klub" : screen_ == 0x0f ? "Street/Klub" : "";
    case 0x32:
        return "Street/Olley";
    case 0x5a:
        if (screen_ == 0x15) return "Showroom/Rat/Bikes";
        if (screen_ == 0x16) return "Showroom/Sport/Bikes";
        if (screen_ == 0x17) return "Showroom/Super/Bikes";
        return {};
    default:
        break;
    }
    if (m.caption >= 0x20 && m.caption <= 0x27)
        return std::string("Chars/") + character_files[m.caption - 0x20] + "-N";
    if (m.caption >= 0x60 && m.caption <= 0x6e)
        return std::string("Showroom/") + bike_files[m.caption - 0x60];
    if (m.caption == 0x122 && screen_ == 12) return "Postcard/RestA";
    return {};
}

void Frontend::title_box(int slot, uint32_t format) {
    title_slot_ = slot;
    title_format_ = format;
}

// 0x00420810: description box, with the course length text of 0x4000 items.
void Frontend::describe(const MenuItem& m) {
    if ((m.flags & item_flag::course_length) && m.caption >= 0x10 && m.caption <= 0x14) {
        const size_t i = size_t(m.caption - 0x10) * 5 + career_.state.level;
        long long length = data_.course_length.at(i);
        int units = 27;
        if (settings_.metric) {
            length = (long long)(double(length) * 1.609);
            units = 28;
        } // 0x00462008
        const char decimal = settings_.language == Language::english ? '.' : ',';
        const std::string value = format_c("%d%c%d", {A(length / 10), A(decimal), A(length % 10)});
        strings_[dynamic_slot] = format_c(str(21 + m.caption - 0x10), {A(value), A(str(units))});
        // The showroom draws the Length line as its own Demi footer (traced
        // y=301) rather than embedding it; keep the formatted tail handy.
        // It is the text after the last blank line of the course template.
        length_footer_.clear();
        {
            const std::string& full = strings_[dynamic_slot];
            size_t p = full.rfind("\n\n");
            std::string tail = p == std::string::npos ? full : full.substr(p + 2);
            while (!tail.empty() && (tail.front() == '\n' || tail.front() == '\r')) tail.erase(tail.begin());
            length_footer_ = tail;
        }
    }
    if (m.desc >= native_slot_base) {
        // Native value item: "<description>\nCurrent: <value>" via the dynamic slot.
        strings_[dynamic_slot] = native_string(m.desc);
        const std::string value = native_value_text(m.caption);
        if (!value.empty()) strings_[dynamic_slot] += "\nCurrent: " + value;
        desc_slot_ = dynamic_slot;
    } else
        desc_slot_ = m.desc;
    desc_format_ = m.format;
    desc_flags_ = m.flags & ~0x900u;
    // 0x00420470 repaints the dark box only when the focused item's bit 3 is
    // set. Pixel-verified across goldens: restroom/jukebox/graphics (0xc)
    // fill; characters (0x5), course (0x4815) and showroom (0x184) bios draw
    // text on the photo. (Caller 0x4208d0 forces `or ebx,0xc` for its dynamic
    // descs, but the fill follows the item flags.)
    desc_fill_ = (m.flags & 0x8u) != 0;
}

void Frontend::set_focus(size_t index) {
    if (index >= items_.size()) return;
    focus_ = int(index);
    const MenuItem& m = items_[index];
    set_background(focus_image(m));
    qualified_overlay_ = career_.mode != GameMode::mano && m.caption >= 0x10 && m.caption <= 0x14 &&
                         (career_.state.qualified & (1u << (m.caption - 0x10)));
    if (m.flags & item_flag::title_caption)
        title_box(m.caption, m.format);
    else if (m.flags & item_flag::title_desc)
        title_box(m.desc, m.format);
    price_text_.clear();
    if ((m.flags & item_flag::price) && m.desc >= 0x80 && m.desc <= 0x8e)
        price_text_ = format_c(str(159), {A(tables_.bike_price.at(size_t(showroom_bike(m.desc))))});
    if (m.flags & item_flag::description)
        describe(m);
    else if (!(m.flags & item_flag::popup_item)) {
        desc_slot_ = 0;
        desc_fill_ = false;
    }
}

void Frontend::apply_restroom_pattern() {
    // 0x00444fb0
    size_t p = 0;
    switch (career_.mode) {
    case GameMode::thrash:
        p = 1;
        break;
    case GameMode::big_game:
        p = career_.state.character ? (!save_file_.empty() ? 2 : 3) : 4;
        break;
    case GameMode::mano:
        p = 6;
        break;
    default:
        p = 0;
        break;
    }
    auto& items = data_.screens[0x10].items;
    const std::string& pattern = data_.restroom_patterns[p];
    for (size_t i = 0; i < pattern.size() && i < items.size(); ++i) {
        if (pattern[i] == 'N')
            items[i].enabled = false;
        else if (pattern[i] == 'Y')
            items[i].enabled = true;
        if (screen_ == 0x10 && i < items_.size()) items_[i].enabled = items[i].enabled;
    }
}

void Frontend::prepare_courses(int screen) {
    auto& items = data_.screens[size_t(screen)].items;
    if (items.size() >= 6) items[5] = data_.course_extra[screen == 10 ? 0 : screen == 12 ? 1 : 2];
}

void Frontend::prepare_characters() {
    // 0x00421df0 case 0xe: per character "Height/Weight/Bike/Cash" text in slots 160..167.
    const char decimal = settings_.language == Language::english ? '.' : ',';
    for (int i = 0; i < 8; ++i) {
        const int rider = character_rider(i + 1);
        const auto body = data_.body[size_t(rider)];
        std::string height, weight;
        const std::string hf = str(settings_.metric ? 4002 : 4000), wf = str(settings_.metric ? 4003 : 4001);
        const int h = settings_.metric ? int(double(body[0]) * 2.54) : body[0]; // 0x004626b0
        const int div = settings_.metric ? 100 : 12;
        if (!hf.empty() && hf[0] == '+')
            height = format_c(hf.substr(1), {A(h / div), A(decimal), A(h % div)});
        else if (!hf.empty() && hf[0] == '-')
            height = format_c(hf.substr(1), {A(h / div), A(h % div)});
        weight = format_c(wf, {A(settings_.metric ? int(double(body[1]) * 0.454) : body[1])}); // 0x004626a8
        const int level = career_.mode == GameMode::mano && net_in_room() ? net_->room()->options.level
                                                                          : career_.state.level;
        const int bike = tables_.level_bike.at(size_t(level) + size_t(rider) * 5);
        int slot = 0x80; // 0x00422400: bike id -> name slot
        for (int s = 0x80; s <= 0x8e; ++s)
            if (showroom_bike(s) == bike) slot = s;
        strings_[size_t(160 + i)] = format_c(
            str(40 + i), {A(height), A(weight), A(str(slot)), A(tables_.start_cash.at(size_t(i + 1)))});
    }
    auto& items = data_.screens[0x0e].items;
    if (items.size() > 8) items[8].target = previous_; // 0x004694b4
}

// 0x0041d970 case 0x1a and 0x00420bb0: a random rider and a line chosen from the relationship value.
void Frontend::prepare_schmooze(int previous) {
    const int character = career_.state.character;
    if (previous == 0x19) schmooze_used_ = character >= 1 ? uint8_t(1u << (character - 1)) : 0;
    if (schmooze_used_ == 0xff) schmooze_used_ = character >= 1 ? uint8_t(1u << (character - 1)) : 0;
    int r = 0;
    do r = int(next_random() & 7);
    while (schmooze_used_ & (1u << r));
    schmooze_used_ |= uint8_t(1u << r);
    const int level = career_.state.level;
    int32_t value = 0;
    if (character < 10) {
        const auto& t = career_.relations[size_t(r)][size_t(character)];
        value = t[1] - t[0] + t[2];
    }
    const int penalty = 0; // no race object in the menus: 0x004642d8 is null, giving 15 >= 4
    int c;
    if (value < (16 - penalty) * 1000 - level * 1500)
        c = 1;
    else
        c = value >= (16 - penalty) * 2000 - level * 3000 ? 3 : 2;
    const int att = data_.attitude[size_t(c)] % 3;
    const auto& row = data_.schmooze[size_t(level * 8 + r)];
    std::vector<int> choices = {row[size_t(att)], row[size_t(3 + att)]};
    if (row[12] == character) choices.push_back(row[size_t(6 + att)]);
    if (row[13] == character) choices.push_back(row[size_t(9 + att)]);
    auto& items = data_.screens[0x1a].items;
    if (!items.empty()) {
        items[0].desc = choices[next_random() % choices.size()];
        items[0].format = data_.schmooze_format[size_t(r * 3 + att)];
        items[0].target = schmooze_target_;
    }
    set_background(std::string("Chars/") + character_files[r] + "-" + "FNH"[att]);
}

void Frontend::prepare_screen(int previous, int screen) {
    switch (screen) {
    case screen_id::net_lobby:
        net_prepare_lobby();
        break;
    case screen_id::net_rooms:
        net_prepare_rooms();
        break;
    case 11:
        for (auto& item : data_.screens[11].items) {
            if (item.handler == 0x0042fc40 || item.handler == 0x0042fef0)
                item.enabled = net_attached() && !net_in_room();
            if (item.target == 12) item.enabled = net_in_room();
            if (item.handler == 0x00431600) item.enabled = true;
        }
        break;
    case 13: {
        auto& rows = data_.screens[13].items;
        const auto prototype = rows.front();
        rows.clear();
        for (int i = 0; i < 9; ++i) {
            auto row = prototype;
            row.x = 16;
            row.y = 122 + i * 38;
            row.w = 608;
            row.h = 34;
            row.caption = i < 8 ? 1056 + i : ns::back;
            row.desc = 0;
            row.handler = i < 8 ? 0x00430380 : 0;
            row.target = i < 8 ? i : 11;
            row.enabled = true;
            row.flags = i < 8 ? 0 : item_flag::escape;
            row.nav = {uint8_t(i ? i - 1 : 8), uint8_t(i < 8 ? i + 1 : 0), uint8_t(i ? i - 1 : 8),
                       uint8_t(i < 8 ? i + 1 : 0)};
            rows.push_back(row);
        }
        break;
    }
    case 10:
    case 12:
    case 0x1b:
        prepare_courses(screen);
        break;
    case 0x0e:
        prepare_characters();
        break;
    case 0x10:
        apply_restroom_pattern();
        break;
    case screen_id::native_display: {
        // Keep this row accessible in Low/Inset. Its help explains that
        // applying a render resolution switches the original race mode to High.
        auto& items = data_.screens[size_t(screen_id::native_display)].items;
        const bool hires = settings_.high_resolution && !settings_.inset;
        for (auto& m : items)
            if (m.caption == ns::render_resolution) {
                m.enabled = true;
                m.flags &= ~item_flag::light;
                m.desc = hires ? ns::d_render_resolution : ns::d_render_lowres;
            }
        break;
    }
    case 0x12: {
        // Plan 18 Task 7: key labels (caps 160-168) show the bound key per action.
        auto& items = data_.screens[0x12].items;
        for (int i = 0; i < 9 && 13 + i < int(items.size()); ++i) strings_[size_t(160 + i)] = key_name(i);
        break;
    }
    case 0x1a:
        prepare_schmooze(previous);
        break;
    case 0x1e: {
        if (mano_results_) {
            data_.screens[0x1e].title = 441;
            break;
        }
        // Results title and "%s finished in ... Place."
        data_.screens[0x1e].title =
            career_.state.level == 4 && career_.state.qualified == all_qualified ? 0x178 : 0xb2;
        if (career_.state.place < 3)
            strings_[dynamic_slot] = format_c(str(201 + career_.state.place), {A(player_name())});
        break;
    }
    case 0x21: {
        // 0x004214e0
        auto& items = data_.screens[0x21].items;
        if (items.empty()) break;
        if (career_.mode == GameMode::big_game) {
            if (report_.game_over)
                items[0].caption = 0xc6;
            else {
                items[0].caption = dynamic_slot;
                strings_[dynamic_slot] =
                    format_c(str(report_.penalty_message), {A(player_name()), A(report_.penalty)});
            }
        } else
            items[0].caption = report_.penalty_message ? report_.penalty_message : 1;
        break;
    }
    default:
        break;
    }
}

void Frontend::enter(int screen) {
    if (screen >= 1 && screen <= 5) { // count 0: PostQuitMessage(course) starts the race (0x00448200)
        events_.push_back({FrontendEvent::Kind::start_race, {}, screen});
        return;
    }
    if (screen == 6 || screen < 0 || screen >= screen_id::screen_count) {
        events_.push_back({FrontendEvent::Kind::quit, {}, 0});
        return;
    }
    if (screen_ > 0 && focus_ >= 0 && size_t(focus_) < (popup_base_ ? popup_base_ : items_.size()))
        last_focus_[size_t(screen_)] = focus_;
    previous_ = screen_;
    popup_setter_ = 0;
    editing_name_ = false;
    prepare_screen(previous_, screen);
    screen_ = screen;
    items_ = data_.screens[size_t(screen)].items;
    popup_base_ = 0;
    // Standings darken and title only after the 0x00421290 handler populates
    // the rows; direct entry shows the bare photo (acceptance golden).
    dark_screen_ = screen == 0x22 && standings_ready_;
    if (!dark_screen_) set_background(background_for(previous_, screen, data_.screens[size_t(screen)].title));
    desc_slot_ = 0;
    desc_fill_ = false;
    price_text_.clear();
    qualified_overlay_ = false;
    if (screen == 0x22 && !standings_ready_)
        title_box(0, 0);
    else
        title_box(data_.screens[size_t(screen)].title, data_.screens[size_t(screen)].title_format);
    // 0x004459f0: restroom and jukebox items without a width take the title box width.
    if (screen == 0x10 || screen == 0x11) {
        const int tw = title_slot_ ? text_.width(Face::badloc, 72, str(title_slot_)) + 0x31 : 0;
        const int w = std::min(0x118, std::max(screen == 0x10 ? 0xe6 : 0xa0, tw));
        for (auto& m : items_)
            if (m.w == 0) m.w = w;
    }
    if (items_.empty()) {
        focus_ = -1;
        return;
    }
    size_t index =
        size_t(last_focus_[size_t(screen)]) < items_.size() ? size_t(last_focus_[size_t(screen)]) : 0;
    if ((screen == 10 || screen == 12 || screen == 0x1b) && career_.mode != GameMode::mano &&
        career_.state.qualified != all_qualified)
        while (career_.state.qualified & (1u << index)) index = (index + 1) % 5;
    // 0x00421cf0: a disabled focus follows the tab chain.
    for (int guard = 0; guard < 64 && (!items_[index].enabled || (items_[index].flags & item_flag::skip));
         ++guard)
        index = items_[index].nav[4] < items_.size() ? items_[index].nav[4] : 0;
    set_focus(index);
    timer_at_ = 0;
    attract_at_ = screen == screen_id::main_menu ? now_ + 60000 : 0; // WM_TIMER 9 re-arm
    credit_set_ = -1;
    if (screen == 7 || screen == 0x23) {
        // 0x00424140: string table 1000 scrolls from y 0x30 (left, tab stops); 2000 from 0x1c8 (centred).
        credit_set_ = screen == 7 ? 0 : 1;
        credit_y_ = screen == 7 ? 0x30 : 0x1c8;
        credit_line_ = 1;
        credit_ticks_ = 0;
        credit_next_ = now_ + 15;
        if (credits_[size_t(credit_set_)].size() < 2) timer_at_ = now_ + 1;
    }
}

void Frontend::open_popup(const std::vector<MenuItem>& popup, int current, int setter) {
    // 0x004224b0
    if (popup_base_) close_popup();
    popup_base_ = items_.size();
    for (auto& m : items_) m.flags |= item_flag::dimmed;
    for (MenuItem m : popup) items_.push_back(m);
    popup_setter_ = setter;
    set_focus(popup_base_ + size_t(std::max(1, current)) < items_.size()
                  ? popup_base_ + size_t(std::max(1, current))
                  : popup_base_);
}

void Frontend::close_popup() {
    // 0x004225e0
    if (!popup_base_) return;
    items_.resize(popup_base_);
    for (auto& m : items_) m.flags &= ~item_flag::dimmed;
    const int restore = last_focus_[0] >= 0 && size_t(last_focus_[0]) < items_.size() ? last_focus_[0] : 0;
    popup_base_ = 0;
    popup_setter_ = 0;
    editing_name_ = false;
    save_list_.clear();
    set_focus(size_t(restore));
}

int Frontend::native_current(uint32_t popup) const {
    // 1-based choice index matching the popup's choice targets.
    switch (popup) {
    case native_popup::display_mode:
        return settings_.display_mode == DisplayMode::fullscreen ? 2 : 1;
    case native_popup::window_size:
        return settings_.window_scale;
    case native_popup::scaling:
        return int(settings_.scale_mode) + 1;
    case native_popup::filtering:
        return settings_.filter == FilterMode::smooth ? 2 : 1;
    case native_popup::on_off:
        return 1; // caller overrides per item; see open_native_popup
    case native_popup::touch:
        return settings_.touch_opacity / 25 + 1;
    case native_popup::resolution:
        return settings_.render_scale + 1; // match = 1, 1x..4x = 2..5
    case native_popup::widescreen:
        return int(settings_.widescreen) + 1;
    case native_popup::visual_style:
        return visual_style_current();
    case native_popup::tex_detail:
        return settings_.texture_detail ? 2 : 1;
    case native_popup::enh_filter:
        return int(settings_.enhancements.filter) + 1;
    case native_popup::enh_grade:
        return int(settings_.enhancements.grade) + 1;
    case native_popup::enh_fog:
        return settings_.enhancements.fog / 25 + 1;
    default:
        return 1;
    }
}

void Frontend::open_native_popup(uint32_t popup) {
    // Setter per popup; on/off popups share one table, the setter is chosen by the focused item.
    int setter = native_setter::fps;
    int current = native_current(popup);
    if (popup_base_ == 0) last_focus_[0] = focus_;
    switch (popup) {
    case native_popup::display_mode:
        setter = native_setter::display_mode;
        break;
    case native_popup::window_size:
        setter = native_setter::window_size;
        break;
    case native_popup::scaling:
        setter = native_setter::scaling;
        break;
    case native_popup::filtering:
        setter = native_setter::filtering;
        break;
    case native_popup::touch:
        setter = native_setter::touch_opacity;
        break;
    case native_popup::resolution:
        setter = native_setter::render_resolution;
        break;
    case native_popup::widescreen: {
        const int capw = focus_ >= 0 && size_t(focus_) < items_.size() ? items_[size_t(focus_)].caption : 0;
        if (capw == ns::enh_wide) {
            setter = native_setter::widescreen;
            current = int(settings_.widescreen) + 1;
        } else
            setter = native_setter::widescreen;
        break;
    }
    case native_popup::visual_style:
        setter = native_setter::visual_style;
        break;
    case native_popup::tex_detail:
        setter = native_setter::tex_detail;
        break;
    case native_popup::enh_filter:
        setter = native_setter::enh_filter;
        break;
    case native_popup::enh_grade:
        setter = native_setter::enh_grade;
        break;
    case native_popup::enh_fog:
        setter = native_setter::enh_fog;
        break;
    case native_popup::on_off: {
        // Which bool this popup edits: the focused value item's caption.
        const int cap = focus_ >= 0 && size_t(focus_) < items_.size() ? items_[size_t(focus_)].caption : 0;
        if (cap == ns::vsync) {
            setter = native_setter::vsync;
            current = settings_.vsync ? 2 : 1;
        } else if (cap == ns::haptics) {
            setter = native_setter::haptics;
            current = settings_.haptics ? 2 : 1;
        } else if (cap == ns::enh_interp) {
            setter = native_setter::enh_interp;
            current = settings_.enhancements.interpolate ? 2 : 1;
        } else if (cap == ns::enh_persp) {
            setter = native_setter::enh_persp;
            current = settings_.enhancements.perspective ? 2 : 1;
        } else if (cap == ns::enh_colour) {
            setter = native_setter::enh_colour;
            current = settings_.enhancements.full_colour ? 2 : 1;
        } else if (cap == ns::enh_bloom) {
            setter = native_setter::enh_bloom;
            current = settings_.enhancements.bloom ? 2 : 1;
        } else if (cap == ns::enh_vig) {
            setter = native_setter::enh_vig;
            current = settings_.enhancements.vignette ? 2 : 1;
        } else if (cap == ns::enh_mb) {
            setter = native_setter::enh_mb;
            current = settings_.enhancements.motion_blur ? 2 : 1;
        } else if (cap == ns::enh_wide) {
            // Widescreen inside Enhancements edits the single Display setting.
            setter = native_setter::widescreen;
            current = int(settings_.widescreen) + 1;
        } else {
            setter = native_setter::fps;
            current = settings_.show_fps ? 2 : 1;
        }
        break;
    }
    default:
        break;
    }
    last_focus_[0] = focus_;
    open_popup(data_.popups.at(popup), current, setter);
}

int Frontend::visual_style_current() const {
    if (!settings_.enhancements.any()) return 1;
    Enhancements preset = enhanced_preset();
    if (settings_.enhancements == preset) return 2;
    return 3;
}
std::string Frontend::native_value_text(int caption) const {
    switch (caption) {
    case ns::display_mode:
        return native_string(settings_.display_mode == DisplayMode::fullscreen ? ns::fullscreen
                                                                               : ns::windowed);
    case ns::window_size:
        return native_string(ns::scale_1x + settings_.window_scale - 1);
    case ns::scaling:
        return native_string(ns::fit + int(settings_.scale_mode));
    case ns::filtering:
        return native_string(settings_.filter == FilterMode::smooth ? ns::smooth : ns::sharp);
    case ns::vsync:
        return native_string(settings_.vsync ? ns::on : ns::off);
    case ns::fps:
        return native_string(settings_.show_fps ? ns::on : ns::off);
    case ns::touch_opacity:
        return native_string(ns::touch_0 + settings_.touch_opacity / 25);
    case ns::haptics:
        return native_string(settings_.haptics ? ns::on : ns::off);
    case ns::render_resolution: // plan 19 Task 8: match + 640x480..2560x1920
        return settings_.render_scale == 0 ? native_string(ns::res_match)
                                           : native_string(ns::scale_1x + settings_.render_scale - 1);
    case ns::widescreen:
        return native_string(ns::wide_off + int(settings_.widescreen));
    case ns::visual_style:
        return native_string(ns::style_original + visual_style_current() - 1);
    case ns::enh_interp:
        return native_string(settings_.enhancements.interpolate ? ns::on : ns::off);
    case ns::enh_persp:
        return native_string(settings_.enhancements.perspective ? ns::on : ns::off);
    case ns::enh_colour:
        return native_string(settings_.enhancements.full_colour ? ns::on : ns::off);
    case ns::enh_filter:
        return native_string(ns::filt_nearest + int(settings_.enhancements.filter));
    case ns::enh_fog:
        return std::to_string(settings_.enhancements.fog) + "%";
    case ns::enh_grade:
        return native_string(ns::grade_none + int(settings_.enhancements.grade));
    case ns::enh_bloom:
        return native_string(settings_.enhancements.bloom ? ns::on : ns::off);
    case ns::enh_vig:
        return native_string(settings_.enhancements.vignette ? ns::on : ns::off);
    case ns::enh_mb:
        return native_string(settings_.enhancements.motion_blur ? ns::on : ns::off);
    case ns::enh_wide:
        return native_string(ns::wide_off + int(settings_.widescreen));
    case ns::tex_detail:
        return native_string(settings_.texture_detail ? ns::tex_enhanced : ns::tex_original);
    default:
        return {};
    }
}

void Frontend::apply_native_setting() {
    // Persist beside the saves directory (sibling settings.txt, as run_game lays out)
    // and ask the host to re-apply the display live.
    const std::string path = (std::filesystem::path(save_dir_).parent_path() / "settings.txt").string();
    save_settings(path, settings_);
    events_.push_back({FrontendEvent::Kind::apply_display, {}, 0});
    // Popups close before applying the value; refresh the restored row's help
    // so its Current line reflects the new setting immediately.
    if (focus_ >= 0 && size_t(focus_) < items_.size()) {
        auto& row = items_[size_t(focus_)];
        if (row.caption == ns::render_resolution) row.desc = ns::d_render_resolution;
        if (row.desc >= native_slot_base) describe(row);
    }
}

std::string Frontend::key_name(int action) const {
    // Plan 18 Task 7: label text for the 0x12 key-label rows. Custom keyboard
    // codes store low-byte-first (0x0040e760); the byte is the keyboard code.
    static const char* kDefaultNames[9] = {"Up", "Down", "Left", "Right", "Z", "X", "C", "?", "?"};
    if (action < 0 || action > 8) return "?";
    if (settings_.custom_controls) {
        const int sc = settings_.controls[size_t(action)] & 0xff;
        if (sc > 0 && sc < SDL_SCANCODE_COUNT) {
            const char* name = SDL_GetScancodeName(SDL_Scancode(sc));
            if (name && *name) return name;
        }
        return "?";
    }
    return kDefaultNames[action];
}

int Frontend::slider_value(int caption) const {
    // Plan 18 Task 6: dialog-300 channel order (master + six), 0..100 each.
    switch (caption) {
    case ns::master:
        return settings_.master_volume;
    case ns::music:
        return settings_.music_volume;
    case ns::effects:
        return settings_.effects_volume;
    case ns::engines:
        return settings_.engine_volume;
    case ns::others:
        return settings_.others_volume;
    case ns::sirens:
        return settings_.siren_volume;
    case ns::interface:
        return settings_.interface_volume;
    case ns::pad_deadzone:
        return settings_.stick_deadzone;
    default:
        return 0;
    }
}

// Slider range {min, max, step} per caption (plan 18 Task 7 dead zone differs).
std::array<int, 3> slider_range(int caption) {
    if (caption == ns::pad_deadzone) return {10, 40, 5};
    return {0, 100, 10};
}

void Frontend::set_slider_value(int caption, int v) {
    const auto range = slider_range(caption);
    v = std::min(range[1], std::max(range[0], v));
    switch (caption) {
    case ns::master:
        settings_.master_volume = v;
        break;
    case ns::music:
        settings_.music_volume = v;
        break;
    case ns::effects:
        settings_.effects_volume = v;
        break;
    case ns::engines:
        settings_.engine_volume = v;
        break;
    case ns::others:
        settings_.others_volume = v;
        break;
    case ns::sirens:
        settings_.siren_volume = v;
        break;
    case ns::interface:
        settings_.interface_volume = v;
        break;
    case ns::pad_deadzone:
        settings_.stick_deadzone = v;
        break;
    default:
        return;
    }
    apply_mixer_setting();
}

void Frontend::adjust_slider(int delta) {
    if (focus_ < 0 || size_t(focus_) >= items_.size()) return;
    const MenuItem& m = items_[size_t(focus_)];
    if (!(m.flags & item_flag::native_slider)) return;
    // Dead-zone-style sliders move by their own step, not the +/-10 default.
    const auto range = slider_range(m.caption);
    const int step = delta < 0 ? -range[2] : range[2];
    set_slider_value(m.caption, slider_value(m.caption) + step);
}

void Frontend::slider_home_end(bool home) {
    if (focus_ < 0 || size_t(focus_) >= items_.size()) return;
    const MenuItem& m = items_[size_t(focus_)];
    if (!(m.flags & item_flag::native_slider)) return;
    const auto range = slider_range(m.caption);
    set_slider_value(m.caption, home ? range[0] : range[1]);
}

void Frontend::apply_mixer_setting() {
    // Same persistence as display settings; the host re-applies channel levels live.
    // No preview click: the original dlgproc plays no wave on change either (test
    // button 0xBC1 only re-posts the live-apply message, which this event is).
    const std::string path = (std::filesystem::path(save_dir_).parent_path() / "settings.txt").string();
    save_settings(path, settings_);
    events_.push_back({FrontendEvent::Kind::apply_mixer, {}, 0});
}

void Frontend::show_dialog(const std::string& text, int kind, std::vector<std::pair<int, int>> buttons) {
    dialog_ = {text.empty() ? std::string(" ") : text, std::move(buttons), 0, kind};
}

bool Frontend::confirm_unsaved(int then) {
    if (career_.mode == GameMode::big_game && career_.unsaved && career_.state.character) {
        dialog_then_ = then;
        show_dialog(str(12), dialog_unsaved, {{386, 1}, {387, 2}, {204, 0}}); // Yes / No / Cancel
        return false;
    }
    return true;
}

void Frontend::finish_dialog(int choice) {
    const int kind = dialog_.kind;
    dialog_ = {};
    if (kind == dialog_quit_race) { // plan 18 Task 8: Yes ends the race, No stays paused
        if (choice == 1) {
            pause_open_ = false;
            events_.push_back({FrontendEvent::Kind::quit_race, {}, 0});
        }
        return;
    }
    // Plan 34 Task 7: lobby dialogs. Leave drops the room and returns to 0x0b;
    // anything else just dismisses (a shown error is not reshown).
    if (kind == dialog_net_error) {
        if (choice == 2 && net_) {
            net_->leave();
            net_shown_error_.clear();
            enter(0x0b);
        }
        return;
    }
    if (kind == dialog_net_connecting) {
        if (net_) net_->leave();
        net_connecting_ = false;
        return;
    }
    if (kind != dialog_unsaved || choice == 0) return;
    if (choice == 1) save_current();
    const int then = dialog_then_;
    dialog_then_ = then_none;
    switch (then) {
    case then_quit:
        enter(6);
        break;
    case then_load:
        open_load_list();
        break;
    case then_mode:
        last_focus_[0] = focus_;
        open_popup(data_.popups.at(popup_mode), int(career_.mode), set_mode);
        break;
    case then_characters:
        character_return_ = 0x10;
        enter(0x0e);
        break;
    default:
        break;
    }
}

void Frontend::save_current() {
    if (save_file_.empty()) {
        save_new();
        return;
    }
    try {
        std::filesystem::create_directories(save_dir_);
        const auto bytes = write_save(career_);
        if (!write_file_atomic(save_file_, bytes.data(), bytes.size()))
            throw std::runtime_error("write failed");
        career_.unsaved = false;
        apply_restroom_pattern();
    } catch (const std::exception&) {
        show_dialog(format_c(str(367), {A(std::filesystem::path(save_file_).filename().string())}),
                    dialog_message, {{205, 0}});
    }
}

void Frontend::save_new() {
    // Native replacement for GetSaveFileNameA (0x00445310): numbered files named after the character.
    const int c = career_.state.character;
    const std::string base = c >= 1 && c <= 8 ? character_files[c - 1] : "Player";
    for (int n = 1; n < 1000; ++n) {
        const std::string path =
            (std::filesystem::path(save_dir_) / (base + " " + std::to_string(n) + ".RRS")).string();
        if (!std::filesystem::exists(path)) {
            save_file_ = path;
            break;
        }
    }
    if (!save_file_.empty()) save_current();
}

void Frontend::open_load_list() {
    // Native replacement for GetOpenFileNameA (0x00445150): newest eight .RRS files.
    std::vector<std::pair<std::filesystem::file_time_type, std::string>> files;
    std::error_code ec;
    for (const auto& e : std::filesystem::directory_iterator(save_dir_, ec)) {
        std::string ext = e.path().extension().string();
        for (auto& ch : ext) ch = char(std::toupper(static_cast<unsigned char>(ch)));
        if (e.is_regular_file(ec) && ext == ".RRS")
            files.emplace_back(e.last_write_time(ec), e.path().string());
    }
    std::sort(files.begin(), files.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
    if (files.size() > 8) files.resize(8);
    if (files.empty()) {
        show_dialog(format_c(str(368), {A(save_dir_)}), dialog_message, {{205, 0}});
        return;
    }
    std::vector<MenuItem> popup = data_.popups.at(popup_mode);
    popup.resize(1);
    save_list_.clear();
    for (size_t i = 0; i < files.size(); ++i) {
        MenuItem m = data_.popups.at(popup_mode)[1];
        m.y = 300 + int(i) * 36 - int(files.size() > 4 ? (files.size() - 4) * 36 : 0);
        m.target = int(i) + 1;
        m.caption = label_slot + int(i);
        const size_t n = files.size();
        m.nav = {uint8_t(i + 1),
                 uint8_t(i + 1),
                 uint8_t(i == 0 ? n : i),
                 uint8_t(i + 2 > n ? 1 : i + 2),
                 uint8_t(i + 2 > n ? 1 : i + 2),
                 uint8_t(i == 0 ? n : i),
                 0,
                 0};
        strings_[size_t(label_slot) + i] = std::filesystem::path(files[i].second).stem().string();
        save_list_.push_back(files[i].second);
        popup.push_back(m);
    }
    last_focus_[0] = focus_;
    open_popup(popup, 1, set_load);
}

std::string Frontend::format_standing(size_t place, const Standing& s) const {
    std::string time = "------------"; // 0x0046afc0
    if (s.tenths >= 0) {
        const char decimal = settings_.language == Language::english ? '.' : ',';
        const std::string sec =
            format_c("%02d%c%d", {A((s.tenths % 600) / 10), A(decimal), A((s.tenths % 600) % 10)});
        time = format_c("%d:%s", {A(s.tenths / 600), A(sec)});
    }
    const std::string name = s.name;
    if (career_.mode == GameMode::big_game)
        return format_c("%2d)  $%4d  %*s  %s", {A((long long)place + 1),
                                                A((long long)tables_.prize.at(std::min<size_t>(place, 14)) *
                                                  (career_.state.level + 1)),
                                                A(12), A(time), A(name)});
    return format_c("%2d)  %*s  %s", {A((long long)place + 1), A(12), A(time), A(name)});
}

void Frontend::race_finished(RaceOutcome outcome, int place, const std::vector<Standing>& standings) {
    mano_results_ = false;
    if (offline_standings_) {
        data_.screens[0x22] = *offline_standings_;
        offline_standings_.reset();
    }
    standings_ = standings;
    standings_ready_ = false; // the 0x00421290 handler repopulates per visit
    report_ = finish_race(career_, tables_, outcome, place);
    if (report_.screen == screen_id::main_menu) {
        enter(screen_id::main_menu);
        return;
    }
    const std::string video = results_video(career_, report_.screen, next_random());
    if (!video.empty()) queue_video(video, 0);
    enter(report_.screen);
}

void Frontend::open_pause(const RgbImage& race_frame) {
    // Plan 18 Task 8: the captured race frame becomes the menu background and
    // its darkened copy fills the screen, as the standings screen does.
    pause_open_ = true;
    pause_return_ = false;
    enter(screen_id::native_pause);
    background_ = race_frame;
    dark_ = race_frame;
    for (auto& p : dark_.pixels) p = darken555(p);
    dark_screen_ = true;
    wide_bg_dirty_ = true;
}

bool Frontend::pause_open() const {
    return pause_open_;
}

int Frontend::menu_width(int display_w, int display_h) const {
    if (settings_.widescreen == Widescreen::off || !settings_.enhancements.any()) return 640;
    const int dw = display_w > 0 ? display_w : display_w_;
    const int dh = display_h > 0 ? display_h : display_h_;
    return logical_width_for(settings_.widescreen, dw, dh) * 2;
}

void Frontend::ensure_wide_bg(int wide_w) const {
    if (!wide_bg_dirty_ && wide_bg_.width == wide_w && wide_bg_.height == 480) return;
    RgbImage src(wide_w, 480, 0), tmp(wide_w, 480, 0);
    for (int y = 0; y < 480; ++y) {
        const int sy = std::min(std::max(0, background_.height - 1), y);
        for (int x = 0; x < wide_w; ++x) {
            const int sx = background_.width > 0 ? (x * background_.width) / wide_w : 0;
            const uint32_t p = background_.contains(sx, sy) ? darken555(background_.at(sx, sy)) : 0;
            src.at(x, y) = p;
        }
    }
    constexpr int radius = 8;
    for (int pass = 0; pass < 2; ++pass) {
        for (int y = 0; y < 480; ++y) {
            for (int x = 0; x < wide_w; ++x) {
                uint32_t r = 0, g = 0, b = 0, n = 0;
                const int x0 = std::max(0, x - radius), x1 = std::min(wide_w - 1, x + radius);
                for (int xx = x0; xx <= x1; ++xx) {
                    const uint32_t p = src.at(xx, y);
                    r += (p >> 16) & 0xff;
                    g += (p >> 8) & 0xff;
                    b += p & 0xff;
                    ++n;
                }
                tmp.at(x, y) = ((r / n) << 16) | ((g / n) << 8) | (b / n);
            }
        }
        for (int y = 0; y < 480; ++y) {
            const int y0 = std::max(0, y - radius), y1 = std::min(479, y + radius);
            for (int x = 0; x < wide_w; ++x) {
                uint32_t r = 0, g = 0, b = 0, n = 0;
                for (int yy = y0; yy <= y1; ++yy) {
                    const uint32_t p = tmp.at(x, yy);
                    r += (p >> 16) & 0xff;
                    g += (p >> 8) & 0xff;
                    b += p & 0xff;
                    ++n;
                }
                src.at(x, y) = ((r / n) << 16) | ((g / n) << 8) | (b / n);
            }
        }
    }
    wide_bg_ = std::move(src);
    wide_bg_dirty_ = false;
}

// Item handlers by original address (0x00423200 calls +0x2c, else enters +4).
int Frontend::run_handler(size_t index) {
    MenuItem& m = items_[index];
    switch (m.handler) {
    case 0:
        return m.target;
    case 0x00445070:
        select_mode(career_, tables_, GameMode::thrash);
        return m.target;
    case 0x004450c0:
        select_mode(career_, tables_, GameMode::big_game);
        return m.target;
    case 0x00430140:
        if (!net_attached()) return 0;
        career_.mode = GameMode::mano;
        return 11;
    case 0x0042fc40:
        net_open_host();
        return 0;
    case 0x0042fef0:
        net_open_rooms();
        return 0;
    case 0x00431600:
    case native_action::net_leave: {
        const bool exit = m.handler == 0x00431600;
        if (net_) net_->leave();
        net_.reset();
        settings_.net_seat.clear();
        settings_.net_room.clear();
        return exit ? 9 : 11;
    }
    case native_action::net_copy:
        if (net_in_room()) {
            FrontendEvent e;
            e.kind = FrontendEvent::Kind::copy_text;
            e.text = net_->room()->code;
            events_.push_back(std::move(e));
        }
        return 0;
    case native_action::net_code:
        net_ensure();
        editing_code_ = true;
        net_code_buffer_.clear();
        return 0;
    case native_action::net_refresh:
        net_open_rooms();
        return 0;
    case native_action::net_spectate:
        net_spectate_ = !net_spectate_;
        return 0;
    case 0x00430350:
        return 13;
    case 0x00430380:
        editing_chat_ = std::clamp(m.target, 0, 7);
        return 0;
    case 0x004213c0: {
        if (career_.mode == GameMode::mano) {
            if (net_in_room()) {
                net_->vote_course(m.target);
                net_->set_ready(true);
            }
            return 0;
        }
        // Course chosen: reset standings, Start video, "Get ready" screen whose item starts the race.
        start_race(career_, m.target);
        standings_.clear();
        queue_video("Start", 6);
        auto& loading = data_.screens[8].items;
        if (!loading.empty()) loading[0].target = m.target;
        data_.screens[8].title = 0xce;
        return 8;
    }
    case 0x0041f5a0:
        if (career_.mode == GameMode::mano && net_in_room()) {
            net_->pick_character(m.target);
            return screen_id::net_lobby;
        }
        choose_character(career_, tables_, m.target);
        return character_return_;
    case 0x0041f570:
        if (m.target == 9) select_mode(career_, tables_, GameMode::attract);
        return m.target;
    case 0x0041f4f0:
        return confirm_unsaved(then_quit) ? 6 : 0;
    case 0x004216e0: {
        // Showroom: bike name title, specification text with the price, Buy/Exit back to the class list.
        const int slot = m.desc;
        if (slot < 0x80 || slot > 0x8e) return 0;
        const int cls = slot < 0x85 ? 0x15 : slot < 0x8a ? 0x16 : 0x17;
        auto& s = data_.screens[0x18];
        s.title = slot;
        s.title_format = showroom_format[slot - 0x80];
        if (s.items.size() >= 2) {
            s.items[0].target = cls;
            s.items[0].enabled = true;
            s.items[0].format = showroom_format[slot - 0x80];
            s.items[0].desc = dynamic_slot;
            s.items[1].target = cls;
            s.items[1].flags = 0x8184;
            s.items[1].desc = dynamic_slot;
            s.items[1].format = showroom_format[slot - 0x80];
        }
        strings_[dynamic_slot] =
            format_c(str(144 + slot - 0x80), {A(tables_.bike_price.at(size_t(showroom_bike(slot))))});
        queue_video(bike_files[slot - 0x80], 0);
        return 0x18;
    }
    case 0x004215e0: {
        const int bike = showroom_bike(data_.screens[0x18].title);
        const PurchaseResult r = buy_bike(career_, tables_, bike);
        m.enabled = false;
        int title = 0xbd, body = 0xbe;
        if (r == PurchaseResult::already_owned) {
            title = 0xbb;
            body = 0xbc;
        } else if (r == PurchaseResult::cannot_afford) {
            title = 0xb9;
            body = 0xba;
        }
        if (items_.size() >= 2) {
            items_[1].desc = body;
            items_[1].flags = 0x900c;
            if (r == PurchaseResult::bought) items_[1].target = 0x14;
            data_.screens[0x18].items = items_;
            title_box(title, 0x4854);
            set_focus(1);
            describe(items_[1]);
        }
        return 0;
    }
    case 0x004216d0:
        if (!data_.screens[0x18].items.empty()) data_.screens[0x18].items[0].enabled = true;
        return m.target;
    case 0x00420b50:
        if (schmooze_target_ == 0x19) {
            schmooze_target_ = 0x1a;
            schmooze_count_ = 0;
        } else if (++schmooze_count_ == 4)
            schmooze_target_ = 0x19;
        return schmooze_target_;
    case 0x00420b90:
        schmooze_target_ = 0x19;
        return 0x19;
    case 0x004214e0:
        if (mano_results_) {
            net_results_table();
            return 0;
        }
        return 0x21; // fines/repairs were applied by finish_race
    case 0x00421290: {
        if (mano_results_) {
            net_results_table();
            return 0;
        }
        // 0x00421290: three rows, plus the player's row when not in the top
        // three. Populated here (not on screen entry): direct entry shows no
        // rows, matching the original.
        auto& items = data_.screens[0x22].items;
        const int place = career_.state.place;
        const int rows = place < 3 ? 3 : 4;
        for (size_t i = 0; i < items.size(); ++i) {
            items[i].caption = 0;
            items[i].flags = 0x8000;
        }
        for (int i = 0; i < rows && size_t(i) < items.size(); ++i) {
            const size_t row = i == 3 ? size_t(place) : size_t(i);
            const Standing s = row < standings_.size() ? standings_[row] : Standing{};
            strings_[size_t(160 + i)] = format_standing(row, s);
            items[size_t(i)].caption = 160 + i;
            items[size_t(i)].flags = 0x8040;
        }
        last_focus_[0x22] = std::min(place, 3);
        if (size_t(last_focus_[0x22]) < items.size()) items[size_t(last_focus_[0x22])].enabled = true;
        standings_ready_ = true;
        return 0x22;
    }
    case 0x0041f6d0:
    case 0x004241e0:
        if (mano_results_) {
            mano_results_ = false;
            return 11;
        }
        return after_results(career_);
    // Restroom.
    case 0x00445150:
        if (confirm_unsaved(then_load)) open_load_list();
        return 0;
    case 0x00445370:
        save_current();
        return 0;
    case 0x00445310:
        save_new();
        return 0;
    case 0x00445460:
        if (confirm_unsaved(then_mode)) {
            last_focus_[0] = focus_;
            open_popup(data_.popups.at(popup_mode), int(career_.mode), set_mode);
        }
        return 0;
    case 0x00445490:
        last_focus_[0] = focus_;
        open_popup(data_.popups.at(popup_level), career_.state.level + 1, setter_level);
        return 0;
    case 0x00445560:
        last_focus_[0] = focus_;
        edit_buffer_ = std::string(career_.state.name.data());
        open_popup(data_.popups.at(popup_name), 1, none);
        editing_name_ = true;
        return 0;
    case 0x004454c0: {
        career_.state.name = {};
        std::copy_n(edit_buffer_.begin(), std::min<size_t>(edit_buffer_.size(), 19),
                    career_.state.name.begin());
        settings_.alias = edit_buffer_.substr(0, 19);
        close_popup();
        return 0;
    }
    case 0x00445390:
        if (confirm_unsaved(then_characters)) {
            character_return_ = 0x10;
            return m.target;
        }
        return 0;
    case 0x00445590:
        return m.target; // jukebox
    case 0x004453c0: {
        const int r = after_results(career_);
        character_return_ = 0x0f;
        return r == 0x0f ? 0x19 : r;
    }
    case 0x00401df0:
        return screen_id::native_sound; // plan 18 Task 6: native look-alike of dialog 300
    case 0x0040fd90:
        return m.target; // controls device init: no native init needed, enter 0x12
    case 0x0040e580:
        // Plan 18 Task 7: the 9 rebind rows (captions 256-264) cover actions 0-8;
        // nitro/camera rows have no UI. Arm press-a-key capture for the action.
        if (m.caption >= 256 && m.caption <= 264) capture_action_ = m.caption - 256;
        return 0;
    case native_action::toggle_mute:
        settings_.mute = !settings_.mute;
        apply_mixer_setting();
        return 0;
    case native_action::toggle_swap:
        settings_.swap_speakers = !settings_.swap_speakers;
        apply_mixer_setting();
        return 0;
    case native_action::toggle_pad_swap:
        settings_.swap_punch_kick = !settings_.swap_punch_kick;
        apply_mixer_setting();
        return 0;
    case native_action::toggle_left_handed:
        settings_.left_handed = !settings_.left_handed;
        apply_mixer_setting();
        return 0;
    case native_action::pause_resume: // plan 18 Task 8: RESUME and Back/B/Escape resume
        pause_open_ = false;
        events_.push_back({FrontendEvent::Kind::resume_race, {}, 0});
        return 0;
    case native_action::pause_quit: // plan 18 Task 8: QUIT RACE asks first; Yes ends the race
        show_dialog(native_string(ns::d_quit_race), dialog_quit_race, {{386, 1}, {387, 0}});
        dialog_.focus = 1; // default to No
        return 0;
    case 0x00445600: {
        // Jukebox track list: popup per focused band caption 0xe1..0xe6; current row = saved track when the group matches.
        const int group = m.caption >= 0xe1 && m.caption <= 0xe6 ? m.caption - 0xe1 : 0;
        const auto& table = jukebox_tracks[size_t(group)];
        int current = 1;
        if (settings_.music_group == group) current = settings_.music_track + 1;
        last_focus_[0] = focus_;
        open_popup(data_.popups.at(table[0]), current, none);
        return 0;
    }
    case 0x004455b0: {
        // Jukebox track select: target - 0xe1 = group, caption - 0xf1 = track (0x00424080 persists Group/Track).
        const int group = m.target - 0xe1, track = m.caption - 0xf1;
        if (group < 0 || group > 5 || track < 0) {
            close_popup();
            return 0;
        }
        settings_.music_group = group;
        settings_.music_track = track;
        events_.push_back({FrontendEvent::Kind::select_soundtrack, {}, 0, group, track});
        close_popup(); // 0x004225e0
        return 0;
    }
    case 0x00445730: {
        // Jukebox video popup (0x473fa0: JESSIE/DUEL). The two "%s : %s" header lines have no native
        // popup-header concept; the EXE item captions (slots 160/161) are shown as-is.
        last_focus_[0] = focus_;
        open_popup(data_.popups.at(popup_jukebox_video), 1, none);
        return 0;
    }
    case 0x004456d0: {
        // Jukebox video play: table 0x473f98[target]; forces VideoMode 2 when off, restores after.
        const std::string path = jukebox_video_path(m.target);
        const int saved = settings_.video_mode;
        if (saved == 0) settings_.video_mode = 2;
        if (!path.empty())
            queue_video(path.substr(0, 6) == "VIDEO/" ? path.substr(6, path.size() - 10) : path, 0);
        settings_.video_mode = saved;
        close_popup(); // 0x004225e0 (restroom redraws from current state)
        return 0;
    }
    // Graphics options.
    case 0x004457b0:
        last_focus_[0] = focus_;
        open_popup(data_.popups.at(popup_resolution),
                   settings_.high_resolution ? 3
                   : settings_.inset         ? 2
                                             : 1,
                   set_resolution);
        return 0;
    case 0x004458a0:
        last_focus_[0] = focus_;
        open_popup(data_.popups.at(popup_biker), settings_.high_res_bikers ? 2 : 1, set_biker);
        return 0;
    case 0x00445960:
        last_focus_[0] = focus_;
        open_popup(data_.popups.at(popup_videos), settings_.video_mode + 1, set_videos);
        return 0;
    case 0x00445440: {
        const int target = m.target;
        if (screen_ == screen_id::net_rooms && !popup_base_) {
            net_join_shown(target - 1);
            return 0;
        }
        const int setter = popup_setter_;
        std::string load_path;
        if (setter == set_load && target >= 1 && size_t(target) <= save_list_.size())
            load_path = save_list_[size_t(target - 1)];
        close_popup();
        if (!target) return 0;
        switch (setter) {
        case native_setter::net_level:
            net_pending_options_.level = uint8_t(target - 1);
            open_popup(data_.popups.at(native_popup::net_ai), 3, native_setter::net_ai);
            break;
        case native_setter::net_ai:
            net_pending_options_.ai = uint8_t(target - 1);
            open_popup(data_.popups.at(native_popup::net_cars), 1, native_setter::net_cars);
            break;
        case native_setter::net_cars:
            net_pending_options_.cars = target == 1;
            open_popup(data_.popups.at(native_popup::net_public), 1, native_setter::net_public);
            break;
        case native_setter::net_public:
            net_pending_options_.is_public = target == 1;
            net_ensure();
            net_->create_room(net_pending_options_);
            net_connecting_ = true;
            show_dialog(str(ns::net_connecting), 4, {{204, 0}});
            break;
        case set_mode:
            if (target != 3) select_mode(career_, tables_, GameMode(target));
            apply_restroom_pattern();
            break;
        case setter_level:
            set_level(career_, tables_, target);
            break;
        case set_resolution:
            settings_.high_resolution = target == 3;
            settings_.inset = target == 2;
            break;
        case set_biker:
            settings_.high_res_bikers = target == 2;
            break;
        case set_videos:
            settings_.video_mode = target - 1;
            break;
        case set_load: {
            const std::string file = std::filesystem::path(load_path).filename().string();
            try {
                std::ifstream in(load_path, std::ios::binary);
                std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)),
                                           std::istreambuf_iterator<char>());
                career_ = read_save(bytes, tables_, career_.rng);
                save_file_ = load_path;
                apply_restroom_pattern();
                items_ = data_.screens[size_t(screen_)].items;
            } catch (const std::exception& e) {
                const bool bogus = std::string(e.what()).find("checksum") != std::string::npos;
                show_dialog(format_c(str(bogus ? 370 : 369), {A(file)}), dialog_message, {{205, 0}});
            }
            break;
        }
        case native_setter::display_mode:
            settings_.display_mode = target == 2 ? DisplayMode::fullscreen : DisplayMode::windowed;
            apply_native_setting();
            break;
        case native_setter::window_size:
            settings_.window_scale = std::min(4, std::max(1, target));
            apply_native_setting();
            break;
        case native_setter::scaling:
            settings_.scale_mode = ScaleMode(std::min(2, std::max(0, target - 1)));
            apply_native_setting();
            break;
        case native_setter::filtering:
            settings_.filter = target == 2 ? FilterMode::smooth : FilterMode::sharp;
            apply_native_setting();
            break;
        case native_setter::vsync:
            settings_.vsync = target == 2;
            apply_native_setting();
            break;
        case native_setter::fps:
            settings_.show_fps = target == 2;
            apply_native_setting();
            break;
        case native_setter::touch_opacity:
            settings_.touch_opacity = std::min(100, std::max(0, (target - 1) * 25));
            apply_native_setting();
            break;
        case native_setter::haptics:
            settings_.haptics = target == 2;
            apply_native_setting();
            break;
        case native_setter::render_resolution:
            settings_.render_scale = std::min(4, std::max(0, target - 1));
            settings_.high_resolution = true;
            settings_.inset = false;
            apply_native_setting();
            break;
        case native_setter::widescreen:
            settings_.widescreen = Widescreen(std::min(3, std::max(0, target - 1)));
            apply_native_setting();
            break;
        case native_setter::visual_style:
            if (target == 1)
                settings_.enhancements = Enhancements{};
            else if (target == 2) {
                settings_.enhancements = enhanced_preset();
                settings_.widescreen = Widescreen::match;
            } // widescreen stays a display choice
            else
                enter(screen_id::native_enhancements);
            apply_native_setting();
            break;
        case native_setter::enh_interp:
        case native_setter::enh_persp:
        case native_setter::enh_colour:
        case native_setter::enh_bloom:
        case native_setter::enh_vig:
        case native_setter::enh_mb: {
            // Which bool: the focused Enhancements row's caption.
            const int cap =
                focus_ >= 0 && size_t(focus_) < items_.size() ? items_[size_t(focus_)].caption : 0;
            const bool v = target == 2;
            if (cap == ns::enh_interp)
                settings_.enhancements.interpolate = v;
            else if (cap == ns::enh_persp)
                settings_.enhancements.perspective = v;
            else if (cap == ns::enh_colour)
                settings_.enhancements.full_colour = v;
            else if (cap == ns::enh_bloom)
                settings_.enhancements.bloom = v;
            else if (cap == ns::enh_vig)
                settings_.enhancements.vignette = v;
            else if (cap == ns::enh_mb)
                settings_.enhancements.motion_blur = v;
            apply_native_setting();
            break;
        }
        case native_setter::enh_filter:
            settings_.enhancements.filter = TextureFilter(std::min(2, std::max(0, target - 1)));
            apply_native_setting();
            break;
        case native_setter::enh_grade:
            settings_.enhancements.grade = Grade(std::min(3, std::max(0, target - 1)));
            apply_native_setting();
            break;
        case native_setter::enh_fog:
            settings_.enhancements.fog = std::min(100, std::max(0, (target - 1) * 25));
            apply_native_setting();
            break;
        case native_setter::enh_wide:
            settings_.widescreen = Widescreen(std::min(3, std::max(0, target - 1)));
            apply_native_setting();
            break;
        case native_setter::tex_detail:
            settings_.texture_detail = target == 2;
            apply_native_setting();
            break;
        }
        return 0;
    }
    default:
        return m.target;
    }
}
int Frontend::cycle_mini_dash() {
    // F10 key-up in the 0x004431d0 modal pump: 0->1->2->0 (full/mini/off).
    settings_.mini_dash = (settings_.mini_dash + 1) % 3;
    return settings_.mini_dash;
}
bool Frontend::toggle_opponent_list() {
    // Menu `/` key dispatch 0x00423777: toggles OpponentList 0<->1 (sete, unambiguous).
    settings_.opponent_list = !settings_.opponent_list;
    return settings_.opponent_list;
}
void Frontend::rebind_key(size_t action, uint16_t code) {
    // 0x0040fbf0 conflict rule via apply_rebind: same-code rows are cleared.
    if (!settings_.custom_controls) {
        settings_.controls = default_keyboard_controls();
        settings_.custom_controls = true;
    }
    apply_rebind(settings_.controls, action, code);
}
void Frontend::reset_keys(bool joystick) {
    // 0x0040fdf0 copies one of the 0x00466618/0x00466630 default tables in.
    settings_.controls = joystick ? default_joystick_controls() : default_keyboard_controls();
    settings_.custom_controls = true;
}

void Frontend::activate(size_t index) {
    if (index >= items_.size() || !items_[index].enabled) return;
    const MenuItem& m = items_[index];
    // Native value item (handler 0, target a native popup key): open the popup
    // with the current choice instead of entering a screen.
    if (m.handler == 0 && uint32_t(m.target) >= 0xF0000000u) {
        open_native_popup(uint32_t(m.target));
        return;
    }
    const int next = run_handler(index);
    if (next == 0) return;
    // Plan 18 Task 8: entering a settings screen from the pause menu arms the
    // return; its Back row (escape flag, no popup open) goes back to the pause
    // menu instead of the screen's original Back target.
    if (screen_ == screen_id::native_pause &&
        (next == screen_id::native_sound || next == screen_id::native_display ||
         next == screen_id::native_gamepad || next == screen_id::native_touch || next == 0x12)) {
        pause_return_ = true;
        enter(next);
        return;
    }
    if (pause_return_ && popup_base_ == 0 && (m.flags & item_flag::escape)) {
        pause_return_ = false;
        enter(screen_id::native_pause);
        return;
    }
    enter(next);
}

void Frontend::navigate(int k) {
    // 0x004227d0: follow nav byte k until an enabled, non-skipped item; stop if the chain loops.
    if (focus_ < 0) return;
    const size_t base = popup_base_;
    size_t start = base + items_[size_t(focus_)].nav[size_t(k)];
    if (start >= items_.size()) return;
    size_t i = start;
    for (int guard = 0; guard < 64; ++guard) {
        if (items_[i].enabled && !(items_[i].flags & item_flag::skip)) {
            set_focus(i);
            return;
        }
        i = base + items_[i].nav[size_t(k)];
        if (i >= items_.size() || i == start) return;
    }
}

void Frontend::credit_tick() {
    // 0x00424230: the first tick re-arms the timer for 1.5 s, later ticks every 15 ms; scroll by the
    // speed, step to the next line at -24, and leave when the table ends.
    const auto& lines = credits_[size_t(credit_set_)];
    credit_next_ += credit_ticks_ == 0 ? 0x5dc : 15;
    ++credit_ticks_;
    credit_y_ -= credit_speed_;
    if (credit_y_ <= -0x18) {
        credit_y_ = 0;
        ++credit_line_;
        if (size_t(credit_line_) >= lines.size()) {
            credit_set_ = -1;
            if (!items_.empty()) activate(0);
        }
    }
}

void Frontend::update(const MenuInput& raw_in, uint64_t now_ms) {
    MenuInput in = raw_in;
    if (const int bar = side_offset(); bar > 0 && (in.pointer_moved || in.pointer_click)) {
        in.pointer_x -= bar;
        if (in.pointer_x < 0 || in.pointer_x >= 640) {
            in.pointer_moved = false;
            in.pointer_click = false;
        }
    }
    now_ = now_ms;
    net_update();
    if (net_edit_input(in)) return;
    if (credit_set_ >= 0) {
        for (const char c : in.text) {
            // 0x004227d0 WM_CHAR: '+' doubles the scroll speed (max 32), '-' halves it.
            if (c == '+') credit_speed_ = credit_speed_ == 0 ? 1 : std::min(32, credit_speed_ * 2);
            if (c == '-') credit_speed_ >>= 1;
        }
        int guard = 0;
        while (credit_set_ >= 0 && now_ >= credit_next_ && guard++ < 4096) credit_tick();
        if (credit_set_ >= 0 && now_ > credit_next_) credit_next_ = now_;
        if (credit_set_ < 0) return;
    }
    if (!dialog_.text.empty()) {
        if (in.left || in.up || in.shift_tab)
            dialog_.focus = (dialog_.focus + int(dialog_.buttons.size()) - 1) % int(dialog_.buttons.size());
        if (in.right || in.down || in.tab) dialog_.focus = (dialog_.focus + 1) % int(dialog_.buttons.size());
        if (in.pointer_click) {
            const int n = int(dialog_.buttons.size());
            for (int i = 0; i < n; ++i) {
                const int bx = 320 - n * 60 + i * 120;
                if (in.pointer_x >= bx && in.pointer_x < bx + 110 && in.pointer_y >= 300 &&
                    in.pointer_y < 330) {
                    finish_dialog(dialog_.buttons[size_t(i)].second);
                    return;
                }
            }
        }
        if (in.select) {
            finish_dialog(dialog_.buttons[size_t(dialog_.focus)].second);
            return;
        }
        if (in.back) {
            finish_dialog(dialog_.buttons.back().second);
            return;
        }
        return;
    }
    if (timer_at_ && now_ >= timer_at_) {
        timer_at_ = 0;
        if (!items_.empty()) activate(0);
        return;
    }
    if (attract_at_ && now_ >= attract_at_) {
        // WM_TIMER 9: nothing disarms it on menu input (no KillTimer(9) on any input path; it fires
        // 60 s after menu entry even mid-navigation). Start the AI demo on the next course.
        attract_at_ = 0;
        attract_course_ = attract_course_ >= 5 ? 1 : attract_course_ + 1; // 0x46aff0 cycles 1..5
        events_.push_back({FrontendEvent::Kind::start_race, {}, attract_course_, 0, 0, true});
        return;
    }
    if (editing_name_) {
        for (char c : in.text)
            if (uint8_t(c) >= 0x20 && edit_buffer_.size() < 19) edit_buffer_.push_back(c);
        if (in.backspace && !edit_buffer_.empty()) edit_buffer_.pop_back();
    }
    if (in.alternate_artwork && !editing_name_) {
        settings_.alternate_artwork = !settings_.alternate_artwork;
        if ((screen_ == 10 || screen_ == 12 || screen_ == 0x1b) && focus_ >= 0) {
            background_name_.clear();
            set_focus(size_t(focus_));
        }
    }
    if (in.opponent_key && !editing_name_) toggle_opponent_list();
    if (capture_action_ >= 0 && in.capture_scancode != 0) {
        // Plan 18 Task 7 press-a-key (`0x0040e760`): Escape cancels with no change;
        // any other key stores low-byte-first (key,0) via the 0x0040fbf0 conflict rule.
        if (in.capture_scancode != SDL_SCANCODE_ESCAPE && in.capture_scancode < 256) {
            if (!settings_.custom_controls) {
                settings_.controls = default_keyboard_controls();
                settings_.custom_controls = true;
            }
            apply_rebind(settings_.controls, size_t(capture_action_), uint16_t(in.capture_scancode));
            strings_[size_t(160 + capture_action_)] = key_name(capture_action_); // 0x0040e7b0 relabel
        }
        capture_action_ = -1; // 0x0040e6d0 disarm
        return;
    }
    if (focus_ < 0) return;
    if (in.pointer_moved || in.pointer_click) {
        for (size_t i = items_.size(); i-- > 0;) {
            const MenuItem& m = items_[i];
            if (!m.enabled || m.w <= 0 || m.h <= 0 || (popup_base_ && i < popup_base_)) continue;
            if (in.pointer_x >= m.x && in.pointer_x < m.x + m.w && in.pointer_y >= m.y &&
                in.pointer_y < m.y + m.h) {
                if (int(i) != focus_) set_focus(i);
                if (in.pointer_click) {
                    activate(i);
                    return;
                }
                break;
            }
        }
    }
    if ((in.left || in.right) && focus_ >= 0 && size_t(focus_) < items_.size() &&
        (items_[size_t(focus_)].flags & item_flag::native_slider)) {
        // Plan 18 Task 6: Left/Right adjust the focused slider by 10 instead of navigating.
        adjust_slider(in.left ? -10 : 10);
        return;
    }
    if (in.left) navigate(0);
    if (in.right) navigate(1);
    if (in.up) navigate(2);
    if (in.down) navigate(3);
    if (in.tab) navigate(4);
    if (in.shift_tab) navigate(5);
    if ((in.home || in.end) && focus_ >= 0 && size_t(focus_) < items_.size() &&
        (items_[size_t(focus_)].flags & item_flag::native_slider)) {
        // Plan 18 Task 6: Home/End go to 0/100 on the focused slider row.
        slider_home_end(in.home);
        return;
    }
    if (in.home || in.end) {
        for (size_t n = 0; n + popup_base_ < items_.size(); ++n) {
            const size_t i = in.home ? popup_base_ + n : items_.size() - 1 - n;
            if (items_[i].enabled) {
                set_focus(i);
                break;
            }
        }
    }
    if (in.select) {
        activate(size_t(focus_));
        return;
    }
    if (in.back) {
        for (size_t i = popup_base_; i < items_.size(); ++i)
            if (items_[i].flags & item_flag::escape) {
                const bool en = items_[i].enabled;
                items_[i].enabled = true;
                activate(i);
                if (i < items_.size()) items_[i].enabled = en;
                return;
            }
    }
}

void Frontend::draw_text_box(RgbImage& img, const TextRect& r, Face face, int height, const std::string& s,
                             unsigned flags, uint32_t colour) const {
    // GDI text with a two-pixel black shadow (0x0041ff80 / 0x00420470 / 0x0041f770).
    TextRect shadow = r;
    text_.draw_box(&img, face, height, shadow, 0, s, flags);
    TextRect top{r.left - 2, r.top - 2, r.right - 2, r.bottom - 2};
    text_.draw_box(&img, face, height, top, colour, s, flags);
}

void Frontend::draw(RgbImage& out, int scale) const {
    // Plan 19 Task 5: menus draw at 640s x 480s. Artwork scales nearest
    // neighbour here (plan 20 swaps in upscaled art); text rasterises at
    // height x scale with shadow offset 2 x scale. scale = 1 is today's path.
    if (scale < 1 || scale > 4) throw std::runtime_error("Frontend scale out of range 1..4");
    const int wide_w = menu_width();
    if (wide_w <= 640) {
        if (scale == 1)
            draw_scale1(out);
        else
            draw_scaled(out, scale);
        return;
    }
    // Plan 21 Task 10: wide presentation renders the 640x480 menu centred with
    // blurred, darkened, stretched side bars computed once per background change.
    RgbImage center;
    if (scale == 1)
        draw_scale1(center);
    else
        draw_scaled(center, scale);
    ensure_wide_bg(wide_w);
    const int bar = (wide_w - 640) / 2;
    out = RgbImage(wide_w * scale, 480 * scale, 0);
    for (int y = 0; y < 480 * scale; ++y) {
        const int bg_y = y / scale;
        for (int x = 0; x < bar * scale; ++x) out.at(x, y) = wide_bg_.at(x / scale, bg_y);
        for (int x = 0; x < 640 * scale; ++x) out.at(bar * scale + x, y) = center.at(x, y);
        for (int x = (bar + 640) * scale; x < wide_w * scale; ++x)
            out.at(x, y) = wide_bg_.at(x / scale, bg_y);
    }
}

void Frontend::draw_scale1(RgbImage& out) const {
    out = dark_screen_ ? dark_ : background_;
    if (credit_set_ >= 0) {
        // 0x00424340: twenty 24-pixel lines in Futura Condensed Bold 24; the first character picks the colour.
        const auto& lines = credits_[size_t(credit_set_)];
        const bool centred = credit_set_ == 1;
        const int tab =
            35 * text_.width(Face::futura_bold, 24, "x"); // DRAWTEXTPARAMS iTabLength 35, margin 40
        int y = credit_y_;
        for (size_t i = size_t(credit_line_); i < lines.size() && i < size_t(credit_line_) + 20;
             ++i, y += 0x18) {
            const std::string& l = lines[i];
            if (l.empty()) continue;
            uint32_t colour = colour_light;
            switch (l[0]) {
            case 'G':
                colour = colour_grey;
                break;
            case 'O':
                colour = colour_orange;
                break;
            case 'R':
                colour = 0xff0000;
                break;
            case 'Y':
                colour = colour_focus;
                break;
            default:
                break;
            }
            const std::string body = l.substr(1);
            for (int pass = 0; pass < 2; ++pass) {
                const int off = pass ? -2 : 0;
                const uint32_t col = pass ? colour : 0;
                if (centred) {
                    TextRect r{off, y + off, 640 + off, y + 0x18 + off};
                    text_.draw_box(&out, Face::futura_bold, 24, r, col, body, dt::center | dt::singleline);
                    continue;
                }
                int x = 40 + off;
                size_t from = 0;
                while (from <= body.size()) {
                    size_t to = body.find('\t', from);
                    if (to == std::string::npos) to = body.size();
                    const std::string part = body.substr(from, to - from);
                    text_.draw(out, Face::futura_bold, 24, x, y + off, col, part);
                    x += text_.width(Face::futura_bold, 24, part);
                    if (to < body.size()) x = 40 + off + ((x - 40 - off) / tab + 1) * tab;
                    from = to + 1;
                }
            }
        }
        return;
    }
    // Title box (0x0041f770): Badloc 72 on the darkened picture, orange with a black shadow.
    const std::string& title = str(title_slot_);
    if (title_slot_ && !title.empty() && title != "*") {
        TextRect m{0, 0, 640, 480};
        text_.draw_box(nullptr, Face::badloc, 72, m, 0, title, dt::calcrect);
        const int tw = m.right - m.left, th = m.bottom - m.top;
        TextRect box;
        if (title_format_ & 4)
            box = {0, 0, 640, 300};
        else if (title_format_ & 1) {
            const int w = tw + 16, h = th + 16;
            box = {320 - w / 2, (480 - h) * 3 / 4, 320 - w / 2 + w, (480 - h) * 3 / 4 + h};
        } else if (title_format_ & 2) {
            const int w = tw < 0x10c ? 0x13c : tw + 0x31;
            box = {640 - w, 0, 640, th + 0x2a};
        } else {
            int w = tw + 0x31;
            if (title_slot_ == 6 && w < 0xe6) w = 0xe6;
            if (w > 640) w = 640; // item 3: clamp the banner to the screen
            box = {0, 0, w, th + 0x2a};
        }
        fill_from(out, dark_, box.left, box.top, box.right - box.left, box.bottom - box.top);
        const auto [t, flags] = title_text_layout(box, title_format_, title_slot_);
        draw_text_box(out, t, Face::badloc, 72, title, flags, colour_orange);
    }
    // Description box (0x00420470): darkened, Futura, pale text with a black shadow.
    std::string desc = str(desc_slot_);
    if (screen_ == 0x18) {
        // The showroom draws the blurb here and the Length line as its own
        // footer below; the embedded copy would double-draw.
        const size_t blank = desc.find("\n\n");
        if (blank != std::string::npos) desc.resize(blank);
    }
    if (desc_slot_ && !desc.empty() && desc != "*") {
        const Face face = screen_ == 0x1a        ? Face::futura_regular
                          : (desc_flags_ & 0x80) ? Face::futura_demi
                                                 : Face::futura_regular;
        const bool native_help =
            desc_slot_ == dynamic_slot &&
            (screen_ >= screen_id::native_display ||
             (screen_ == 0x13 && focus_ >= 0 && items_[size_t(focus_)].caption == ns::display));
        const int height = native_help ? 24 : screen_ == 0x1a ? 36 : (desc_flags_ & 0x80) ? 24 : 32;
        TextRect box;
        if (native_help)
            box = {316, 122, 632, 296};
        else if (desc_flags_ & 0x1000)
            box = {0, 300, 640, 0x1a4};
        else {
            const bool wide = (desc_flags_ & 0x900) != 0;
            const int maxw = wide ? 0x270 : 300;
            TextRect m{0, 0, maxw, 0};
            text_.draw_box(nullptr, face, height, m, 0, desc, dt::wordbreak | dt::calcrect);
            const int w = std::min(maxw, m.right - m.left), h = std::min(0x186, m.bottom - m.top);
            int x = (desc_format_ & 2) ? 0x13c : 8, y = 0x7a;
            if (wide) {
                x = (0x270 - w) / 2;
                y = 0x17c - (screen_ == 0x0b ? 16 : 0);
            }
            box = {x, y, x + w + 16, y + h + 16};
        }
        // 0x00420470 repaints the dark box only when the focused item takes
        // it (desc_fill_): characters-screen bios are text on the photo.
        if (desc_fill_ || native_help)
            fill_from(out, dark_, box.left, box.top, box.right - box.left, box.bottom - box.top);
        const auto [t, flags] = native_help
                                    ? std::make_pair(TextRect{326, 128, 626, 292}, unsigned(dt::wordbreak))
                                    : description_text_layout(box, desc_flags_, desc_format_);
        draw_text_box(out, t, face, height, desc, flags, colour_light);
    }
    if (screen_ == 0x18 && !length_footer_.empty()) {
        // Traced showroom Length footer: Demi, right-aligned, top y=301.
        const int w = text_.width(Face::futura_demi, 24, length_footer_);
        draw_text_box(out, {621 - w, 300, 621, 332}, Face::futura_demi, 24, length_footer_,
                      dt::right | dt::singleline, colour_light);
    }
    if (!price_text_.empty()) {
        TextRect box{0x10, 0x154, 0x10 + 0xf0 + 16, 0x154 + 30 + 16};
        fill_from(out, dark_, box.left, box.top, box.right - box.left, box.bottom - box.top);
        draw_text_box(out, {box.left + 8, box.top + 8, box.right - 8, box.bottom}, Face::futura_regular, 32,
                      price_text_, dt::center, colour_light);
    }
    // Buttons (0x0041fc90 / 0x0041ff80).
    for (size_t i = 0; i < items_.size(); ++i) {
        const MenuItem& m = items_[i];
        if (m.w <= 0 || m.h <= 0) continue;
        // Standings rows are handler-enabled (flags 0x40); direct entry
        // shows none, matching the original bare screen.
        if (screen_ == 0x22 && !(m.flags & 0x40)) continue;
        const std::string caption = caption_text(m);
        if (!(m.flags & item_flag::keep_background))
            fill_from(out, dark_, m.x, m.y, m.w, m.h);
        else if (m.caption == 0)
            continue;
        if (caption.empty() || caption == "*") continue;
        uint32_t colour;
        if (!m.enabled)
            colour = (m.flags & item_flag::light) ? colour_light : colour_grey;
        else if (int(i) != focus_ && !(m.flags & 0x10000))
            colour = (m.flags & item_flag::skip)     ? colour_orange
                     : (m.flags & item_flag::dimmed) ? colour_grey
                                                     : colour_light;
        else
            colour = m.caption == 0xb8 ? 0xffffff : colour_focus;
        if (screen_ == 0x22) {
            // Standings in a fixed-pitch layout (original: New Courier Bold 18).
            const int cell = text_.width(Face::futura_bold, 20, "0");
            int x = m.x + 0x19;
            for (char c : caption) {
                const std::string ch(1, c);
                text_.draw(out, Face::futura_bold, 20,
                           x + (cell - text_.width(Face::futura_bold, 20, ch)) / 2, m.y + 2, 0, ch);
                text_.draw(out, Face::futura_bold, 20,
                           x + (cell - text_.width(Face::futura_bold, 20, ch)) / 2 - 2, m.y, colour, ch);
                x += cell;
            }
            continue;
        }
        const bool wide = m.x == 0 && m.w == 640;
        if (wide) {
            draw_text_box(out, {m.x + 0x19, m.y + 2, m.x + m.w, m.y + m.h}, Face::futura_regular, 32, caption,
                          dt::wordbreak | dt::center, colour_light);
        } else {
            draw_text_box(out, {m.x + 0x19, m.y + 2, m.x + m.w, m.y + m.h}, Face::futura_bold, 24, caption,
                          dt::wordbreak, colour);
            if (colour == colour_focus || colour == 0xffffff) {
                // Wingdings arrow (0x0046b044) substitute: a right-pointing triangle.
                for (int pass = 0; pass < 2; ++pass) {
                    const int ox = m.x + 6 + (pass ? 0 : 2), oy = m.y + 6 + (pass ? 0 : 2);
                    for (int y = 0; y < 13; ++y)
                        for (int x = 0; x <= 6 - std::abs(y - 6); ++x)
                            if (out.contains(ox + x, oy + y))
                                out.at(ox + x, oy + y) = pass ? colour_yellow : 0;
                }
            }
        }
    }
    // Standings footer (dynamic slot: "%s finished in ... Place."): drawn in
    // the last row's rect on every visit once set (direct entry included).
    if (screen_ == 0x22 && items_.size() == 15) {
        const std::string& foot = str(dynamic_slot);
        if (!foot.empty() && foot != "*") {
            const MenuItem& last = items_.back();
            draw_text_box(out, {last.x + 0x19, last.y + 2, last.x + last.w, last.y + last.h},
                          Face::futura_regular, 32, foot, dt::wordbreak | dt::center, colour_light);
        }
    }
    draw_net_overlay(out, 1);
    if (qualified_overlay_) {
        const std::string& q = str(181);
        text_.draw(out, Face::badloc, 42, 0x1c4, 0x110, 0, q);
        text_.draw(out, Face::badloc, 42, 0x1c2, 0x10e, colour_yellow, q);
    }
    if (!dialog_.text.empty()) {
        // Native modal box for the original MessageBoxA prompts.
        TextRect m{0, 0, 520, 0};
        text_.draw_box(nullptr, Face::futura_regular, 28, m, 0, dialog_.text, dt::wordbreak | dt::calcrect);
        const int h = m.bottom - m.top;
        const int top = std::max(20, 290 - h - 16);
        fill_from(out, dark_, 40, top - 16, 560, 340 - top + 32);
        for (int x = 40; x < 600; ++x) {
            out.at(x, top - 16) = colour_light;
            out.at(x, 340 + 15) = colour_light;
        }
        draw_text_box(out, {60, top, 580, 290}, Face::futura_regular, 28, dialog_.text,
                      dt::wordbreak | dt::center, colour_light);
        const int n = int(dialog_.buttons.size());
        for (int i = 0; i < n; ++i) {
            const int bx = 320 - n * 60 + i * 120;
            draw_text_box(out, {bx, 300, bx + 110, 330}, Face::futura_bold, 24,
                          str(dialog_.buttons[size_t(i)].first), dt::center,
                          i == dialog_.focus ? colour_focus : colour_light);
        }
    }
}

void Frontend::draw_scaled(RgbImage& out, int scale) const {
    // Scaled menu path (scale 2..4). Structure mirrors draw_scale1 with every
    // geometry term multiplied by scale and text at height x scale.
    const RgbImage& bg = dark_screen_ ? dark_ : background_;
    out = RgbImage(640 * scale, 480 * scale, 0);
    auto blit_bg = [&](int dx, int dy, int w, int h) {
        for (int yy = 0; yy < h * scale; ++yy)
            for (int xx = 0; xx < w * scale; ++xx) {
                const int sx = dx + xx / scale, sy = dy + yy / scale;
                if (sx < bg.width && sy < bg.height) out.at(dx * scale + xx, dy * scale + yy) = bg.at(sx, sy);
            }
    };
    blit_bg(0, 0, 640, 480);
    auto textbox = [&](TextRect r, Face face, int height, const std::string& str, unsigned flags,
                       uint32_t colour) {
        TextRect rs{r.left * scale, r.top * scale, r.right * scale, r.bottom * scale};
        TextRect shadow = rs;
        text_.draw_box(&out, face, height * scale, shadow, 0, str, flags);
        TextRect top{rs.left - 2 * scale, rs.top - 2 * scale, rs.right - 2 * scale, rs.bottom - 2 * scale};
        text_.draw_box(&out, face, height * scale, top, colour, str, flags);
    };
    auto fill = [&](int x, int y, int w, int h) {
        for (int yy = 0; yy < h * scale; ++yy)
            for (int xx = 0; xx < w * scale; ++xx) {
                const int sx = x + xx / scale, sy = y + yy / scale;
                if (sx < dark_.width && sy < dark_.height)
                    out.at(x * scale + xx, y * scale + yy) = dark_.at(sx, sy);
            }
    };
    if (credit_set_ >= 0) {
        // 0x00424340: twenty 24-pixel lines in Futura Condensed Bold 24; the first character picks the colour.
        const auto& lines = credits_[size_t(credit_set_)];
        const bool centred = credit_set_ == 1;
        const int tab =
            35 * text_.width(Face::futura_bold, 24 * scale, "x"); // DRAWTEXTPARAMS iTabLength 35, margin 40
        int y = credit_y_ * scale;
        for (size_t i = size_t(credit_line_); i < lines.size() && i < size_t(credit_line_) + 20;
             ++i, y += 0x18 * scale) {
            const std::string& l = lines[i];
            if (l.empty()) continue;
            uint32_t colour = colour_light;
            switch (l[0]) {
            case 'G':
                colour = colour_grey;
                break;
            case 'O':
                colour = colour_orange;
                break;
            case 'R':
                colour = 0xff0000;
                break;
            case 'Y':
                colour = colour_focus;
                break;
            default:
                break;
            }
            const std::string body = l.substr(1);
            for (int pass = 0; pass < 2; ++pass) {
                const int off = pass ? -2 * scale : 0;
                const uint32_t col = pass ? colour : 0;
                if (centred) {
                    TextRect r{off, y + off, 640 * scale + off, y + 0x18 * scale + off};
                    text_.draw_box(&out, Face::futura_bold, 24 * scale, r, col, body,
                                   dt::center | dt::singleline);
                    continue;
                }
                int x = 40 * scale + off;
                size_t from = 0;
                while (from <= body.size()) {
                    size_t to = body.find('\t', from);
                    if (to == std::string::npos) to = body.size();
                    const std::string part = body.substr(from, to - from);
                    text_.draw(out, Face::futura_bold, 24 * scale, x, y + off, col, part);
                    x += text_.width(Face::futura_bold, 24 * scale, part);
                    if (to < body.size()) x = 40 * scale + off + ((x - 40 * scale - off) / tab + 1) * tab;
                    from = to + 1;
                }
            }
        }
        return;
    }
    // Title box (0x0041f770): Badloc 72 on the darkened picture, orange with a black shadow.
    const std::string& title = str(title_slot_);
    if (title_slot_ && !title.empty() && title != "*") {
        TextRect m{0, 0, 640, 480};
        text_.draw_box(nullptr, Face::badloc, 72, m, 0, title, dt::calcrect);
        const int tw = m.right - m.left, th = m.bottom - m.top;
        TextRect box;
        if (title_format_ & 4)
            box = {0, 0, 640, 300};
        else if (title_format_ & 1) {
            const int w = tw + 16, h = th + 16;
            box = {320 - w / 2, (480 - h) * 3 / 4, 320 - w / 2 + w, (480 - h) * 3 / 4 + h};
        } else if (title_format_ & 2) {
            const int w = tw < 0x10c ? 0x13c : tw + 0x31;
            box = {640 - w, 0, 640, th + 0x2a};
        } else {
            int w = tw + 0x31;
            if (title_slot_ == 6 && w < 0xe6) w = 0xe6;
            box = {0, 0, w, th + 0x2a};
        }
        fill(box.left, box.top, box.right - box.left, box.bottom - box.top);
        const auto [t, flags] = title_text_layout(box, title_format_, title_slot_);
        textbox(t, Face::badloc, 72, title, flags, colour_orange);
    }
    // Description box (0x00420470): darkened, Futura, pale text with a black shadow.
    std::string desc = str(desc_slot_);
    if (screen_ == 0x18) {
        // The showroom draws the blurb here and the Length line as its own
        // footer below; the embedded copy would double-draw.
        const size_t blank = desc.find("\n\n");
        if (blank != std::string::npos) desc.resize(blank);
    }
    if (desc_slot_ && !desc.empty() && desc != "*") {
        const Face face = screen_ == 0x1a        ? Face::futura_regular
                          : (desc_flags_ & 0x80) ? Face::futura_demi
                                                 : Face::futura_regular;
        const bool native_help =
            desc_slot_ == dynamic_slot &&
            (screen_ >= screen_id::native_display ||
             (screen_ == 0x13 && focus_ >= 0 && items_[size_t(focus_)].caption == ns::display));
        const int height = native_help ? 24 : screen_ == 0x1a ? 36 : (desc_flags_ & 0x80) ? 24 : 32;
        TextRect box;
        if (native_help)
            box = {316, 122, 632, 296};
        else if (desc_flags_ & 0x1000)
            box = {0, 300, 640, 0x1a4};
        else {
            const bool wide = (desc_flags_ & 0x900) != 0;
            const int maxw = wide ? 0x270 : 300;
            TextRect m{0, 0, maxw, 0};
            text_.draw_box(nullptr, face, height, m, 0, desc, dt::wordbreak | dt::calcrect);
            const int w = std::min(maxw, m.right - m.left), h = std::min(0x186, m.bottom - m.top);
            int x = (desc_format_ & 2) ? 0x13c : 8, y = 0x7a;
            if (wide) {
                x = (0x270 - w) / 2;
                y = 0x17c - (screen_ == 0x0b ? 16 : 0);
            }
            box = {x, y, x + w + 16, y + h + 16};
        }
        if (desc_fill_ || native_help) fill(box.left, box.top, box.right - box.left, box.bottom - box.top);
        const auto [t, flags] = native_help
                                    ? std::make_pair(TextRect{326, 128, 626, 292}, unsigned(dt::wordbreak))
                                    : description_text_layout(box, desc_flags_, desc_format_);
        textbox(t, face, height, desc, flags, colour_light);
    }
    if (screen_ == 0x18 && !length_footer_.empty()) {
        const int w = text_.width(Face::futura_demi, 24, length_footer_);
        textbox({621 - w, 300, 621, 332}, Face::futura_demi, 24, length_footer_, dt::right | dt::singleline,
                colour_light);
    }
    if (!price_text_.empty()) {
        TextRect box{0x10, 0x154, 0x10 + 0xf0 + 16, 0x154 + 30 + 16};
        fill(box.left, box.top, box.right - box.left, box.bottom - box.top);
        textbox({box.left + 8, box.top + 8, box.right - 8, box.bottom}, Face::futura_regular, 32, price_text_,
                dt::center, colour_light);
    }
    // Buttons (0x0041fc90 / 0x0041ff80).
    for (size_t i = 0; i < items_.size(); ++i) {
        const MenuItem& m = items_[i];
        if (m.w <= 0 || m.h <= 0) continue;
        // Standings rows are handler-enabled (flags 0x40); direct entry
        // shows none, matching the original bare screen.
        if (screen_ == 0x22 && !(m.flags & 0x40)) continue;
        const std::string caption = caption_text(m);
        if (!(m.flags & item_flag::keep_background))
            fill(m.x, m.y, m.w, m.h);
        else if (m.caption == 0)
            continue;
        if (caption.empty() || caption == "*") continue;
        uint32_t colour;
        if (!m.enabled)
            colour = (m.flags & item_flag::light) ? colour_light : colour_grey;
        else if (int(i) != focus_ && !(m.flags & 0x10000))
            colour = (m.flags & item_flag::skip)     ? colour_orange
                     : (m.flags & item_flag::dimmed) ? colour_grey
                                                     : colour_light;
        else
            colour = m.caption == 0xb8 ? 0xffffff : colour_focus;
        if (screen_ == 0x22) {
            // Standings in a fixed-pitch layout (original: New Courier Bold 18).
            const int cell = text_.width(Face::futura_bold, 20 * scale, "0");
            int x = (m.x + 0x19) * scale;
            for (char c : caption) {
                const std::string ch(1, c);
                text_.draw(out, Face::futura_bold, 20 * scale,
                           x + (cell - text_.width(Face::futura_bold, 20 * scale, ch)) / 2,
                           m.y * scale + 2 * scale, 0, ch);
                text_.draw(out, Face::futura_bold, 20 * scale,
                           x + (cell - text_.width(Face::futura_bold, 20 * scale, ch)) / 2 - 2 * scale,
                           m.y * scale, colour, ch);
                x += cell;
            }
            continue;
        }
        const bool wide = m.x == 0 && m.w == 640;
        if (wide) {
            textbox({m.x + 0x19, m.y + 2, m.x + m.w, m.y + m.h}, Face::futura_regular, 32, caption,
                    dt::wordbreak | dt::center, colour_light);
        } else {
            textbox({m.x + 0x19, m.y + 2, m.x + m.w, m.y + m.h}, Face::futura_bold, 24, caption,
                    dt::wordbreak, colour);
            if (colour == colour_focus || colour == 0xffffff) {
                // Wingdings arrow (0x0046b044) substitute: a right-pointing triangle.
                for (int pass = 0; pass < 2; ++pass) {
                    const int ox = (m.x + 6 + (pass ? 0 : 2)) * scale,
                              oy = (m.y + 6 + (pass ? 0 : 2)) * scale;
                    for (int y = 0; y < 13 * scale; ++y)
                        for (int x = 0; x <= 6 * scale - std::abs(y - 6 * scale); ++x)
                            if (out.contains(ox + x, oy + y))
                                out.at(ox + x, oy + y) = pass ? colour_yellow : 0;
                }
            }
        }
    }
    // Standings footer, mirroring draw_scale1 (dynamic slot in the last row's rect).
    if (screen_ == 0x22 && items_.size() == 15) {
        const std::string& foot = str(dynamic_slot);
        if (!foot.empty() && foot != "*") {
            const MenuItem& last = items_.back();
            textbox({last.x + 0x19, last.y + 2, last.x + last.w, last.y + last.h}, Face::futura_regular, 32,
                    foot, dt::wordbreak | dt::center, colour_light);
        }
    }
    draw_net_overlay(out, scale);
    if (qualified_overlay_) {
        const std::string& q = str(181);
        text_.draw(out, Face::badloc, 42 * scale, 0x1c4 * scale, 0x110 * scale, 0, q);
        text_.draw(out, Face::badloc, 42 * scale, 0x1c2 * scale, 0x10e * scale, colour_yellow, q);
    }
    if (!dialog_.text.empty()) {
        // Native modal box for the original MessageBoxA prompts.
        TextRect m{0, 0, 520, 0};
        text_.draw_box(nullptr, Face::futura_regular, 28, m, 0, dialog_.text, dt::wordbreak | dt::calcrect);
        const int h = m.bottom - m.top;
        const int top = std::max(20, 290 - h - 16);
        fill(40, top - 16, 560, 340 - top + 32);
        for (int x = 40 * scale; x < 600 * scale; ++x) {
            out.at(x, (top - 16) * scale) = colour_light;
            out.at(x, (340 + 15) * scale) = colour_light;
        }
        textbox({60, top, 580, 290}, Face::futura_regular, 28, dialog_.text, dt::wordbreak | dt::center,
                colour_light);
        const int n = int(dialog_.buttons.size());
        for (int i = 0; i < n; ++i) {
            const int bx = 320 - n * 60 + i * 120;
            textbox({bx, 300, bx + 110, 330}, Face::futura_bold, 24, str(dialog_.buttons[size_t(i)].first),
                    dt::center, i == dialog_.focus ? colour_focus : colour_light);
        }
    }
}
} // namespace rr
