#include "frontend.hpp"
#include "native_screens.hpp"
#include "native_strings.hpp"
#include <algorithm>
#include <filesystem>

namespace rr {
namespace {
std::string substitute(std::string text, std::initializer_list<std::string> args) {
    for (const auto& arg : args) {
        size_t p = text.find("%s"), d = text.find("%d");
        if (d < p) p = d;
        if (p != std::string::npos) text.replace(p, 2, arg);
    }
    return text;
}
} // namespace
void Frontend::net_attach(RoomClientFactory factory, net::Hello hello) {
    net_factory_ = std::move(factory);
    net_hello_ = std::move(hello);
    for (auto& item : data_.screens[9].items)
        if (item.handler == 0x00430140) item.enabled = bool(net_factory_);
    if (screen_ == 9)
        for (auto& item : items_)
            if (item.handler == 0x00430140) item.enabled = bool(net_factory_);
}
bool Frontend::net_in_room() const {
    return net_ && net_->in_room();
}
void Frontend::net_ensure() {
    if (net_) return;
    auto hello = net_hello_;
    const auto alias = !settings_.alias.empty() ? settings_.alias : std::string(career_.state.name.data());
    hello.alias = (alias.empty() ? player_name() : alias).substr(0, net::max_alias);
    net_ = std::make_unique<net::RoomClient>(net_factory_, std::move(hello));
}
void Frontend::net_close_connecting() {
    if (dialog_.kind == 4) dialog_ = {};
    net_connecting_ = false;
}
void Frontend::net_show_error() {
    if (!net_ || net_->error().empty() || net_->error() == net_shown_error_) return;
    net_close_connecting();
    net_shown_error_ = net_->error();
    show_dialog(net_shown_error_, 3, {{205, 0}, {ns::net_leave, 2}});
}
void Frontend::net_open_host() {
    if (!net_factory_) return;
    net_pending_options_ = {};
    open_popup(data_.popups.at(native_popup::net_level), 1, native_setter::net_level);
}
void Frontend::net_open_rooms() {
    net_ensure();
    net_->request_list();
    enter(screen_id::net_rooms);
}
void Frontend::net_join_shown(int index) {
    if (index < 0 || size_t(index) >= net_shown_rooms_.size()) return;
    net_->join_room_text(net_shown_rooms_[size_t(index)].code, net_spectate_);
    net_connecting_ = true;
    show_dialog(str(ns::net_connecting), 4, {{204, 0}});
}
void Frontend::net_join_invite(const std::string& text) {
    if (!net_factory_) return;
    career_.mode = GameMode::mano;
    // Web launcher intents ('@' never appears in a room code or invite link):
    // "@host" opens the Host popup, "@rooms" the public list, and
    // "@watch:CODE" joins CODE as a spectator.
    if (text == "@host" || text == "@rooms") {
        enter(0x0b);
        text == "@host" ? net_open_host() : net_open_rooms();
        return;
    }
    std::string join = text;
    if (join.starts_with("@watch:")) {
        net_spectate_ = true;
        join.erase(0, 7);
    }
    net_ensure();
    net_->join_room_text(join, net_spectate_);
    enter(0x0b);
    net_connecting_ = true;
    show_dialog(str(ns::net_connecting), 4, {{204, 0}});
}
std::string Frontend::net_roster_line(int slot) const {
    if (!net_in_room()) return {};
    const auto& info = net_->room()->seats[size_t(slot)];
    if (!info.occupied) return {};
    return info.alias + "  " + (info.character ? str(357 + info.character) : "Choose a character") + "  " +
           std::to_string(info.ping_ms) + " ms" + (info.connected ? "" : " (away)");
}
void Frontend::net_prepare_lobby() {
    auto& screen = data_.screens[screen_id::net_lobby];
    strings_[1048] = net_in_room() ? "Room " + net_->room()->code : str(ns::net_lobby_title);
    screen.title = 1048;
    for (int slot = 0; slot < 8; ++slot) {
        strings_[1040 + slot] = net_roster_line(slot);
        screen.items[size_t(slot)].w = 300;
    }
    screen.items[8].enabled = screen.items[9].enabled = screen.items[10].enabled = net_in_room();
}
void Frontend::net_prepare_rooms() {
    net_shown_rooms_ = net_ ? net_->rooms() : std::vector<net::RoomListEntry>{};
    auto& rows = data_.screens[screen_id::net_rooms].items;
    for (size_t i = 0; i < 8; ++i) {
        rows[i].w = 300;
        rows[i].enabled = i < net_shown_rooms_.size();
        strings_[1008 + i].clear();
        if (rows[i].enabled) {
            const auto& room = net_shown_rooms_[i];
            strings_[1008 + i] = room.host + "  " + std::to_string(room.level + 1) + "  " +
                                 std::to_string(8 - room.humans) + "  " + str(409 + int(room.status)) + "  " +
                                 room.code;
        }
    }
    strings_[1016] = str(413) + " / " + str(414) + " / " + str(415) + " / " + str(416);
    data_.screens[screen_id::net_rooms].title = ns::net_rooms_title;
}
void Frontend::net_update() {
    if (!net_) return;
    net_->update(now_);
    net_show_error();
    if (net_in_room()) {
        const auto code = net_->room()->code;
        std::string token;
        if (net_->seat_token())
            for (auto b : *net_->seat_token()) {
                const char* hex = "0123456789abcdef";
                token += hex[b >> 4];
                token += hex[b & 15];
            }
        if (settings_.net_room != code || settings_.net_seat != token) {
            settings_.net_room = code;
            settings_.net_seat = token;
            save_settings((std::filesystem::path(save_dir_).parent_path() / "settings.txt").string(),
                          settings_);
        }
        if (net_connecting_) {
            net_close_connecting();
            character_return_ = screen_id::net_lobby;
            enter(0x0e);
        }
    }
    for (auto& chat : net_->take_chat()) {
        net_chat_log_.push_back(chat.alias + ": " + chat.text);
        if (net_chat_log_.size() > 6) net_chat_log_.erase(net_chat_log_.begin());
    }
    for (const auto& e : net_->take_roster()) {
        std::string line;
        if (e.kind == net::RoomClient::RosterEvent::Kind::joined)
            line = substitute(str(404), {e.alias});
        else if (e.kind == net::RoomClient::RosterEvent::Kind::left)
            line = substitute(str(420), {e.alias});
        else if (e.kind == net::RoomClient::RosterEvent::Kind::character)
            line = substitute(str(405), {e.alias, str(357 + e.value)});
        else
            line = substitute(str(407), {e.alias, str(15 + e.value)});
        net_chat_log_.push_back(line);
        if (net_chat_log_.size() > 6) net_chat_log_.erase(net_chat_log_.begin());
    }
    if (auto start = net_->take_start()) {
        net_close_connecting();
        FrontendEvent event;
        event.kind = FrontendEvent::Kind::start_net_race;
        event.net_start = *start;
        events_.push_back(std::move(event));
        return;
    }
    if (!popup_base_ &&
        (screen_ == screen_id::net_lobby || screen_ == screen_id::net_rooms || screen_ == 12) &&
        now_ >= net_lobby_at_) {
        net_lobby_at_ = now_ + 250;
        const int focused = focus_;
        if (screen_ == screen_id::net_lobby) net_prepare_lobby();
        if (screen_ == screen_id::net_rooms) net_prepare_rooms();
        if (screen_ == 12 && net_in_room()) {
            strings_[1049] = substitute(
                str(312), {std::to_string(net_->vote_count()), std::to_string(net_->occupied_count())});
            title_box(1049, data_.screens[12].title_format);
        } else {
            items_ = data_.screens[size_t(screen_)].items;
            if (focused >= 0 && size_t(focused) < items_.size() && items_[size_t(focused)].enabled)
                set_focus(size_t(focused));
            else
                for (size_t i = 0; i < items_.size(); ++i)
                    if (items_[i].enabled) {
                        set_focus(i);
                        break;
                    }
            title_box(data_.screens[size_t(screen_)].title, data_.screens[size_t(screen_)].title_format);
        }
    }
}
bool Frontend::net_edit_input(const MenuInput& in) {
    if (!editing_code_ && editing_chat_ < 0) return false;
    auto& buffer = editing_code_ ? net_code_buffer_ : chat_messages_[size_t(editing_chat_)];
    const size_t limit = editing_code_ ? 256 : net::max_chat_text;
    for (char c : in.text)
        if (uint8_t(c) >= 32 && buffer.size() < limit) buffer.push_back(c);
    if (in.backspace && !buffer.empty()) buffer.pop_back();
    if (in.back) {
        editing_code_ = false;
        editing_chat_ = -1;
        return true;
    }
    if (in.select) {
        if (editing_code_) {
            net_->join_room_text(buffer, net_spectate_);
            editing_code_ = false;
            net_connecting_ = true;
            show_dialog(str(ns::net_connecting), 4, {{204, 0}});
        } else {
            settings_.net_chat = chat_messages_;
            save_settings((std::filesystem::path(save_dir_).parent_path() / "settings.txt").string(),
                          settings_);
            editing_chat_ = -1;
            for (size_t i = 0; i < 8; ++i)
                strings_[1056 + i] = "F" + std::to_string(i + 1) + " " + chat_messages_[i];
        }
    }
    return true;
}
void Frontend::mano_finished(const net::Official& official, const std::vector<std::string>& aliases,
                             int local) {
    const auto result = mano_official(official, aliases, local);
    mano_results_ = true;
    net_video_rng_ = net_video_rng_ * 1664525u + 1013904223u;
    queue_video(
        result.video_base + std::to_string(net_video_rng_ % (result.video_base == "LOSE" ? 10 : 6) + 1), 0);
    report_ = {};
    report_.screen = result.screen;
    standings_.clear();
    for (const auto& row : result.table) standings_.push_back({row.name, -1, row.player});
    // The native mano table contains every official seat (no career prizes).
    if (!offline_standings_) offline_standings_ = data_.screens[0x22];
    auto& table = data_.screens[0x22];
    const auto prototype = table.items.front();
    table.items.clear();
    std::vector<net::OfficialRow> rows = official.rows;
    std::stable_sort(rows.begin(), rows.end(), [](const auto& a, const auto& b) {
        return (a.place ? a.place : 255) < (b.place ? b.place : 255);
    });
    for (size_t i = 0; i < rows.size(); ++i) {
        const auto& row = rows[i];
        auto item = prototype;
        item.x = 24;
        item.y = 130 + int(i) * 32;
        item.w = 590;
        item.h = 30;
        strings_[1064 + i] = (row.place ? std::to_string(row.place) : "--") + ". " +
                             (row.seat < aliases.size() ? aliases[row.seat] : "Racer");
        item.caption = 1064 + int(i);
        item.handler = 0;
        item.target = 0;
        item.enabled = false;
        item.flags = item_flag::light;
        table.items.push_back(item);
    }
    auto back = prototype;
    back.caption = ns::back;
    back.y = 412;
    back.h = 38;
    back.enabled = true;
    back.handler = 0;
    back.target = 11;
    back.flags = item_flag::escape | item_flag::light;
    table.items.push_back(back);
    table.title = official.agreed ? 441 : ns::net_unofficial;
    for (size_t i = 0; i < table.items.size(); ++i) table.items[i].nav.fill(uint8_t(table.items.size() - 1));
    enter(result.screen);
}
void Frontend::net_results_table() {
    standings_ready_ = true;
    enter(0x22);
}
void Frontend::draw_net_overlay(RgbImage& out, int scale) const {
    if (screen_ == screen_id::net_rooms) {
        TextRect box{24 * scale, 114 * scale, 620 * scale, 132 * scale};
        text_.draw_box(&out, Face::futura_bold, 16 * scale, box, 0xdefff0, str(1016), 0);
    }
    if (screen_ != screen_id::net_lobby || net_chat_log_.empty()) return;
    for (int y = 304 * scale; y < 468 * scale; ++y)
        for (int x = 316 * scale; x < 636 * scale; ++x)
            if (out.contains(x, y)) out.at(x, y) = dark_.at(x / scale, y / scale);
    for (size_t i = 0; i < net_chat_log_.size(); ++i) {
        TextRect box{322 * scale, (308 + int(i) * 26) * scale, 632 * scale, (334 + int(i) * 26) * scale};
        text_.draw_box(&out, Face::futura_regular, 16 * scale, box, 0xdefff0, net_chat_log_[i],
                       dt::wordbreak);
    }
}

} // namespace rr
