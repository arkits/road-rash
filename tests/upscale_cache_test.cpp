#include "upscale_cache.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include "temp_dir.hpp"
namespace {
std::vector<uint8_t> source_for(const std::string& asset, uint32_t index) {
    if (asset == "DATA/X.CEL" && index == 3) return {1, 2, 3, 4};
    if (asset == "A") return {9};
    return {};
}
std::vector<uint8_t> zip_stored(const std::vector<std::pair<std::string, std::vector<uint8_t>>>& files) {
    // Minimal stored-method zip writer for the test (mirrors unzip_checked).
    std::vector<uint8_t> out;
    std::vector<size_t> lho;
    auto u16 = [&](uint16_t v) {
        out.push_back(uint8_t(v));
        out.push_back(uint8_t(v >> 8));
    };
    auto u32 = [&](uint32_t v) {
        out.push_back(uint8_t(v));
        out.push_back(uint8_t(v >> 8));
        out.push_back(uint8_t(v >> 16));
        out.push_back(uint8_t(v >> 24));
    };
    std::vector<std::tuple<std::string, uint32_t, uint32_t>> central;
    for (auto& kv : files) {
        lho.push_back(out.size());
        out.insert(out.end(), {'P', 'K', 3, 4});
        u16(20);
        u16(0);
        u16(0);
        u16(0);
        u16(0);
        u32(0);
        u32(uint32_t(kv.second.size()));
        u32(uint32_t(kv.second.size()));
        u16(uint16_t(kv.first.size()));
        u16(0);
        out.insert(out.end(), kv.first.begin(), kv.first.end());
        out.insert(out.end(), kv.second.begin(), kv.second.end());
        central.emplace_back(kv.first, uint32_t(kv.second.size()), uint32_t(lho.back()));
    }
    const size_t cd = out.size();
    for (auto& e : central) {
        out.insert(out.end(), {'P', 'K', 1, 2});
        u16(20);
        u16(20);
        u16(0);
        u16(0);
        u16(0);
        u16(0);
        u32(0);
        u32(std::get<1>(e));
        u32(std::get<1>(e));
        u16(uint16_t(std::get<0>(e).size()));
        u16(0);
        u16(0);
        u16(0);
        u16(0);
        u32(0);
        u32(uint32_t(std::get<2>(e)));
        out.insert(out.end(), std::get<0>(e).begin(), std::get<0>(e).end());
    }
    const size_t end = out.size();
    out.insert(out.end(), {'P', 'K', 5, 6});
    u16(0);
    u16(0);
    u16(uint16_t(central.size()));
    u16(uint16_t(central.size()));
    u32(uint32_t(end - cd));
    u32(uint32_t(cd));
    u16(0);
    return out;
}
} // namespace
int main() {
    int failures = 0;
    auto check = [&](bool ok, const char* what) {
        if (!ok) {
            ++failures;
            std::cerr << what << '\n';
        }
    };
    const auto root =
        (std::filesystem::temp_directory_path() / ("rr-upscale-test-" + rr::test::process_tag())).string();
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    rr::UpscaleCache cache(root);
    const std::vector<uint8_t> src{1, 2, 3, 4};
    rr::UpscaledImage img;
    img.width = 8;
    img.height = 8;
    img.indexed = true;
    img.indices.assign(64, 5);
    check(cache.store("DATA/X.CEL", 3, 2, src, rr::UpscaleMethod::mmpx, img), "store indexed entry");
    {
        const auto got = cache.find("DATA/X.CEL", 3, 2, src, {rr::UpscaleMethod::mmpx});
        check(got && got->width == 8 && got->indices == img.indices, "find returns stored entry");
    }
    check(!cache.find("DATA/X.CEL", 3, 2, std::vector<uint8_t>{9, 9}, {rr::UpscaleMethod::mmpx}),
          "re-imported disc misses");
    {
        // Stale method version written by hand is ignored.
        const auto bytes =
            rr::encode_rrup(rr::UpscaleMethod::mmpx, rr::mmpx_version - 1, rr::source_hash32(src), img);
        const auto got = rr::decode_rrup(bytes, nullptr, nullptr, nullptr);
        check(got.has_value(), "hand-written stale file decodes");
        // find() must ignore it: overwrite the stored file with the stale one.
        std::filesystem::create_directories(std::filesystem::path(root) / "mmpx" / "2" / "DATA_X.CEL", ec);
        std::ofstream(std::filesystem::path(root) / "mmpx" / "2" / "DATA_X.CEL" / "3.rrup",
                      std::ios::binary | std::ios::trunc)
            .write(reinterpret_cast<const char*>(bytes.data()), long(bytes.size()));
        check(!cache.find("DATA/X.CEL", 3, 2, src, {rr::UpscaleMethod::mmpx}),
              "stale method version ignored");
        check(cache.store("DATA/X.CEL", 3, 2, src, rr::UpscaleMethod::mmpx, img), "re-store current version");
    }
    {
        rr::UpscaledImage ai;
        ai.width = 4;
        ai.height = 4;
        ai.indexed = false;
        ai.rgb.assign(16, 0xff0000u);
        check(cache.store("DATA/X.CEL", 3, 2, src, rr::UpscaleMethod::ai, ai), "store ai entry");
        const auto pref =
            cache.find("DATA/X.CEL", 3, 2, src, {rr::UpscaleMethod::ai, rr::UpscaleMethod::mmpx});
        check(pref && !pref->indexed, "ai preferred over mmpx");
        const auto only = cache.find("DATA/X.CEL", 3, 2, src, {rr::UpscaleMethod::mmpx});
        check(only && only->indexed, "mmpx-only query returns mmpx");
    }
    {
        // Every truncation and random bytes treated as missing.
        const auto full =
            rr::encode_rrup(rr::UpscaleMethod::mmpx, rr::mmpx_version, rr::source_hash32(src), img);
        for (size_t n = 0; n < full.size(); ++n) {
            std::vector<uint8_t> t(full.begin(), full.begin() + long(n));
            check(!rr::decode_rrup(t, nullptr, nullptr, nullptr), "truncation treated as missing");
        }
        check(!rr::decode_rrup(std::vector<uint8_t>{7, 7, 7, 7}, nullptr, nullptr, nullptr),
              "random bytes missing");
    }
    {
        // Interrupted store (*.tmp left behind) invisible to find.
        std::filesystem::create_directories(std::filesystem::path(root) / "mmpx" / "2" / "LEFTOVER", ec);
        std::ofstream(std::filesystem::path(root) / "mmpx" / "2" / "LEFTOVER" / "0.rrup.tmp",
                      std::ios::binary)
            .write("half", 4);
        check(!cache.find("LEFTOVER", 0, 2, src, {rr::UpscaleMethod::mmpx}), "tmp invisible");
    }
    {
        // Pack import: one valid, one hash mismatch, one truncated.
        rr::UpscaledImage small;
        small.width = 2;
        small.height = 2;
        small.indexed = true;
        small.indices.assign(4, 1);
        const auto good = rr::encode_rrup(rr::UpscaleMethod::mmpx, rr::mmpx_version,
                                          rr::source_hash32(std::vector<uint8_t>{9}), img);
        // Mismatch: valid file whose recorded hash is for other source bytes.
        const auto mismatch = rr::encode_rrup(rr::UpscaleMethod::mmpx, rr::mmpx_version,
                                              rr::source_hash32(std::vector<uint8_t>{7, 7}), small);
        const auto trunc = std::vector<uint8_t>(good.begin(), good.begin() + long(good.size() / 2));
        auto zip = zip_stored(
            {{"mmpx/2/A/0.rrup", good}, {"mmpx/2/A/1.rrup", mismatch}, {"mmpx/2/A/2.rrup", trunc}});
        const auto r = cache.import_pack(zip, [](const std::string& a, uint32_t) {
            if (a == "A") return std::vector<uint8_t>{9};
            return std::vector<uint8_t>{1};
        });
        check(r.imported == 1 && r.skipped == 2, "pack 1 imported 2 skipped");
        // Fuzz-adjacent ZIP validation: traversal, duplicates rejected.
        try {
            rr::unzip_checked(zip_stored({{"/abs", {1}}}));
            check(false, "absolute path throws");
        } catch (const std::runtime_error&) {
        }
        try {
            rr::unzip_checked(zip_stored({{"../up", {1}}}));
            check(false, "traversal throws");
        } catch (const std::runtime_error&) {
        }
        try {
            rr::unzip_checked(zip_stored({{"a", {1}}, {"a", {2}}}));
            check(false, "duplicate throws");
        } catch (const std::runtime_error&) {
        }
        try {
            rr::unzip_checked(std::vector<uint8_t>{1, 2, 3});
            check(false, "tiny zip throws");
        } catch (const std::runtime_error&) {
        }
        try {
            rr::unzip_checked(zip, 0);
            check(false, "inflated bound throws");
        } catch (const std::runtime_error&) {
        }
    }
    try {
        cache.find("", 0, 1, src, {rr::UpscaleMethod::mmpx});
        check(false, "empty asset throws");
    } catch (const std::runtime_error&) {
    }
    std::filesystem::remove_all(root, ec);
    std::cout << failures << " failures\n";
    return failures ? 1 : 0;
}
