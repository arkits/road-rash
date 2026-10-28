// Plan 28 Task 4: RaceScreen interface (plan 23 Task 3's AppScreen signatures).
#pragma once
#include <SDL3/SDL.h>
#include <cstdint>
#include <memory>
#include "display.hpp"
#include "framebuffer.hpp"
#include "game_types.hpp"
#include "input.hpp"
#include "lifecycle.hpp"
#include "race_actions.hpp"
#include "race_caches.hpp"
#include "race_overlay.hpp"
#include "sdl_handles.hpp"
#include "session.hpp"

namespace rr {
constexpr int race_end_delay = 0xb4; // 0x00416010: ticks the race keeps running after it is decided

// Loop helpers shared between the screen and game_main.cpp's driver.
// SpriteCache/CourseCanvas stay in game_main.cpp (used by menus/video too);
// the screen holds pointers borrowed from the driver.
class RaceScreen {
  public:
    RaceScreen(Game&, SDL_Renderer*, const RaceOptions&, DisplayDriver&);
    void event(const SDL_Event&);
    bool iterate(uint64_t now_ns); // false when the race loop would have exited
    RaceEnd result() const { return result_; }
    const NetworkOverlay* network_overlay() const { return network_overlay_.get(); }

  private:
    Game& g_;
    SDL_Renderer* renderer_;
    const RaceOptions& opt_;
    DisplayDriver& display_;
    RaceEnd result_;
    LifecycleWatch lifecycle_;
    TouchInput touch_;
    TickClock clock_;
    SpriteCache sprites_;
    CourseCanvas canvas_;
    GamepadPtr pad_;
    TexturePtr pause_tex_;
    int pause_scale_w_ = 0;
    RgbImage pause_img_;
    std::unique_ptr<FpsOverlay> fps_;
    std::unique_ptr<NetworkOverlay> network_overlay_;
    RemoteSmoothing remote_smoothing_;
    std::optional<net::Official> official_;
    std::array<std::string, 3> announcements_{};
    void step_online(uint64_t now, uint32_t controls);
    bool running_ = true, decided_ = false;
    int end_ticks_ = 0;
    uint64_t last_ = 0;
    bool restart_ = false;
    // 0x00415f70 palette fades: race start (0x00415d90) blocks input and the
    // simulation until it ends, then the loop clock starts; race end
    // (0x00415e90) runs after the decided race's 0xb4 ticks. Wall-clock
    // driven at 14 ms per upload; presentation only (Game::fade_lut).
    enum class Fade { none, in, out };
    Fade fade_ = Fade::none;
    uint64_t fade_start_ = 0;     // 0: starts on the next iterate
    bool fade_from_half_ = false; // quit-confirm leaves the half palette up
    bool hold_frame_ = false;     // fade-in before its first upload: keep the old screen
    void update_fade(uint64_t now);

    RaceActionContext ctx_now() const;
    void end_as_quit();
    void reset_clock(uint64_t now_ns);
    void step_sim(uint64_t now);
    void render(uint64_t now);
    void draw_scene(FrameProfile*);
    RgbImage pause_frame();
};
} // namespace rr
