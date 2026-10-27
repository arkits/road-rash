// Real WebSocket clients for local/staging smoke tests. Each owns a full
// disc-backed Race and Rollback. --ticks terminates at a shared sealed cutoff
// and reports Outcome::left: this is a transport/determinism test, not a claim
// that scripted riders completed the course.
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

int main(int argc, char** argv) {
    try {
        std::string url = "ws://127.0.0.1:8080/net";
        std::string root = std::string(RR_SOURCE_DIR) + "/../cdrom/ROADRASH";
        int count = 2, ticks = 600, timeout = 90, close_tick = -1;
        bool rtc = false, require_rtc = false;
        for (int i = 1; i < argc; ++i) {
            std::string a = argv[i];
            if (a == "--help") {
                std::cout << "net-bots [--url ws://host/net] [--disc ROADRASH] [--clients 2..8] [--ticks N] "
                             "[--timeout seconds] [--rtc|--no-udp] [--require-rtc] [--rtc-close-tick N]\n";
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
            else
                throw std::runtime_error("unknown option " + a);
        }
        if (count < 2 || count > 8 || ticks < 1 || ticks > 216000 || timeout < 1)
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
        auto hello = make_hello("Bot", Platform::bot, verified_data_digest(assets));
        std::vector<std::unique_ptr<Client>> clients;
        std::vector<Channel*> transports;
        std::vector<int> depths;
        uint64_t max_stalls = 0, late = 0, late_total = 0, input_total = 0;
        std::vector<int32_t> measured_tick(size_t(count), -1);
        std::vector<uint64_t> base_stalls(size_t(count), 0);
        for (int k = 0; k < count; ++k) {
            hello.alias = "Bot" + std::to_string(k + 1);
            auto channel = ws_connect(url, RtcOptions{.enabled = rtc});
            transports.push_back(channel.get());
            clients.push_back(std::make_unique<Client>(std::move(channel), hello, factory));
        }
        for (auto& client : clients) client->set_automatic_results(false);
        std::string code;
        bool created = false;
        std::vector<bool> joined(size_t(count), false), selected(size_t(count), false),
            ready(size_t(count), false), reported(size_t(count), false), official(size_t(count), false);
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
                    if (std::holds_alternative<Welcome>(*m) && k == 0 && !created) {
                        c.send(RoomCreate{RoomOptions{0, 2, true, false}});
                        created = true;
                    }
                    if (auto s = std::get_if<RoomState>(&*m)) {
                        if (k == 0) code = s->code;
                        const int occupied =
                            int(std::count_if(s->seats.begin(), s->seats.end(),
                                              [](const SeatInfo& seat) { return seat.occupied; }));
                        // Wait for the complete roster before readying any seat.
                        if (occupied == count && !selected[size_t(k)]) {
                            c.send(PickCharacter{uint8_t(k + 1)});
                            c.send(VoteCourse{4});
                            selected[size_t(k)] = true;
                        }
                    }
                    if (auto o = std::get_if<Official>(&*m)) {
                        if (!o->agreed || o->rows.size() != size_t(count))
                            throw std::runtime_error("unofficial or incomplete result");
                        official[size_t(k)] = true;
                    }
                }
                if (selected[size_t(k)] && !ready[size_t(k)]) {
                    const auto transport = transports[size_t(k)]->transport_stats();
                    // The shared bot process builds races synchronously. Finish
                    // signalling before Ready so asset loading cannot starve it.
                    if (!rtc || transport.rtc_open || transport.fallbacks || now - begin > 6000) {
                        c.send(Ready{});
                        ready[size_t(k)] = true;
                    }
                }
                if (c.failed()) throw std::runtime_error("server rejection, desync or client queue overflow");
                if (k && !code.empty() && !joined[size_t(k)]) {
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
            std::this_thread::sleep_for(std::chrono::milliseconds(4));
        }
        throw std::runtime_error("timed out waiting for official cutoff results");
    } catch (const std::exception& e) {
        std::cerr << "net-bots: " << e.what() << "\n";
        return 1;
    }
}
