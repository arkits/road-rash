// Plan 33b Task 2: retried renames, machine-local data and the one-time
// migration of an imported disc out of the roaming profile.
#include "atomic_file.hpp"
#include "check.hpp"
#include "platform_files.hpp"
#include "temp_dir.hpp"
#include "utf8_path.hpp"
#include <SDL3/SDL.h>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace fs = std::filesystem;

namespace {
std::string read_text(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), {});
}
void write_text(const fs::path& p, const std::string& s) {
    fs::create_directories(p.parent_path());
    std::ofstream(p, std::ios::binary) << s;
}
} // namespace

int main() {
    rr::test::Checks check;
    const fs::path root = fs::temp_directory_path() / ("rr-platform-files-" + rr::test::process_tag());
    fs::create_directories(root);

    // Plain replace.
    write_text(root / "a.txt", "new");
    write_text(root / "b.txt", "old");
    std::error_code ec;
    check(rr::rename_with_retry(root / "a.txt", root / "b.txt", ec) && read_text(root / "b.txt") == "new",
          "rename_with_retry replaces the destination");

    // Migration: same-volume move with every file intact.
    write_text(root / "roaming" / "game" / "ROADRASH" / "ROADRASH.EXE", "exe");
    write_text(root / "roaming" / "game" / "ROADRASH" / "DATA" / "NAPA.CRS", "crs");
    write_text(root / "roaming" / "game" / "ROADRASH" / ".rr-import", "rr-import 1\n2\n");
    check(rr::migrate_directory(root / "roaming" / "game", root / "local" / "game"),
          "migration moves the tree");
    check(!fs::exists(root / "roaming" / "game") &&
              read_text(root / "local" / "game" / "ROADRASH" / "DATA" / "NAPA.CRS") == "crs" &&
              fs::exists(root / "local" / "game" / "ROADRASH" / ".rr-import"),
          "migrated tree is complete and the old one is gone");
    // Never over an existing destination.
    write_text(root / "roaming2" / "game" / "x", "1");
    check(!rr::migrate_directory(root / "roaming2" / "game", root / "local" / "game") &&
              fs::exists(root / "roaming2" / "game" / "x"),
          "migration refuses an existing destination and keeps the source");

    const std::string local = rr::local_data_directory();
    check(!local.empty() && fs::is_directory(rr::path_from_utf8(local)), "local data directory exists");
#if defined(_WIN32)
    check(local.find("AppData\\Local") != std::string::npos ||
              local.find("AppData/Local") != std::string::npos,
          "Windows local data is under %LOCALAPPDATA%");
    // A destination held open without FILE_SHARE_DELETE (as scanners do)
    // blocks the replace; a short hold is retried, a long one fails cleanly.
    auto hold = [&](const fs::path& p, int ms) {
        return std::thread([p, ms] {
            HANDLE h = ::CreateFileW(p.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                     OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
            std::this_thread::sleep_for(std::chrono::milliseconds(ms));
            if (h != INVALID_HANDLE_VALUE) ::CloseHandle(h);
        });
    };
    const fs::path target = root / "settings.txt";
    write_text(target, "old");
    {
        auto t = hold(target, 150);
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
        check(rr::write_file_atomic(target, "short", 5) && read_text(target) == "short",
              "a briefly held destination is replaced after retries");
        t.join();
    }
    {
        auto t = hold(target, 3000);
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
        const bool ok = rr::write_file_atomic(target, "long", 4);
        t.join();
        fs::path tmp = target;
        tmp += ".tmp";
        check(!ok && read_text(target) == "short" && !fs::exists(tmp),
              "a long hold fails cleanly: old file kept, temporary removed");
    }
#endif
    fs::remove_all(root, ec);
    return check.finish("platform_files");
}
