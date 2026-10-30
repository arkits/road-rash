// Plan 33b Task 2: file-system behaviour that differs by OS.
#pragma once
#include <filesystem>
#include <string>
#include <system_error>

namespace rr {
// Renames `from` to `to`, replacing an existing file `to`. On Windows,
// antivirus and indexers briefly hold new files open, so access-denied,
// sharing and lock violations are retried (10 attempts, 10-200 ms apart)
// before failing; `to` is never left half-written. Elsewhere a plain rename.
bool rename_with_retry(const std::filesystem::path& from, const std::filesystem::path& to,
                       std::error_code& ec);

// Machine-local (non-roaming) data: %LOCALAPPDATA%\RoadRashNative\RoadRash on
// Windows, the SDL pref path elsewhere. UTF-8. Holds the imported disc and
// logs; saves and settings stay in the (roaming) pref path.
std::string local_data_directory();

// Moves the directory `from` to `to` when `to` does not exist yet: a rename
// on the same volume, otherwise a copy that is verified (file count and
// sizes) before `from` is removed. Returns false and leaves `from` intact on
// any failure. Used once to move an imported disc out of the roaming profile.
bool migrate_directory(const std::filesystem::path& from, const std::filesystem::path& to);

// Plan 33b Task 5: portable mode, a native addition. When `portable.txt` sits
// next to the executable, saves, settings, the imported disc and logs all live
// in `<executable directory>/data` (created on demand) instead of the per-user
// folders, so the game runs from a USB stick. Returns that directory (UTF-8,
// no trailing separator), or "" when not portable. Desktop only: Android and
// the browser never are. RR_PREF_OVERRIDE still takes precedence over it.
std::string portable_data_directory();
// The same decision for an explicit executable directory (testable).
std::string portable_data_directory_in(const std::filesystem::path& executable_directory);
} // namespace rr
