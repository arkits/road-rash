// Plan 33 Task A4: UTF-8 path boundary.
#include "utf8_path.hpp"

namespace rr {
std::filesystem::path path_from_utf8(std::string_view utf8) {
    return std::filesystem::path(std::u8string(utf8.begin(), utf8.end()));
}
std::string path_to_utf8(const std::filesystem::path& path) {
    const std::u8string u = path.u8string();
    return std::string(u.begin(), u.end());
}
std::FILE* open_file(const std::filesystem::path& path, const char* mode) {
#ifdef _WIN32
    std::wstring wide_mode;
    for (const char* m = mode; *m; ++m) wide_mode.push_back(wchar_t(*m));
    return ::_wfopen(path.c_str(), wide_mode.c_str());
#else
    return std::fopen(path.c_str(), mode);
#endif
}
} // namespace rr
