#include "atomic_file.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include "temp_dir.hpp"
namespace fs = std::filesystem;
static std::string slurp(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), {}};
}

int main() {
    int failures = 0;
    auto check = [&](bool v, const char* what) {
        if (!v) {
            ++failures;
            std::cerr << "FAIL " << what << '\n';
        }
    };
    const fs::path dir = fs::temp_directory_path() / ("rr-atomic-" + rr::test::process_tag());
    fs::remove_all(dir);
    fs::create_directories(dir);
    const fs::path f = dir / "save.RRS";
    check(rr::write_file_atomic(f, "first", 5) && slurp(f) == "first", "create");
    check(rr::write_file_atomic(f, "second!", 7) && slurp(f) == "second!", "replace");
    {
        std::ofstream stale(dir / "save.RRS.tmp");
        stale << "garbage";
    }
    check(rr::write_file_atomic(f, "third", 5) && slurp(f) == "third", "stale tmp overwritten");
    check(!fs::exists(dir / "save.RRS.tmp"), "no tmp left behind");
    fs::create_directory(dir / "blocked.RRS.tmp"); // temp path cannot be opened as a file
    {
        std::ofstream orig(dir / "blocked.RRS");
        orig << "keep";
    }
    check(!rr::write_file_atomic(dir / "blocked.RRS", "lost", 4), "failure reported");
    check(slurp(dir / "blocked.RRS") == "keep", "original untouched on failure");
    check(!rr::write_file_atomic(dir / "missing" / "x.RRS", "x", 1), "missing directory fails");
    fs::remove_all(dir);
    if (failures == 0) std::cout << "atomic_file OK\n";
    return failures ? 1 : 0;
}
