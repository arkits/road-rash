// Plan 33a Tasks 4-5: Steam Deck / gamescope detection, the in-game file
// browser's starting folders and Deck first-run defaults. Run-time checks
// only (no compile-time #ifdef for the Deck); none of this touches the
// simulation. Compiled on every desktop: the detection is false and the
// roots are the user's own folders off Linux.
#pragma once
#include "settings.hpp"
#include <functional>
#include <string>
#include <vector>

namespace rr::platform {
// Reads an environment variable; nullptr when unset. Tests inject a map.
using EnvLookup = std::function<const char*(const char*)>;
EnvLookup process_env();

// Steam sets SteamDeck=1 for games it starts on a Deck; Game Mode runs them
// in gamescope, which sets XDG_CURRENT_DESKTOP=gamescope.
bool is_steam_deck(const EnvLookup& env);
bool is_gamescope(const EnvLookup& env);

// Lists the directory names under `dir` (no recursion; empty on error).
using DirectoryLister = std::function<std::vector<std::string>(const std::string& dir)>;
std::vector<std::string> list_subdirectories(const std::string& dir);

// Folders the in-game browser offers at its top level, in this order and
// without duplicates or missing folders: $HOME, $HOME/Downloads, then each
// folder in /run/media/$USER (SteamOS SD cards and USB drives),
// /media/$USER and /mnt. macOS adds /Volumes/*; Windows uses
// %USERPROFILE%, its Downloads and every drive letter.
struct BrowseRoot {
    std::string label, path;
};
std::vector<BrowseRoot> browse_roots(const EnvLookup& env, const DirectoryLister& list_dirs,
                                     const std::function<bool(const std::string&)>& is_dir);
std::vector<BrowseRoot> browse_roots(); // the process environment and file system

// First-run defaults on a Deck or under gamescope (plan 33a Task 5 Step 4):
// fullscreen, widescreen matched to the display (16:10 on the Deck), render
// scale matched to the display (the largest 480-line multiple that fits: 1x
// on the 800-line Deck panel, 2x on a 1080p dock). Pad prompts already
// follow SDL_GetGamepadButtonLabel, so a Deck shows A/B/X/Y. Applied only
// when no settings file exists yet.
void apply_handheld_defaults(Settings&);
bool wants_handheld_defaults(const EnvLookup& env);

// SDL hints set before SDL_Init (plan 33a Task 4 Step 1, Task 5 Step 5):
// the app ID and name match the .desktop file and AppStream ID, so Wayland
// compositors and PipeWire show the right name and icon, and the Steam
// on-screen keyboard opens for text input (Steam's own environment wins).
void set_startup_hints();

// Plan 33a Task 4 Step 2: SDL keeps the screen saver off while the game runs
// (its idle inhibitor: the D-Bus ScreenSaver/portal inhibit on Linux). It
// stays off in races and videos; in menus it is allowed again after five
// minutes without input, and any input turns it off again.
class ScreenSaverPolicy {
  public:
    static constexpr uint64_t menu_idle_ms = 5 * 60 * 1000;
    // Call once per frame. Returns true when allowed() changed, so the caller
    // calls SDL_EnableScreenSaver or SDL_DisableScreenSaver.
    bool update(uint64_t now_ms, bool playing, bool input);
    bool allowed() const { return allowed_; }

  private:
    uint64_t last_input_ms_ = 0;
    bool started_ = false, allowed_ = false;
};
} // namespace rr::platform
