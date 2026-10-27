#include "render_threads.hpp"
#include "texture.hpp"
#include <algorithm>
#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>
namespace rr {
namespace {
int16_t narrow_point(int32_t v) {
    const uint16_t bits = uint16_t(v);
    int16_t r;
    std::memcpy(&r, &bits, 2);
    return r;
}
} // namespace
struct RenderThreads::Impl {
    struct Job {
        int band = 0, top = 0, bottom = 0;
    };
    explicit Impl(int n) : stop(false), generation(0), pending(0) {
        for (int i = 0; i < n; ++i) workers.emplace_back([this] { loop(); });
    }
    ~Impl() {
        {
            std::lock_guard<std::mutex> lock(mutex);
            stop = true;
        }
        wake.notify_all();
        for (auto& t : workers) t.join();
    }
    void loop() {
        uint64_t seen = 0;
        for (;;) {
            Job work{-1, 0, 0};
            bool have = false;
            {
                std::unique_lock<std::mutex> lock(mutex);
                if (queue.empty()) {
                    wake.wait(lock, [&] { return stop || generation != seen; });
                    if (stop) return;
                    seen = generation;
                } else if (stop) {
                    return;
                }
                if (!queue.empty()) {
                    work = queue.back();
                    queue.pop_back();
                    have = true;
                }
            }
            if (have) {
                std::exception_ptr error;
                try {
                    // job_ptr is set once per run and cleared after join;
                    // scratch[band] is this band's alone (Task 4).
                    (*job_ptr)(work.band, work.top, work.bottom, scratch[size_t(work.band)]);
                } catch (...) {
                    error = std::current_exception();
                }
                std::lock_guard<std::mutex> lock(mutex);
                if (error && !first_error) first_error = error;
                if (--pending == 0) done.notify_all();
            }
        }
    }
    std::mutex mutex;
    std::condition_variable wake, done;
    std::vector<std::thread> workers;
    std::vector<Job> queue;
    std::vector<RasterScratch64> scratch; // one per band; grown only
    const std::function<void(int, int, int, RasterScratch64&)>* job_ptr = nullptr;
    bool stop;
    uint64_t generation;
    size_t pending;
    std::exception_ptr first_error;
};
RenderThreads::RenderThreads(int threads) : threads_(threads < 1 ? 1 : threads) {
#if defined(__EMSCRIPTEN__) && !defined(__EMSCRIPTEN_PTHREADS__)
    // Single-thread browser/Node builds still accept settings that request
    // workers. Run the identical band jobs inline instead of throwing from
    // std::thread. The native/threaded builds retain their worker pool.
    threads_ = 1;
#endif
    if (threads_ > 1) impl_ = new Impl(threads_);
}
RenderThreads::~RenderThreads() {
    delete impl_;
    delete inline_holder_;
}
RenderThreads::Impl& RenderThreads::inline_scratch() const {
    if (!inline_holder_) inline_holder_ = new Impl(0);
    return *inline_holder_;
}
// Shared dispatch: sizes scratch once per (bands, height) growth, sets the
// job pointer once, queues one small Job per band (no per-band std::function).
void RenderThreads::run_impl(Impl& impl, int bands, int height,
                             const std::function<void(int, int, int, RasterScratch64&)>& job) {
    const int threads = int(impl.workers.size());
    if (bands < 1 || height < 1) throw std::runtime_error("Render bands out of range");
    if (int(impl.scratch.size()) < bands) impl.scratch.resize(size_t(bands));
    for (auto& sc : impl.scratch)
        if (int(sc.rows.size()) < height) sc.resize(height);
    if (threads <= 1 || bands == 1) {
        for (int b = 0; b < bands; ++b) {
            const int top = (height * b) / bands, bottom = (height * (b + 1)) / bands - 1;
            job(b, top, bottom, impl.scratch[size_t(b)]);
        }
        return;
    }
    {
        std::lock_guard<std::mutex> lock(impl.mutex);
        for (int b = bands - 1; b >= 0; --b) {
            const int top = (height * b) / bands, bottom = (height * (b + 1)) / bands - 1;
            impl.queue.push_back({b, top, bottom});
        }
        impl.pending = size_t(bands);
        impl.job_ptr = &job;
        ++impl.generation;
    }
    impl.wake.notify_all();
    std::unique_lock<std::mutex> lock(impl.mutex);
    impl.done.wait(lock, [&] { return impl.pending == 0; });
    impl.job_ptr = nullptr;
    if (impl.first_error) {
        std::exception_ptr error = std::exchange(impl.first_error, nullptr);
        lock.unlock();
        std::rethrow_exception(error);
    }
}
void RenderThreads::run(int bands, int height, const std::function<void(int, int, int)>& job) {
    // Legacy overload: per-band scratch is unused; forward through one shared job.
    // The 'wide' wrapper captures one pointer (SBO-clean, no heap job).
    struct Wrap {
        const std::function<void(int, int, int)>* inner;
    };
    Wrap w{&job};
    Wrap* pw = &w;
    std::function<void(int, int, int, RasterScratch64&)> wide =
        [pw](int b, int top, int bottom, RasterScratch64&) { (*pw->inner)(b, top, bottom); };
    if (threads_ == 1) {
        run_impl(inline_scratch(), bands, height, wide);
        return;
    }
    run_impl(*impl_, bands, height, wide);
}
void RenderThreads::run(int bands, int height,
                        const std::function<void(int, int, int, RasterScratch64&)>& job) {
    if (threads_ == 1) {
        run_impl(inline_scratch(), bands, height, job);
        return;
    }
    run_impl(*impl_, bands, height, job);
}
// Band entry: the existing node loop with RasterClip rows [top, bottom].
// Every band renders the whole node list in order, clipped to its rows, so
// the union is bit-identical to one thread (Review Focus 2).
struct ThreadedCtx {
    Framebuffer* fb;
    const std::vector<DrawNode>* nodes;
    const ViewMode* mode;
    uint32_t flat_mask;
    bool wide_surface;
    bool perspective_on;
};
static void threaded_band(ThreadedCtx& c, int top, int bottom, RasterScratch64& scratch) {
    Framebuffer& fb = *c.fb;
    const std::vector<DrawNode>& nodes = *c.nodes;
    const ViewMode& mode = *c.mode;
    const uint32_t flat_mask = c.flat_mask;
    const bool wide_surface = c.wide_surface;
    const bool perspective_on = c.perspective_on;

    RasterClip clip{0, top, fb.width(), bottom};
    for (const auto& node : nodes) {
        if (!node.texture && !node.flat_index) continue;
        std::array<RasterPoint, 4> points;
        for (size_t i = 0; i < 4; ++i)
            points[i] = {mode.map(node.view_points[i].x), mode.map(node.view_points[i].y)};
        int32_t minx = points[0].x, maxx = minx, miny = points[0].y, maxy = miny;
        for (const auto& p : points) {
            minx = std::min(minx, p.x);
            maxx = std::max(maxx, p.x);
            miny = std::min(miny, p.y);
            maxy = std::max(maxy, p.y);
        }
        if (minx > clip.right || maxx < clip.left || miny > clip.bottom || maxy < clip.top) continue;
        if (node.texture && node.texture->mips.empty() && !(node.texture->flags & flat_mask)) continue;
        if (!node.texture || (node.texture->flags & flat_mask)) {
            draw_flat_quad(fb, {points, node.texture ? node.texture->flat_index : *node.flat_index}, clip);
            continue;
        }
        const auto& texture = *node.texture;
        if (texture.flags & 0x2000)
            throw std::runtime_error("Dynamic rider texture must be specialized before rendering");
        bool flip_u = node.flip_u;
        if (!normalize_quad(points, texture.flags, &flip_u)) continue;
        const auto& bounds = texture.bounds;
        const uint32_t src_w = uint32_t(std::abs(int64_t(bounds[2]) - bounds[0]) + 1);
        const uint32_t src_h = uint32_t(std::abs(int64_t(bounds[3]) - bounds[1]) + 1);
        const uint16_t u0 = uint16_t(flip_u ? bounds[2] - 1 : bounds[0] + 1);
        const uint16_t u1 = uint16_t(flip_u ? bounds[0] + 1 : bounds[2] - 1);
        const uint16_t v0 = uint16_t(node.flip_v ? bounds[3] - 1 : bounds[1] + 1);
        const uint16_t v1 = uint16_t(node.flip_v ? bounds[1] + 1 : bounds[3] - 1);
        const std::array<std::array<uint16_t, 2>, 4> uv = {{{u0, v0}, {u1, v0}, {u1, v1}, {u0, v1}}};
        if (perspective_on &&
            (node.view_depth[0] | node.view_depth[1] | node.view_depth[2] | node.view_depth[3])) {
            const int psigned =
                texture_mip_level_signed(uint32_t(maxx - minx + 1), uint32_t(maxy - miny + 1), src_w, src_h,
                                         texture.mips.size(), texture.upscaled.size());
            if (psigned >= 0) {
                const size_t plevel = size_t(psigned);
                const auto& pmip = texture.mips.at(plevel);
                std::array<PerspectiveVertex, 4> pv;
                for (size_t i = 0; i < 4; ++i) {
                    const float inv = node.view_depth[i] > 0 ? 256.0f / float(node.view_depth[i]) : 0.0f;
                    pv[i] = {points[i].x, points[i].y, float(uv[i][0] >> plevel), float(uv[i][1] >> plevel),
                             inv};
                }
                draw_textured_quad_perspective(fb, pv, pmip, (texture.flags & 2) != 0, clip);
                continue;
            }
        }
        if (wide_surface) {
            const int signed_level =
                texture_mip_level_signed(uint32_t(maxx - minx + 1), uint32_t(maxy - miny + 1), src_w, src_h,
                                         texture.mips.size(), texture.upscaled.size());
            std::array<TextureVertexWide, 4> vertices;
            if (signed_level < 0) {
                const size_t up = size_t(-signed_level - 1);
                const auto& mip = texture.upscaled.at(up);
                const unsigned s = unsigned(1) << (up + 1);
                for (size_t i = 0; i < 4; ++i)
                    vertices[i] = {points[i].x, points[i].y, uint16_t(uv[i][0] * s), uint16_t(uv[i][1] * s)};
                draw_textured_quad_wide(fb, vertices, mip.pixels, mip.width, mip.stride_shift,
                                        (texture.flags & 2) != 0, clip, true, scratch);
            } else {
                const size_t level = size_t(signed_level);
                for (size_t i = 0; i < 4; ++i)
                    vertices[i] = {points[i].x, points[i].y, uint16_t(uv[i][0] >> level),
                                   uint16_t(uv[i][1] >> level)};
                const auto& mip = texture.mips.at(level);
                draw_textured_quad_wide(
                    fb, vertices, mip.sampling_storage.empty() ? mip.pixels : mip.sampling_storage, mip.width,
                    mip.stride_shift, (texture.flags & 2) != 0, clip, false, scratch);
            }
            continue;
        }
        const size_t level = texture_mip_level(uint32_t(maxx - minx + 1), uint32_t(maxy - miny + 1), src_w,
                                               src_h, texture.mips.size());
        std::array<TextureVertex, 4> vertices;
        for (size_t i = 0; i < 4; ++i)
            vertices[i] = {narrow_point(points[i].x), narrow_point(points[i].y), uint16_t(uv[i][0] >> level),
                           uint16_t(uv[i][1] >> level)};
        const auto& mip = texture.mips.at(level);
        draw_textured_quad(fb, vertices, mip.sampling_storage.empty() ? mip.pixels : mip.sampling_storage,
                           mip.stride_shift, (texture.flags & 2) != 0, clip);
    }
}
void render_nodes_threaded(Framebuffer& fb, const std::vector<DrawNode>& nodes, const ViewMode& mode,
                           uint32_t flat_mask, RenderThreads& pool, bool perspective_on) {
    if (fb.width() != mode.surface_width || fb.height() != mode.surface_height)
        throw std::runtime_error("View/RGB framebuffer size mismatch");
    const int bands = std::max(1, pool.count());
    ThreadedCtx ctx{
        &fb, &nodes, &mode, flat_mask, mode.surface_width > 640 || mode.surface_height > 480, perspective_on};
    // Single-pointer capture (8 bytes, std::function SBO-clean): no per-run heap job.
    ThreadedCtx* pc = &ctx;
    pool.run(bands, fb.height(), [pc](int, int top, int bottom, RasterScratch64& scratch) {
        threaded_band(*pc, top, bottom, scratch);
    });
}
} // namespace rr
