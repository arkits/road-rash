// Plan 33a Task 5: see file_browser.hpp.
#include "file_browser.hpp"
#include "utf8_path.hpp"
#include <algorithm>
#include <filesystem>
#include <set>
#include <system_error>
#include <tuple>

namespace rr {
namespace {
char lower(char c) {
    return c >= 'A' && c <= 'Z' ? char(c - 'A' + 'a') : c;
}
std::string folded(const std::string& s) {
    std::string out(s);
    std::transform(out.begin(), out.end(), out.begin(), lower);
    return out;
}
bool ends_with_iso(const std::string& name) {
    return name.size() > 4 && iequals(name.substr(name.size() - 4), ".iso");
}
bool hidden(const std::string& name) {
    return !name.empty() && name[0] == '.';
}
// Case-insensitive name order with the exact bytes as the tie-break.
bool name_less(const std::string& a, const std::string& b) {
    return std::forward_as_tuple(folded(a), a) < std::forward_as_tuple(folded(b), b);
}
const DirEntry* find_entry(const std::vector<DirEntry>& entries, const char* name, bool directory) {
    for (const auto& e : entries)
        if (e.directory == directory && iequals(e.name, name)) return &e;
    return nullptr;
}
} // namespace

bool iequals(const std::string& a, const std::string& b) {
    return a.size() == b.size() &&
           std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) { return lower(x) == lower(y); });
}

std::string join_path(const std::string& dir, const std::string& name) {
    return path_to_utf8(path_from_utf8(dir) / path_from_utf8(name));
}

std::string parent_path(const std::string& path) {
    const auto p = path_from_utf8(path);
    const auto parent = p.parent_path();
    return parent == p || parent.empty() ? std::string() : path_to_utf8(parent);
}

std::optional<std::vector<DirEntry>> read_directory(const std::string& dir) {
    std::error_code ec;
    std::filesystem::directory_iterator it(path_from_utf8(dir), ec);
    if (ec) return std::nullopt;
    std::vector<DirEntry> out;
    for (std::filesystem::directory_iterator end; it != end; it.increment(ec)) {
        if (ec) break;
        DirEntry e;
        e.name = path_to_utf8(it->path().filename());
        std::error_code type_ec;
        e.directory = it->is_directory(type_ec);
        if (!e.directory && it->is_regular_file(type_ec)) e.size = it->file_size(type_ec);
        if (type_ec) e.size = 0;
        out.push_back(std::move(e));
    }
    return out;
}

FileBrowser::FileBrowser(std::vector<platform::BrowseRoot> roots, DirReader reader)
    : roots_(std::move(roots)), reader_(std::move(reader)) {
    show_roots();
}

void FileBrowser::show_roots() {
    items_.clear();
    for (const auto& r : roots_) items_.push_back({Kind::root, r.label, r.path});
    dir_.clear();
    error_.clear();
    focus_ = 0;
}

bool FileBrowser::open(const std::string& dir) {
    const auto entries = reader_(dir);
    if (!entries) {
        error_ = "Cannot open " + dir;
        return false;
    }
    std::vector<Item> items;
    if (!parent_path(dir).empty()) items.push_back({Kind::parent, "..", parent_path(dir)});
    if (find_entry(*entries, "ROADRASH.EXE", false) || find_entry(*entries, "ROADRASH", true))
        items.push_back({Kind::use_folder, "Use this folder", dir});
    std::vector<const DirEntry*> dirs, isos;
    for (const auto& e : *entries) {
        if (hidden(e.name)) continue;
        if (e.directory)
            dirs.push_back(&e);
        else if (ends_with_iso(e.name))
            isos.push_back(&e);
    }
    const auto by_name = [](const DirEntry* a, const DirEntry* b) { return name_less(a->name, b->name); };
    std::sort(dirs.begin(), dirs.end(), by_name); // total-order: folded name, then exact bytes
    std::sort(isos.begin(), isos.end(), by_name); // total-order: folded name, then exact bytes
    for (const auto* e : dirs) items.push_back({Kind::directory, e->name + "/", join_path(dir, e->name)});
    for (const auto* e : isos) items.push_back({Kind::iso, e->name, join_path(dir, e->name)});
    items_ = std::move(items);
    dir_ = dir;
    error_.clear();
    focus_ = 0;
    return true;
}

FileBrowser::Result FileBrowser::activate(std::string& chosen) {
    if (items_.empty()) return Result::none;
    const Item item = items_[size_t(focus_)];
    switch (item.kind) {
    case Kind::root:
    case Kind::parent:
    case Kind::directory:
        open(item.path);
        return Result::none;
    case Kind::use_folder:
    case Kind::iso:
        chosen = item.path;
        return Result::chose;
    }
    return Result::none;
}

FileBrowser::Result FileBrowser::back() {
    if (dir_.empty()) return Result::closed;
    const bool at_root = std::any_of(roots_.begin(), roots_.end(),
                                     [&](const platform::BrowseRoot& r) { return r.path == dir_; });
    const std::string parent = parent_path(dir_);
    if (at_root || parent.empty() || !open(parent)) show_roots();
    return Result::none;
}

