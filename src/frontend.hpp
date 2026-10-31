#pragma once
#include "career.hpp"
#include "input.hpp"
#include "net/mano.hpp"
#include "net/protocol.hpp"
#include "settings.hpp"
#include "text.hpp"
#include <array>
#include <deque>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace rr {
class Assets;

// One 0x38-byte menu item (screen tables at 0x0046a390).
struct MenuItem {
    int32_t target = 0;                 // +4 screen to enter when activated without a handler
    int32_t caption = 0;                // +8 string slot
    int32_t x = 0, y = 0, w = 0, h = 0; // +0xc..+0x18, 640x480 coordinates
    std::array<uint8_t, 8> nav{};       // +0x1c left, right, up, down, tab, shift-tab (0x004227d0)
    uint32_t flags = 0;                 // +0x24
    bool enabled = false;               // +0x28
    uint32_t handler = 0;               // +0x2c original handler address
    int32_t desc = 0;                   // +0x30 description string slot
    uint32_t format = 0;                // +0x34 DrawText format of the title/description boxes
};

// Item flags.
namespace item_flag {
constexpr uint32_t title_caption = 0x1, title_desc = 0x2, description = 0x4, popup_item = 0x10,
                   keep_background = 0x20, light = 0x40, course_length = 0x4000, price = 0x2000,
                   escape = 0x8000, skip = 0x40000, dimmed = 0x80000,
                   native_slider =
                       0x01000000; // plan 18 Task 6: native slider row (bit never set by the original)
}

struct MenuScreen {
    std::vector<MenuItem> items;
    int32_t title = 0; // screen title string slot (0x0041f770)
    uint32_t title_format = 0;
};

// Everything the front end reads from the user's executable.
struct FrontendData {
    std::array<MenuScreen, screen_id::screen_count> screens;
    std::map<uint32_t, std::vector<MenuItem>> popups; // by original address
    std::array<MenuItem, 3> course_extra{};       // 6th course-select item: Thrash, mano, Big Game templates
    std::array<std::string, 8> restroom_patterns; // 0x00473598: 'Y'/'N' per restroom item
    std::array<uint32_t, 25> course_length{};     // 0x00468f58: tenths of miles [course*5 + level]
    std::array<std::array<uint16_t, 14>, 40> schmooze{}; // 0x0046a6e0: [level*8 + rider]
    std::array<uint8_t, 4> attitude{};                   // 0x0046ab44
    std::array<uint32_t, 24> schmooze_format{};          // 0x0046a680
    std::array<std::array<int32_t, 2>, 10> body{}; // rider table 0x00463c78 +8 height (in), +0xc weight (lb)
};
FrontendData read_frontend_data(const std::vector<uint8_t>& exe);
// Game string table (0x0042f5d0) plus the unit formats 4000..4003 the front end loads directly.
std::vector<std::string> frontend_strings(const std::map<uint32_t, std::u16string>& resources);
// 0x00424140 loads string tables 1000 (screen 7) and 2000 (screen 0x23) with the 0x0042f5d0 rules.
std::array<std::vector<std::string>, 2> credit_lines(const std::map<uint32_t, std::u16string>& resources);

struct MenuInput {
    bool left = false, right = false, up = false, down = false, tab = false, shift_tab = false;
    bool home = false, end = false, select = false, back = false, backspace = false;
    bool alternate_artwork = false; // 'A' key (0x004227d0)
    bool opponent_key = false;      // '/' key toggles the opponent list (0x00423777)
    int capture_scancode = 0;       // plan 18 Task 7: raw SDL scancode of this keydown (0 none)
    bool pointer_moved = false, pointer_click = false;
    int pointer_x = -1, pointer_y = -1; // 640x480 coordinates
    std::string text;                   // typed characters (Windows-1252)
};

struct FrontendEvent {
    enum class Kind {
        none,
        play_video,
        start_race,
        start_net_race, // online: net_start carries the server Start
        copy_text,      // online: put text on the clipboard (room code)
        quit,
        select_soundtrack,
        apply_display,
        apply_mixer,
        resume_race,
        quit_race
    } kind = Kind::none;
    std::string video; // path relative to the ROADRASH directory, e.g. VIDEO/START3.AVI
    int course = 0;    // 1..5
    int group = 0,
        track = 0;     // select_soundtrack: 0-based group (menu caption - 0xe1) and track (caption - 0xf1)
    bool demo = false; // start_race: AI attract demo (any input exits, no results)
    // Plan 34 Task 7: appended after the original fields so the existing
    // aggregate initialisers keep working.
    std::string text;     // copy_text: clipboard bytes
    net::Start net_start; // start_net_race: roster, seed, course, options
};

