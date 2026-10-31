// Plan 29 Task 1: global operator-new counter (include in exactly one TU per binary).
#pragma once
#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdlib>
#include <new>
#include <cstdio>
#include <string>

// Plan 33a: Linux ThreadSanitizer links operator new/delete from its static
// runtime, so a replacement here would be a duplicate definition. The TSan
// tree still runs these tests for their threads; counts then read 0 and the
// counter says so. Normal and ASan trees count for real.
#if defined(__linux__) && defined(__SANITIZE_THREAD__)
#define RR_ALLOC_COUNTER_UNAVAILABLE 1
#elif defined(__linux__) && defined(__has_feature)
#if __has_feature(thread_sanitizer)
#define RR_ALLOC_COUNTER_UNAVAILABLE 1
#endif
#endif

namespace rr::test {
#ifdef RR_ALLOC_COUNTER_UNAVAILABLE
inline constexpr bool counting_available = false;
#else
inline constexpr bool counting_available = true;
#endif
inline std::atomic<size_t> allocations{0};
inline std::atomic<bool> counting{false};
// Sizes of the first counted allocations, for diagnosing a failing count.
inline std::atomic<size_t> sizes[16];
struct CountAllocs { // RAII: counts operator new calls in scope
    CountAllocs() {
        if constexpr (!counting_available) {
            static const bool noted =
                (std::fputs("alloc counter: unavailable under Linux TSan; counts read 0\n", stderr), true);
            (void)noted;
        }
        for (auto& s : sizes) s = 0;
        allocations = 0;
        counting = true;
    }
    ~CountAllocs() { counting = false; }
    size_t count() const { return allocations.load(); }
    // "24 24 8 ..." for the first (up to 16) counted allocations.
    std::string sizes_text() const {
        std::string out;
        for (size_t i = 0; i < std::min<size_t>(count(), 16); ++i)
            out += std::to_string(sizes[i].load()) + ' ';
        return out;
    }
};
} // namespace rr::test

#if defined(RR_ALLOC_COUNTER_DEFINE) && !defined(RR_ALLOC_COUNTER_UNAVAILABLE)
namespace rr::test {
inline void* counted_malloc(std::size_t n) noexcept {
    void* p = std::malloc(n ? n : 1);
    if (p && counting.load()) {
        const size_t i = allocations.fetch_add(1);
        if (i < 16) sizes[i] = n;
    }
    return p;
}
} // namespace rr::test
void* operator new(std::size_t n) {
    void* p = rr::test::counted_malloc(n);
    if (!p) throw std::bad_alloc();
    return p;
}
// The nothrow form too (std::stable_sort's buffer): otherwise the runtime's
// own nothrow new (ASan's, under the sanitizer) would pair with the free()
// below. Plan 33a: found by the Linux ASan tree.
void* operator new(std::size_t n, const std::nothrow_t&) noexcept {
    return rr::test::counted_malloc(n);
}
void operator delete(void* p) noexcept {
    std::free(p);
}
void operator delete(void* p, std::size_t) noexcept {
    std::free(p);
}
void operator delete(void* p, const std::nothrow_t&) noexcept {
    std::free(p);
}
#endif