void FileBrowser::move(int delta) {
    if (items_.empty()) return;
    focus_ = std::clamp(focus_ + delta, 0, int(items_.size()) - 1);
}

void FileBrowser::set_focus(int index) {
    if (index >= 0 && index < int(items_.size())) focus_ = index;
}

std::vector<DiscCandidate> discover_discs(const std::vector<platform::BrowseRoot>& roots,
                                          const DirReader& reader, const std::function<bool()>& stop) {
    constexpr uint64_t min_iso = 400ull << 20, max_iso = 800ull << 20;
    std::vector<DiscCandidate> found;
    std::set<std::string> seen;
    // Breadth-first per root, so shallow matches come first.
    for (const auto& root : roots) {
        std::vector<std::string> level{root.path};
        for (int depth = 0; depth <= 2 && !level.empty(); ++depth) {
            std::vector<std::string> next;
            for (const auto& dir : level) {
                if (stop()) return found;
                const auto entries = reader(dir);
                if (!entries) continue;
                std::vector<const DirEntry*> sorted;
                for (const auto& e : *entries)
                    if (!hidden(e.name)) sorted.push_back(&e);
                const auto by_name = [](const DirEntry* a, const DirEntry* b) {
                    return name_less(a->name, b->name);
                };
                std::sort(sorted.begin(), sorted.end(),
                          by_name); // total-order: folded name, then exact bytes
                for (const auto* e : sorted) {
                    const std::string path = join_path(dir, e->name);
                    if (e->directory && iequals(e->name, "ROADRASH")) {
                        const auto inner = reader(path);
                        if (inner && find_entry(*inner, "ROADRASH.EXE", false) && seen.insert(path).second)
                            found.push_back({path, path, false});
                    } else if (!e->directory && ends_with_iso(e->name) && e->size >= min_iso &&
                               e->size <= max_iso && seen.insert(path).second) {
                        found.push_back({path, path, true});
                    }
                    if (e->directory && depth < 2) next.push_back(path);
                }
            }
            level = std::move(next);
        }
    }
    return found;
}

Nav NavInput::press(Nav n, uint64_t now_ms) {
    held_ = n;
    next_repeat_ = now_ms + repeat_delay_ms;
    return n;
}

void NavInput::release() {
    held_ = Nav::none;
    stick_x_ = stick_y_ = 0;
}

Nav NavInput::tick(uint64_t now_ms) {
    if (held_ == Nav::none || now_ms < next_repeat_) return Nav::none;
    next_repeat_ = now_ms + repeat_interval_ms;
    return held_;
}

Nav NavInput::event(const SDL_Event& e, uint64_t now_ms) {
    switch (e.type) {
    case SDL_EVENT_KEY_DOWN:
        gamepad_last_ = false;
        switch (e.key.key) {
        case SDLK_UP:
            return Nav::up;
        case SDLK_DOWN:
            return Nav::down;
        case SDLK_LEFT:
            return Nav::left;
        case SDLK_RIGHT:
            return Nav::right;
        case SDLK_TAB:
            return (e.key.mod & SDL_KMOD_SHIFT) ? Nav::up : Nav::down;
        case SDLK_RETURN:
        case SDLK_KP_ENTER:
        case SDLK_SPACE:
            return e.key.repeat ? Nav::none : Nav::activate;
        case SDLK_ESCAPE:
        case SDLK_BACKSPACE:
        case SDLK_AC_BACK:
            return e.key.repeat ? Nav::none : Nav::back;
        case SDLK_PAGEUP:
            return Nav::page_up;
        case SDLK_PAGEDOWN:
            return Nav::page_down;
        default:
            return Nav::none;
        }
    case SDL_EVENT_MOUSE_MOTION:
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_FINGER_DOWN:
        gamepad_last_ = false;
        return Nav::none;
    case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
        gamepad_last_ = true;
        switch (e.gbutton.button) {
        case SDL_GAMEPAD_BUTTON_DPAD_UP:
            return press(Nav::up, now_ms);
        case SDL_GAMEPAD_BUTTON_DPAD_DOWN:
            return press(Nav::down, now_ms);
        case SDL_GAMEPAD_BUTTON_DPAD_LEFT:
            return press(Nav::left, now_ms);
        case SDL_GAMEPAD_BUTTON_DPAD_RIGHT:
            return press(Nav::right, now_ms);
        case SDL_GAMEPAD_BUTTON_SOUTH:
            return Nav::activate;
        case SDL_GAMEPAD_BUTTON_EAST:
            return Nav::back;
        case SDL_GAMEPAD_BUTTON_NORTH:
            return Nav::alternate;
        case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER:
            return Nav::page_up;
        case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER:
            return Nav::page_down;
        default:
            return Nav::none;
        }
    case SDL_EVENT_GAMEPAD_BUTTON_UP: {
        const auto b = e.gbutton.button;
        const Nav n = b == SDL_GAMEPAD_BUTTON_DPAD_UP      ? Nav::up
                      : b == SDL_GAMEPAD_BUTTON_DPAD_DOWN  ? Nav::down
                      : b == SDL_GAMEPAD_BUTTON_DPAD_LEFT  ? Nav::left
                      : b == SDL_GAMEPAD_BUTTON_DPAD_RIGHT ? Nav::right
                                                           : Nav::none;
        if (n != Nav::none && n == held_) held_ = Nav::none;
        return Nav::none;
    }
    case SDL_EVENT_GAMEPAD_AXIS_MOTION: {
        if (e.gaxis.axis != SDL_GAMEPAD_AXIS_LEFTX && e.gaxis.axis != SDL_GAMEPAD_AXIS_LEFTY)
            return Nav::none;
        const bool x = e.gaxis.axis == SDL_GAMEPAD_AXIS_LEFTX;
        int& state = x ? stick_x_ : stick_y_;
        const int v = e.gaxis.value;
        const int dir = v >= stick_press ? 1 : v <= -stick_press ? -1 : 0;
        const Nav minus = x ? Nav::left : Nav::up, plus = x ? Nav::right : Nav::down;
        if (dir != 0 && dir != state) {
            state = dir;
            gamepad_last_ = true;
            return press(dir > 0 ? plus : minus, now_ms);
        }
        if (state != 0 && (v < stick_release && v > -stick_release)) {
            if (held_ == (state > 0 ? plus : minus)) held_ = Nav::none;
            state = 0;
        }
        return Nav::none;
    }
    case SDL_EVENT_GAMEPAD_REMOVED:
        release();
        return Nav::none;
    default:
        return Nav::none;
    }
}

