// Real WebSocket clients for local/staging smoke tests. Each owns a full
// disc-backed Race and Rollback. --ticks terminates at a shared sealed cutoff
// and reports Outcome::left: this is a transport/determinism test, not a claim
// that scripted riders completed the course.
//
// Plan 33b Task 6 cross-play: several processes (one per platform) share one
// room. --host-alias NAME makes this process's first client host a public
// room as NAME; --join-alias NAME makes every client here find that room in
// the public list (which only shows rooms with this build's fingerprint and
// data digest) and join it. --seats is the room's total; the server marks
// the result official only when every seat reported the same tick and hash.
#include "assets.hpp"
#include "net/client.hpp"
#include "net/compat.hpp"
#include "net/rtc_channel.hpp"
#include <chrono>
#include <bit>
#include <algorithm>
#include <filesystem>
#include <iostream>
#include <thread>
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

int main(int argc, char** argv) {
    try {
        std::string url = "ws://127.0.0.1:8080/net";
        std::string root = std::string(RR_SOURCE_DIR) + "/../cdrom/ROADRASH";
        int count = 2, ticks = 600, timeout = 90, close_tick = -1, seats = 0, first = 0;
        bool rtc = false, require_rtc = false, native_platform = false;
        std::string host_alias, join_alias;
        for (int i = 1; i < argc; ++i) {
            std::string a = argv[i];
            if (a == "--help") {
                std::cout << "net-bots [--url ws://host/net] [--disc ROADRASH] [--clients 1..8] [--ticks N] "
                             "[--timeout seconds] [--rtc|--no-udp] [--require-rtc] [--rtc-close-tick N]\n"
                             "         [--seats N --host-alias NAME | --join-alias NAME] [--first N] "
                             "[--native-platform]\n";
                return 0;
            }
            if (a == "--rtc") {
                rtc = true;
                continue;
            }
            if (a == "--no-udp") {
                rtc = false;
                continue;
            }
            if (a == "--native-platform") {
                native_platform = true;
                continue;
            }
            if (a == "--require-rtc") {
                rtc = true;
                require_rtc = true;
                continue;
            }
            if (i + 1 == argc) throw std::runtime_error("missing option value");
            std::string v = argv[++i];
            if (a == "--url")
                url = v;
            else if (a == "--disc")
                root = v;
            else if (a == "--clients")
                count = std::stoi(v);
            else if (a == "--ticks")
                ticks = std::stoi(v);
            else if (a == "--rtc-close-tick")
                close_tick = std::stoi(v);
            else if (a == "--timeout")
                timeout = std::stoi(v);
            else if (a == "--seats")
                seats = std::stoi(v);
            else if (a == "--first")
                first = std::stoi(v);
            else if (a == "--host-alias")
                host_alias = v;
            else if (a == "--join-alias")
                join_alias = v;
            else
                throw std::runtime_error("unknown option " + a);
        }
        const bool joining = !join_alias.empty(), hosting = !host_alias.empty();
        if (!seats) seats = count;
        if (joining && hosting) throw std::runtime_error("--host-alias and --join-alias are exclusive");
        if (count < (hosting || joining ? 1 : 2) || count > seats || seats > 8 || first < 0 ||
            first + count > 8 || ticks < 1 || ticks > 216000 || timeout < 1 ||
            host_alias.size() > rr::net::max_alias || join_alias.size() > rr::net::max_alias)
            throw std::runtime_error("invalid limits");
        rr::Assets assets(root);
        auto exe = assets.read("ROADRASH.EXE");
        if (exe.empty()) throw std::runtime_error("disc executable not found");
        using namespace rr::net;
        auto factory = [&](const Start& s) {
            return std::make_unique<rr::RaceSetup>(rr::make_race_setup(assets, exe, race_config(s)));
        };
        // Bots and interactive clients use the same verified data and
        // simulation fingerprint, so staging smoke exercises the real gate.
#if defined(__EMSCRIPTEN__)
        constexpr auto host_platform = Platform::web;
#elif defined(__APPLE__)
        constexpr auto host_platform = Platform::macos;
#elif defined(_WIN32)
        constexpr auto host_platform = Platform::windows;
#else
        constexpr auto host_platform = Platform::linux_desktop;
#endif
        auto hello =
            make_hello("Bot", native_platform ? host_platform : Platform::bot, verified_data_digest(assets));
        // The room list filters on these; print them so mismatched builds are obvious.
        std::cout << "net-bots fingerprint " << std::hex << hello.sim_fingerprint << " digest ";
        for (const uint8_t b : hello.data_digest) std::cout << (b >> 4) << (b & 15);
        std::cout << std::dec << " platform " << int(hello.platform) << std::endl;
        std::vector<std::unique_ptr<Client>> clients;
        std::vector<Channel*> transports;
        std::vector<int> depths;
        uint64_t max_stalls = 0, late = 0, late_total = 0, input_total = 0;
        std::vector<int32_t> measured_tick(size_t(count), -1);
        std::vector<uint64_t> base_stalls(size_t(count), 0);
        for (int k = 0; k < count; ++k) {
            hello.alias = k == 0 && hosting ? host_alias : "Bot" + std::to_string(first + k + 1);
            auto channel = ws_connect(url, RtcOptions{.enabled = rtc});
            transports.push_back(channel.get());
            clients.push_back(std::make_unique<Client>(std::move(channel), hello, factory));
        }
        for (auto& client : clients) client->set_automatic_results(false);
        std::string code;
        bool created = false;
        uint64_t next_list = 0;
        std::vector<bool> welcomed(size_t(count), false), joined(size_t(count), false),
            full_roster(size_t(count), false), ready(size_t(count), false), reported(size_t(count), false),
            official(size_t(count), false);
        std::optional<uint64_t> final_hash;
        const uint64_t begin = steady_ms();
        while (steady_ms() - begin < uint64_t(timeout) * 1000) {
            uint64_t now = steady_ms();
            for (int k = 0; k < count; ++k) {
                auto& c = *clients[size_t(k)];
                c.update(
                    now,
                    [k](int32_t t) {
                        return uint16_t(rr::control_accelerate |
                                        ((t / (43 + 7 * k)) % 3 == 1 ? rr::control_left : 0));
                    },
                    ticks);
                if (c.rollback()) {
                    const auto tick = c.rollback()->current_tick();
                    if (tick < 360)
                        base_stalls[size_t(k)] = c.stats().stalls;
                    else {
                        if (tick != measured_tick[size_t(k)] || c.stats().rollback_depth > 0)
                            depths.push_back(c.stats().rollback_depth);
                        measured_tick[size_t(k)] = tick;
                        max_stalls = std::max(max_stalls, c.stats().stalls - base_stalls[size_t(k)]);
                    }
                    late = std::max(late, c.stats().late);
                    if (close_tick >= 0 && c.rollback()->confirmed_tick() >= close_tick)
                        transports[size_t(k)]->disable_rtc();
                }
                while (auto m = c.poll_event()) {
                    if (std::holds_alternative<Welcome>(*m)) {
                        welcomed[size_t(k)] = true;
                        if (k == 0 && !created && !joining) {
                            // A cross-play host lists the room so other processes find it.
                            c.send(RoomCreate{RoomOptions{0, 2, true, hosting}});
                            created = true;
                        }
                    }
                    if (auto list = std::get_if<RoomListReply>(&*m); list && joining && code.empty())
                        for (const auto& room : list->rooms)
                            if (room.host == join_alias && room.status != RoomStatus::racing) {
                                code = room.code;
                                std::cout << "net-bots found room " << code << " hosted by " << join_alias
                                          << std::endl;
                            }
                    if (auto s = std::get_if<RoomState>(&*m)) {
                        if (k == 0 && !joining) code = s->code;
                        const int occupied =
                            int(std::count_if(s->seats.begin(), s->seats.end(),
                                              [](const SeatInfo& seat) { return seat.occupied; }));
                        full_roster[size_t(k)] = occupied == seats;
                    }
                    if (auto o = std::get_if<Official>(&*m)) {
                        if (!o->agreed || o->rows.size() != size_t(seats))
                            throw std::runtime_error("unofficial or incomplete result");
                        // The server agrees over the seats that reported; one that
                        // left without a result gets place 15. Bots report place 0,
                        // so every seat must have confirmed the same tick and hash.
                        if (std::any_of(o->rows.begin(), o->rows.end(),
                                        [](const OfficialRow& row) { return row.place != 0; }))
                            throw std::runtime_error("a seat left without reporting its result");
                        official[size_t(k)] = true;
                    }
                }
                if (full_roster[size_t(k)] && !ready[size_t(k)]) {
                    const auto transport = transports[size_t(k)]->transport_stats();
                    // The shared bot process builds races synchronously. Finish
                    // signalling before Ready so asset loading cannot starve it.
                    if (!rtc || transport.rtc_open || transport.fallbacks || now - begin > 6000) {
                        // Final votes trigger Start on the server, so delay the
                        // choices too, rather than only the redundant Ready.
                        c.send(PickCharacter{uint8_t(first + k + 1)});
                        c.send(VoteCourse{4});
                        c.send(Ready{});
                        ready[size_t(k)] = true;
                    }
                }
                if (c.failed()) throw std::runtime_error("server rejection, desync or client queue overflow");
                if (joining && k == 0 && welcomed[0] && code.empty() && now >= next_list) {
                    c.send(RoomList{});
                    next_list = now + 1000;
                }
                if ((k || joining) && welcomed[size_t(k)] && !code.empty() && !joined[size_t(k)]) {
                    c.send(RoomJoin{code, false});
                    joined[size_t(k)] = true;
                }
                if (auto r = c.rollback(); r && r->confirmed_tick() >= ticks - 1 && !reported[size_t(k)]) {
                    const uint64_t h = rr::state_hash(*c.race());
                    if (final_hash && *final_hash != h) throw std::runtime_error("final hash mismatch");
                    final_hash = h;
                    c.send(Result{ticks - 1, Outcome::left, 0, h});
                    reported[size_t(k)] = true;
                    std::cout << "bot " << k << " confirmed " << ticks << " hash " << h << " stalls "
                              << c.stats().stalls << "\n";
                }
            }
            if (std::all_of(official.begin(), official.end(), [](bool b) { return b; })) {
                if (auto r = clients.front()->rollback())
                    for (size_t t = 360; t < std::min(r->frames().size(), size_t(ticks)); ++t) {
                        late_total += std::popcount(r->frames()[t].late);
                        input_total += r->frames()[t].words.size();
                    }
                std::sort(depths.begin(), depths.end());
                auto percentile = [&](double p) {
                    return depths.empty() ? 0 : depths[size_t(p * double(depths.size() - 1))];
                };
                uint64_t sent = 0, received = 0, fallbacks = 0;
                bool missing_rtc = false;
                size_t transport_index = 0;
                for (auto* channel : transports) {
                    auto stats = channel->transport_stats();
                    std::cout << "client " << transport_index++ << " rtc_sent=" << stats.rtc_sent
                              << " rtc_received=" << stats.rtc_received << " fallbacks=" << stats.fallbacks
                              << "\n";
                    missing_rtc |= !stats.rtc_sent || !stats.rtc_received;
                    sent += stats.rtc_sent;
                    received += stats.rtc_received;
                    fallbacks += stats.fallbacks;
                }
                std::cout << "transport rtc_sent=" << sent << " rtc_received=" << received
                          << " fallbacks=" << fallbacks << " rollback_p50=" << percentile(.5)
                          << " rollback_p95=" << percentile(.95) << " rollback_p99=" << percentile(.99)
                          << " late=" << late << " live_late=" << late_total << " live_inputs=" << input_total
                          << " stalls=" << max_stalls << "\n";
                if (require_rtc && missing_rtc)
                    throw std::runtime_error("DataChannel required but no race traffic crossed it");
                std::cout << "net-bots OK: official cutoff results in room " << code << "\n";
                return 0;
            }
#ifdef __EMSCRIPTEN__
            emscripten_sleep(4); // Asyncify: lets the WebSocket callbacks run
#else
            std::this_thread::sleep_for(std::chrono::milliseconds(4));
#endif
        }
        for (size_t k = 0; k < clients.size(); ++k) {
            const auto stats = transports[k]->transport_stats();
            std::cerr << "client " << k << " welcomed=" << welcomed[k] << " joined=" << joined[k]
                      << " full_roster=" << full_roster[k] << " selected=" << ready[k]
                      << " rtc_open=" << stats.rtc_open << " fallbacks=" << stats.fallbacks
                      << " rtc_sent=" << stats.rtc_sent << " rtc_received=" << stats.rtc_received << "\n";
        }
        throw std::runtime_error("timed out waiting for official cutoff results");
    } catch (const std::exception& e) {
        std::cerr << "net-bots: " << e.what() << "\n";
        return 1;
    }
}
