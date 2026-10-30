// Plan 33 Task A4: non-ASCII paths survive the SDL <-> std::filesystem
// boundary (on Windows this is the regression test for ANSI code-page
// conversion; on POSIX the helpers are identity).
#include "atomic_file.hpp"
#include "check.hpp"
#include "importer.hpp"
#include "settings.hpp"
#include "sha256.hpp"
#include "temp_dir.hpp"
#include "utf8_path.hpp"
#include <SDL3/SDL.h>
#include <cstring>
#include <filesystem>

namespace fs = std::filesystem;

int main() {
    rr::test::Checks check;
    const std::string dir_name =
        "T\xc3\xabst \xe3\x83\x87\xe3\x82\xa3\xe3\x83\xac\xe3\x82\xaf\xe3\x83\x88\xe3\x83\xaa"; // Tëst ディレクトリ
    const std::string file_name = "\xc3\x9cn\xc3\xaf"
                                  "code.rrs"; // Ünïcode.rrs
    const fs::path root = fs::temp_directory_path() / ("rr-utf8-" + rr::test::process_tag());
    const fs::path dir = root / rr::path_from_utf8(dir_name);
    fs::create_directories(dir);
    const fs::path file = dir / rr::path_from_utf8(file_name);
    const char payload[] = "road rash";
    check(rr::write_file_atomic(file, payload, sizeof payload - 1), "atomic write to a Unicode path");
    bool listed = false;
    for (const auto& entry : fs::directory_iterator(dir))
        listed |= rr::path_to_utf8(entry.path().filename()) == file_name;
    check(listed, "directory listing returns the UTF-8 name unchanged");
    check(rr::path_to_utf8(file).find(dir_name) != std::string::npos,
          "path_to_utf8 keeps the directory name");
    size_t size = 0;
    void* bytes = SDL_LoadFile(rr::path_to_utf8(file).c_str(), &size);
    check(bytes && size == sizeof payload - 1 && std::memcmp(bytes, payload, size) == 0,
          "SDL reads the UTF-8 path");
    SDL_free(bytes);
    if (std::FILE* f = rr::open_file(file, "rb")) {
        char buf[16] = {};
        check(std::fread(buf, 1, sizeof buf, f) == sizeof payload - 1, "open_file reads the Unicode path");
        std::fclose(f);
    } else {
        check(false, "open_file opens the Unicode path");
    }
    const fs::path renamed = dir / rr::path_from_utf8("\xe7\x8e\x8b\xe4\xbc\x9f.rrs"); // 王伟.rrs
    std::error_code error;
    fs::rename(file, renamed, error);
    check(!error && fs::exists(renamed), "rename between Unicode names");
    // Plan 33b Task 2 Step 2: the game's std::string (UTF-8) paths for
    // settings and import. On Windows these go through narrow CRT/STL calls,
    // so this passes only with the UTF-8 active-code-page manifest.
    const std::string settings_path = rr::path_to_utf8(dir / "settings.txt");
    rr::Settings settings;
    settings.mini_dash = 2;
    settings.video_mode = 1;
    check(rr::save_settings(settings_path, settings), "settings save under a Unicode profile path");
    const rr::Settings loaded = rr::load_settings(settings_path);
    check(loaded.mini_dash == 2 && loaded.video_mode == 1, "settings load back from the Unicode path");
    const fs::path disc = dir / "disc" / "ROADRASH";
    fs::create_directories(disc / "DATA");
    {
        std::FILE* f = rr::open_file(disc / "ROADRASH.EXE", "wb");
        std::fputs("exe", f);
        std::fclose(f);
    }
    {
        std::FILE* f = rr::open_file(disc / "DATA" / "NAPA.CRS", "wb");
        std::fputs("crs", f);
        std::fclose(f);
    }
    const std::vector<rr::ReferenceHash> hashes = {{"ROADRASH.EXE", rr::sha256({'e', 'x', 'e'})},
                                                   {"DATA/NAPA.CRS", rr::sha256({'c', 'r', 's'})}};
    const auto imported =
        rr::import_game(rr::path_to_utf8(dir / "disc"), rr::path_to_utf8(dir / "game" / "ROADRASH"), hashes);
    check(imported.ok && fs::exists(dir / "game" / "ROADRASH" / "DATA" / "NAPA.CRS"),
          "folder import from and into Unicode paths");
    fs::remove_all(root, error);
    return check.finish("utf8_path");
}
