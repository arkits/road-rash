// Plan 29 Task 1: global operator-new counter (include in exactly one TU per binary).
#pragma once
#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdlib>
#include <new>
#include <string>

namespace rr::test {
inline std::atomic<size_t> allocations{0};
inline std::atomic<bool> counting{false};
// Sizes of the first counted allocations, for diagnosing a failing count.
inline std::atomic<size_t> sizes[16];
struct CountAllocs { // RAII: counts operator new calls in scope
    CountAllocs() {
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

#ifdef RR_ALLOC_COUNTER_DEFINE
void* operator new(std::size_t n) {
    void* p = std::malloc(n ? n : 1);
    if (!p) throw std::bad_alloc();
    if (rr::test::counting.load()) {
        const size_t i = rr::test::allocations.fetch_add(1);
        if (i < 16) rr::test::sizes[i] = n;
    }
    return p;
}
void operator delete(void* p) noexcept {
    std::free(p);
}
void operator delete(void* p, std::size_t) noexcept {
    std::free(p);
}
#endif
