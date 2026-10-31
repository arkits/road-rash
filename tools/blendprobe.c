/* GDI anti-aliased text blending ground truth on a 16-bit surface (plan 13,
 * exact menu text). Builds with i686-w64-mingw32-gcc, runs under Wine headless.
 * Draws one many-level glyph (Futura Condensed Regular 72 'W') white on black
 * into a 32-bit DIB to read each pixel's smoothing intensity, then draws it in
 * each text colour over random backgrounds on an RGB565 DIB (the menu
 * surface's format) and prints, per colour, background and intensity, the
 * resulting pixel:
 *   B <colorref> <bg565> <intensity> <out565>
 * Intensity is the 32-bit white-on-black value. */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char** argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: blendprobe.exe <TEXT-dir-with-trailing-backslash> [backgrounds]\n");
        return 1;
    }
    char path[MAX_PATH];
    snprintf(path, sizeof path, "%sFUTR.DLL", argv[1]);
    if (!AddFontResourceA(path)) {
        printf("ADDFONT %s FAILED\n", path);
        return 1;
    }
    const int count = argc > 2 ? atoi(argv[2]) : 4096;
    HDC screen = GetDC(NULL);
    HDC dc = CreateCompatibleDC(screen);
    ReleaseDC(NULL, screen);
    const int w = 96, h = 96;
    struct {
        BITMAPINFOHEADER h;
        DWORD masks[3];
    } bi;
    memset(&bi, 0, sizeof bi);
    bi.h.biSize = sizeof bi.h;
    bi.h.biWidth = w;
    bi.h.biHeight = -h;
    bi.h.biPlanes = 1;
    bi.h.biBitCount = 32;
    void* bits32 = NULL;
    HBITMAP bmp32 = CreateDIBSection(dc, (BITMAPINFO*)&bi, DIB_RGB_COLORS, &bits32, NULL, 0);
    bi.h.biBitCount = 16;
    bi.h.biCompression = BI_BITFIELDS;
    bi.masks[0] = 0xf800;
    bi.masks[1] = 0x07e0;
    bi.masks[2] = 0x001f;
    void* bits16 = NULL;
    HBITMAP bmp16 = CreateDIBSection(dc, (BITMAPINFO*)&bi, DIB_RGB_COLORS, &bits16, NULL, 0);
    HFONT font = CreateFontA(72, 0, 0, 0, 0, 0, 0, 0, 0, OUT_TT_ONLY_PRECIS, 0, DEFAULT_QUALITY, 0,
                             "Futura Condensed Regular");
    SelectObject(dc, font);
    SetBkMode(dc, TRANSPARENT);
    SetTextAlign(dc, TA_TOP | TA_LEFT | TA_NOUPDATECP);
    HGDIOBJ old = SelectObject(dc, bmp32);
    memset(bits32, 0, (size_t)w * h * 4);
    SetTextColor(dc, RGB(255, 255, 255));
    TextOutA(dc, 8, 8, "W", 1);
    GdiFlush();
    // One sample pixel per distinct intensity.
    int sample[256];
    for (int i = 0; i < 256; ++i) sample[i] = -1;
    const unsigned char* p32 = bits32;
    for (int i = 0; i < w * h; ++i)
        if (p32[i * 4] && sample[p32[i * 4]] < 0) sample[p32[i * 4]] = i;
    SelectObject(dc, old);
    // Menu colours as the game passes them (0x0041fc90), shadow black, white, and probes.
    const COLORREF colours[] = {RGB(0xde, 0xff, 0xf0), RGB(0xa0, 0xa0, 0xa0), RGB(0xff, 0x64, 0x32),
                                RGB(0xe1, 0xe1, 0x80), RGB(0, 0, 0),          RGB(0xff, 0xff, 0xff),
                                RGB(0x12, 0x34, 0x56), RGB(0x80, 0x40, 0xc0)};
    old = SelectObject(dc, bmp16);
    unsigned seed = 12345;
    unsigned short* p16 = bits16;
    for (unsigned c = 0; c < sizeof colours / sizeof colours[0]; ++c) {
        SetTextColor(dc, colours[c]);
        for (int n = 0; n < count; ++n) {
            seed = seed * 1103515245u + 12345u;
            const unsigned short bg = (unsigned short)(n < 65536 && count >= 65536 ? n : seed >> 16);
            for (int i = 0; i < w * h; ++i) p16[i] = bg;
            TextOutA(dc, 8, 8, "W", 1);
            GdiFlush();
            for (int v = 1; v < 256; ++v)
                if (sample[v] >= 0)
                    printf("B %06lx %04x %02x %04x\n", (unsigned long)colours[c], bg, v, p16[sample[v]]);
        }
    }
    SelectObject(dc, old);
    printf("DONE\n");
    return 0;
}
