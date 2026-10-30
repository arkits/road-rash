#include "assets.hpp"
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include "temp_dir.hpp"

int main() {
    int failures = 0;
    auto check = [&](bool v, const char* what) {
        if (!v) {
            ++failures;
            std::cerr << "FAIL " << what << '\n';
        }
    };
    auto dir = std::filesystem::temp_directory_path() / ("rr-assets-test-" + rr::test::process_tag());
    std::filesystem::create_directories(dir / "DATA");
    {
        std::ofstream(dir / "DATA" / "X.BIN", std::ios::binary) << "abc";
    }
    rr::Assets assets(dir.string());
    check(assets.exists("DATA/X.BIN"), "exists");
    check(!assets.exists("DATA/NOPE.BIN"), "missing");
    auto bytes = assets.read("DATA/X.BIN");
    check(bytes.size() == 3 && bytes[0] == 'a', "read contents");
    try {
        assets.read("DATA/NOPE.BIN");
        check(false, "throw on missing");
    } catch (const std::runtime_error& e) {
        check(std::string(e.what()).find("NOPE.BIN") != std::string::npos, "error names path");
    }
    std::filesystem::remove_all(dir);
    std::cout << failures << " failures\n";
    return failures ? 1 : 0;
}
