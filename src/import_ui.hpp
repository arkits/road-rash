#pragma once
#include <SDL3/SDL.h>
#include <string>
namespace rr {
// Returns after verified import or window close. Screenshot mode renders one
// frame without opening a dialog, for first-run visual verification.
bool run_import_screen(SDL_Window*, SDL_Renderer*, const std::string& destination,
                       const std::string& screenshot = {}, bool smoke = false);
// Startup progress screen in the import screen's style, for loads before the
// disc's own fonts and art are decoded. Presents one frame.
void draw_loading_screen(SDL_Renderer*, const char* stage);
} // namespace rr
