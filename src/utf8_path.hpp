// Plan 33 Task A4: the UTF-8 boundary between SDL (UTF-8 everywhere) and
// std::filesystem. path(const char*) and path::string() use the ANSI code
// page on Windows, so non-ASCII user names would break pref paths; these
// helpers convert through char8_t instead and are identity on POSIX.
#pragma once
#include <cstdio>
#include <filesystem>
#include <string>
#include <string_view>

namespace rr {
std::filesystem::path path_from_utf8(std::string_view utf8);
std::string path_to_utf8(const std::filesystem::path& path);
// fopen with a UTF-8-correct path (_wfopen on Windows).
std::FILE* open_file(const std::filesystem::path& path, const char* mode);
} // namespace rr
