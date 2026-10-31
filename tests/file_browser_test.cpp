// Plan 33a Task 5: in-game browser, disc discovery, import-screen navigation.
// Synthetic directory trees and SDL events; no display or disc needed.
#include "file_browser.hpp"
#include "input.hpp"
#include <iostream>
#include <map>

namespace {
int failures = 0;
void check(bool ok, const char* message) {
    if (!ok) {
        ++failures;
        std::cerr << "FAIL " << message << '\n';
    }
}

// A fake file system: directory path -> entries.
struct Tree {
    std::map<std::string, std::vector<rr::DirEntry>> dirs;
    int reads = 0;
    rr::DirReader reader() {
        return [this](const std::string& dir) -> std::optional<std::vector<rr::DirEntry>> {
            ++reads;
            const auto it = dirs.find(dir);
            if (it == dirs.end()) return std::nullopt;
            return it->second;
        };
    }
};
std::string p(std::initializer_list<const char*> parts) {
    std::string out;
    for (const char* part : parts) out = out.empty() ? part : rr::join_path(out, part);
    return out;
}
rr::DirEntry dir(const char* name) {
    return {name, true, 0};
}
rr::DirEntry file(const char* name, uint64_t size = 1) {
    return {name, false, size};
}

SDL_Event key(SDL_Keycode k, bool repeat = false, SDL_Keymod mod = SDL_KMOD_NONE) {
    SDL_Event e{};
    e.type = SDL_EVENT_KEY_DOWN;
    e.key.key = k;
    e.key.repeat = repeat;
    e.key.mod = mod;
    return e;
}
SDL_Event button(Uint8 b, bool down = true) {
    SDL_Event e{};
    e.type = down ? SDL_EVENT_GAMEPAD_BUTTON_DOWN : SDL_EVENT_GAMEPAD_BUTTON_UP;
    e.gbutton.button = b;
    e.gbutton.down = down;
    return e;
}
SDL_Event axis(Uint8 a, Sint16 v) {
    SDL_Event e{};
    e.type = SDL_EVENT_GAMEPAD_AXIS_MOTION;
    e.gaxis.axis = a;
    e.gaxis.value = v;
    return e;
}
} // namespace

