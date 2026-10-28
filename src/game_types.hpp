// Plan 28 Task 4: shared game types (moved unchanged from game_main.cpp).
#pragma once
#include <SDL3/SDL.h>
#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>
#include "actor_nodes.hpp"
#include "assets.hpp"
#include "bob.hpp"
#include "course_scene.hpp"
#include "road.hpp"
#include "display.hpp"
#include "framebuffer.hpp"
#include "frontend.hpp"
#include "mixer.hpp"
#include "race.hpp"
#include "race_music.hpp"
#include "race_setup.hpp"
#include "view.hpp"
#include "settings.hpp"
#include "net/protocol.hpp"
#include "sound.hpp"
#include "text.hpp"
#include "traffic.hpp"
#include "view_state.hpp"

namespace rr {
namespace net {
class Client;
}
struct FrameBench; // frame_profile.hpp
// Display settings driver (plan 18 Task 3): owns the window/renderer state derived
// from Settings. Each mode applies its logical height (480 menus/videos, 480 race:
// the race composes the dashboard inside the 480 frame, so no 510 path exists yet).
std::string pref_directory(); // plan 20 Task 3 cache root (defined in game_types.cpp)

struct DisplayDriver {
    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;
    Settings* settings = nullptr; // borrowed: single source of truth lives with the caller
    std::string settings_path;
    DisplayConfig config;
    long toggle_at = -1; // debug: toggle fullscreen at this frame/tick count (--toggle-fullscreen-at)
    long frame = 0;
    int render_scale = 1;   // plan 19 Task 8: effective scale, re-queried on apply
    int render_threads = 1; // plan 19 Task 8: worker count for the band renderer
    void apply(int logical_h);
    void toggle_fullscreen();
    void maybe_toggle() {
        if (toggle_at >= 0 && ++frame >= toggle_at) {
            toggle_at = -1;
            toggle_fullscreen();
        }
    }
};

struct Game {
    DepthTable depth;
    RaceAssets assets;         // plan 26 Task 3: course-scoped (scene, hud, actors, specs, ...)
    RaceInstance instance;     // plan 26 Task 3: race-scoped (race, traffic, pedestrians, ring)
    RaceConfig config;         // plan 26 Task 3: course/level/bike/career/opponents for (re)builds
    std::vector<uint8_t> exe_; // plan 26 Task 3: EXE image for (re)builds
    // Thin accessors so call-site churn stays mechanical.
    Race& race() { return *instance.race; }
    const Race& race() const { return *instance.race; }
    CourseScene* scene() { return assets.scene.get(); }
    const CourseScene* scene() const { return assets.scene.get(); }
    TrafficSystem* traffic() { return instance.traffic.get(); }
    const TrafficSystem* traffic() const { return instance.traffic.get(); }
    std::vector<ActorAnimationState> actor_states;
    std::vector<int> previous_attacks;
    std::vector<bool> previous_crashes;
    bool metric = false;
    std::string player_name = "Player";
    std::vector<std::string> rider_names;
    std::string course_name;
    int bike = 0;
    int texture_detail = 0;                  // plan 20 Task 3: 0 original, 1 enhanced (MMPX)
    mutable bool upscaled_attached = false;  // scene textures carry MMPX levels
    Widescreen widescreen = Widescreen::off; // plan 21 Task 8: live setting
    int display_w = 0, display_h = 0;        // window size for Widescreen::match (0 = unknown)
    Enhancements enhancements;               // plan 21 Task 1: synced per frame; all-off = original
    mutable bool post_teleported = true;     // plan 21 Task 7: first frame seeds blur history
    mutable bool view_ready = false;
    ViewState view_prev, view_cur, view_drawn; // plan 21 Task 2: interpolated frame state
    // Clear interpolation/blur history: the next frame shows the live race
    // without blending across a discontinuity (restart, resume, teleport).
    void reset_view_history() const {
        post_teleported = true;
        view_ready = false;
    }
    // Blended position overrides for the current frame (id -> pose).
    const ActorPose* blend_pose(uint32_t id) const {
        for (const auto& p : view_drawn.actors)
            if (p.id == id) return &p;
        return nullptr;
    }
    uint32_t blend_camera_position() const {
        return view_ready ? view_drawn.camera_position_q8 : race().camera_position();
    }
    Camera blend_camera() const { return view_ready ? view_drawn.camera : race().camera(); }
    int32_t logical_width() const;
    int countdown = 0; // set to countdown_ticks by callers (game_main.cpp)
    // RaceScreen's race-start/race-end palette fade (0x00415d90/0x00415e90)
    // as a channel map; empty when no fade is running. Presentation only.
    mutable std::optional<ChannelLut> fade_lut;
    std::vector<Image8> bobs;
    RiderRemaps remaps;
    Palette palette;
    bool career = false;     // race ends after the finish delay instead of restarting
    std::string quit_prompt; // string 316, shown after Escape in a career race
    bool quitting = false;
    std::string pause_prompt;   // string 317, shown by the Pause key (0x004234f7)
    bool pause_message = false; // 0x004431d0 pump with string 317 open
    // DAT_004c5bd0 while a 0x004431d0 pump runs (DAT_004753b8): the quit or
    // pause prompt, set by RaceScreen each frame; empty otherwise.
    mutable std::string banner_message;
    RaceMusicPlayer* race_music = nullptr;
    SoundSystem* sound = nullptr; // race effects and engines (0x00434980)
    Mixer* mixer = nullptr;

    void load_course(const Assets& a, const std::vector<uint8_t>& exe);
    void restart();
    std::vector<DrawNode> rider_nodes(const RoadSlice& near, const RoadSlice& far, int road, int32_t ceiling,
                                      bool back) const;
    void tick(uint32_t controls);
    void sync_actor_states();
    void present_online(const RaceInstance&, int seat);
};

struct RaceEnd {
    std::optional<net::Official> official; // mano results; caller applies no career changes
    bool quit_app = false;
    RaceOutcome outcome = RaceOutcome::lost;
    int place = 0; // 0-based
};

struct RaceOptions {
    bool auto_drive = false, start_paused = false, demo = false; // attract demo: any input exits quietly
    std::string screenshot;
    std::array<uint16_t, 11> key_codes{}; // plan 18 Task 7: honored when custom_keys
    bool custom_keys = false;
    bool swap_punch_kick = false;            // plan 18 Task 7: native gamepad page
    int stick_deadzone = 35;                 // plan 18 Task 7: percent, 10..40
    bool left_handed = false;                // plan 18 Task 7: native touch page
    Frontend* menu = nullptr;                // plan 18 Task 8: pause menu; null keeps the old freeze
    std::function<void()> apply_settings;    // host applies display+mixer levels on menu events
    bool show_fps = false;                   // plan 18 Task 9: FPS + ticks/sec overlay
    const Assets* assets = nullptr;          // fonts for the overlay; null disables it
    int render_scale = 1;                    // plan 19 Task 4: race surface is 640s x 480s
    const Settings* settings = nullptr;      // plan 19 Task 8: live scale via effective_render_scale
    class OfflineCapture* capture = nullptr; // fixed 60 Hz offline video/audio export
    std::function<void(const net::Message&)> lobby_event; // retain room messages while racing
    net::Client* online = nullptr; // borrowed room client; already has a Start/race at screen entry
    std::array<std::string, 8> chat_messages{};
    bool smooth_remote = true, network_hud = false;
    FrameBench* bench = nullptr; // --bench: phase timings, ends the race after bench->frames
};
} // namespace rr
