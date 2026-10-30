// Plan 33 Task A8: font candidates always end with the packaged DejaVu
// fallback next to the executable, and RR_FONT_SEARCH=none leaves only it.
#include "check.hpp"
#include "system_fonts.hpp"
#include <SDL3/SDL.h>

int main() {
    rr::test::Checks check;
    for (auto weight : {rr::FontWeight::title, rr::FontWeight::bold, rr::FontWeight::body}) {
        const auto all = rr::system_font_candidates(weight);
        check(!all.empty() && all.back().path.find("fonts/DejaVuSans") != std::string::npos,
              "packaged DejaVu fallback is the last candidate");
#if !defined(__APPLE__) && !defined(__ANDROID__) && !defined(_WIN32)
        bool dejavu = false, noto = false;
        for (const auto& c : all) {
            dejavu |= c.path.rfind("/usr/share/fonts/TTF/", 0) == 0; // Arch/SteamOS
            noto |= c.path.find("NotoSans") != std::string::npos;
        }
        check(dejavu && noto, "Linux searches the Arch/SteamOS DejaVu and Noto locations");
#endif
    }
#if defined(_WIN32)
    // Plan 33b Task 3 Step 5: the import screen's face is Segoe UI from %WINDIR%\Fonts.
    {
        const auto body = rr::system_font_candidates(rr::FontWeight::body);
        size_t size = 0;
        void* data = SDL_LoadFile(body.front().path.c_str(), &size);
        check(data && size > 0 && body.front().path.find("segoeui.ttf") != std::string::npos,
              "Windows loads Segoe UI for the import screen");
        SDL_free(data);
    }
#endif
    SDL_setenv_unsafe("RR_FONT_SEARCH", "none", 1);
    const auto packaged = rr::system_font_candidates(rr::FontWeight::bold);
    check(packaged.size() == 1 && packaged[0].path.find("DejaVuSans-Bold.ttf") != std::string::npos,
          "RR_FONT_SEARCH=none leaves only the packaged face");
    return check.finish("system_fonts");
}
