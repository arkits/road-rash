#pragma once
#include "assets.hpp"
#include <array>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>
#include "framebuffer.hpp"
#include "race.hpp"
#include "text.hpp"
namespace rr {
struct HudState {
    int place = 1, distance = 0, speed_angle = 0, rpm_angle = 0;
    int stamina = 0, condition = 0, target_stamina = 0;
    std::string player, target;
    // DAT_004b8a34: signed route gap to the target (Q8 samples, +0x1f8
    // units); > 0 the target is behind, <= 0 ahead. Drawn only with a target.
    int32_t target_gap = 0;
    // MiniDash numeric speed (0x004468e0), independent of travelled distance.
    // Appended to preserve existing aggregate initializer field positions.
    int speed = 0;
};
// 0x00438550, run every frame from 0x00438450: the opponent the dashboard
// names. d = player.+0x1f8 - opponent.+0x1f8 over every opponent slot (no
// crash/finish filter); +0x1f8 is the Q8 distance still to the finish
// (0x00407410). The nearest behind wins only when behind*2 < ahead
// (0x00438550, strict); gap > 0 behind, <= 0 ahead. rider is -1 when there
// is no target (no opponent ahead and none close enough behind, or no
// opponents at all); the gap is then 0. The original leaves the stale
// -0x20000000 in DAT_004b8a34 with a null target, which no draw path reads.
struct NearestOpponent {
    int rider = -1;
    int32_t gap = 0;
};
NearestOpponent nearest_opponent(const Race&);
// MiniDash 0x00467a98 (F10 cycles 0->1->2->0 in 0x004431d0): 0 full dashboard,
// 1 mini (numeric speed, no needles/name bars), 2 off (0x00446c30 draws nothing).
enum class DashMode : int { full = 0, mini = 1, off = 2 };
// Original full high-resolution dashboard. Geometry and indexed artwork come
// from the imported executable/disc; text and pen rasterization use native code.
// Needle pen: CreatePen(PS_SOLID, HiRes?3:2, 0x81172) cached per HiRes slot,
// MoveToEx/LineTo in 0x00446c30 (speed needle first, rpm second, so the rpm
// needle is on top). Native selection matches the width (3 high, 2 low) and
// the raw COLORREF 0x081172 = RGB 0x721108 through the per-frame palette
// path (never a hard-coded 8-bit index: the realized index animates 37/52).
// The band/cap SHAPE is still the native block Bresenham (a Windows GDI
// reference raster or a parked-needle capture is needed to settle it;
// needle-trace handoff 20261015 §7): full-width band at high resolution,
// 2-wide band at low resolution, both scaled by the compose scale.
// Review 2026-10-13 item 2: exact memo for the per-frame dashboard map.
// Same nearest-distance metric as the old inline search, memoized per exact
// color (a cell table would quantize: two colors in one RGB555 cell can map
// to different indices). Entries persist across frames; the caller resets on
// palette change. Cleared maps keep capacity: no per-frame allocation.
class DashLut {
  public:
    uint8_t index(uint32_t c, const Palette& pal);
    void reset() { memo_.clear(); }
    size_t size() const { return memo_.size(); }

