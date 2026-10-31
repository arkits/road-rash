// Plan 33a Tasks 4-5: Deck/gamescope detection, browser roots, handheld
// first-run defaults, the menu screen-saver rule and suspend detection.
#include "platform_linux.hpp"
#include "session.hpp"
#include <iostream>
#include <map>
#include <set>

namespace {
int failures = 0;
void check(bool ok, const char* message) {
    if (!ok) {
        ++failures;
        std::cerr << "FAIL " << message << '\n';
    }
}
rr::platform::EnvLookup env(std::map<std::string, std::string> vars) {
    return [vars = std::move(vars)](const char* name) -> const char* {
        const auto it = vars.find(name);
        return it == vars.end() ? nullptr : it->second.c_str();
    };
}
} // namespace

int main() {
    using namespace rr::platform;
    check(!is_steam_deck(env({})) && !is_gamescope(env({})) && !wants_handheld_defaults(env({})),
          "a desktop is neither");
    check(is_steam_deck(env({{"SteamDeck", "1"}})) && !is_steam_deck(env({{"SteamDeck", "0"}})),
          "Steam's SteamDeck=1 marks a Deck");
    check(is_gamescope(env({{"XDG_CURRENT_DESKTOP", "gamescope"}})) &&
              !is_gamescope(env({{"XDG_CURRENT_DESKTOP", "KDE"}})),
          "gamescope is Game Mode; Desktop Mode (KDE) is not");
    check(wants_handheld_defaults(env({{"SteamDeck", "1"}})) &&
              wants_handheld_defaults(env({{"XDG_CURRENT_DESKTOP", "gamescope"}})),
          "either one asks for handheld defaults");

    // Roots: home, Downloads, then mounted media, existing folders only, no duplicates.
    {
        const std::map<std::string, std::vector<std::string>> children{{"/run/media/deck", {"SD", "USB"}},
                                                                       {"/media/deck", {"Disc"}},
                                                                       {"/mnt", {"data"}},
                                                                       {"/Volumes", {"Disk"}}};
        const std::set<std::string> dirs{"/home/deck",         "/home/deck/Downloads",
                                         "/run/media/deck/SD", "/media/deck/Disc",
                                         "/mnt/data",          "/Volumes/Disk"};
        const auto roots = browse_roots(
            env({{"HOME", "/home/deck/"}, {"USER", "deck"}}),
            [&](const std::string& d) {
                const auto it = children.find(d);
                return it == children.end() ? std::vector<std::string>{} : it->second;
            },
            [&](const std::string& d) { return dirs.count(d) > 0; });
        std::vector<std::string> paths;
        for (const auto& r : roots) paths.push_back(r.path);
#if defined(__APPLE__)
        const std::vector<std::string> expected{"/home/deck", "/home/deck/Downloads", "/Volumes/Disk"};
#elif !defined(_WIN32)
        const std::vector<std::string> expected{"/home/deck", "/home/deck/Downloads", "/run/media/deck/SD",
                                                "/media/deck/Disc", "/mnt/data"};
#endif
#ifndef _WIN32
        check(paths == expected, "roots in order, trailing slash trimmed, missing USB folder left out");
        check(!roots.empty() && roots[0].label == "Home" && roots.back().label != "", "root labels");
#endif
        (void)browse_roots(); // the process environment and file system: must not throw
    }

    // Handheld first-run defaults.
    {
        rr::Settings s = rr::default_settings();
        apply_handheld_defaults(s);
        check(s.display_mode == rr::DisplayMode::fullscreen && s.widescreen == rr::Widescreen::match &&
                  s.render_scale == 0,
              "fullscreen, widescreen matched (16:10 on a Deck), render scale matched to the display");
        const rr::Settings round = rr::parse_settings(rr::format_settings(s));
        check(round.display_mode == s.display_mode && round.widescreen == s.widescreen &&
                  round.render_scale == s.render_scale,
              "the defaults survive the settings file");
    }

    // Screen saver: off in races and videos; allowed in menus after 5 idle minutes.
    {
        ScreenSaverPolicy saver;
        const uint64_t idle = ScreenSaverPolicy::menu_idle_ms;
        check(!saver.update(1000, false, false) && !saver.allowed(), "starts off");
        check(!saver.update(1000 + idle - 1, false, false), "still off just before five minutes");
        check(saver.update(1000 + idle, false, false) && saver.allowed(),
              "allowed after five idle menu minutes");
        check(saver.update(1000 + idle + 5, false, true) && !saver.allowed(), "any input turns it off");
        check(saver.update(2000 + 2 * idle, false, false) && saver.allowed(), "idle again");
        check(saver.update(2000 + 2 * idle + 1, true, false) && !saver.allowed(),
              "a race or video turns it off");
        check(!saver.update(2000 + 4 * idle, true, false) && !saver.allowed(),
              "and keeps it off however long");
        check(!saver.update(2000 + 4 * idle + 10, false, false),
              "back in the menu the idle time starts over");
    }

    // Suspend: a wall-clock gap over 2 s pauses (explicit resume needed).
    {
        const uint64_t ms = 1000000;
        rr::SessionState s;
        check(!s.frame(1000 * ms) && !s.paused(), "the first frame only records the time");
        check(!s.frame(1016 * ms) && !s.frame(3016 * ms) && !s.paused(), "a 2 s gap is not a suspend");
        check(s.frame(5017 * ms) && s.paused() && s.take_clock_reset(), "a gap over 2 s pauses");
        check(!s.frame(60000 * ms) && s.paused(), "a gap while paused is not another jump");
        s.resume();
        check(!s.frame(60016 * ms) && !s.paused(), "an explicit resume continues");
        check(!s.frame(60032 * ms, true) && !s.paused(), "the caller freezes (pause menu)");
        check(!s.frame(95000 * ms) && !s.paused(),
              "a gap that began while the caller was frozen is not a jump");
        check(s.frame(99000 * ms, true) && s.paused(),
              "a gap that began unfrozen is, whatever the caller does now");
    }
    std::cout << failures << " failures\n";
    return failures ? 1 : 0;
}
