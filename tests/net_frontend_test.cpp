#include <cstdlib>
#include "assets.hpp"
#include "check.hpp"
#include "frontend.hpp"
#include "native_screens.hpp"
#include "net/client.hpp"
#include "net/compat.hpp"
#include "room.hpp"
#include "race_screen.hpp"
#include "pe_strings.hpp"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>

int main() {
    rr::test::Checks check;
    const std::string root = std::string(RR_SOURCE_DIR) + "/../cdrom/ROADRASH";
    if (!std::filesystem::exists(root + "/ROADRASH.EXE")) return 77;
    const auto dir =
        std::filesystem::temp_directory_path() /
        ("rr-net-front-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(dir);
    rr::Assets assets(root);
    auto exe = assets.read("ROADRASH.EXE");
    auto resources = rr::read_string_resources(assets.read("TEXT/ENU.DLL"));
    rr::TextRenderer text(assets);
    auto tables = rr::read_career_tables(exe);
    rr::Career hc, gc;
    hc.state.cash = 12345;
    hc.state.qualified = 7;
    hc.state.character = 3;
    hc.rng = 4321;
    hc.mode = rr::GameMode::big_game;
    gc = hc;
    rr::Settings hs, gs;
    hs.alias = "Host";
    gs.alias = "Guest";
    auto data = rr::read_frontend_data(exe);
    rr::add_native_screens(data, false);
    rr::Frontend host(assets, text, rr::frontend_strings(resources), rr::credit_lines(resources), data,
                      tables, hc, hs, (dir / "host/saves").string());
    rr::Frontend guest(assets, text, rr::frontend_strings(resources), rr::credit_lines(resources), data,
                       tables, gc, gs, (dir / "guest/saves").string());
    const auto career_before = rr::write_save(hc);
    const uint32_t rng_before = hc.rng;
    uint64_t now = 1000;
    rr::netd::ServerConfig config;
    config.seed = 42;
    config.load_ms = 0;
    rr::netd::Server server(config);
    auto factory = [&] {
        auto pair = rr::net::loopback_pair({}, true, [&] { return now; });
        server.add_connection(std::move(pair.second), now);
        return std::move(pair.first);
    };
    auto hello =
        rr::net::make_hello("ignored", rr::net::Platform::bot, rr::net::verified_data_digest(assets));
    host.net_attach(factory, hello);
    guest.net_attach(factory, hello);
    auto pump = [&] {
        for (int i = 0; i < 60; ++i) {
            now += 10;
            server.step(now);
            host.update({}, now);
            guest.update({}, now);
        }
    };
    auto click = [&](rr::Frontend& front, auto predicate) {
        for (const auto& item : front.items())
            if (item.enabled && predicate(item)) {
                rr::MenuInput input;
                input.pointer_click = true;
                input.pointer_x = item.x + 2;
                input.pointer_y = item.y + item.h / 2;
                front.update(input, ++now);
                return true;
            }
        check(false, "requested menu action exists");
        return false;
    };
    auto handler = [&](rr::Frontend& front, uint32_t address) {
        return click(front, [&](auto m) { return m.handler == address; });
    };
    auto choice = [&](rr::Frontend& front, int target) {
        return click(front, [&](auto m) { return m.handler == 0x00445440 && m.target == target; });
    };
    host.enter(9);
    guest.enter(9);
    handler(host, 0x00430140);
    handler(guest, 0x00430140);
    check(host.screen() == 11, "main mano entry enabled after attaching transport");
    handler(host, 0x0042fc40);
    choice(host, 1);
    choice(host, 1);
    choice(host, 2);
    choice(host, 1);
    pump();
    check(host.net_in_room() && host.screen() == 14, "Host wizard creates room then character screen");
    handler(host, 0x0041f5a0);
    pump();
    check(host.screen() == rr::screen_id::net_lobby, "character selection returns roster");
    handler(guest, 0x0042fef0);
    pump();
    check(guest.screen() == rr::screen_id::net_rooms, "Join shows compatible public rooms");
    choice(guest, 1);
    pump();
    check(guest.net_in_room() && guest.screen() == 14, "room list joins then asks character");
    click(guest, [](auto m) { return m.handler == 0x0041f5a0 && m.target == 2; });
    pump();
    handler(host, rr::native_action::net_copy);
    bool copied = false;
    rr::FrontendEvent event;
    while (host.poll(event))
        if (event.kind == rr::FrontendEvent::Kind::copy_text)
            copied = event.text == host.net_client()->room()->code;
    check(copied, "copy action exposes exact room code");
    // Render every network screen for 1x/2x layout inspection (local assets only).
    const char* captures = std::getenv("RR_NET_MENU_CAPTURE");
    auto capture = [&](rr::Frontend& front, const std::string& name) {
        if (!captures) return;
        for (int scale : {1, 2}) {
            rr::RgbImage image;
            front.draw(image, scale);
            std::ofstream out(std::filesystem::path(captures) / (name + "-" + std::to_string(scale) + ".ppm"),
                              std::ios::binary);
            out << "P6\n" << image.width << " " << image.height << "\n255\n";
            for (auto pixel : image.pixels) {
                out.put(char(pixel >> 16));
                out.put(char(pixel >> 8));
                out.put(char(pixel));
            }
        }
    };
    capture(host, "roster");
    host.enter(13);
    click(host, [](auto m) { return m.handler == 0x00430380 && m.target == 0; });
    rr::MenuInput typed;
    typed.text = "Catch me!";
    host.update(typed, ++now);
    typed = {};
    typed.select = true;
    host.update(typed, ++now);
    check(host.chat_messages()[0] == "Catch me!", "chat slot edit commits");
    check(host.items().size() == 9 && hs.net_chat[0] == "Catch me!",
          "chat uses eight editable rows and persists phrases");
    capture(host, "chat");
    host.enter(11);
    guest.enter(11);
    host.enter(12);
    guest.enter(12);
    capture(host, "vote");
    handler(host, 0x004213c0);
    handler(guest, 0x004213c0);
    pump();
    std::optional<rr::net::Start> start_h, start_g;
    while (host.poll(event))
        if (event.kind == rr::FrontendEvent::Kind::start_net_race) start_h = event.net_start;
    while (guest.poll(event))
        if (event.kind == rr::FrontendEvent::Kind::start_net_race) start_g = event.net_start;
    check(start_h && start_g && start_h->race_id == start_g->race_id,
          "all votes start same race via frontend events");
    if (!start_h || !start_g) return check.finish("net_frontend");
    // Real client/rollback simulation on the channels borrowed from the lobby.
    std::array<std::unique_ptr<rr::net::Client>, 2> clients;
    std::array<rr::Frontend*, 2> fronts{&host, &guest};
    std::array<rr::net::Start, 2> starts{*start_h, *start_g};
    for (size_t slot = 0; slot < 2; ++slot) {
        clients[slot] = std::make_unique<rr::net::Client>(
            fronts[slot]->net_client()->release_channel(), hello, [&](const auto& start) {
                return std::make_unique<rr::RaceSetup>(
                    rr::make_race_setup(assets, exe, rr::net::race_config(start)));
            });
        clients[slot]->set_automatic_results(false);
        clients[slot]->begin(starts[slot], now);
    }
    std::array<std::optional<rr::net::Official>, 2> official;
    std::array<bool, 2> reported{};
    for (int iteration = 0; iteration < 800 && (!official[0] || !official[1]); ++iteration) {
        now += 10;
        server.step(now);
        for (size_t slot = 0; slot < 2; ++slot) {
            auto& client = *clients[slot];
            client.update(now, [](int32_t) { return uint16_t(rr::control_accelerate); }, 420);
            while (auto message = client.poll_event()) {
                if (std::holds_alternative<rr::net::Start>(*message)) continue;
                fronts[slot]->net_client()->inject(*message, now);
                if (auto result = std::get_if<rr::net::Official>(&*message)) official[slot] = *result;
            }
            if (client.rollback()->confirmed_tick() >= 419 && !reported[slot]) {
                client.send(rr::net::Result{419,
                                            slot == 0 ? rr::net::Outcome::won : rr::net::Outcome::finished,
                                            uint8_t(slot + 1), rr::state_hash(*client.race())});
                reported[slot] = true;
            }
        }
    }
    check(official[0] && official[1] && official[0]->agreed && *official[0] == *official[1],
          "channel handover runs real rollback and official cutoff results");
    if (official[0] && official[1])
        for (size_t slot = 0; slot < 2; ++slot) {
            fronts[slot]->net_client()->resume(clients[slot]->release_channel());
            fronts[slot]->net_client()->take_official();
            fronts[slot]->mano_finished(*official[slot], {"Host", "Guest"}, int(slot));
        }
    check(rr::write_save(hc) == career_before && hc.rng == rng_before,
          "mano menus and results cannot change career save or RNG");
    bool result_video = false;
    while (host.poll(event))
        if (event.kind == rr::FrontendEvent::Kind::play_video) {
            result_video |= event.video.find("/WIN") != std::string::npos;
            check(event.video.find("DUEL") == std::string::npos, "no DUEL result video");
        }
    check(result_video, "winner uses normal WIN result video");
    handler(host, 0x00421290);
    capture(host, "results");
    rr::MenuInput back;
    back.back = true;
    host.update(back, ++now);
    guest.enter(11);
    pump();
    check(host.screen() == 11 && host.net_in_room(), "official results return to mano with room retained");
    host.enter(12);
    guest.enter(12);
    handler(host, 0x004213c0);
    handler(guest, 0x004213c0);
    pump();
    std::optional<rr::net::Start> rematch;
    while (host.poll(event))
        if (event.kind == rr::FrontendEvent::Kind::start_net_race) rematch = event.net_start;
    check(rematch && rematch->race_id == start_h->race_id + 1,
          "rematch starts next race on retained connection");
    if (official[1]) {
        auto busted = *official[1];
        busted.agreed = false;
        for (auto& row : busted.rows)
            if (row.seat == 1) {
                row.outcome = rr::net::Outcome::busted;
                row.place = 0;
            }
        guest.mano_finished(busted, {"Host", "Guest"}, 1);
        handler(guest, 0x004214e0);
        check(guest.screen() == 0x22, "busted online result goes to standings without career fines");
        check(guest.items().back().enabled && (guest.items().back().flags & rr::item_flag::light),
              "standings Back is visible and enabled");
        capture(guest, "unofficial");
    }
    guest.enter(rr::screen_id::net_rooms);
    capture(guest, "rooms");
    // Web launcher intents passed as --room: Host popup, public list, spectate.
    {
        rr::Career wc = gc;
        rr::Settings ws = gs;
        rr::Frontend web(assets, text, rr::frontend_strings(resources), rr::credit_lines(resources), data,
                         tables, wc, ws, (dir / "web/saves").string());
        web.net_attach(factory, hello);
        web.enter(9);
        web.net_join_invite("@host");
        const bool level_popup = std::any_of(web.items().begin(), web.items().end(), [](const auto& m) {
            return m.enabled && m.handler == 0x00445440;
        });
        check(web.screen() == 11 && level_popup && !web.net_in_room(), "@host opens the Host level popup");
        capture(web, "host-level");
        // Every Host wizard popup clears the mano menu rows it opens over (the
        // EXE level popup's own rows covered Chat Messages/Restroom/Disconnect).
        auto popup_clear = [&] {
            for (const auto& p : web.items())
                if (p.flags & rr::item_flag::popup_item)
                    for (const auto& m : web.items())
                        if ((m.flags & rr::item_flag::dimmed) && p.x < m.x + m.w && m.x < p.x + p.w &&
                            p.y < m.y + m.h && m.y < p.y + p.h)
                            return false;
            return true;
        };
        check(popup_clear(), "level popup clears the menu rows");
        check(web.items().back().caption == 95, "level popup keeps the original Level 1-5 captions");
        for (int target : {2, 3, 1}) { // level 2, AI full, cars on
            choice(web, target);
            check(popup_clear(), "Host wizard popup clears the menu rows");
        }
        capture(web, "host-visibility");
        choice(web, 2); // unlisted: keeps the public list below unchanged
        rr::Frontend lister(assets, text, rr::frontend_strings(resources), rr::credit_lines(resources), data,
                            tables, wc, ws, (dir / "web2/saves").string());
        lister.net_attach(factory, hello);
        lister.enter(9);
        lister.net_join_invite("@rooms");
        for (int i = 0; i < 60; ++i) {
            now += 10;
            server.step(now);
            lister.update({}, now);
        }
        check(lister.screen() == rr::screen_id::net_rooms && lister.net_client()->rooms().size() == 1,
              "@rooms opens the public room list");
        rr::Frontend watcher(assets, text, rr::frontend_strings(resources), rr::credit_lines(resources), data,
                             tables, wc, ws, (dir / "web3/saves").string());
        watcher.net_attach(factory, hello);
        watcher.enter(9);
        watcher.net_join_invite("@watch:" + host.net_client()->room()->code);
        for (int i = 0; i < 60; ++i) {
            now += 10;
            server.step(now);
            host.update({}, now);
            watcher.update({}, now);
        }
        check(watcher.net_in_room() && watcher.net_client()->my_seat() == 0xff,
              "@watch:CODE joins as a spectator");
    }
    check(rr::write_save(hc) == career_before && rr::write_save(gc) == career_before &&
              hc.rng == rng_before && gc.rng == rng_before,
          "all online result paths preserve both careers");
    std::filesystem::remove_all(dir);
    return check.finish("net_frontend");
}
