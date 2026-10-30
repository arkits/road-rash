// Plan 29 Task 4: indexed threaded path must not allocate per frame (failing first).
#define RR_ALLOC_COUNTER_DEFINE
#include "alloc_counter.hpp"
#include "bench_scene.hpp"
#include "framebuffer.hpp"
#include "render_threads.hpp"
#include "view.hpp"
#include <iostream>

namespace {
uint64_t checksum_idx(const rr::Framebuffer& fb) {
    uint64_t h = 1469598103934665603ULL;
    for (int y = 0; y < fb.height(); ++y)
        for (int x = 0; x < fb.width(); ++x) {
            h ^= fb.row(y)[x];
            h *= 1099511628211ULL;
        }
    return h;
}
} // namespace

int main() {
    int failures = 0;
    auto check = [&](bool v, const char* what) {
        if (!v) {
            ++failures;
            std::cerr << "FAIL " << what << '\n';
        }
    };
    rr::test::BenchScene scene = rr::test::make_bench_scene();
    rr::ViewMode mode = rr::view_mode(true);
    mode.surface_width = 1280;
    mode.surface_height = 960; // scale 2 (wide path + scratch)
    rr::RenderThreads pool(4);
    rr::Framebuffer fb(1280, 960);
    fb.clear(0);
    rr::render_nodes_threaded(fb, scene.nodes, mode, 16, pool);
    const uint64_t first = checksum_idx(fb);
    // Two warm-up frames: worker thread_locals + pool state settle (frame 1
    // still sizes 4 per-worker workspaces; frames 2+ are steady-state).
    fb.clear(0);
    rr::render_nodes_threaded(fb, scene.nodes, mode, 16, pool);
    {
        rr::test::CountAllocs c;
        fb.clear(0);
        rr::render_nodes_threaded(fb, scene.nodes, mode, 16, pool);
        const size_t n = c.count();
        std::cout << "steady-state allocations: " << n << (n ? " (sizes: " + c.sizes_text() + ")" : "")
                  << '\n';
        check(n == 0, "zero allocations in steady state");
    }
    check(checksum_idx(fb) == first, "checksum stable across frames");
    if (failures == 0) std::cout << "render_threads_alloc OK\n";
    return failures ? 1 : 0;
}