ImportMenu::ImportMenu(std::vector<platform::BrowseRoot> roots, DirReader reader, bool folder_buttons)
    : browser_(std::move(roots), std::move(reader)), folder_buttons_(folder_buttons) {}

void ImportMenu::open_browser() {
    if (view_ == View::browser) return;
    view_ = View::browser;
    browser_.show_roots();
}

void ImportMenu::close_browser() {
    view_ = View::main;
    if (folder_buttons_) focus_ = column_ = int(Button::browse);
}

void ImportMenu::set_candidates(std::vector<DiscCandidate> c) {
    candidates_ = std::move(c);
    focus_ = std::min(focus_, button_count() + int(candidates_.size()) - 1);
}

void ImportMenu::set_focus(int index) {
    if (index < 0 || index >= button_count() + int(candidates_.size())) return;
    focus_ = index;
    if (index < button_count()) column_ = index;
}

ImportMenu::Action ImportMenu::press(Button b) {
    if (!folder_buttons_ && b != Button::iso) return {};
    set_focus(int(b));
    switch (b) {
    case Button::iso:
        return {Action::Kind::iso_dialog, {}};
    case Button::folder:
        return {Action::Kind::folder_dialog, {}};
    case Button::browse:
        open_browser();
        return {};
    }
    return {};
}

ImportMenu::Action ImportMenu::choose_candidate(int i) {
    if (i < 0 || i >= int(candidates_.size())) return {};
    set_focus(button_count() + i);
    return {Action::Kind::import_path, candidates_[size_t(i)].path};
}

ImportMenu::Action ImportMenu::nav(Nav n) {
    if (view_ == View::browser) {
        std::string chosen;
        switch (n) {
        case Nav::up:
            browser_.move(-1);
            break;
        case Nav::down:
            browser_.move(1);
            break;
        case Nav::page_up:
            browser_.move(-8);
            break;
        case Nav::page_down:
            browser_.move(8);
            break;
        case Nav::right:
            if (!browser_.items().empty()) {
                const auto kind = browser_.items()[size_t(browser_.focus())].kind;
                if (kind == FileBrowser::Kind::root || kind == FileBrowser::Kind::directory ||
                    kind == FileBrowser::Kind::parent)
                    browser_.activate(chosen);
            }
            break;
        case Nav::activate:
            if (browser_.activate(chosen) == FileBrowser::Result::chose) {
                close_browser();
                return {Action::Kind::import_path, chosen};
            }
            break;
        case Nav::left:
        case Nav::back:
            if (browser_.back() == FileBrowser::Result::closed) close_browser();
            break;
        case Nav::alternate:
            browser_.show_roots();
            break;
        case Nav::none:
            break;
        }
        return {};
    }
    const int buttons = button_count(), rows = int(candidates_.size());
    switch (n) {
    case Nav::left:
        if (focus_ < buttons) set_focus(std::max(0, focus_ - 1));
        break;
    case Nav::right:
        if (focus_ < buttons) set_focus(std::min(buttons - 1, focus_ + 1));
        break;
    case Nav::up:
        if (focus_ > buttons)
            set_focus(focus_ - 1);
        else if (focus_ == buttons)
            set_focus(column_);
        break;
    case Nav::down:
        if (focus_ < buttons && rows > 0)
            set_focus(buttons);
        else if (focus_ >= buttons && focus_ + 1 < buttons + rows)
            set_focus(focus_ + 1);
        break;
    case Nav::activate:
        return focus_ < buttons ? press(Button(focus_)) : choose_candidate(focus_ - buttons);
    case Nav::page_up:
    case Nav::page_down:
    case Nav::back:
    case Nav::alternate:
    case Nav::none:
        break;
    }
    return {};
}
} // namespace rr