// One finisher in the race results (0x00420e30 rows).
struct Standing {
    std::string name;    // Windows-1252
    int32_t tenths = -1; // race time in tenths of a second, -1 for none
    bool player = false;
};

// The original front end: screen state machine of 0x00421df0, keyboard/mouse handling of
// 0x004227d0, item handlers, and the GDI drawing of 0x0041fc90 / 0x0041f770 / 0x00420470,
// drawn into a 640x480 RGB image.
class Frontend {
  public:
    // credits: the scrolling string tables at 1000 (opening) and 2000 (finale), see credit_lines().
    Frontend(const Assets&, const TextRenderer&, std::vector<std::string> strings,
             std::array<std::vector<std::string>, 2> credits, FrontendData, CareerTables, Career&, Settings&,
             std::string save_directory);
    void start(); // intro video, then the Credits screen
    void update(const MenuInput&, uint64_t now_ms);
    bool poll(FrontendEvent&); // next host action
    // Plan 33b Task 4: button names of the connected pad (default: none connected).
    void set_pad_labels(PadLabels labels) { pad_labels_ = std::move(labels); }
    void draw(RgbImage&, int scale = 1) const; // 640s x 480s; scale = 1 keeps today's output exactly
    void draw_net_overlay(RgbImage&, int scale) const;
    void draw_scale1(RgbImage&) const;            // original 640x480 path, bit-identical
    void draw_scaled(RgbImage&, int scale) const; // 640s x 480s, s = 2..4
    void race_finished(RaceOutcome, int place, const std::vector<Standing>&);
    // Plan 34 Task 7: official mano results. Places come from the Official
    // table; the career is not changed. Plays the normal outcome video (never
    // DUEL), shows the official table, and returns to 0x0b with the room kept
    // for a rematch. aliases: Start-roster aliases in seat order.
    void mano_finished(const net::Official& official, const std::vector<std::string>& aliases,
                       int local_seat);
    // Plan 34 Task 7: online lobby. attach() stages the connection credentials
    // (the room client is created lazily on the first Host/Join so the Hello
    // alias is current); the main-menu mano item stays disabled until then.
    using RoomClientFactory = net::RoomClient::Factory;
    void net_attach(RoomClientFactory, net::Hello hello);
    bool net_attached() const { return (bool)net_factory_; }
    bool net_in_room() const;
    net::RoomClient* net_client() { return net_.get(); }
    // Invite: join text (a bare code or an invite link) from the command line,
    // the browser URL or a paste. Enters 0x0b and joins.
    void net_join_invite(const std::string& text);
    const std::array<std::string, 8>& chat_messages() const { return chat_messages_; }
    void enter(int screen); // 0x00421df0
    int screen() const { return screen_; }
    int focus() const { return focus_; }
    const std::vector<MenuItem>& items() const { return items_; }
    bool dialog_open() const { return !dialog_.text.empty(); }
    std::string caption_text(const MenuItem&) const;
    const std::string& current_save() const { return save_file_; }
    std::string player_name() const; // 0x0041c140
    bool editing_text() const { return editing_name_ || editing_code_ || editing_chat_ >= 0; }
    const std::string& string_slot(int slot) const { return str(slot); }
    // In-race toggles: F10 cycles MiniDash 0->1->2->0 (0x004431d0 pump); the menu `/`
    // key toggles the opponent list 0<->1 (0x00423777, sete). Returns the new value.
    int cycle_mini_dash();
    bool toggle_opponent_list();
    // Key rebinding storage (Configure Input 0x12): validates via
    // apply_rebind (0x0040fbf0 conflict rule: unbind, never swap/refuse),
    // marks settings custom, and refreshes the 0x12 current-key labels.
    void rebind_key(size_t action, uint16_t code);
    void reset_keys(bool joystick); // 0x0040fdf0: copy a default table in
    // In-race pause menu (plan 18 Task 8): darken the captured race frame and
    // enter native_pause. Back/B/Escape on the pause screen resumes. Sub-screens'
    // Back returns to the pause screen, not to Graphics Options, when entered
    // from it. Emits resume_race / quit_race (race ends with the Task 1 outcome).
    void open_pause(const RgbImage& race_frame);
    bool pause_open() const;
    int menu_width(int display_w = 0, int display_h = 0) const;
    int side_offset(int display_w = 0, int display_h = 0) const {
        return (menu_width(display_w, display_h) - 640) / 2;
    }
    void set_display_size(int w, int h) {
        display_w_ = w;
        display_h_ = h;
    }
    // Test-only pins (plan 13 Task 6 Step 2): the RNG stream still advances
    // the same draws as without them.
    void set_rest_picture_for_test(int n) { rest_picture_override_ = n; }
    void set_schmooze_for_test(int rider, int attitude) {
        schmooze_rider_override_ = rider;
        schmooze_attitude_override_ = attitude;
    }

