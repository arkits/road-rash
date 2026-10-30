// Plan 33b Task 2: file-system behaviour that differs by OS.
#include "platform_files.hpp"
#include "utf8_path.hpp"
#include <SDL3/SDL.h>
#include <chrono>
#include <cstdint>
#include <stdexcept>
#include <thread>
#include <vector>
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <knownfolders.h>
#include <shlobj.h>
#endif

namespace rr {
namespace fs = std::filesystem;

bool rename_with_retry(const fs::path& from, const fs::path& to, std::error_code& ec) {
    ec.clear();
#if defined(_WIN32)
    int delay_ms = 10;
    for (int attempt = 0; attempt < 10; ++attempt) {
        if (::MoveFileExW(from.c_str(), to.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            ec.clear(); // an earlier attempt's error must not survive a successful retry
            return true;
        }
        const DWORD error = ::GetLastError();
        ec.assign(int(error), std::system_category());
        if (error != ERROR_ACCESS_DENIED && error != ERROR_SHARING_VIOLATION && error != ERROR_LOCK_VIOLATION)
            return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
        delay_ms = delay_ms * 2 > 200 ? 200 : delay_ms * 2;
    }
    return false;
#else
    fs::rename(from, to, ec);
    return !ec;
#endif
}

std::string portable_data_directory_in(const fs::path& executable_directory) {
    std::error_code ec;
    if (executable_directory.empty() || !fs::is_regular_file(executable_directory / "portable.txt", ec))
        return {};
    const fs::path dir = executable_directory / "data";
    fs::create_directories(dir, ec);
    if (ec) throw std::runtime_error("Cannot create portable data directory: " + path_to_utf8(dir));
    return path_to_utf8(dir);
}

std::string portable_data_directory() {
#if defined(__ANDROID__) || defined(__EMSCRIPTEN__)
    return {};
#else
    const char* base = SDL_GetBasePath(); // cached by SDL; not freed
    return base ? portable_data_directory_in(path_from_utf8(base)) : std::string();
#endif
}

std::string local_data_directory() {
    if (std::string portable = portable_data_directory(); !portable.empty()) return portable;
#if defined(_WIN32)
    PWSTR folder = nullptr;
    if (SUCCEEDED(::SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_CREATE, nullptr, &folder))) {
        const fs::path base(folder);
        ::CoTaskMemFree(folder);
        const fs::path dir = base / L"RoadRashNative" / L"RoadRash";
        std::error_code ec;
        fs::create_directories(dir, ec);
        if (!ec) return path_to_utf8(dir);
    } else if (folder) {
        ::CoTaskMemFree(folder);
    }
#endif
    char* pref = SDL_GetPrefPath("RoadRashNative", "RoadRash");
    if (!pref) throw std::runtime_error(SDL_GetError());
    std::string result = pref;
    SDL_free(pref);
    while (!result.empty() && (result.back() == '/' || result.back() == '\\')) result.pop_back();
    return result;
}

namespace {
struct TreeSummary {
    uintmax_t files = 0, bytes = 0;
    bool ok = true;
};
TreeSummary summarize(const fs::path& root) {
    TreeSummary s;
    std::error_code ec;
    for (fs::recursive_directory_iterator it(root, ec), end; !ec && it != end; it.increment(ec)) {
        if (it->is_regular_file(ec)) {
            ++s.files;
            s.bytes += it->file_size(ec);
        }
        if (ec) break;
    }
    s.ok = !ec;
    return s;
}
} // namespace

bool migrate_directory(const fs::path& from, const fs::path& to) {
    std::error_code ec;
    if (!fs::is_directory(from, ec) || fs::exists(to, ec)) return false;
    fs::create_directories(to.parent_path(), ec);
    if (ec) return false;
    if (rename_with_retry(from, to, ec)) return true;
    // Different volumes: copy into a staging name, verify, then swap in.
    fs::path staging = to;
    staging += ".migrating";
    fs::remove_all(staging, ec);
    fs::copy(from, staging, fs::copy_options::recursive, ec);
    const TreeSummary a = summarize(from), b = summarize(staging);
    if (ec || !a.ok || !b.ok || a.files != b.files || a.bytes != b.bytes ||
        !rename_with_retry(staging, to, ec)) {
        fs::remove_all(staging, ec);
        return false;
    }
    fs::remove_all(from, ec);
    return true;
}
} // namespace rr
