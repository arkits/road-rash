// Plan 33 Task A6: portable per-process names for test temp paths (replaces
// POSIX getpid(), which MSVC lacks). Stable for the life of the process.
#pragma once
#include <chrono>
#include <cstdint>
#include <random>
#include <string>

namespace rr::test {
inline const std::string& process_tag() {
    static const std::string tag = [] {
        std::random_device device;
        const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
        return std::to_string(uint64_t(now) ^ (uint64_t(device()) << 32 | device()));
    }();
    return tag;
}
} // namespace rr::test