int main() {
    const std::string home = p({"/", "home", "deck"}), card = p({"/", "run", "media", "deck", "SD"});
    Tree t;
    t.dirs[home] = {dir("Downloads"), dir(".cache"),     dir("games"), file("b.ISO"),
                    file("a.iso"),    file("notes.txt"), dir("Zeta"),  dir("alpha")};
    t.dirs[p({home.c_str(), "games"})] = {dir("roadrash")};
    t.dirs[p({home.c_str(), "games", "roadrash"})] = {file("roadrash.exe"), dir("data")};
    t.dirs[p({home.c_str(), "Downloads"})] = {file("RoadRash.iso", 650ull << 20), file("tiny.iso", 1 << 20)};
    t.dirs[card] = {dir("ROADRASH"), file("big.iso", 900ull << 20)};
    t.dirs[p({card.c_str(), "ROADRASH"})] = {file("ROADRASH.EXE"), dir("DATA")};
    const std::vector<rr::platform::BrowseRoot> roots{{"Home", home}, {"SD", card}};

    // Listing: "..", disc-folder entry, folders, then ISOs; case-insensitive
    // with exact bytes as the tie-break; hidden and other files left out.
    {
        rr::FileBrowser b(roots, t.reader());
        check(b.items().size() == 2 && b.items()[0].kind == rr::FileBrowser::Kind::root &&
                  b.directory().empty(),
              "starts at the root list");
        check(b.open(home), "opens a folder");
        std::vector<std::string> labels;
        for (const auto& item : b.items()) labels.push_back(item.label);
        const std::vector<std::string> expected{"..",    "alpha/", "Downloads/", "games/",
                                                "Zeta/", "a.iso",  "b.ISO"};
        check(labels == expected, "folders then ISOs, case-insensitive order, no hidden or other files");
        check(!b.open(p({home.c_str(), "missing"})) && !b.error().empty() && b.directory() == home,
              "an unreadable folder keeps the listing and reports an error");
        std::string chosen;
        b.set_focus(3); // games/
        check(b.activate(chosen) == rr::FileBrowser::Result::none &&
                  b.directory() == p({home.c_str(), "games"}),
              "activating a folder opens it");
        check(b.items().size() == 3 && b.items()[1].kind == rr::FileBrowser::Kind::use_folder,
              "a folder holding a roadrash/ folder (any case) offers itself");
        b.set_focus(2); // roadrash/
        b.activate(chosen);
        check(b.items().size() == 3 && b.items()[1].kind == rr::FileBrowser::Kind::use_folder,
              "a folder holding roadrash.exe (any case) offers itself");
        b.set_focus(1);
        check(b.activate(chosen) == rr::FileBrowser::Result::chose &&
                  chosen == p({home.c_str(), "games", "roadrash"}),
              "use this folder chooses it");
        check(b.back() == rr::FileBrowser::Result::none && b.directory() == p({home.c_str(), "games"}),
              "back goes up a folder");
        b.back();
        check(b.directory() == home, "back reaches the root folder");
        check(b.back() == rr::FileBrowser::Result::none && b.directory().empty(),
              "back from a root shows the roots");
        check(b.back() == rr::FileBrowser::Result::closed, "back from the root list closes");
        b.open(card);
        check(b.items()[1].kind == rr::FileBrowser::Kind::use_folder,
              "a folder holding ROADRASH/ offers itself");
        b.move(100);
        check(b.focus() == int(b.items().size()) - 1, "focus clamps at the end");
        b.move(-100);
        check(b.focus() == 0, "focus clamps at the start");
    }

    // Discovery: ROADRASH folders and 400-800 MB ISOs, two levels deep, in
    // root order, stopping on request.
    {
        auto found = rr::discover_discs(roots, t.reader(), [] { return false; });
        std::vector<std::string> paths;
        for (const auto& c : found) paths.push_back(c.path);
        const std::vector<std::string> expected{p({home.c_str(), "Downloads", "RoadRash.iso"}),
                                                p({home.c_str(), "games", "roadrash"}),
                                                p({card.c_str(), "ROADRASH"})};
        check(paths == expected, "finds the ISO and both disc folders, skipping too-small and too-big ISOs");
        check(found.size() == 3 && found[0].iso && !found[1].iso, "candidate kinds");
        Tree deep;
        deep.dirs["/r"] = {dir("a")};
        deep.dirs[p({"/r", "a"})] = {dir("b")};
        deep.dirs[p({"/r", "a", "b"})] = {dir("c")};
        deep.dirs[p({"/r", "a", "b", "c"})] = {dir("ROADRASH")};
        deep.dirs[p({"/r", "a", "b", "c", "ROADRASH"})] = {file("ROADRASH.EXE")};
        check(rr::discover_discs({{"r", "/r"}}, deep.reader(), [] { return false; }).empty(),
              "three levels down is out of reach");
        int calls = 0;
        t.reads = 0;
        rr::discover_discs(roots, t.reader(), [&] { return ++calls > 1; });
        check(t.reads == 1, "a stop request ends the scan");
    }

    // Navigation input: keys, D-pad repeat, stick hysteresis.
    {
        rr::NavInput nav;
        check(nav.event(key(SDLK_DOWN), 0) == rr::Nav::down &&
                  nav.event(key(SDLK_DOWN, true), 0) == rr::Nav::down,
              "arrow keys move, with key repeat");
        check(nav.event(key(SDLK_RETURN, true), 0) == rr::Nav::none,
              "a repeated Enter does not activate twice");
        check(nav.event(key(SDLK_TAB, false, SDL_KMOD_LSHIFT), 0) == rr::Nav::up, "Shift+Tab goes up");
        check(nav.event(key(SDLK_ESCAPE), 0) == rr::Nav::back && !nav.gamepad_last(), "Escape goes back");
        check(nav.event(button(SDL_GAMEPAD_BUTTON_DPAD_DOWN), 1000) == rr::Nav::down && nav.gamepad_last(),
              "D-pad moves and marks the pad as the last device");
        check(nav.tick(1399) == rr::Nav::none && nav.tick(1400) == rr::Nav::down &&
                  nav.tick(1509) == rr::Nav::none && nav.tick(1510) == rr::Nav::down,
              "a held D-pad repeats after 400 ms, then every 110 ms");
        nav.event(button(SDL_GAMEPAD_BUTTON_DPAD_DOWN, false), 1520);
        check(nav.tick(5000) == rr::Nav::none, "releasing the D-pad stops the repeat");
        check(nav.event(button(SDL_GAMEPAD_BUTTON_SOUTH), 0) == rr::Nav::activate &&
                  nav.event(button(SDL_GAMEPAD_BUTTON_EAST), 0) == rr::Nav::back &&
                  nav.event(button(SDL_GAMEPAD_BUTTON_NORTH), 0) == rr::Nav::alternate &&
                  nav.event(button(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER), 0) == rr::Nav::page_down,
              "A activates, B backs, Y alternates, RB pages");
        check(nav.event(axis(SDL_GAMEPAD_AXIS_LEFTY, -20000), 2000) == rr::Nav::up,
              "stick up past the threshold");
        check(nav.event(axis(SDL_GAMEPAD_AXIS_LEFTY, -12000), 2010) == rr::Nav::none,
              "inside the hysteresis band nothing changes");
        check(nav.tick(2400) == rr::Nav::up, "a held stick repeats");
        nav.event(axis(SDL_GAMEPAD_AXIS_LEFTY, -3000), 2450);
        check(nav.tick(9000) == rr::Nav::none, "centring the stick stops the repeat");
        check(nav.event(axis(SDL_GAMEPAD_AXIS_LEFTY, -20000), 9100) == rr::Nav::up, "and it can fire again");
        nav.event(key(SDLK_A), 9200);
        check(!nav.gamepad_last(), "a key press makes the keyboard the last device");
    }

    // Import screen focus order and activation (finding F11).
    {
        rr::ImportMenu menu(roots, t.reader(), true);
        check(menu.focus() == 0 && menu.view() == rr::ImportMenu::View::main, "focus starts on Choose ISO");
        check(menu.nav(rr::Nav::activate).kind == rr::ImportMenu::Action::Kind::iso_dialog,
              "A on Choose ISO");
        menu.nav(rr::Nav::right);
        check(menu.focus() == 1 &&
                  menu.nav(rr::Nav::activate).kind == rr::ImportMenu::Action::Kind::folder_dialog,
              "right, then A on Choose folder");
        menu.nav(rr::Nav::right);
        menu.nav(rr::Nav::right);
        check(menu.focus() == 2, "focus stops at Browse");
        menu.nav(rr::Nav::down);
        check(menu.focus() == 2, "no rows below without discovered discs");
        menu.set_candidates({{"one", "/one", true}, {"two", "/two", false}});
        menu.nav(rr::Nav::down);
        menu.nav(rr::Nav::down);
        check(menu.focus() == 4, "down walks the discovered discs");
        const auto chosen = menu.nav(rr::Nav::activate);
        check(chosen.kind == rr::ImportMenu::Action::Kind::import_path && chosen.path == "/two",
              "A on a discovered disc imports it");
        menu.nav(rr::Nav::up);
        menu.nav(rr::Nav::up);
        check(menu.focus() == 2, "up from the first disc returns to the last button column");
        menu.nav(rr::Nav::activate);
        check(menu.view() == rr::ImportMenu::View::browser && menu.browser().directory().empty(),
              "Browse opens the browser at the root list");
        menu.nav(rr::Nav::activate); // Home
        menu.nav(rr::Nav::page_down);
        check(menu.browser().focus() == int(menu.browser().items().size()) - 1, "page down moves by a page");
        const auto iso = menu.nav(rr::Nav::activate);
        check(iso.kind == rr::ImportMenu::Action::Kind::import_path &&
                  iso.path == p({home.c_str(), "b.ISO"}) && menu.view() == rr::ImportMenu::View::main,
              "choosing an ISO imports it and closes the browser");
        menu.open_browser();
        menu.nav(rr::Nav::back);
        check(menu.view() == rr::ImportMenu::View::main && menu.focus() == 2,
              "B at the root list closes the browser onto Browse");
        rr::ImportMenu phone({}, t.reader(), false);
        phone.nav(rr::Nav::right);
        check(phone.button_count() == 1 && phone.focus() == 0 &&
                  phone.press(rr::ImportMenu::Button::browse).kind == rr::ImportMenu::Action::Kind::none &&
                  phone.view() == rr::ImportMenu::View::main,
              "without folder access only Choose ISO exists");
    }

    // A hot-plugged pad (SDL virtual gamepad) drives the import screen.
    if (SDL_Init(SDL_INIT_GAMEPAD)) {
        SDL_VirtualJoystickDesc desc;
        SDL_INIT_INTERFACE(&desc);
        desc.type = SDL_JOYSTICK_TYPE_GAMEPAD;
        desc.naxes = SDL_GAMEPAD_AXIS_COUNT;
        desc.nbuttons = SDL_GAMEPAD_BUTTON_COUNT;
        desc.name = "rr virtual pad";
        rr::GamepadPtr pad;
        rr::NavInput nav;
        rr::ImportMenu menu(roots, t.reader(), true);
        rr::ImportMenu::Action last;
        const auto pump = [&] {
            SDL_Event e;
            while (SDL_PollEvent(&e)) {
                if (rr::track_gamepad(pad, e)) nav.release();
                if (const rr::Nav n = nav.event(e, 0); n != rr::Nav::none) last = menu.nav(n);
            }
        };
        const SDL_JoystickID id = SDL_AttachVirtualJoystick(&desc);
        pump();
        check(id && pad, "a pad plugged in on the import screen opens");
        SDL_Joystick* joy = SDL_GetJoystickFromID(id);
        const auto press = [&](SDL_GamepadButton b) {
            SDL_SetJoystickVirtualButton(joy, b, true);
            SDL_UpdateJoysticks();
            pump();
            SDL_SetJoystickVirtualButton(joy, b, false);
            SDL_UpdateJoysticks();
            pump();
        };
        press(SDL_GAMEPAD_BUTTON_DPAD_RIGHT);
        press(SDL_GAMEPAD_BUTTON_DPAD_RIGHT);
        check(menu.focus() == 2, "the virtual D-pad moves focus to Browse");
        press(SDL_GAMEPAD_BUTTON_SOUTH);
        check(menu.view() == rr::ImportMenu::View::browser, "the virtual A opens the browser");
        press(SDL_GAMEPAD_BUTTON_EAST);
        check(menu.view() == rr::ImportMenu::View::main, "the virtual B closes it");
        SDL_DetachVirtualJoystick(id);
        pump();
        check(!pad, "unplugging releases the pad");
        SDL_Quit();
    } else {
        check(false, SDL_GetError());
    }
    std::cout << failures << " failures\n";
    return failures ? 1 : 0;
}
