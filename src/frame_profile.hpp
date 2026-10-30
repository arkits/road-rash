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
    FrameProfile current;
    bool done() const { return long(totals.size()) >= frames; }
    void finish_frame(uint64_t total_ns, int ticks) {
        if (seen++ >= warmup && !done()) {
            totals.push_back(total_ns);
            sim_ticks += uint64_t(std::max(0, ticks));
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
        s += "},\n  \"slow_frames\": [";
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
};
} // namespace rr
