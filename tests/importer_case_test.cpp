// Plan 33 Task A5: folder import finds disc files case-insensitively (a Linux
// loop mount of the ISO shows lower-case names) and stores the canonical
// upper-case names; entries that differ only in case are rejected.
#include "check.hpp"
#include "importer.hpp"
#include "sha256.hpp"
#include "temp_dir.hpp"
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

int main() {
    rr::test::Checks check;
    const fs::path root = fs::temp_directory_path() / ("rr-import-case-" + rr::test::process_tag());
    const std::vector<rr::ReferenceHash> hashes = {{"ROADRASH.EXE", rr::sha256({'e', 'x', 'e'})},
                                                   {"DATA/COURSES/NAPA.CRS", rr::sha256({'c', 'r', 's'})}};
    auto write = [](const fs::path& p, const char* text) {
        fs::create_directories(p.parent_path());
        std::ofstream(p, std::ios::binary) << text;
    };
    // Lower-case loop mount with the disc's ROADRASH sub-directory.
    write(root / "lower" / "roadrash" / "roadrash.exe", "exe");
    write(root / "lower" / "roadrash" / "data" / "courses" / "napa.crs", "crs");
    const auto lower = rr::import_game((root / "lower").string(), (root / "out-lower").string(), hashes);
    check(lower.ok, "lower-case folder imports");
    check(fs::exists(root / "out-lower" / "ROADRASH.EXE") &&
              fs::exists(root / "out-lower" / "DATA" / "COURSES" / "NAPA.CRS"),
          "destination keeps the canonical upper-case names");
    // Mixed case, folder picked at the ROADRASH level itself.
    write(root / "mixed" / "RoadRash.Exe", "exe");
    write(root / "mixed" / "Data" / "COURSES" / "Napa.crs", "crs");
    check(rr::import_game((root / "mixed").string(), (root / "out-mixed").string(), hashes).ok,
          "mixed-case folder imports");
    // DATA and data side by side (possible on case-sensitive file systems).
    write(root / "ambiguous" / "ROADRASH.EXE", "exe");
    write(root / "ambiguous" / "DATA" / "COURSES" / "NAPA.CRS", "crs");
    write(root / "ambiguous" / "data" / "courses" / "napa.crs", "crs");
    std::error_code error;
    const bool case_sensitive =
        !fs::equivalent(root / "ambiguous" / "DATA", root / "ambiguous" / "data", error);
    if (case_sensitive) {
        const auto ambiguous =
            rr::import_game((root / "ambiguous").string(), (root / "out-ambiguous").string(), hashes);
        bool reported = false;
        for (const auto& m : ambiguous.missing)
            reported |= m.find("differ only in case") != std::string::npos;
        check(!ambiguous.ok && reported && !fs::exists(root / "out-ambiguous"),
              "entries differing only in case are rejected with a clear message");
    } else {
        std::cout << "ambiguous-case check not applicable: case-insensitive temp file system\n";
    }
    fs::remove_all(root, error);
    return check.finish("importer_case");
}