  private:
    struct Dialog {
        std::string text;
        std::vector<std::pair<int, int>> buttons;
        int focus = 0;
        int kind = 0;
    };
    const std::string& str(int slot) const;
    void activate(size_t index);
    int run_handler(size_t index);
    void set_focus(size_t index);
    void navigate(int direction);
    void open_popup(const std::vector<MenuItem>&, int current, int setter);
    void close_popup();
    void open_native_popup(uint32_t popup); // plan 18 Task 5: value item -> native popup
    int native_current(uint32_t popup) const;
    int visual_style_current() const; // plan 21 Task 9: 1 Original, 2 Enhanced, 3 Custom
    std::string native_value_text(int caption) const;
    void apply_native_setting();         // persist + emit apply_display
    int slider_value(int caption) const; // plan 18 Task 6: 0..100 for slider rows
    void set_slider_value(int caption, int v);
    void adjust_slider(int delta);                // Left/Right (+-10) on the focused slider row
    void slider_home_end(bool home);              // Home/End: 0/100 on the focused slider row
    void apply_mixer_setting();                   // persist + emit apply_mixer
    std::string key_name(int action) const;       // plan 18 Task 7: 0x12 label text
    PadLabels pad_labels_;                        // plan 33b Task 4: Gamepad page names
    mutable std::array<std::string, 4> pad_rows_; // rows built from pad_labels_ in str()
    void apply_restroom_pattern();
    void set_background(const std::string& name);
    std::string background_for(int previous, int screen, int title);
    std::string focus_image(const MenuItem&) const;
    void prepare_screen(int previous, int screen);
    void prepare_courses(int screen);
    void prepare_characters();
    void prepare_schmooze(int previous);
    void title_box(int slot, uint32_t format);
    void describe(const MenuItem&);
    void show_dialog(const std::string& text, int kind, std::vector<std::pair<int, int>> buttons);
    void finish_dialog(int choice);
    bool confirm_unsaved(int then); // 0x004450e0; false while a dialog is pending
    void save_current();
    void save_new();
    void open_load_list();
    // Plan 34 Task 7: online lobby driver (polled from update()).
    void net_ensure();        // create the room client on first Host/Join
    void net_update();        // poll, dialogs, Start events, roster refresh
    void net_open_host();     // Host wizard: level, AI, cars, public popups
    void net_open_rooms();    // enter the room list (requests a fresh list)
    void net_prepare_lobby(); // roster rows + room-code title
    void net_prepare_rooms(); // room rows + code/spectate rows
    void net_join_shown(int index);
    bool net_edit_input(const MenuInput&);
    void net_show_error();
    void net_close_connecting();
    std::string net_roster_line(int seat) const;
    void queue_video(const std::string& base, int variants);
    std::string format_standing(size_t place, const Standing&) const;
    void draw_text_box(RgbImage&, const TextRect&, Face, int height, const std::string&, unsigned flags,
                       uint32_t colour) const;

    const Assets& assets_;
    const TextRenderer& text_;
    std::vector<std::string> strings_;
    FrontendData data_;
    CareerTables tables_;
    Career& career_;
    Settings& settings_;
    std::string save_dir_, save_file_;
    int capture_action_ = -1;   // plan 18 Task 7: press-a-key arming (-1 idle)
    bool pause_open_ = false;   // plan 18 Task 8: in-race pause menu showing
    bool pause_return_ = false; // plan 18 Task 8: sub-screen Back returns to native_pause
    std::deque<FrontendEvent> events_;

