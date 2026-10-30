// Plan 33 Task A8: system font discovery.
#include "system_fonts.hpp"
#include <SDL3/SDL.h>
#include <cstring>

namespace rr {
std::vector<FontCandidate> system_font_candidates(FontWeight weight) {
    std::vector<FontCandidate> out;
    const char* search = SDL_getenv("RR_FONT_SEARCH");
    const bool system = !(search && std::strcmp(search, "none") == 0);
    const bool heavy = weight != FontWeight::body;
    if (system) {
#if defined(__APPLE__)
        out.push_back({"/System/Library/Fonts/Supplemental/Futura.ttc", weight == FontWeight::title  ? 4
                                                                        : weight == FontWeight::bold ? 2
                                                                                                     : 0});
#elif defined(__ANDROID__)
        if (weight == FontWeight::title) out.push_back({"/system/fonts/Roboto-Black.ttf"});
        if (weight == FontWeight::bold) out.push_back({"/system/fonts/Roboto-Bold.ttf"});
        out.push_back({"/system/fonts/Roboto-Regular.ttf"});
#elif defined(_WIN32)
        const char* windir = SDL_getenv("WINDIR");
        const std::string fonts = std::string(windir && *windir ? windir : "C:\\Windows") + "\\Fonts\\";
        if (weight == FontWeight::title) out.push_back({fonts + "seguibl.ttf"});
        out.push_back({fonts + (heavy ? "segoeuib.ttf" : "segoeui.ttf")});
        out.push_back({fonts + (heavy ? "arialbd.ttf" : "arial.ttf")});
#else
        // Debian/Ubuntu, Arch/SteamOS, Fedora, openSUSE; then Noto, Liberation.
        const char* dejavu = heavy ? "DejaVuSans-Bold.ttf" : "DejaVuSans.ttf";
        for (const char* dir : {"/usr/share/fonts/truetype/dejavu/", "/usr/share/fonts/TTF/",
                                "/usr/share/fonts/dejavu-sans-fonts/", "/usr/share/fonts/truetype/",
                                "/usr/share/fonts/dejavu/"})
            out.push_back({std::string(dir) + dejavu});
        const char* noto = heavy ? "NotoSans-Bold.ttf" : "NotoSans-Regular.ttf";
        for (const char* dir : {"/usr/share/fonts/noto/", "/usr/share/fonts/truetype/noto/",
                                "/usr/share/fonts/google-noto/", "/usr/share/fonts/google-noto-vf/"})
            out.push_back({std::string(dir) + noto});
        const char* liberation = heavy ? "LiberationSans-Bold.ttf" : "LiberationSans-Regular.ttf";
        for (const char* dir : {"/usr/share/fonts/truetype/liberation/", "/usr/share/fonts/liberation-sans/",
                                "/usr/share/fonts/liberation/"})
            out.push_back({std::string(dir) + liberation});
#endif
    }
    if (const char* base = SDL_GetBasePath()) // packaged fallback (33a/33b packaging)
        out.push_back({std::string(base) + "fonts/" + (heavy ? "DejaVuSans-Bold.ttf" : "DejaVuSans.ttf")});
    return out;
}
} // namespace rr
