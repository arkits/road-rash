// Race frame profiler for `rr-game --bench` (docs/PERFORMANCE.md).
// Wall-clock phase timings of the presentation loop; never touches the
// simulation. A null FrameProfile* disables every scope.
#pragma once
#include <SDL3/SDL.h>
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace rr {
struct FrameProfile {
    enum Phase {
        sim,
        scene_nodes,
        horizon,
        raster,
        grade,
        post,
        dash,
        remap,
        upload,
        sprites_hud,
        present,
        count
    };
    static constexpr const char* names[count] = {"sim",    "scene_nodes", "horizon", "raster",
                                                 "grade",  "post",        "dash",    "remap",
                                                 "upload", "sprites_hud", "present"};
    std::array<uint64_t, count> frame_ns{};
    void add(Phase p, uint64_t ns) { frame_ns[p] += ns; }
};

class ProfileScope {
  public:
    ProfileScope(FrameProfile* p, FrameProfile::Phase phase)
        : p_(p), phase_(phase), start_(p ? SDL_GetTicksNS() : 0) {}
    ~ProfileScope() {
        if (p_) p_->add(phase_, SDL_GetTicksNS() - start_);
    }
    ProfileScope(const ProfileScope&) = delete;
    ProfileScope& operator=(const ProfileScope&) = delete;

  private:
    FrameProfile* p_;
    FrameProfile::Phase phase_;
    uint64_t start_;
};

// Collects per-frame totals and phase sums after a warm-up, then writes JSON.
struct FrameBench {
    long warmup = 60, frames = 600;
    std::string out_path; // empty: stdout only
    std::string label;    // free text copied into the report
    std::vector<uint64_t> totals;
    std::array<uint64_t, FrameProfile::count> phase_sum{};
    std::array<uint64_t, FrameProfile::count> phase_max{};
    long seen = 0;
    struct Slow {
        long frame;
        uint64_t total_ns;
        int phase;
        uint64_t phase_ns;
    };
    std::vector<Slow> slow; // frames over 2x the 60 Hz budget, first 32
    uint64_t sim_ticks = 0;
    // Plan 33b Task 4 pacing: frame start-to-start intervals and the ticks the
    // 60 Hz clock stepped for each. With the clock's sub-tick remainder the
    // books balance exactly: ticks + remainder change = interval x 60, so
    // drift_ticks is only rounding unless the clock lost time (a stall past
    // the step cap). Frames where the race reset its clock on purpose (focus
    // regained, resume) are left out and counted in clock_resets.
    std::vector<uint64_t> intervals;
    uint64_t wall_ns = 0, prev_start = 0, clock_resets = 0;
    int64_t balance_q16 = 0; // sum of (ticks << 16) + alpha change over counted frames
    uint32_t prev_alpha = 0;
    std::array<uint64_t, 4> step_frames{}; // frames that stepped 0, 1, 2, 3+ ticks
    FrameProfile current;
    bool done() const { return long(totals.size()) >= frames; }
    // start_ns: this frame's iterate timestamp (0: pacing not recorded);
    // alpha_q16: the clock's remainder after this frame's advance.
    void finish_frame(uint64_t total_ns, int ticks, uint64_t start_ns = 0, uint32_t alpha_q16 = 0,
                      bool clock_reset = false) {
        const uint64_t interval = prev_start && start_ns > prev_start ? start_ns - prev_start : 0;
        prev_start = start_ns;
        const uint32_t alpha_before = prev_alpha;
        prev_alpha = alpha_q16;
        if (seen++ >= warmup && !done()) {
            totals.push_back(total_ns);
            sim_ticks += uint64_t(std::max(0, ticks));
            step_frames[size_t(std::clamp(ticks, 0, 3))]++;
            if (clock_reset)
                ++clock_resets;
            else if (interval) {
                intervals.push_back(interval);
                wall_ns += interval;
                balance_q16 +=
                    (int64_t(std::max(0, ticks)) << 16) + int64_t(alpha_q16) - int64_t(alpha_before);
            }
            if (total_ns > 33'333'333 && slow.size() < 32) {
                int worst = 0;
                for (int i = 1; i < FrameProfile::count; ++i)
                    if (current.frame_ns[size_t(i)] > current.frame_ns[size_t(worst)]) worst = i;
                slow.push_back({long(totals.size()) - 1, total_ns, worst, current.frame_ns[size_t(worst)]});
            }
            for (int i = 0; i < FrameProfile::count; ++i) {
                phase_sum[size_t(i)] += current.frame_ns[size_t(i)];
                phase_max[size_t(i)] = std::max(phase_max[size_t(i)], current.frame_ns[size_t(i)]);
            }
        }
        current = FrameProfile{};
    }
    // `context` is a pre-formatted JSON object body (no braces) describing the run.
    std::string report(const std::string& context) const {
        std::vector<uint64_t> sorted = totals;
        std::sort(sorted.begin(), sorted.end()); // total-order: integers
        const auto pct = [&](double q) {
            if (sorted.empty()) return 0.0;
            return double(sorted[std::min(sorted.size() - 1, size_t(q * double(sorted.size() - 1) + 0.5))]) /
                   1e6;
        };
        uint64_t sum = 0;
        for (uint64_t t : totals) sum += t;
        const double n = double(std::max<size_t>(1, totals.size()));
        const double mean_ms = double(sum) / n / 1e6;
        std::string s = "{\n  \"label\": \"" + label + "\",\n" + context;
        char buf[512];
        std::snprintf(
            buf, sizeof buf,
            "  \"frames\": %zu,\n  \"warmup\": %ld,\n  \"sim_ticks\": %llu,\n  \"fps_mean\": %.2f,\n"
            "  \"frame_ms\": {\"mean\": %.3f, \"p50\": %.3f, \"p95\": %.3f, \"p99\": %.3f, \"max\": %.3f},\n"
            "  \"phases_ms_mean\": {",
            totals.size(), warmup, (unsigned long long)sim_ticks, mean_ms > 0 ? 1000.0 / mean_ms : 0.0,
            mean_ms, pct(0.50), pct(0.95), pct(0.99), pct(1.0));
        s += buf;
        for (int i = 0; i < FrameProfile::count; ++i) {
            std::snprintf(buf, sizeof buf, "%s\"%s\": %.3f", i ? ", " : "", FrameProfile::names[i],
                          double(phase_sum[size_t(i)]) / n / 1e6);
            s += buf;
        }
        s += "},\n  \"phases_ms_max\": {";
        for (int i = 0; i < FrameProfile::count; ++i) {
            std::snprintf(buf, sizeof buf, "%s\"%s\": %.3f", i ? ", " : "", FrameProfile::names[i],
                          double(phase_max[size_t(i)]) / 1e6);
            s += buf;
        }
        s += "},\n";
        s += pacing_json();
        s += "  \"slow_frames\": [";
        for (size_t i = 0; i < slow.size(); ++i) {
            std::snprintf(buf, sizeof buf,
                          "%s{\"frame\": %ld, \"ms\": %.3f, \"worst\": \"%s\", \"worst_ms\": %.3f}",
                          i ? ", " : "", slow[i].frame, double(slow[i].total_ns) / 1e6,
                          FrameProfile::names[slow[i].phase], double(slow[i].phase_ns) / 1e6);
            s += buf;
        }
        s += "]\n}\n";
        return s;
    }
    // Ticks the measured wall time should have produced at exactly 60 Hz.
    double expected_ticks() const { return double(wall_ns) * 60.0 / 1e9; }
    double drift_ticks() const { return double(balance_q16) / 65536.0 - expected_ticks(); }
    std::string pacing_json() const {
        std::vector<uint64_t> sorted = intervals;
        std::sort(sorted.begin(), sorted.end()); // total-order: integers
        const auto pct = [&](double q) {
            if (sorted.empty()) return 0.0;
            return double(sorted[std::min(sorted.size() - 1, size_t(q * double(sorted.size() - 1) + 0.5))]) /
                   1e6;
        };
        char buf[512];
        std::snprintf(buf, sizeof buf,
                      "  \"pacing\": {\"wall_s\": %.3f, \"expected_ticks\": %.3f, \"drift_ticks\": %.3f, "
                      "\"interval_ms\": {\"p50\": %.3f, \"p99\": %.3f, \"max\": %.3f}, "
                      "\"step_frames\": [%llu, %llu, %llu, %llu], \"clock_resets\": %llu},\n",
                      double(wall_ns) / 1e9, expected_ticks(), drift_ticks(), pct(0.50), pct(0.99), pct(1.0),
                      (unsigned long long)step_frames[0], (unsigned long long)step_frames[1],
                      (unsigned long long)step_frames[2], (unsigned long long)step_frames[3],
                      (unsigned long long)clock_resets);
        return buf;
    }
};
} // namespace rr