  private:
    std::unordered_map<uint32_t, uint8_t> memo_;
};
// Needle pen selected per frame from the resolution (0x00446c30:
// CreatePen(PS_SOLID, HiRes+2, 0x81172)). width is the GDI band width in
// device pixels; color is the raw COLORREF as RGB (0x081172 -> 0x721108).
struct NeedlePen {
    int width;
    uint32_t color;
};
// Pre-race countdown presentation, from DAT_004753c0 (ticks left). 0x00442dd0
// sets it to 0xb4 (0x168 in network play); 0x00442e10 decrements it by the
// frame's steps and runs no simulation (0x0044b2e0) while it is positive; on
// expiry it uploads the full palette DAT_004b8490. Until then the race shows
// the half-brightness copy DAT_004b8030 (0x00448f90: every channel >> 1) that
// 0x004163d0 fades in. 0x0044c780 skips the whole dashboard draw 0x00446c30
// while the timer still equals its start value (the frame before the first
// step). Inside 0x00446c30 the countdown block is outside the MiniDash != 2
// test, so it shows with the dashboard off as well: sprintf("%d", t/60 + 1)
// into DAT_004c5bd0, drawn with the big Badloc face (0x00442990: 0x80 high,
// 0x48 low resolution) by 0x00447b20 with DrawText flags 0x4851.
struct CountdownBanner {
    bool dashboard = true; // 0x0044c780: countdown != start value
    bool dim = false;      // half-brightness palette still selected
    std::string text;      // "3", "2", "1"; empty once the race is live
};
CountdownBanner countdown_banner(int countdown, int start);
// Race palette presentation as a per-channel map from the full palette
// (DAT_004b8490) value to the uploaded value. half_palette_lut is
// DAT_004b8030 (0x00448f90: >> 1).
using ChannelLut = std::array<uint8_t, 256>;
ChannelLut identity_lut();
ChannelLut half_palette_lut();
// 0x00415f70 runs a fade worker from timeSetEvent(14 ms, periodic) and
// blocks until it releases its semaphore on the 64th callback; the race
// loop's clock (0x004163b0) starts afterwards. 63 uploads happen first.
constexpr int palette_fade_uploads = 63;
constexpr uint64_t palette_fade_period_ns = 14'000'000;
// Race-start fade-in 0x00415d90: table 0x467860 = {1.0, 0.96^1 .. 0.96^63}
// (iterated double products by 0x462000 = 0.96); upload n (counter 63..1)
// is trunc(half * table[n - 1]), so the last one is the half palette.
// uploads = callbacks so far (0..63); 0 shows black (the loading screen's
// palette before the first upload is not modelled).
ChannelLut fade_in_lut(int uploads);
// Race-end fade-out 0x00415e90: copy the half palette, then every callback
// x = trunc(x * 0.96) in place (cumulative) and upload. uploads 0 leaves
// the full palette in place.
ChannelLut fade_out_lut(int uploads);
class Hud {
  public:
    Hud(const Assets&, const std::vector<uint8_t>& exe, int bike, bool metric);
    // rider_names is indexed like Race::riders(); the target is the
    // nearest opponent (0x00446b80), not the last rider hit.
    HudState state(const Race&, const std::string& player, const std::vector<std::string>& rider_names) const;
    // Pen for the tach/speedo needles at the current resolution: width
    // HiRes?3:2, color 0x721108 (0x00446c30 pen cache slot HiRes).
    NeedlePen pen() const;
    static NeedlePen needle_pen(bool high_res);
    RgbImage compose(const HudState&, const PhysicsTables&, DashMode mode = DashMode::full,
                     int scale = 1) const;
    // 0x00446c30 countdown block into a compose() image (640x480 x scale).
    // Rect {view left, view top + 0x78 (120 high, 60 low), view right, view
    // bottom}; 0x00447b20 draws black at the rect, then white at -2,-2
    // (DAT_0049e120 = HiRes + 1). High resolution: Badloc 0x80, +120,
    // shadow 2. Low mode: Badloc 0x48, +60, shadow 1 in 320x240 space,
    // pixel-doubled like the low dashboard art. Empty text draws nothing.
    void draw_countdown(RgbImage&, const std::string& text, int scale = 1) const;
    // 0x004431d0 in-race message (DAT_004753b8 set): the same 0x00446c30
    // block draws DAT_004c5bd0 with the face already selected for the
    // dashboard, small Badloc DAT_004c1210 (0x20 high / 0x12 low): string
    // 316 quit prompt, 317 "Game paused.". Text is Windows-1252.
    void draw_message(RgbImage&, const std::string& text, int scale = 1) const;
    // Low-mode artwork: 320-wide KLODASH/MLODASH dashboard + 95-stride LILMETER,
    // selected in low resolution (0x004457f0 choice 1/2); MiniDash picks the stride.
    void set_low_mode(bool low);
    static int meter_level(int current, int base);
    static int numeric_speed(int32_t velocity, bool metric);
    static int cycle_mini_dash(int mode); // 0->1->2->0, as the F10 pump does
  private:
    void draw_banner(RgbImage&, const std::string& text, bool big, int scale) const;
    bool metric_;
    bool low_ = false;
    Palette palette_;
    TextRenderer text_;
    std::vector<uint8_t> dashboard_, dashboard_low_, meter_, meter_low_;
    std::array<int32_t, 55> layout_{};
    std::array<int32_t, 55> layout_mini_{};
};
} // namespace rr
