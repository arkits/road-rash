// Plan 34 Task 5: input sealer.
#include "sealer.hpp"

#include <algorithm>

namespace rr::netd {

Sealer::Sealer(int humans)
    : humans_(std::clamp(humans, 1, net::max_seats)), pending_(size_t(humans_)), last_word_(size_t(humans_), 0),
      human_(size_t(humans_), true), human_next_(size_t(humans_), true) {}

void Sealer::input(int seat, int32_t first_tick, std::span<const uint16_t> words) {
    if (seat < 0 || seat >= humans_) return;
    auto& pending = pending_[size_t(seat)];
    for (size_t i = 0; i < words.size(); ++i) {
        // 64-bit: a hostile first_tick near INT32_MAX must not overflow.
        const int64_t tick = int64_t(first_tick) + int64_t(i);
        if (tick <= sealed_tick()) {
            ++counters_.dropped_old;
            continue;
        }
        if (tick > int64_t(sealed_tick()) + max_ahead) {
            ++counters_.dropped_ahead;
            continue;
        }
        pending.emplace(int32_t(tick), words[i]); // keeps the first word for a tick
    }
}

void Sealer::set_human(int seat, bool human) {
    if (seat >= 0 && seat < humans_) human_next_[size_t(seat)] = human;
}

int Sealer::seal_through(int32_t tick) {
    int n = 0;
    while (sealed_tick() < tick) {
        const int32_t t = sealed_tick() + 1;
        net::SealedFrame f;
        f.words.assign(size_t(humans_), 0);
        for (int s = 0; s < humans_; ++s) {
            const size_t i = size_t(s);
            human_[i] = human_next_[i];
            auto& pending = pending_[i];
            while (!pending.empty() && pending.begin()->first < t) pending.erase(pending.begin());
            if (!human_[i]) {
                // AI-driven seat: the word is unused by the simulation; keep
                // the last human word so a hand-back starts from it.
                f.words[i] = last_word_[i];
                ++counters_.ai_ticks;
                if (!pending.empty() && pending.begin()->first == t) pending.erase(pending.begin());
                continue;
            }
            f.present |= uint8_t(1u << s);
            if (!pending.empty() && pending.begin()->first == t) {
                f.words[i] = pending.begin()->second;
                pending.erase(pending.begin());
            } else {
                f.words[i] = last_word_[i]; // repeat-last, flagged
                f.late |= uint8_t(1u << s);
                ++counters_.late;
            }
            last_word_[i] = f.words[i];
        }
        log_.push_back(std::move(f));
        ++counters_.sealed;
        ++n;
    }
    return n;
}

net::Frames Sealer::frames(int32_t first, int count) const {
    net::Frames out;
    out.humans = uint8_t(humans_);
    out.first_tick = std::max<int32_t>(first, 0);
    const int32_t end = std::min<int32_t>(out.first_tick + count, int32_t(log_.size()));
    for (int32_t t = out.first_tick; t < end; ++t) out.frames.push_back(log_[size_t(t)]);
    return out;
}

} // namespace rr::netd
