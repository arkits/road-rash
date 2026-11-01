#pragma once
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <tuple>
#include <vector>

namespace rr {
class Assets;

// 32-bit 0x00RRGGBB image used by the front end (the original menus are 15/16-bit
// DirectDraw surfaces, not the 8-bit race surface).
struct RgbImage {
    int width = 0, height = 0;
    std::vector<uint32_t> pixels;
    RgbImage() = default;
    RgbImage(int w, int h, uint32_t fill = 0) : width(w), height(h), pixels(size_t(w) * size_t(h), fill) {}
    uint32_t& at(int x, int y) { return pixels[size_t(y) * size_t(width) + size_t(x)]; }
    uint32_t at(int x, int y) const { return pixels[size_t(y) * size_t(width) + size_t(x)]; }
    bool contains(int x, int y) const { return x >= 0 && y >= 0 && x < width && y < height; }
};

struct TextRect {
    int left = 0, top = 0, right = 0, bottom = 0;
};

// Faces the original installs from TEXT/*.DLL (0x0042f720): these files are TrueType fonts.
// The last two are Windows system faces the game requests by name (0x00423b25 menu focus
// arrow and 0x00442990 dashboard gap arrow: Wingdings; 0x00423afd standings: "New Courier
// Bold", which GDI resolves to Courier New Bold). They load from the host's fonts when present
// (system_font_paths); Wingdings falls back to Wine's own LGPL Wingdings, built in.
enum class Face { badloc, futura_regular, futura_bold, futura_demi, wingdings, courier_new_bold };

// Host font files tried in order for a system face (empty for the disc faces).
std::vector<std::string> system_font_paths(Face);

// Win32 DrawText flags used by the original menus.
namespace dt {
constexpr unsigned center = 0x1, right = 0x2, vcenter = 0x4, bottom = 0x8, wordbreak = 0x10,
                   singleline = 0x20, noclip = 0x100, calcrect = 0x400;
}

// Surface the text is drawn on. Wine-GDI (DEFAULT_QUALITY, system smoothing on) draws 1-bit
// glyphs on <=8-bit DIBs (the race surface) and 17-level anti-aliased glyphs on the 16-bit
// RGB565 menu surface.
enum class TextSurface { indexed8, rgb565 };

// Wine's 16-bit DIB anti-aliased text blend of text colour over dst (0x00RRGGBB, RGB565-exact)
// at smoothing intensity 2..15; exposed for the blendprobe regression.
uint32_t gdi_blend565(uint32_t dst, uint32_t text, int level);

// Text drawing with the original's own TrueType fonts, read from the user's disc at runtime.
// Reproduces Wine-GDI's TextOut: pinned FreeType 2.13.3 at the GDI ppem, v40 hinting, whole-pixel
// advances and tmHeight line pitch. indexed8: mono-target 1-bit glyphs (glyphprobe 8446/8448).
// rgb565: normal-target 17-level glyphs (8448/8448) blended as Wine's 16-bit DIB driver does
// (blendprobe 491520/491520). Ground truth is Wine's renderer, not Windows'.
class TextRenderer {
  public:
    explicit TextRenderer(const Assets&, TextSurface = TextSurface::indexed8);
    TextRenderer(Face, std::vector<uint8_t> ttf); // single face, for tests
    // Disc faces from assets plus the system faces from these font files (tests).
    TextRenderer(const Assets&, TextSurface, const std::map<Face, std::string>& system_fonts);
    ~TextRenderer();
    TextRenderer(const TextRenderer&) = delete;
    TextRenderer& operator=(const TextRenderer&) = delete;
    bool has(Face) const;
    // Whether the face maps this Windows-1252 byte to a glyph (else .notdef).
    bool covers(Face, uint8_t ch) const;
    // Strings are Windows-1252 bytes as the game uses them. height is the GDI cell height.
    int width(Face, int height, const std::string& text) const;
    int line_height(Face, int height) const;
    void draw(RgbImage&, Face, int height, int x, int y, uint32_t rgb, const std::string& text) const;
    // DrawTextEx-like layout: newlines, word wrap, left/centre/right, vertical centre/bottom for single
    // lines, clipped to rect (ETO_CLIPPED) unless dt::noclip, and to the image. With dt::calcrect nothing is drawn and rect is resized to the text.
    int draw_box(RgbImage*, Face, int height, TextRect& rect, uint32_t rgb, const std::string& text,
                 unsigned flags) const;

  private:
    struct FontData;
    static std::unique_ptr<FontData> open_font(std::vector<uint8_t> bytes, bool use_os2,
                                               const std::string& what);
    void load_disc_faces(const Assets&);
    struct Glyph {
        int x0 = 0, y0 = 0, w = 0, h = 0, advance = 0;
        std::vector<uint8_t> ink; // smoothing intensity 0..16 (1-bit glyphs: 0 or 16)
    };
    const Glyph& glyph(Face, int height, uint8_t ch) const;
    const FontData& font(Face) const;
    void draw_clipped(RgbImage&, const TextRect* clip, Face, int height, int x, int y, uint32_t rgb,
                      const std::string& text) const;
    TextSurface surface_ = TextSurface::indexed8;
    std::map<Face, std::unique_ptr<FontData>> fonts_;
    mutable std::map<std::tuple<Face, int, uint8_t>, Glyph> cache_;
};
} // namespace rr
