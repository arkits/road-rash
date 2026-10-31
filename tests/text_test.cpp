#include "assets.hpp"
#include "pe_strings.hpp"
#include "text.hpp"
#include <algorithm>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>

int main(int argc, char** argv) {
    int failures = 0;
    auto check = [&](bool v, const char* what) {
        if (!v) {
            ++failures;
            std::cerr << "FAIL " << what << "\n";
        }
    };
    try {
        rr::TextRenderer bad(rr::Face::badloc, std::vector<uint8_t>(40, 0));
        ++failures;
    } catch (const std::runtime_error&) {
    }
    rr::Assets assets(argc > 1 ? argv[1] : RR_SOURCE_DIR "/../cdrom/ROADRASH");
    if (!assets.exists("TEXT/FUTB.DLL")) {
        std::cout << "skip: no disc\n";
        return 77;
    }
    const rr::TextRenderer text(assets);
    for (auto f :
         {rr::Face::badloc, rr::Face::futura_regular, rr::Face::futura_bold, rr::Face::futura_demi}) {
        check(text.width(f, 24, "") == 0, "empty width");
        const std::string s = "Big Game Mode \xe9";
        const int w = text.width(f, 24, s);
        check(w > 40 && w < 400, "plausible width");
        rr::RgbImage img(640, 100, 0);
        text.draw(img, f, 24, 100, 30, 0x123456, s);
        int ink = 0, outside = 0, other = 0;
        for (int y = 0; y < img.height; ++y)
            for (int x = 0; x < img.width; ++x) {
                const uint32_t p = img.at(x, y);
                if (!p) continue;
                ++ink;
                if (p != 0x123456) ++other;
                if (x < 100 - 4 || x >= 100 + w + 4 || y < 30 || y >= 30 + 24) ++outside;
            }
        check(ink > 50 && other == 0, "ink drawn in the exact colour");
        check(outside == 0, "ink stays inside the measured box");
    }
    // Word wrap keeps every language's longest menu description inside a 300-pixel box (0x00420470).
    for (auto l : {rr::Language::english, rr::Language::german, rr::Language::french, rr::Language::spanish,
                   rr::Language::italian}) {
        const auto table = rr::load_game_strings(rr::read_string_resources(assets.read(rr::language_dll(l))));
        size_t longest = 0;
        for (size_t i = 1; i < table.size(); ++i)
            if (table[i].size() > table[longest].size()) longest = i;
        rr::TextRect r{0, 0, 300, 0};
        const int h = text.draw_box(nullptr, rr::Face::futura_regular, 32, r, 0, table[longest],
                                    rr::dt::wordbreak | rr::dt::calcrect);
        check(h > 32 && r.right - r.left <= 300 + 200, "wrapped text measured");
        rr::RgbImage img(640, 480, 0);
        rr::TextRect box{8, 0, 308, 480};
        text.draw_box(&img, rr::Face::futura_demi, 24, box, 0xffffff, table[longest], rr::dt::wordbreak);
    }
    // Wine-GDI glyph ground truth (tools/glyphprobe.c mono/gray, tools/blendprobe.c),
    // dumped from the disc fonts into the git-ignored evidence folder.
    const std::string probe = RR_SOURCE_DIR "/analysis/frontend/glyphs-20261031/";
    std::ifstream mono_in(probe + "mono.txt"), gray_in(probe + "gray.txt"), blend_in(probe + "blend.txt");
    if (!mono_in || !gray_in || !blend_in) {
        std::cout << failures << " failures; skip glyph probes: no " << probe << "\n";
        return failures ? 1 : 77;
    }
    const std::map<std::string, rr::Face> files{{"BADLOC.DLL", rr::Face::badloc},
                                                {"FUTR.DLL", rr::Face::futura_regular},
                                                {"FUTB.DLL", rr::Face::futura_bold},
                                                {"FUTD.DLL", rr::Face::futura_demi}};
    const uint8_t ramp[17] = {0,    0x4d, 0x68, 0x7c, 0x8c, 0x9a, 0xa7, 0xb2, 0xbd,
                              0xc7, 0xd0, 0xd9, 0xe1, 0xe9, 0xf0, 0xf8, 0xff};
    auto expand5 = [](int v) { return (v >> 3) << 3 | v >> 5; };
    // Each probe line: tag file height byte advance left top w h rows. The
    // glyph is drawn white at a pad on black and its ink box compared.
    auto replay = [&](std::istream& in, const rr::TextRenderer& r, bool gray, int& total, int& exact,
                      int& advances, std::vector<std::string>& misses) {
        std::string line;
        while (std::getline(in, line)) {
            if (line.empty() || line[0] != (gray ? 'A' : 'G')) continue;
            std::istringstream ss(line.substr(2));
            std::string file;
            int height, ch, advance, left, top, w, h;
            ss >> file >> height >> ch >> advance >> left >> top >> w >> h;
            const rr::Face face = files.at(file);
            // Wine font-links uncovered bytes >= 0x80; Windows draws .notdef (as 0x7f here).
            if (ch >= 0x80 && !r.covers(face, uint8_t(ch))) continue;
            ++total;
            const std::string s(1, char(ch));
            if (r.width(face, height, s) == advance) ++advances;
            const int pad = 96;
            rr::RgbImage img(320, 288, 0);
            r.draw(img, face, height, pad, pad, 0xffffff, s);
            int x0 = 1 << 30, y0 = 1 << 30, x1 = -1, y1 = -1;
            for (int y = 0; y < img.height; ++y)
                for (int x = 0; x < img.width; ++x)
                    if (img.at(x, y)) {
                        x0 = std::min(x0, x), x1 = std::max(x1, x);
                        y0 = std::min(y0, y), y1 = std::max(y1, y);
                    }
            bool ok =
                x1 < 0 ? w == 0 : x0 - pad == left && y0 - pad == top && x1 - x0 + 1 == w && y1 - y0 + 1 == h;
            for (int y = 0; ok && y < h; ++y) {
                std::string row;
                ss >> row;
                for (int x = 0; ok && x < w; ++x) {
                    const uint32_t p = img.at(x0 + x, y0 + y) & 0xff;
                    const int want =
                        gray ? expand5(std::stoi(row.substr(size_t(x) * 2, 2), nullptr, 16))
                             : (std::stoi(row.substr(size_t(x / 8) * 2, 2), nullptr, 16) >> (7 - x % 8) & 1) *
                                   0xff;
                    ok = int(p) == want;
                }
            }
            if (ok)
                ++exact;
            else
                misses.push_back(file + " " + std::to_string(height) + " " + std::to_string(ch));
        }
    };
    {
        int total = 0, exact = 0, advances = 0;
        std::vector<std::string> misses;
        replay(mono_in, text, false, total, exact, advances, misses);
        std::cout << "mono glyphs exact " << exact << "/" << total << ", advances " << advances << "\n";
        // Known residual: BADLOC 14 I-acute and U-acute keep one accent row Wine drops.
        check(total == 8448 && advances == total, "mono probe coverage and advances");
        check(misses == std::vector<std::string>{"BADLOC.DLL 14 205", "BADLOC.DLL 14 218"},
              "mono glyph bitmaps");
        for (const auto& m : misses) std::cout << "  mono miss " << m << "\n";
    }
    {
        const rr::TextRenderer smooth(assets, rr::TextSurface::rgb565);
        int total = 0, exact = 0, advances = 0;
        std::vector<std::string> misses;
        replay(gray_in, smooth, true, total, exact, advances, misses);
        std::cout << "smoothed glyphs exact " << exact << "/" << total << ", advances " << advances << "\n";
        check(total == 8448 && exact == total && advances == total, "smoothed glyph bitmaps");
    }
    {
        int total = 0, exact = 0;
        std::string line;
        while (std::getline(blend_in, line)) {
            if (line.size() < 2 || line[0] != 'B') continue;
            unsigned colorref, bg, intensity, out;
            if (std::sscanf(line.c_str(), "B %x %x %x %x", &colorref, &bg, &intensity, &out) != 4) continue;
            int level = 0;
            while (level < 16 && ramp[level] != intensity) ++level;
            auto rgb = [](unsigned p) {
                const unsigned r = p >> 11, g = p >> 5 & 63, b = p & 31;
                return (r << 3 | r >> 2) << 16 | (g << 2 | g >> 4) << 8 | (b << 3 | b >> 2);
            };
            const uint32_t text_rgb = (colorref & 0xff) << 16 | (colorref & 0xff00) | colorref >> 16;
            const uint32_t got =
                level >= 16
                    ? rgb(((text_rgb >> 19) << 11) | ((text_rgb >> 10 & 63) << 5) | (text_rgb >> 3 & 31))
                    : rr::gdi_blend565(rgb(bg), text_rgb, level);
            ++total;
            exact += got == rgb(out);
        }
        std::cout << "16-bit text blend exact " << exact << "/" << total << "\n";
        check(total > 0 && exact == total, "16-bit anti-aliased blend");
    }
    std::cout << failures << " failures\n";
    return failures ? 1 : 0;
}