    int screen_ = 0, previous_ = 0, focus_ = -1;
    std::vector<MenuItem> items_;                           // current items including an open popup
    size_t popup_base_ = 0;                                 // 0x00496020
    int popup_setter_ = 0;                                  // 0x0049e0a8
    std::array<int, screen_id::screen_count> last_focus_{}; // 0x0046a3a0
    int character_return_ = screen_id::street;              // 0x0049705c
    int schmooze_target_ = 0x19, schmooze_count_ = 0;       // 0x00469e1c, 0x0046ab40
    uint8_t schmooze_used_ = 0;                             // 0x00485ce4
    int rest_picture_ = 1;                                  // 0x00485ce8
    int rest_picture_override_ = 0;       // test-only pin for background_for (plan 13 Task 6 Step 2)
    int schmooze_rider_override_ = -1;    // test-only rider 0..7 for prepare_schmooze
    int schmooze_attitude_override_ = -1; // test-only attitude 0..2 (FNH) for prepare_schmooze
    RgbImage background_, dark_;
    std::string background_name_;
    int display_w_ = 0, display_h_ = 0;
    mutable RgbImage wide_bg_;
    mutable bool wide_bg_dirty_ = true;
    void ensure_wide_bg(int wide_w) const;
    int title_slot_ = 0;
    uint32_t title_format_ = 0;
    int desc_slot_ = 0;
    uint32_t desc_format_ = 0, desc_flags_ = 0;
    bool desc_fill_ = false;    // 0x00420470 repaints the dark box only for flags & 8 items
    std::string length_footer_; // course Length line for the showroom footer (stale = faithful)
    std::string player_footer_; // 0x4208d0 bh&8 player line, centered at y=380 (masked in tests)
    std::string price_footer_;  // 0x4208d0 bh&1 bike line, centered at y=380 (masked in tests)
    std::string price_text_;
    bool qualified_overlay_ = false;
    bool dark_screen_ = false; // standings: the darkened copy fills the screen
    uint64_t now_ = 0, timer_at_ = 0;
    uint64_t attract_at_ = 0; // 0x00421df0/WM_TIMER 9: 60 s main-menu idle starts the demo
    int attract_course_ = 0;  // 0x46aff0: cycles 1..5 across idle windows
    // Scrolling credits (0x00424140 / 0x00424230): lines, y, first line, speed (0x00468f50), timer ticks.
    std::array<std::vector<std::string>, 2> credits_;
    int credit_set_ = -1, credit_y_ = 0, credit_line_ = 1, credit_speed_ = 1, credit_ticks_ = 0;
    uint64_t credit_next_ = 0;
    void credit_tick();
    Dialog dialog_;
    int dialog_then_ = 0;
    bool editing_name_ = false;
    std::string edit_buffer_;
    std::vector<std::string> save_list_;
    std::vector<Standing> standings_;
    RaceReport report_;
    bool standings_ready_ = false; // 0x22 rows/title/darken set by the 0x00421290 handler
    // Plan 34 Task 7: online lobby state. net_ owns the room connection for
    // the life of the room visit (lent to net::Client during races).
    RoomClientFactory net_factory_;
    net::Hello net_hello_;
    std::unique_ptr<net::RoomClient> net_;
    net::RoomOptions net_pending_options_{};
    bool net_connecting_ = false; // connecting dialog up
    std::string net_shown_error_; // last error already shown as a dialog
    std::string net_code_buffer_; // Join code entry (bare code or invite link)
    bool editing_code_ = false;
    bool net_spectate_ = false; // join toggle on the rooms screen
    std::vector<net::RoomListEntry> net_shown_rooms_;
    std::array<std::string, 8> chat_messages_{}; // F1-F8 slots (0x0d screen)
    int editing_chat_ = -1;                      // 0x0d row being edited, -1 none
    int mano_title_ = 0;                         // saved 0x0c title slot while the vote title shows
    std::vector<std::string> net_chat_log_;      // last lobby chat lines
    uint64_t net_lobby_at_ = 0;                  // last roster refresh
    bool mano_results_ = false;
    uint32_t net_video_rng_ = 1;
    std::optional<MenuScreen> offline_standings_;
    void net_results_table();
    uint32_t next_random();
};
} // namespace rr
