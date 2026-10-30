#include "net/client.hpp"
#include <algorithm>
#include <cmath>
#include <bit>
#include <stdexcept>

namespace rr::net {
RaceConfig race_config(const Start& start) {
    static constexpr const char* courses[] = {"CITY", "HIWAY", "MEDLY", "NAPA", "CANYN"};
    if (start.course < 1 || start.course > 5 || start.humans.empty() || start.humans.size() > 8)
        throw std::invalid_argument("Invalid Start roster or course");
    NetRaceConfig net;
    net.seed = start.seed;
    net.ai_bikers = start.options.ai;
    net.cars = start.options.cars;
    for (size_t slot = 0; slot < start.humans.size(); ++slot) {
        const auto& human = start.humans[slot];
        net.humans.push_back({int(slot), human.character, human.bike, human.alias});
    }
    return net_race_config(courses[start.course - 1], start.options.level, net);
}

Client::Client(std::unique_ptr<Channel> c, Hello h, Factory f, int w, int d)
    : reliable_(std::move(c)), hello_(std::move(h)), factory_(std::move(f)),
      sound_history_(std::clamp(w, 1, 64)), window_(std::clamp(w, 1, 64)), delay_cap_(std::clamp(d, 0, 3)) {}
void Client::begin(const Start& start, uint64_t now) {
    skip_hello();
    receive(start, now);
}
void Client::attach_unreliable(std::unique_ptr<Channel> c) {
    unreliable_ = std::move(c);
}
void Client::send(const Message& m) {
    reliable_->send(encode(m));
}
void Client::race_send(const Message& m) {
    auto* c =
        unreliable_ && unreliable_->status() == ChannelStatus::open ? unreliable_.get() : reliable_.get();
    c->send(encode(m));
}
std::optional<Message> Client::poll_event() {
    if (events_.empty()) return {};
    Message m = std::move(events_.front());
    events_.pop_front();
    return m;
}
std::optional<Client::OutcomeEvent> Client::poll_outcome() {
    if (outcomes_.empty()) return {};
    auto event = outcomes_.front();
    outcomes_.pop_front();
    return event;
}
void Client::receive(const Message& m, uint64_t now) {
    if (std::holds_alternative<Welcome>(m)) welcomed_ = true;
    if (auto p = std::get_if<Ping>(&m)) {
        send(Pong{p->t, 0, uint32_t(now)});
        return;
    }
    if (auto p = std::get_if<Pong>(&m)) {
        if (!ping_ || *ping_ != p->t) return;
        const double rtt = uint32_t(uint32_t(now) - p->t);
        ping_.reset();
        stats_.rtt_ms = samples_.empty() ? rtt : stats_.rtt_ms * .875 + rtt * .125;
        samples_.push_back(rtt);
        if (samples_.size() > 64) samples_.pop_front();
        auto ordered = std::vector<double>(samples_.begin(), samples_.end());
        std::sort(ordered.begin(), ordered.end()); // total-order: finite RTT samples
        stats_.jitter_ms = (ordered[(ordered.size() - 1) * 95 / 100] - ordered.front()) / 2;
        stats_.lead =
            std::clamp(int(std::ceil((stats_.rtt_ms / 2 + stats_.jitter_ms) * .06)) + 1, 1, window_);
        stats_.input_delay = std::min(delay_cap_, int(std::ceil(stats_.jitter_ms * .06)));
        remote_tick_ = p->server_tick + rtt * .03;
        remote_ms_ = now;
        if (start_ && !synced_) {
            clock_tick_ = remote_tick_;
            last_ms_ = now; // this sample already describes the current time
            synced_ = true;
        }
        return;
    }
    if (auto s = std::get_if<Start>(&m)) {
        // Duplicate starts must never reset an active race.
        if (start_ && start_->race_id == s->race_id) return;
        const int local = s->your_seat < s->humans.size() ? int(s->your_seat) : -1;
        setup_ = factory_(*s);
        if (!setup_) {
            failed_ = true;
            return;
        }
        rollback_ =
            std::make_unique<Rollback>(setup_->instance, int(s->humans.size()), local, window_, s->countdown);
        sound_history_ = SoundHistory(window_);
        sounds_.clear();
        outcomes_.clear();
        outcome_codes_.fill(1);
        result_sent_ = false;
        rollback_->on_predicted = [this](const Rollback::TickEffects& effects) {
            sound_history_.deliver(effects.tick, effects.sounds, [this](const SoundEventRecord& sound) {
                if (sounds_.size() < 256) sounds_.push_back(sound);
            });
        };
        rollback_->on_confirmed = [this, local](const Rollback::TickEffects& effects) {
            for (size_t slot = 0; slot < effects.outcomes.size(); ++slot) {
                const auto& outcome = effects.outcomes[slot];
                if (outcome.code != outcome_codes_[slot]) {
                    if (outcomes_.size() < 32) outcomes_.push_back({effects.tick, int(slot), outcome});
                    outcome_codes_[slot] = outcome.code;
                }
            }
            if (automatic_results_ && effects.terminal && !result_sent_ && local >= 0) {
                const auto& outcome = effects.outcomes[size_t(local)];
                send(Result{effects.tick, outcome.code == 1 ? Outcome::left : Outcome(outcome.code),
                            uint8_t(std::clamp(outcome.place, 0, 15)), effects.terminal_hash});
                result_sent_ = true;
            }
        };
        start_ = *s;
        finished_ = false;
        inputs_.clear();
        sampled_ = -1;
        synced_ = false;
        ping_.reset();
        next_ping_ = now;
    }
    auto frames = [&](const Frames& fs) {
        if (!rollback_ || fs.humans != start_->humans.size() || fs.first_tick < 0 ||
            int64_t(fs.first_tick) + int64_t(fs.frames.size()) > 216000 ||
            fs.first_tick > rollback_->sealed_tick() + 1025) {
            ++stats_.malformed;
            return;
        }
        const int32_t previous = rollback_->sealed_tick();
        int32_t t = fs.first_tick;
        for (const auto& f : fs.frames) {
            std::vector<uint32_t> words(f.words.begin(), f.words.end());
            rollback_->sealed(t++, words, f.late, f.present);
        }
        for (int32_t tick = previous + 1; tick <= rollback_->sealed_tick(); ++tick)
            stats_.late += std::popcount(rollback_->frames()[size_t(tick)].late);
    };
    if (auto f = std::get_if<Frames>(&m)) {
        frames(*f);
        return;
    }
    if (auto l = std::get_if<LogChunk>(&m)) {
        if (start_ && l->race_id == start_->race_id) frames(l->frames);
        return;
    }
    if (std::holds_alternative<Official>(m)) finished_ = true;
    if (std::holds_alternative<Reject>(m) || std::holds_alternative<Desync>(m)) failed_ = true;
    if (events_.size() == 128) {
        failed_ = true;
        return;
    }
    events_.push_back(m);
}
void Client::update(uint64_t now, const Sampler& sample, int32_t stop) {
    if (!reliable_) return; // the channel went back to the lobby (Task 7 handover)
    if (!hello_sent_ && reliable_->status() == ChannelStatus::open) {
        send(hello_);
        hello_sent_ = true;
    }
    auto drain = [&](Channel* c) {
        if (!c) return;
        for (int n = 0; n < 256; ++n) {
            auto b = c->poll();
            if (!b) break;
            auto m = decode(*b);
            if (m)
                receive(*m, now);
            else
                ++stats_.malformed;
        }
    };
    drain(reliable_.get());
    drain(unreliable_.get());
    if (reliable_->status() == ChannelStatus::closed || reliable_->status() == ChannelStatus::failed)
        failed_ = true;
    if (welcomed_ && now >= next_ping_) {
        ping_ = uint32_t(now);
        race_send(Ping{*ping_});
        next_ping_ = now + 500;
    }
    const uint64_t elapsed = last_ms_ ? now - last_ms_ : 0;
    last_ms_ = now;
    if (!rollback_ || !synced_ || failed_ || finished_) return;
    const double desired = remote_tick_ + double(now - remote_ms_) * .06;
    stats_.rate = desired > clock_tick_ + .25 ? 61 : desired < clock_tick_ - .25 ? 59 : 60;
    clock_tick_ += double(elapsed) * stats_.rate / 1000;
    const int32_t target =
        std::clamp(int32_t(std::floor(clock_tick_)) + stats_.lead, 0, std::min(stop, 216000));
    const int32_t sample_end = std::min(target + stats_.input_delay, rollback_->sealed_tick() + window_ + 1);
    // Sample once, including during a stall. Keep the resend queue inside the
    // rollback horizon; inputs older than an authoritative seal are obsolete.
    while (!inputs_.empty() && inputs_.front().first <= rollback_->sealed_tick()) inputs_.pop_front();
    sampled_ = std::max(sampled_, rollback_->sealed_tick());
    while (start_->your_seat != 0xff && sampled_ + 1 < sample_end) {
        const int32_t t = ++sampled_;
        const uint16_t w = sample(std::max(0, t - stats_.input_delay));
        rollback_->local_input(t, w);
        inputs_.emplace_back(t, w);
    }
    if (now >= next_send_) {
        for (size_t i = 0; i < inputs_.size(); i += max_input_batch) {
            Input in;
            in.first_tick = inputs_[i].first;
            for (size_t j = i; j < std::min(inputs_.size(), i + max_input_batch); ++j)
                in.words.push_back(inputs_[j].second);
            race_send(in);
        }
        race_send(Ack{rollback_->sealed_tick()});
        next_send_ = now + 16;
    }
    rollback_->advance_to(target);
    stats_.rollback_depth = rollback_->last_rollback_depth();
    stats_.stalls += rollback_->stalled();
    for (auto h : rollback_->take_hashes()) send(Hash{h.tick, h.hash});
}
} // namespace rr::net
