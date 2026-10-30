// Plan 33 Task A8: system font discovery for the import and loading screens
// (the disc's own fonts are unavailable before import). Candidates are tried
// in order; the last ones are DejaVu files that Linux/Windows packages place
// in fonts/ next to the executable, so a desktop package never falls back to
// SDL's debug font. RR_FONT_SEARCH=none skips the system locations (tests).
#pragma once
#include <string>
#include <vector>

namespace rr {
enum class FontWeight { title, bold, body };
struct FontCandidate {
    std::string path; // UTF-8
    int index = 0;    // face index inside a .ttc
};
std::vector<FontCandidate> system_font_candidates(FontWeight weight);
} // namespace rr
