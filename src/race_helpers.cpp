// Plan 28 Task 4: shared race helpers (moved unchanged from game_main.cpp).
#include "race_helpers.hpp"
#include <cstdlib>
#include "course_scene.hpp"
#include "hud.hpp"
#include "pe_strings.hpp"
#include "race_actions.hpp"
#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>
#include <string>

namespace rr {
GamepadPtr open_first_gamepad() {
    int count = 0;
    SDL_JoystickID* ids = SDL_GetGamepads(&count);
    GamepadPtr pad;
    for (int i = 0; ids && i < count && !pad; ++i) pad.reset(SDL_OpenGamepad(ids[i]));
    SDL_free(ids);
    return pad;
}

// Demo/smoke driver for --autopilot (native, not the original AI). Picks a
// line clear of traffic and riders (overtaking only when oncoming cars leave
// time), follows a blocking car's speed when no line is clear, eases off for
// bends ahead and lets go of every control while crashed: the crash walk back
// to the bike (0x0040a600 state 38) only converges with no input, see
// steer_into_traffic.
uint32_t autopilot(const Race& race, const TrafficSystem* traffic) {
    if (race.crashed()) return 0;
    const auto& route = race.route();
    const auto& bike = race.bike();
    const int32_t speed = race.speed();
    const size_t here = race.position() >> 8, last = route.size() - 1;
    const size_t aim = std::min(last, here + 3 + size_t(std::max(0, speed) / 1500));
    const auto& s = route[aim];
    // Lateral positions relative to the road centre of their own sample.
    const int32_t me = race.lateral() - route[here].offset;
    // Candidate lines every 62 units across the narrowest road between here
    // and the aim point, plus lane centres and lane lines (250-unit lanes);
    // the first right-hand lane first, then the closest line. A car is touched
    // within 94 lateral units (car contact box dl < 24064 Q8), so a line 120
    // from every car is clear: the gap between two lanes' traffic counts.
    int32_t road_left = s.path.left, road_right = s.path.right;
    for (size_t i = here; i <= aim; ++i) {
        road_left = std::min(road_left, route[i].path.left);
        road_right = std::min(road_right, route[i].path.right);
    }
    std::vector<int32_t> lanes;
    for (int32_t x = -road_left + 40; x <= road_right - 40; x += 62) lanes.push_back(x);
    for (int32_t x = -road_left + 125; x <= road_right - 125; x += 125)
        lanes.push_back(x); // centres and lines
    if (lanes.empty()) lanes.push_back(0);
    const int32_t home = road_right >= 250 ? 125 : road_right / 2; // first right-hand lane
    std::stable_sort(lanes.begin(), lanes.end(), [&](int32_t a, int32_t b) {
        if ((a == home) != (b == home)) return a == home;
        return std::abs(a - me) < std::abs(b - me);
    });
    // Traffic within reach; speeds become Q8 samples per tick (speed/260,
    // measured: 10000 covers 0.15 samples a tick). Oncoming cars (lane < 0)
    // carry a negative velocity.
    struct Obstacle {
        int32_t x, gap;
        double velocity;
    };
    std::vector<Obstacle> obstacles;
    if (traffic)
        for (const auto& car : traffic->cars()) {
            if (!car.active) continue;
            const int64_t gap = int64_t(car.position) - int64_t(race.position());
            if (gap < -128 || gap > 64 * 256) continue;
            const size_t at = std::min(last, size_t(std::max<int64_t>(0, car.position >> 8)));
            obstacles.push_back({(car.lateral >> 8) - route[at].offset, int32_t(gap),
                                 (car.lane < 0 ? -car.speed : car.speed) / 260.0});
        }
    // Other riders on this roadway: contact shoves the bike sideways.
    for (size_t i = 1; i < race.riders().size(); ++i) {
        const auto& rider = race.riders()[i];
        if (rider.route != race.riders()[0].route) continue; // crashed bikes still block
        const int64_t gap = int64_t(rider.bike.i32(0x1c)) - int64_t(race.position());
        if (gap < -128 || gap > 64 * 256) continue;
        const size_t at = std::min(last, size_t(std::max<int64_t>(0, rider.bike.i32(0x1c) >> 8)));
        obstacles.push_back(
            {(rider.bike.i32(0x28) >> 8) - route[at].offset, int32_t(gap), rider.bike.i32(0xec) / 260.0});
    }
    const double pass_velocity = std::max(speed, 7000) / 260.0;
    // Nearest same-direction obstacle in a line within 20 samples.
    auto ahead_in = [&](int32_t lane) -> const Obstacle* {
        const Obstacle* best = nullptr;
        for (const auto& o : obstacles)
            if (o.velocity >= 0 && o.gap > -128 && o.gap < 20 * 256 && std::abs(o.x - lane) < 120 &&
                (!best || o.gap < best->gap))
                best = &o;
        return best;
    };
    const Obstacle* blocker = ahead_in(lanes.front());
    double need = 90; // ticks to change line, plus to get past the blocker
    if (blocker) need += (blocker->gap + 3 * 256) / std::max(1.0, pass_velocity - blocker->velocity);
    auto clear = [&](int32_t lane) {
        if (ahead_in(lane)) return false;
        for (const auto& o : obstacles) {
            if (o.velocity >= 0 || std::abs(o.x - lane) >= 120) continue;
            if (o.gap < 128) return false; // alongside
            if (o.gap / (pass_velocity - o.velocity) < need * 1.2 + 40)
                return false; // meets before the pass ends
        }
        return true;
    };
    int32_t target = lanes.front(), limit = 1 << 30;
    bool found = false;
    for (const int32_t lane : lanes)
        if (clear(lane)) {
            target = lane;
            found = true;
            break;
        }
    if (!found && blocker && blocker->gap > 64 && blocker->gap < 14 * 256) // follow it until a lane opens
        limit = int32_t(std::max(0.0, blocker->velocity) * 260) + 300;
    // Threading close past a car: keep the closing speed modest.
    for (const auto& o : obstacles)
        if (o.gap > -64 && o.gap < 8 * 256 && std::abs(o.x - target) < 160)
            limit = std::min(limit, std::max(6000, int32_t(std::max(0.0, o.velocity) * 260) + 3000));
    // Zero-width road data (CITY's route tail past sample 5456 until plan 17's
    // race bounds land): the wall stand-in crashes above 4000, so crawl the centre.
    if (road_left + road_right < 100) {
        target = 0;
        limit = std::min(limit, 3500);
    }
    // Bend severity ahead (path bend per sample) sets a speed ceiling.
    int32_t bend = 0;
    for (size_t i = here; i <= std::min(last, here + 12); ++i)
        bend = std::max(bend, std::abs(route[i].path.bend));
    // move() pushes the bike outward by about bend x distance a tick against
    // a fixed steering authority, so the sustainable speed goes as 1/bend.
    if (bend > 0) limit = std::min(limit, std::clamp(200000 / bend, 4500, 13500));
    // Steer on the lateral position 16 ticks on (+0x28 Q8 plus the +0x10c
    // lateral velocity, as move() integrates it) so the bike does not overshoot.
    // The target is relative to the centre of the sample aimed at: through a
    // split the road centre (offset) moves quickly under the bike.
    const int32_t predicted = int32_t((int64_t(bike.i32(0x28)) + int64_t(bike.i32(0x10c)) * 16) >> 8);
    const int32_t error = s.offset + target - predicted;
    uint32_t c = 0;
    if (error > 20)
        c |= rr::control_right;
    else if (error < -20)
        c |= rr::control_left;
    if (speed > limit + 800)
        c |= rr::control_brake;
    else if (speed < std::min(3000, limit) || (speed < limit && std::abs(target - me) < 500))
        c |= control_accelerate;
    // Start a swing only beside a live rider, not on a periodic timer.
    // Use fight_reach's longitudinal, lateral and height limits. Native
    // combat owns swing completion and resolves whether the hit lands.
    const auto& player = race.riders()[0];
    if (!player.finished && player.attack.timer == 0) {
        const int reach = weapon_reach[std::clamp(player.weapon, 0, 3)] * 0x1000;
        for (size_t i = 1; i < race.riders().size(); ++i) {
            const auto& other = race.riders()[i];
            if (other.route != player.route || other.parked || !other.live || other.finished ||
                other.crash_active || other.half_detached || other.bike.i32(0x2f4) != 0)
                continue;
            if (std::abs(int64_t(other.bike.i32(0x1c)) - bike.i32(0x1c)) < 0x40 &&
                std::abs(int64_t(other.bike.i32(0x28)) - bike.i32(0x28)) < reach &&
                std::abs(int64_t(other.bike.i32(0x20)) - bike.i32(0x20)) < 0x3000) {
                c |= control_punch;
                break;
            }
        }
    }
    return c;
}

// Riding frame from the steering state. Frames 4..11 of HIBOB.DAT are the lean sequence
// (4 strongest right, 11 strongest left) and 3 is straight; the original picks frames through
// its RRAN/RRFD animation system (0x00445a60, 0x0043ae90). Napa uses ActorNodes;
// these inspected frame helpers remain for the other courses.
// Attack frames picked by inspection of HIBOB.DAT (wind-up, strike) per weapon and side.
int attack_frame(const Rider& r) {
    const bool strike = r.attack.timer >= attack_legacy_hit_tick;
    switch (r.attack.weapon) {
    case 1:
        return r.attack.right ? (strike ? 143 : 141) : (strike ? 142 : 140); // club
    case 2:
        return r.attack.right ? (strike ? 102 : 101) : (strike ? 106 : 105); // chain
    case 3:
        return r.attack.right ? 60 : 66; // kick
    default:
        return r.attack.right ? (strike ? 45 : 44) : (strike ? 52 : 51); // fists
    }
}

int riding_frame(const Bike& bike) {
    const int32_t steer = bike.i32(0x288), limit = 0x2e00;
    const int level = std::abs(steer) * 8 / limit; // 0..8
    if (level < 1) return 3;
    if (steer > 0) return level < 3 ? 6 : (level < 6 ? 5 : 4);
    return level < 3 ? 8 : (level < 5 ? 9 : (level < 7 ? 10 : 11));
}

// Player bike: original sprite, horizontal position projected at the bike's depth, bottom of the view.
void draw_bike(SDL_Renderer* r, const rr::Game& g, SpriteCache& cache, const std::vector<RoadSlice>& slices) {
    const Race& race = g.race();
    const auto& me = race.riders()[0];
    if (g.assets.actors) return;
    const int frame = race.crashed() ? 21 : me.attack.timer ? attack_frame(me) : riding_frame(race.bike());
    SDL_Texture* t = cache.get(r, g, frame, 0);
    if (!t) return;
    float w, h;
    SDL_GetTextureSize(t, &w, &h);
    const uint32_t sample = race.position() >> 8;
    if (slices.size() < 2 || sample < slices.front().sample || sample >= slices.back().sample) return;
    const size_t at = sample - slices.front().sample;
    const auto& a = slices[at];
    const auto& b = slices[at + 1];
    const float fraction = float(race.position() & 255) / 256;
    const auto& pa = a.road[a.own];
    const auto& pb = b.road[b.own];
    const float base = (float(pa.screen_y) + (float(pb.screen_y) - float(pa.screen_y)) * fraction) / 32768;
    const int lateral = race.lateral() - race.route()[sample].offset;
    const float x = (float(a.screen_x(a.own, lateral)) +
                     (float(b.screen_x(b.own, lateral)) - float(a.screen_x(a.own, lateral))) * fraction) /
                    65536;
    const float size = (float(a.scale) + (float(b.scale) - float(a.scale)) * fraction) / 32768;
    const SDL_FRect dst{x * 2 - w * size / 2, base - h * size, w * size, h * size};
    SDL_RenderTexture(r, t, nullptr, &dst);
}

// Opponents: positioned on the projected road at their sample (interpolated within it) and scaled
// by depth relative to depth 512, capped at the player sprite's size and baseline so riders level
// with the player match it. The original sprite scale law, raster and sort order are plan 03 work.
void draw_opponents(SDL_Renderer* r, const rr::Game& g, SpriteCache& cache,
                    const std::vector<RoadSlice>& slices) {
    const Race& race = g.race();
    struct Item {
        int32_t depth;
        float x, y, scale;
        int frame, colour;
    };
    std::vector<Item> items;
    for (size_t i = 1; i < race.riders().size(); ++i) {
        const Rider& rd = race.riders()[i];
        if (g.assets.actors) continue;
        const uint32_t pos = uint32_t(rd.bike.i32(0x1c));
        if (pos >> 8 < slices.front().sample || pos >> 8 >= slices.back().sample) continue;
        const size_t n = (pos >> 8) - slices.front().sample;
        const auto& a = slices[n];
        const auto& b = slices[n + 1];
        const auto& sample = race.route_of(rd)[pos >> 8];
        const int half = (a.count == 2 && b.count == 2) ? (rd.route == 1 ? 1 : 0) : 0;
        const int32_t lateral = (rd.bike.i32(0x28) >> 8) - sample.offset;
        const float f = float(pos & 0xff) / 256.f;
        const float xa = float(a.screen_x(half, lateral)) / 65536.f,
                    xb = float(b.screen_x(half, lateral)) / 65536.f;
        const float ya = float(a.road[half].screen_y) / 65536.f, yb = float(b.road[half].screen_y) / 65536.f;
        const float scale = (float(a.scale) + (float(b.scale) - float(a.scale)) * f) / 32768.f;
        const int frame = rd.bike.i32(0x2f4) ? 21
                          : rd.attack.timer  ? attack_frame(rd)
                                             : riding_frame(rd.bike);
        items.push_back({a.depth, xa + (xb - xa) * f, ya + (yb - ya) * f, scale, frame, rd.colour});
    }
    std::sort(items.begin(), items.end(), [](const Item& p, const Item& q) { return p.depth > q.depth; });
    const SDL_Rect clip{0, 0, 640, 480};
    SDL_SetRenderClipRect(r, &clip);
    for (const auto& it : items) {
        SDL_Texture* t = cache.get(r, g, it.frame, it.colour);
        if (!t) continue;
        float w, h;
        SDL_GetTextureSize(t, &w, &h);
        // Capped at the player sprite's size and baseline (see the comment above draw_opponents).
        const float s = std::min(it.scale, 1.f);
        const float base = std::min(it.y * 2, 476.f);
        const SDL_FRect dst{it.x * 2 - w * s / 2, base - h * s, w * s, h * s};
        SDL_RenderTexture(r, t, nullptr, &dst);
    }
    SDL_SetRenderClipRect(r, nullptr);
}

void draw_hud(SDL_Renderer* r, const rr::Game& g) {
    const Race& race = g.race();
    char line[160];
    const int seconds = race.ticks() / rr::ticks_per_second;
    const int progress = int(uint64_t(race.position() >> 8) * 100 / (race.route().size() - 1));
    SDL_SetRenderDrawColor(r, 255, 255, 255, 255);
    SDL_snprintf(line, sizeof line, "%s  PLACE %d/%zu  SPEED %3d  GEAR %d  TIME %d:%02d  %3d%%",
                 g.course_name.c_str(), race.place(0), race.riders().size(), race.speed() / 100,
                 race.gear() + 1, seconds / 60, seconds % 60, progress);
    if (!g.assets.hud) SDL_RenderDebugText(r, 8, 8, line);
    // The original's only single-player in-race banner is the countdown
    // digit (hud.hpp CountdownBanner), drawn by CourseCanvas with the
    // dashboard. DAT_004c5bd0 is otherwise written only by the pause message
    // (0x004431d0) and the network race end (0x00416010): there is no wipeout
    // text and no finish text in a career race. The debug-text digit below
    // stands in only when no course scene or dashboard is loaded.
    const auto banner = countdown_banner(g.countdown, rr::countdown_ticks);
    if (!banner.text.empty() && (!g.scene() || !g.assets.hud)) {
        SDL_SetRenderScale(r, 6, 6);
        SDL_RenderDebugText(r, 320.f / 6 - 4, 20, banner.text.c_str());
        SDL_SetRenderScale(r, 1, 1);
    } else if (banner.text.empty() && race.finished() && !g.career) {
        // Native quick-race shell (no results screen): finish and restart prompt.
        const int finish = race.riders()[0].finish_tick / rr::ticks_per_second;
        SDL_snprintf(line, sizeof line, "FINISHED %d%s  %d:%02d", race.place(0),
                     race.place(0) == 1   ? "ST"
                     : race.place(0) == 2 ? "ND"
                     : race.place(0) == 3 ? "RD"
                                          : "TH",
                     finish / 60, finish % 60);
        SDL_SetRenderScale(r, 3, 3);
        SDL_RenderDebugText(r, 50, 60, line);
        SDL_SetRenderScale(r, 1, 1);
        SDL_RenderDebugText(r, 200, 260, "Enter / A / tap: race again");
    }
    // Stamina bars: player and the rider it last hit.
    auto bar = [&](float y, const char* label, const rr::RiderStats& st) {
        SDL_SetRenderDrawColor(r, 255, 255, 255, 255);
        SDL_RenderDebugText(r, 8, y, label);
        const SDL_FRect back{72, y - 1, 120, 9},
            fill{72, y - 1, 120.f * float(st.current) / float(std::max(1, st.base)), 9};
        SDL_SetRenderDrawColor(r, 40, 40, 40, 255);
        SDL_RenderFillRect(r, &back);
        SDL_SetRenderDrawColor(r, 220, 60, 40, 255);
        SDL_RenderFillRect(r, &fill);
    };
    if (!g.assets.hud) {
        bar(24, "YOU", race.riders()[0].stamina);
        if (race.last_target() > 0) bar(36, "THEM", race.riders()[size_t(race.last_target())].stamina);
    }
}

// One race: the playable loop. In career mode the race is decided by the player finishing
// (0x00434660: code 0x7f), wrecking or quitting (code 5), and runs on for 0xb4 ticks.
// Plan 18 Task 8: capture the presented race frame into a 640x480 menu
// background (box-sample down from the window surface).
RgbImage capture_race_frame(SDL_Renderer* renderer) {
    rr::RgbImage frame(640, 480, 0);
    rr::SurfacePtr raw(SDL_RenderReadPixels(renderer, nullptr));
    if (!raw) return frame;
    rr::SurfacePtr s(SDL_ConvertSurface(raw.get(), SDL_PIXELFORMAT_XRGB8888));
    if (!s) return frame;
    const auto* px = static_cast<const uint32_t*>(s->pixels);
    const int sw = s->w, sh = s->h, pitch = s->pitch / 4;
    for (int y = 0; y < 480; ++y) {
        const int y0 = y * sh / 480, y1 = (y + 1) * sh / 480;
        for (int x = 0; x < 640; ++x) {
            const int x0 = x * sw / 640, x1 = (x + 1) * sw / 640;
            uint64_t r = 0, gg = 0, b = 0, n = 0;
            for (int sy = y0; sy < std::max(y0 + 1, y1); ++sy)
                for (int sx = x0; sx < std::max(x0 + 1, x1); ++sx) {
                    const uint32_t p = px[size_t(sy) * size_t(pitch) + size_t(sx)];
                    r += (p >> 16) & 0xff;
                    gg += (p >> 8) & 0xff;
                    b += p & 0xff;
                    ++n;
                }
            frame.at(x, y) = uint32_t(((r / n) << 16) | ((gg / n) << 8) | (b / n));
        }
    }
    return frame;
}

// Plan 18 Task 8: while the pause menu is open it owns all input, with the
// menu loop's mapping (arrows + Enter/Escape, d-pad + A/B, pointer/touch).
void pause_event(Frontend* menu, const SDL_Event& e, GamepadPtr& pad, SDL_Renderer* renderer) {
    MenuInput in;
    if (e.type == SDL_EVENT_GAMEPAD_ADDED && !pad) {
        pad.reset(SDL_OpenGamepad(e.gdevice.which));
        return;
    }
    if (e.type == SDL_EVENT_GAMEPAD_REMOVED && pad && SDL_GetGamepadID(pad.get()) == e.gdevice.which) {
        pad.reset();
        return;
    }
    if (e.type == SDL_EVENT_KEY_DOWN) {
        if (!e.key.repeat && e.key.scancode > 0 && e.key.scancode < SDL_SCANCODE_COUNT)
            in.capture_scancode = int(e.key.scancode);
        switch (e.key.key) {
        case SDLK_LEFT:
            in.left = true;
            break;
        case SDLK_RIGHT:
            in.right = true;
            break;
        case SDLK_UP:
            in.up = true;
            break;
        case SDLK_DOWN:
            in.down = true;
            break;
        case SDLK_HOME:
            in.home = true;
            break;
        case SDLK_END:
            in.end = true;
            break;
        case SDLK_RETURN:
        case SDLK_KP_ENTER:
        case SDLK_SPACE:
            if (!e.key.repeat) in.select = true;
            break;
        case SDLK_ESCAPE:
        case SDLK_P:
        case SDLK_AC_BACK:
            if (!e.key.repeat) in.back = true;
            break;
        default:
            break;
        }
    } else if (e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN && pad &&
               SDL_GetGamepadID(pad.get()) == e.gbutton.which) {
        switch (e.gbutton.button) {
        case SDL_GAMEPAD_BUTTON_DPAD_LEFT:
            in.left = true;
            break;
        case SDL_GAMEPAD_BUTTON_DPAD_RIGHT:
            in.right = true;
            break;
        case SDL_GAMEPAD_BUTTON_DPAD_UP:
            in.up = true;
            break;
        case SDL_GAMEPAD_BUTTON_DPAD_DOWN:
            in.down = true;
            break;
        case SDL_GAMEPAD_BUTTON_SOUTH:
        case SDL_GAMEPAD_BUTTON_START:
            in.select = true;
            break;
        case SDL_GAMEPAD_BUTTON_EAST:
        case SDL_GAMEPAD_BUTTON_BACK:
            in.back = true;
            break;
        default:
            break;
        }
    } else if (e.type == SDL_EVENT_MOUSE_MOTION || e.type == SDL_EVENT_MOUSE_BUTTON_DOWN ||
               e.type == SDL_EVENT_FINGER_DOWN) {
        float x = 0, y = 0;
        if (e.type == SDL_EVENT_FINGER_DOWN) {
            int w = 0, h = 0;
            SDL_Window* window = SDL_GetRenderWindow(renderer);
            if (window) SDL_GetWindowSize(window, &w, &h);
            SDL_RenderCoordinatesFromWindow(renderer, e.tfinger.x * float(w), e.tfinger.y * float(h), &x, &y);
        } else if (e.type == SDL_EVENT_MOUSE_MOTION) {
            if (e.motion.which == SDL_TOUCH_MOUSEID) return;
            SDL_RenderCoordinatesFromWindow(renderer, e.motion.x, e.motion.y, &x, &y);
        } else {
            if (e.button.which == SDL_TOUCH_MOUSEID) return;
            SDL_RenderCoordinatesFromWindow(renderer, e.button.x, e.button.y, &x, &y);
        }
        in.pointer_x = int(x);
        in.pointer_y = int(y);
        in.pointer_moved = true;
        if (e.type != SDL_EVENT_MOUSE_MOTION) in.pointer_click = true;
    } else
        return;
    menu->update(in, SDL_GetTicks());
}
} // namespace rr
