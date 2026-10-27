#include "text.hpp"
#include "assets.hpp"
#include "pe_strings.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>
#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#include "stb_truetype.h"

namespace rr {
struct TextRenderer::FontData {
    std::vector<uint8_t> bytes;
    stbtt_fontinfo info{};
    int win_ascent = 0, win_descent = 0; // OS/2 usWinAscent/usWinDescent: GDI cell metrics
    int units_per_em = 0;                // head.unitsPerEm for the GDI ppem rule
};

// GDI pixel size for a positive lfHeight cell (0x0042f720 table): ppem rounds
// H*upm/cell, then steps down once when the rounded scaled cell would exceed
// H. Probed under Wine-GDI (textprobe sweeps, heights 8..80, all four faces:
// 260/292 match plain rounding; the 32 outliers all take exactly one less).
int gdi_ppem(int height, int upm, int cell) {
    int p = (height * upm + cell / 2) / cell;
    if ((cell * p + upm / 2) / upm > height) --p;
    return p < 1 ? 1 : p;
}

TextRenderer::TextRenderer(const Assets& assets) {
    // 0x0042f720 installs Badloc, Futura Bold, Futura Demi and Futura Regular from Text\.
    const std::pair<Face, const char*> files[] = {{Face::badloc, "TEXT/BADLOC.DLL"},
                                                  {Face::futura_bold, "TEXT/FUTB.DLL"},
                                                  {Face::futura_demi, "TEXT/FUTD.DLL"},
                                                  {Face::futura_regular, "TEXT/FUTR.DLL"}};
    for (const auto& [face, path] : files) {
        auto f = std::make_unique<FontData>();
        f->bytes = assets.read(path);
        if (!stbtt_InitFont(&f->info, f->bytes.data(), stbtt_GetFontOffsetForIndex(f->bytes.data(), 0)))
            throw std::runtime_error(std::string("Invalid TrueType font ") + path);
        fonts_[face] = std::move(f);
    }
    for (auto& [face, f] : fonts_) {
        (void)face;
        const uint32_t os2 = stbtt__find_table(f->info.data, uint32_t(f->info.fontstart), "OS/2");
        if (os2 && os2 + 78 <= f->bytes.size()) {
            f->win_ascent = ttUSHORT(f->info.data + os2 + 74);
            f->win_descent = ttUSHORT(f->info.data + os2 + 76);
        } else {
            int a, d, g;
            stbtt_GetFontVMetrics(&f->info, &a, &d, &g);
            f->win_ascent = a;
            f->win_descent = -d;
        }
        const uint32_t head = stbtt__find_table(f->info.data, uint32_t(f->info.fontstart), "head");
        if (head && head + 20 <= f->bytes.size()) f->units_per_em = ttUSHORT(f->info.data + head + 18);
        if (f->win_ascent + f->win_descent <= 0) throw std::runtime_error("Font has no vertical metrics");
        if (f->units_per_em <= 0) throw std::runtime_error("Font has no units-per-em");
    }
}

TextRenderer::TextRenderer(Face face, std::vector<uint8_t> ttf) {
    auto f = std::make_unique<FontData>();
    f->bytes = std::move(ttf);
    if (f->bytes.size() < 12 || stbtt_GetFontOffsetForIndex(f->bytes.data(), 0) < 0 ||
        !stbtt_InitFont(&f->info, f->bytes.data(), stbtt_GetFontOffsetForIndex(f->bytes.data(), 0)))
        throw std::runtime_error("Invalid TrueType font");
    int a, d, g;
    stbtt_GetFontVMetrics(&f->info, &a, &d, &g);
    f->win_ascent = a;
    f->win_descent = -d;
    const uint32_t head = stbtt__find_table(f->info.data, uint32_t(f->info.fontstart), "head");
    if (head && head + 20 <= f->bytes.size()) f->units_per_em = ttUSHORT(f->info.data + head + 18);
    if (f->units_per_em <= 0) throw std::runtime_error("Font has no units-per-em");
    fonts_[face] = std::move(f);
}

TextRenderer::~TextRenderer() = default;

bool TextRenderer::has(Face f) const {
    return fonts_.count(f) != 0;
}

const TextRenderer::FontData& TextRenderer::font(Face f) const {
    const auto it = fonts_.find(f);
    if (it == fonts_.end()) throw std::runtime_error("Font face not loaded");
    return *it->second;
}

const TextRenderer::Glyph& TextRenderer::glyph(Face face, int height, uint8_t ch) const {
    const auto key = std::make_tuple(face, height, ch);
    const auto it = cache_.find(key);
    if (it != cache_.end()) return it->second;
    const FontData& f = font(face);
    const float scale = float(height) / float(f.win_ascent + f.win_descent);
    // GDI advances: design advance rounded at the ppem scale (textprobe ABC
    // dumps match 380/380 ASCII advances; the bitmap stays unhinted stb).
    const int ppem = gdi_ppem(height, f.units_per_em, f.win_ascent + f.win_descent);
    const int codepoint = int(cp1252_codepoint(ch));
    Glyph g;
    int advance = 0, bearing = 0;
    stbtt_GetCodepointHMetrics(&f.info, codepoint, &advance, &bearing);
    g.advance = (advance * ppem + f.units_per_em / 2) / f.units_per_em;
    int w = 0, h = 0, xo = 0, yo = 0;
    unsigned char* bitmap = stbtt_GetCodepointBitmap(&f.info, scale, scale, codepoint, &w, &h, &xo, &yo);
    if (bitmap) {
        g.w = w;
        g.h = h;
        g.x0 = xo;
        g.y0 = int(std::lround(f.win_ascent * scale)) + yo;
        g.ink.resize(size_t(w) * size_t(h));
        for (size_t i = 0; i < g.ink.size(); ++i) g.ink[i] = bitmap[i] >= 128;
        stbtt_FreeBitmap(bitmap, nullptr);
    }
    return cache_.emplace(key, std::move(g)).first->second;
}

int TextRenderer::width(Face face, int height, const std::string& text) const {
    int w = 0;
    for (unsigned char c : text) w += glyph(face, height, c).advance;
    return w;
}

int TextRenderer::line_height(Face face, int height) const {
    // GDI multiline pitch: tmAscent + tmDescent at the ppem scale (textprobe
    // TEXTMETRIC sweeps; e.g. 32px Futura Regular advances 33, not 32).
    const FontData& f = font(face);
    const int ppem = gdi_ppem(height, f.units_per_em, f.win_ascent + f.win_descent);
    return (f.win_ascent * ppem + f.units_per_em / 2) / f.units_per_em +
           (f.win_descent * ppem + f.units_per_em / 2) / f.units_per_em;
}

void TextRenderer::draw(RgbImage& image, Face face, int height, int x, int y, uint32_t rgb,
                        const std::string& text) const {
    for (unsigned char c : text) {
        const Glyph& g = glyph(face, height, c);
        for (int gy = 0; gy < g.h; ++gy)
            for (int gx = 0; gx < g.w; ++gx) {
                if (!g.ink[size_t(gy) * size_t(g.w) + size_t(gx)]) continue;
                const int px = x + g.x0 + gx, py = y + g.y0 + gy;
                if (image.contains(px, py)) image.at(px, py) = rgb;
            }
        x += g.advance;
    }
}

int TextRenderer::draw_box(RgbImage* image, Face face, int height, TextRect& rect, uint32_t rgb,
                           const std::string& text, unsigned flags) const {
    // Split into lines: hard breaks on '\n' ("\r" ignored); word wrap at spaces when requested.
    std::vector<std::string> lines;
    const int max_width = rect.right - rect.left;
    std::string paragraph;
    auto flush = [&](const std::string& p) {
        if (!(flags & dt::wordbreak) || (flags & dt::singleline)) {
            lines.push_back(p);
            return;
        }
        std::string line;
        size_t i = 0;
        while (i <= p.size()) {
            size_t j = p.find(' ', i);
            if (j == std::string::npos) j = p.size();
            const std::string word = p.substr(i, j - i);
            const std::string candidate = line.empty() ? word : line + " " + word;
            if (!line.empty() && width(face, height, candidate) > max_width) {
                lines.push_back(line);
                line = word;
            } else
                line = candidate;
            i = j + 1;
        }
        lines.push_back(line);
    };
    for (char c : text) {
        if (c == '\r') continue;
        if (c == '\n' && !(flags & dt::singleline)) {
            flush(paragraph);
            paragraph.clear();
        } else
            paragraph.push_back(c == '\n' ? ' ' : c);
    }
    flush(paragraph);
    const int pitch = line_height(face, height);
    const int total = int(lines.size()) * pitch;
    if (flags & dt::calcrect) {
        int w = 0;
        for (const auto& l : lines) w = std::max(w, width(face, height, l));
        // GDI returns the measured width even under maxw (textprobe
        // CALCRECT rights 296/293/269/156 vs input 300); the old
        // wordbreak carve-out pinned boxes to maxw and shifted
        // right-aligned text.
        if (max_width > 0) rect.right = rect.left + w;
        rect.bottom = rect.top + total;
        return total;
    }
    int y = rect.top;
    if (flags & dt::singleline) {
        if (flags & dt::vcenter)
            y = rect.top + (rect.bottom - rect.top - height) / 2;
        else if (flags & dt::bottom)
            y = rect.bottom - height;
    }
    for (const auto& l : lines) {
        const int w = width(face, height, l);
        int x = rect.left;
        if (flags & dt::center)
            x = rect.left + (max_width - w) / 2;
        else if (flags & dt::right)
            x = rect.right - w;
        if (image) draw(*image, face, height, x, y, rgb, l);
        y += pitch;
    }
    return total;
}
} // namespace rr
