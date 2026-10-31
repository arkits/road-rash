// Plan 33a Task 5: the in-game file and folder browser behind the import
// screen's "Browse" button, disc auto-discovery, and the pad/keyboard
// navigation both views share. The models are pure (directory reads are
// injected) so tests drive them without a file system or a display. Used
// when the user picks Browse, when SDL's file dialog fails or does not answer
// within 3 s, and as the default under gamescope (Steam Deck Game Mode),
// where neither the XDG portal nor zenity is known to appear.
#pragma once
#include "platform_linux.hpp"
#include <SDL3/SDL.h>
#include <atomic>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace rr {
struct DirEntry {
    std::string name; // UTF-8
    bool directory = false;
    uint64_t size = 0; // bytes, files only
};
// Entries of `dir` (any order), or nullopt when it cannot be read.
using DirReader = std::function<std::optional<std::vector<DirEntry>>(const std::string& dir)>;
std::optional<std::vector<DirEntry>> read_directory(const std::string& dir);

bool iequals(const std::string& a, const std::string& b); // ASCII case-insensitive
// Joins with the platform separator; UTF-8 in and out.
std::string join_path(const std::string& dir, const std::string& name);
std::string parent_path(const std::string& path); // "" at a file-system root

class FileBrowser {
  public:
    enum class Kind { root, parent, use_folder, directory, iso };
    struct Item {
        Kind kind;
        std::string label, path;
    };
    enum class Result { none, chose, closed };

    explicit FileBrowser(std::vector<platform::BrowseRoot> roots, DirReader reader = read_directory);
    // The top level: one item per root folder.
    void show_roots();
    // Lists `dir`: "..", then "Use this folder" when it holds ROADRASH.EXE or
    // a ROADRASH folder, then subfolders, then *.iso files; each group sorted
    // case-insensitively with the exact name as the tie-break (a total order).
    // Hidden (dot) entries are left out. Returns false, keeping the current
    // listing and setting error(), when the folder cannot be read.
    bool open(const std::string& dir);
    // Activates the focused item: opens a folder, or returns `chose` with the
    // ISO or disc folder in `chosen`.
    Result activate(std::string& chosen);
    // Up one folder; from a root folder back to the root list; from the root
    // list, `closed`.
    Result back();
    void move(int delta); // focus, clamped to the list
    void set_focus(int index);
    const std::vector<Item>& items() const { return items_; }
    int focus() const { return focus_; }
    const std::string& directory() const { return dir_; } // "" at the root list
    const std::string& error() const { return error_; }

  private:
    std::vector<platform::BrowseRoot> roots_;
    DirReader reader_;
    std::vector<Item> items_;
    std::string dir_, error_;
    int focus_ = 0;
};

// First-run disc search (plan 33a Task 5 Step 3): each root and the folders
// up to two levels below it, for a ROADRASH folder holding ROADRASH.EXE (any
// case) and *.iso files between 400 and 800 MB. Name-based only; the import
// still verifies every hash. Stops when `stop()` returns true (the 2 s cap,
// or the user acted). Results in search order, without duplicates.
struct DiscCandidate {
    std::string label, path;
    bool iso = false;
};
std::vector<DiscCandidate> discover_discs(const std::vector<platform::BrowseRoot>& roots,
                                          const DirReader& reader, const std::function<bool()>& stop);

// Menu navigation shared by the import screen and the browser: arrow keys,
// Tab, Enter/Space, Escape/Backspace, Page Up/Down; D-pad, left stick, South
// (activate), East (back), North (alternate), shoulders (page). Held D-pad and
// stick directions repeat after 400 ms, every 110 ms.
enum class Nav { none, up, down, left, right, activate, back, page_up, page_down, alternate };
class NavInput {
  public:
    static constexpr uint64_t repeat_delay_ms = 400, repeat_interval_ms = 110;
    static constexpr int stick_press = 16000, stick_release = 8000;
    Nav event(const SDL_Event& e, uint64_t now_ms);
    Nav tick(uint64_t now_ms); // call once per frame for held-direction repeats
    void release();            // forget a held direction (focus loss, pad removal)
    bool gamepad_last() const { return gamepad_last_; }

  private:
    Nav held_ = Nav::none;
    uint64_t next_repeat_ = 0;
    int stick_x_ = 0, stick_y_ = 0; // -1, 0, 1 per axis
    bool gamepad_last_ = false;
    Nav press(Nav n, uint64_t now_ms);
};

// Focus and actions of the import screen (plan 33a Task 5 Step 1, finding
// F11): the button row (Choose ISO, Choose folder, Browse; ISO only where
// there is no folder picker), then the discovered discs, one per row; or the
// browser when it is open. Pure: the screen turns the returned actions into
// SDL dialogs and imports.
class ImportMenu {
  public:
    enum class View { main, browser };
    enum class Button { iso = 0, folder = 1, browse = 2 };
    struct Action {
        enum class Kind { none, iso_dialog, folder_dialog, import_path } kind = Kind::none;
        std::string path; // import_path
    };
    ImportMenu(std::vector<platform::BrowseRoot> roots, DirReader reader, bool folder_buttons);
    Action nav(Nav n);
    Action press(Button b);         // click, tap or I/F/B shortcut
    Action choose_candidate(int i); // click or tap on a discovered disc
    void open_browser();
    void close_browser();
    void set_candidates(std::vector<DiscCandidate> c);
    // Main view focus: buttons are 0..button_count()-1, discovered disc i is
    // button_count() + i.
    int focus() const { return focus_; }
    void set_focus(int index);
    int button_count() const { return folder_buttons_ ? 3 : 1; }
    View view() const { return view_; }
    FileBrowser& browser() { return browser_; }
    const FileBrowser& browser() const { return browser_; }
    const std::vector<DiscCandidate>& candidates() const { return candidates_; }

  private:
    FileBrowser browser_;
    std::vector<DiscCandidate> candidates_;
    View view_ = View::main;
    bool folder_buttons_;
    int focus_ = 0, column_ = 0; // column_: the button to return to from the disc rows
};
} // namespace rr
