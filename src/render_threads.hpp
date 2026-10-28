#pragma once
// Plan 19 Task 6: fixed worker pool for band-parallel rasterisation.
// Threads are created once and reused; no thread creation per frame.
// Plan 29 Task 4: the pool owns one RasterScratch64 per band (sized to the
// tallest surface seen; resized only on growth, never per frame). The
// scratch overload passes the band's scratch to the job; the shared job
// pointer is set once per run, so no std::function is copied per band.
#include "texture.hpp"
#include "world_renderer.hpp"
#include <functional>
namespace rr {
class RenderThreads {
  public:
    explicit RenderThreads(int threads); // 1 = run inline, no threads created
    ~RenderThreads();
    RenderThreads(const RenderThreads&) = delete;
    RenderThreads& operator=(const RenderThreads&) = delete;
    // Calls job(band, top_row, bottom_row_inclusive) for `bands` equal row
    // ranges and waits. Bands split rows [0, height) as evenly as possible.
    void run(int bands, int height, const std::function<void(int, int, int)>& job);
    // Scratch overload: job(band, top, bottom, scratch) gets the band's
    // persistent scratch (pool-owned). Keeps the plan 25 exception propagation.
    void run(int bands, int height, const std::function<void(int, int, int, RasterScratch64&)>& job);
    int count() const { return threads_; }

  private:
    struct Impl;
    Impl* impl_ = nullptr;
    mutable Impl* inline_holder_ = nullptr; // Task 4: scratch for the threads_==1 path (pool-affine)
    Impl& inline_scratch() const;
    int threads_ = 1;
    static void run_impl(Impl&, int bands, int height,
                         const std::function<void(int, int, int, RasterScratch64&)>& job);
};
void render_nodes_threaded(Framebuffer&, const std::vector<DrawNode>&, const ViewMode&, uint32_t flat_mask,
                           RenderThreads&, bool perspective = false);
} // namespace rr
