// Plan 33a Tasks 4-5: see platform_linux.hpp.
#include "platform_linux.hpp"
#include "utf8_path.hpp"
#include <SDL3/SDL.h>
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <set>
#include <system_error>

// SDL 3.4.12+ (the pinned 3.4.18 has it); a development build against an
// older system SDL sets the same hint name, which that SDL ignores.
#ifndef SDL_HINT_ENABLE_STEAM_SCREEN_KEYBOARD
#define SDL_HINT_ENABLE_STEAM_SCREEN_KEYBOARD "SDL_ENABLE_STEAM_SCREEN_KEYBOARD"
#endif

namespace rr::platform {
EnvLookup process_env() {
    return [](const char* name) -> const char* { return std::getenv(name); };
}

bool is_steam_deck(const EnvLookup& env) {
    const char* deck = env("SteamDeck");
    return deck && std::strcmp(deck, "1") == 0;
}

bool is_gamescope(const EnvLookup& env) {
    const char* desktop = env("XDG_CURRENT_DESKTOP");
    return desktop && std::strcmp(desktop, "gamescope") == 0;
}

bool wants_handheld_defaults(const EnvLookup& env) {
    return is_steam_deck(env) || is_gamescope(env);
}

std::vector<std::string> list_subdirectories(const std::string& dir) {
    std::vector<std::string> names;
    std::error_code ec;
    for (std::filesystem::directory_iterator it(path_from_utf8(dir), ec), end; !ec && it != end;
         it.increment(ec)) {
        std::error_code type_ec;
        if (it->is_directory(type_ec)) names.push_back(path_to_utf8(it->path().filename()));
    }
    std::sort(names.begin(), names.end()); // total-order: distinct names in one directory
    return names;
}

std::vector<BrowseRoot> browse_roots(const EnvLookup& env, const DirectoryLister& list_dirs,
                                     const std::function<bool(const std::string&)>& is_dir) {
    std::vector<BrowseRoot> roots;
    std::set<std::string> seen;
    const auto trimmed = [](std::string path) {
        while (path.size() > 1 && (path.back() == '/' || path.back() == '\\') &&
               !(path.size() == 3 && path[1] == ':')) // keep "C:\"
            path.pop_back();
        return path;
    };
    auto add = [&](const std::string& label, const std::string& raw) {
        const std::string path = trimmed(raw);
        if (path.empty() || !seen.insert(path).second || !is_dir(path)) return;
        roots.push_back({label, path});
    };
    auto add_children = [&](const std::string& parent, const char* sep) {
        for (const auto& name : list_dirs(parent)) add(name, parent + sep + name);
    };
#ifdef _WIN32
    if (const char* profile = env("USERPROFILE"); profile && *profile) {
        add("Home", profile);
        add("Downloads", trimmed(profile) + "\\Downloads");
    }
    for (char drive = 'A'; drive <= 'Z'; ++drive)
        add(std::string(1, drive) + ":", std::string(1, drive) + ":\\");
#else
    const char* home = env("HOME");
    if (home && *home) {
        add("Home", home);
        add("Downloads", trimmed(home) + "/Downloads");
    }
#ifdef __APPLE__
    add_children("/Volumes", "/");
#else
    const char* user = env("USER");
    if (user && *user) {
        add_children(std::string("/run/media/") + user, "/");
        add_children(std::string("/media/") + user, "/");
    }
    add_children("/mnt", "/");
#endif
#endif
    return roots;
}

std::vector<BrowseRoot> browse_roots() {
    return browse_roots(process_env(), list_subdirectories, [](const std::string& path) {
        std::error_code ec;
        return std::filesystem::is_directory(path_from_utf8(path), ec);
    });
}

void apply_handheld_defaults(Settings& s) {
    s.display_mode = DisplayMode::fullscreen;
    s.widescreen = Widescreen::match;
    s.render_scale = 0; // match display: the largest 480-line multiple that fits (1x on an 800-line Deck)
}

void set_startup_hints() {
    SDL_SetHint(SDL_HINT_APP_ID, "org.roadrash.nativeport");
    SDL_SetHint(SDL_HINT_APP_NAME, "Road Rash Native");
    // Normal priority: Steam's own SDL_ENABLE_STEAM_SCREEN_KEYBOARD
    // environment variable (Big Picture) still decides when it is set.
    SDL_SetHint(SDL_HINT_ENABLE_STEAM_SCREEN_KEYBOARD, "1");
}

bool ScreenSaverPolicy::update(uint64_t now_ms, bool playing, bool input) {
    if (!started_ || input || playing) {
        started_ = true;
        last_input_ms_ = now_ms;
    }
    const bool allowed = !playing && now_ms - last_input_ms_ >= menu_idle_ms;
    if (allowed == allowed_) return false;
    allowed_ = allowed;
    return true;
}
} // namespace rr::platform
