#include "atomic_file.hpp"
#include "platform_files.hpp"
#include "utf8_path.hpp"
#include <cstdio>
#include <system_error>
#if defined(_WIN32)
#include <io.h>
#else
#include <unistd.h>
#endif
namespace rr {
bool write_file_atomic(const std::filesystem::path& path, const void* data, size_t size) {
    std::filesystem::path tmp = path;
    tmp += ".tmp";
    std::FILE* f = open_file(tmp, "wb");
    if (!f) return false;
    bool ok = size == 0 || std::fwrite(data, 1, size, f) == size;
    ok = std::fflush(f) == 0 && ok;
#if defined(_WIN32)
    ok = ::_commit(::_fileno(f)) == 0 && ok;
#else
    ok = ::fsync(::fileno(f)) == 0 && ok;
#endif
    ok = std::fclose(f) == 0 && ok;
    std::error_code ec;
    if (ok) rename_with_retry(tmp, path, ec); // plan 33b: retried on Windows
    if (!ok || ec) {
        std::filesystem::remove(tmp, ec);
        return false;
    }
    return true;
}
} // namespace rr
