#include "text.hpp"
#include "assets.hpp"
#include "pe_strings.hpp"
#include <algorithm>
#include <stdexcept>
#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_ADVANCES_H
#include FT_DRIVER_H
#include FT_MODULE_H
#include FT_OUTLINE_H
#include FT_TRUETYPE_TABLES_H

namespace rr {
// Wine-GDI renders these faces with its bundled FreeType 2.13.3; the build pins
// that release (cmake/freetype.cmake). Each face owns its library: the renderer
// caches glyphs and never shares FreeType state between instances.
struct TextRenderer::FontData {
    std::vector<uint8_t> bytes;
    FT_Library library = nullptr;
    FT_Face face = nullptr;
    int win_ascent = 0, win_descent = 0; // OS/2 usWinAscent/usWinDescent: GDI cell metrics
    int units_per_em = 0;                // head.unitsPerEm for the GDI ppem rule
    ~FontData() {
        if (face) FT_Done_Face(face);
        if (library) FT_Done_FreeType(library);
    }
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

// Opens the face. use_os2 selects OS/2 win metrics (the game faces); without
// it, or without an OS/2 table, the hhea ascender/descender stand in.
std::unique_ptr<TextRenderer::FontData> TextRenderer::open_font(std::vector<uint8_t> bytes, bool use_os2,
                                                                const std::string& what) {
    auto f = std::make_unique<TextRenderer::FontData>();
    f->bytes = std::move(bytes);
    if (FT_Init_FreeType(&f->library)) throw std::runtime_error("FreeType initialisation failed");
    // Wine's default: the v40 ("minimal") TrueType interpreter.
    FT_UInt interpreter = TT_INTERPRETER_VERSION_40;
    FT_Property_Set(f->library, "truetype", "interpreter-version", &interpreter);
    if (f->bytes.size() < 12 ||
        FT_New_Memory_Face(f->library, f->bytes.data(), FT_Long(f->bytes.size()), 0, &f->face) ||
        !FT_IS_SCALABLE(f->face))
        throw std::runtime_error("Invalid TrueType font" + what);
    const auto* os2 = static_cast<const TT_OS2*>(FT_Get_Sfnt_Table(f->face, FT_SFNT_OS2));
    if (use_os2 && os2 && os2->version != 0xffff) {
        f->win_ascent = os2->usWinAscent;
        f->win_descent = os2->usWinDescent;
    } else {
        f->win_ascent = f->face->ascender;
        f->win_descent = -f->face->descender;
    }
    f->units_per_em = f->face->units_per_EM;
    if (f->win_ascent + f->win_descent <= 0) throw std::runtime_error("Font has no vertical metrics");
    if (f->units_per_em <= 0) throw std::runtime_error("Font has no units-per-em");
    return f;
}

TextRenderer::TextRenderer(const Assets& assets, TextSurface surface) : surface_(surface) {
    // 0x0042f720 installs Badloc, Futura Bold, Futura Demi and Futura Regular from Text\.
    const std::pair<Face, const char*> files[] = {{Face::badloc, "TEXT/BADLOC.DLL"},
                                                  {Face::futura_bold, "TEXT/FUTB.DLL"},
                                                  {Face::futura_demi, "TEXT/FUTD.DLL"},
                                                  {Face::futura_regular, "TEXT/FUTR.DLL"}};
    for (const auto& [face, path] : files)
        fonts_[face] = open_font(assets.read(path), true, std::string(" ") + path);
}

TextRenderer::TextRenderer(Face face, std::vector<uint8_t> ttf) {
    fonts_[face] = open_font(std::move(ttf), false, "");
}

TextRenderer::~TextRenderer() = default;

bool TextRenderer::has(Face f) const {
    return fonts_.count(f) != 0;
}

bool TextRenderer::covers(Face f, uint8_t ch) const {
    return FT_Get_Char_Index(font(f).face, cp1252_codepoint(ch)) != 0;
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
    // Wine-GDI TextOutA glyphs (tools/glyphprobe.c, all four faces at 11 heights):
    // FreeType at the integer GDI ppem with v40 hinting; the hinted metric box
    // floored and ceiled to whole pixels; top at tmAscent - ceil(bearingY), rows
    // above the cell top dropped (accented capitals at small sizes). 1-bit
    // surfaces hint for the mono target and rasterise mono (8446/8448); 16-bit
    // ones hint for the normal target and quantise 8-bit coverage to 17 levels
    // by truncation, levels 0 and 1 drawing nothing (8448/8448).
    const bool smooth = surface_ == TextSurface::rgb565;
    const int ppem = gdi_ppem(height, f.units_per_em, f.win_ascent + f.win_descent);
    Glyph g;
    if (FT_Set_Pixel_Sizes(f.face, 0, FT_UInt(ppem))) return cache_.emplace(key, std::move(g)).first->second;
    const FT_UInt index = FT_Get_Char_Index(f.face, cp1252_codepoint(ch)); // 0: .notdef
    // Advance: design units scaled to 26.6, then rounded (8448/8448; ties near
    // .5 px go up, which the hinted advance does not reproduce).
    FT_Fixed units = 0;
    FT_Get_Advance(f.face, index, FT_LOAD_NO_SCALE, &units);
    g.advance = int((FT_MulFix(units, f.face->size->metrics.x_scale) + 32) >> 6);
    const FT_Int32 target = smooth ? FT_LOAD_TARGET_NORMAL : FT_LOAD_TARGET_MONO;
    if (FT_Load_Glyph(f.face, index, FT_LOAD_NO_BITMAP | FT_LOAD_IGNORE_GLOBAL_ADVANCE_WIDTH | target))
        return cache_.emplace(key, std::move(g)).first->second;
    FT_GlyphSlot slot = f.face->glyph;
    const FT_Glyph_Metrics& m = slot->metrics;
    const FT_Pos left = m.horiBearingX & -64, right = (m.horiBearingX + m.width + 63) & -64;
    const FT_Pos top = (m.horiBearingY + 63) & -64, bottom = (m.horiBearingY - m.height) & -64;
    const int w = int((right - left) >> 6), h = int((top - bottom) >> 6);
    const int ascent = int((FT_MulFix(f.win_ascent, f.face->size->metrics.y_scale) + 32) >> 6);
    if (w > 0 && h > 0 && slot->format == FT_GLYPH_FORMAT_OUTLINE) {
        const int pitch = smooth ? w : (w + 7) / 8;
        std::vector<uint8_t> bits(size_t(pitch) * size_t(h));
        FT_Bitmap bitmap{};
        bitmap.width = unsigned(w);
        bitmap.rows = unsigned(h);
        bitmap.pitch = pitch;
        bitmap.pixel_mode = smooth ? FT_PIXEL_MODE_GRAY : FT_PIXEL_MODE_MONO;
        bitmap.num_grays = smooth ? 256 : 2;
        bitmap.buffer = bits.data();
        FT_Outline_Translate(&slot->outline, -left, -bottom);
        FT_Outline_Get_Bitmap(f.library, &slot->outline, &bitmap);
        const int y0 = ascent - int(top >> 6);
        const int skip = std::min(h, std::max(0, -y0));
        g.x0 = int(left >> 6);
        g.y0 = y0 + skip;
        g.w = w;
        g.h = h - skip;
        g.ink.resize(size_t(g.w) * size_t(g.h));
        for (int y = 0; y < g.h; ++y) {
            const uint8_t* row = bits.data() + size_t(y + skip) * size_t(pitch);
            for (int x = 0; x < w; ++x) {
                const int level = smooth ? row[x] * 17 / 256 : (row[x / 8] >> (7 - x % 8) & 1) * 16;
                g.ink[size_t(y) * size_t(w) + size_t(x)] = uint8_t(level > 1 ? level : 0);
            }
        }
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

namespace {
// Wine's 16-bit DIB text blend (dibdrv draw_glyph_16 / aa_color / get_range),
// over a 17-step gamma ramp. Channels are 8-bit expansions of RGB565 fields;
// the result is truncated back to 565.
constexpr uint8_t aa_ramp[17] = {0,    0x4d, 0x68, 0x7c, 0x8c, 0x9a, 0xa7, 0xb2, 0xbd,
                                 0xc7, 0xd0, 0xd9, 0xe1, 0xe9, 0xf0, 0xf8, 0xff};

uint32_t expand565(uint32_t rgb) {
    const uint32_t r = rgb >> 19 & 0x1f, g = rgb >> 10 & 0x3f, b = rgb >> 3 & 0x1f;
    return (r << 3 | r >> 2) << 16 | (g << 2 | g >> 4) << 8 | (b << 3 | b >> 2);
}

int aa_channel(int dst, int text, int level) {
    if (dst == text) return dst;
    if (dst > text) {
        const int max = aa_ramp[16 - level] + (0xff - aa_ramp[16 - level]) * text / 0xff;
        return text + (dst - text) * (max - text) / (0xff - text);
    }
    const int min = aa_ramp[level] * text / aa_ramp[16];
    return text - (text - dst) * (text - min) / text;
}

} // namespace

uint32_t gdi_blend565(uint32_t dst, uint32_t text, int level) {
    dst = expand565(dst);
    text = expand565(text);
    uint32_t out = 0;
    for (int shift : {16, 8, 0})
        out |= uint32_t(aa_channel(int(dst >> shift & 0xff), int(text >> shift & 0xff), level)) << shift;
    return expand565(out);
}

void TextRenderer::draw(RgbImage& image, Face face, int height, int x, int y, uint32_t rgb,
                        const std::string& text) const {
    draw_clipped(image, nullptr, face, height, x, y, rgb, text);
}

void TextRenderer::draw_clipped(RgbImage& image, const TextRect* clip, Face face, int height, int x, int y,
                                uint32_t rgb, const std::string& text) const {
    const bool smooth = surface_ == TextSurface::rgb565;
    if (smooth) rgb = expand565(rgb);
    for (unsigned char c : text) {
        const Glyph& g = glyph(face, height, c);
        for (int gy = 0; gy < g.h; ++gy)
            for (int gx = 0; gx < g.w; ++gx) {
                const int level = g.ink[size_t(gy) * size_t(g.w) + size_t(gx)];
                if (!level) continue;
                const int px = x + g.x0 + gx, py = y + g.y0 + gy;
                if (!image.contains(px, py)) continue;
                if (clip && (px < clip->left || px >= clip->right || py < clip->top || py >= clip->bottom))
                    continue;
                uint32_t& p = image.at(px, py);
                p = level >= 16 ? rgb : gdi_blend565(p, rgb, level);
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
    // Wine user32 TEXT_NextLineW/TEXT_WordBreak: fit whole characters, then
    // break at the word boundary. When the first character that does not fit
    // starts a word, a left-aligned line keeps the space before it (it counts
    // toward CALCRECT width); otherwise the break works back to the previous
    // space and drops it. Centre/right lines never keep the space. Breaks are
    // at spaces only (ScriptBreak soft breaks in the game's strings).
    // textprobe CALCRECT: 300/296/293/300/269/156 reproduced.
    auto flush = [&](const std::string& p) {
        if (!(flags & dt::wordbreak) || (flags & dt::singleline)) {
            lines.push_back(p);
            return;
        }
        std::string rest = p;
        while (true) {
            size_t fit = 0;
            for (int w = 0; fit < rest.size(); ++fit) {
                w += glyph(face, height, uint8_t(rest[fit])).advance;
                if (w > max_width) break;
            }
            if (fit >= rest.size()) {
                lines.push_back(rest);
                return;
            }
            const bool aligned = (flags & (dt::center | dt::right)) != 0;
            size_t len, used;
            bool word_fits = fit > 0;
            long q = long(fit); // Wine's p
            if (word_fits) {
                if (rest[fit - 1] == ' ' && rest[fit] != ' ')
                    --q; // soft break: the word just fitted
                else {
                    long i = long(fit);
                    while (i > 0 && rest[size_t(--i + 1)] != ' ') --q;
                    --q;
                    word_fits = i != 0 || rest[size_t(i + 1)] == ' ';
                }
            }
            if (word_fits) {
                if (!aligned || rest[size_t(q)] != ' ') ++q;
                len = size_t(q);
                used = len + (len < rest.size() && rest[len] == ' ' ? 1 : 0);
            } else {
                // No word fits: take the whole word, discard one trailing space.
                len = rest.find(' ', fit);
                if (len == std::string::npos) len = rest.size();
                used = len + (len < rest.size() ? 1 : 0);
            }
            lines.push_back(rest.substr(0, len));
            if (used >= rest.size()) return;
            rest = rest.substr(used);
        }
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
        if (image) draw_clipped(*image, (flags & dt::noclip) ? nullptr : &rect, face, height, x, y, rgb, l);
        y += pitch;
    }
    return total;
}
} // namespace rr
