#include "hud.hpp"
#include <cstdlib>
#include "exe_tables.hpp"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
namespace rr {
namespace {
constexpr uint32_t transparent = 0x01000000;
int32_t high_product(int32_t a, int32_t b) {
    return int32_t((int64_t(a) * b) >> 32);
}
// Native stand-in for the GDI pen stroke: a Bresenham spine stamped with a
// band x band block per step. band is the full GDI width in destination
// pixels (pen width x compose scale): 3 -> centered 3-wide (exact old
// behavior), 2 -> 2-wide anchored at the spine pixel (low-resolution
// approximation; the exact GDI band/cap shape is still open per the
// needle-trace handoff §7).
void line(RgbImage& out, int x, int y, int end_x, int end_y, uint32_t color, int band = 3) {
    if (band < 1) band = 1;
    const int lo = -(band / 2), hi = (band - 1) / 2;
    const int dx = std::abs(end_x - x), sx = x < end_x ? 1 : -1, dy = -std::abs(end_y - y),
              sy = y < end_y ? 1 : -1;
    int error = dx + dy;
    for (int guard = 0; guard < 2048; ++guard) {
        for (int yy = y + lo; yy <= y + hi; ++yy)
            for (int xx = x + lo; xx <= x + hi; ++xx)
                if (out.contains(xx, yy)) out.at(xx, yy) = color;
        if (x == end_x && y == end_y) return;
        const int twice = 2 * error;
        if (twice >= dy) {
            error += dy;
            x += sx;
        }
        if (twice <= dx) {
            error += dx;
            y += sy;
        }
    }
}
} // namespace
Hud::Hud(const Assets& assets, const std::vector<uint8_t>& exe, int bike, bool metric)
    : metric_(metric), palette_(Palette::from_raw(assets.read("DATA/PALETTE.RAW"))), text_(assets) {
    if (bike < 0 || bike >= 15) throw std::runtime_error("Invalid dashboard bike");
    const std::string prefix = bike < 5 ? "RAT" : bike < 10 ? "SUPER" : "SPORT";
    const std::string dash = metric ? "KHIDASH.BOB" : "MHIDASH.BOB",
                      dash_low = metric ? "KLODASH.BOB" : "MLODASH.BOB";
    dashboard_ = assets.read("IMAGES/BIKES/" + prefix + "/" + dash);
    dashboard_low_ = assets.read("IMAGES/BIKES/" + prefix + "/" + dash_low);
    meter_ = assets.read("IMAGES/BIGMETER.BOB");
    meter_low_ = assets.read("IMAGES/LILMETER.BOB");
    // 0x0044c5f0 returns bytes-read / width; 122 is a maximum,
    // while the RAT artwork on this disc contains 119 complete rows.
    // Low dashboards are 320-wide (RAT/SUPER 60 rows, SPORT 61); LILMETER is
    // the 95-stride half of the 190-stride BIGMETER pair (0x004457f0 low path).
    if (dashboard_.empty() || dashboard_.size() % 640 || dashboard_.size() > 640 * 122 ||
        meter_.size() != 190 * 64)
        throw std::runtime_error("Invalid dashboard artwork");
    if (dashboard_low_.empty() || dashboard_low_.size() % 320 || meter_low_.size() != 95 * 32)
        throw std::runtime_error("Invalid low-mode dashboard artwork");
    const auto read_stride = [&](int index) {
        const auto data = read_exe_data(exe, 0x00474f50 + uint32_t(index) * 0xdc, 0xdc);
        std::array<int32_t, 55> out{};
        for (size_t i = 0; i < out.size(); ++i) {
            uint32_t v = 0;
            for (int j = 0; j < 4; ++j) v |= uint32_t(data[i * 4 + j]) << (8 * j);
            std::memcpy(&out[i], &v, 4);
        }
        return out;
    };
    // 0x00446c30 indexes layout (MiniDash + HiRes*2)*0xdc at 0x00474f50:
    // strides 0/1 are the low (320-wide) full/mini rows, 2/3 the high rows.
    layout_ = read_stride(2);
    layout_mini_ = read_stride(3);
}
CountdownBanner countdown_banner(int countdown, int start) {
    CountdownBanner b;
    b.dashboard = countdown != start;
    b.dim = countdown > 0;
    // DAT_004753c0 / 0x3c + 1 with "%d" (DAT_00466654): 179..120 -> "3",
    // 119..60 -> "2", 59..1 -> "1"; the start value (180, "4") is never drawn.
    if (countdown > 0) b.text = std::to_string(countdown / 60 + 1);
    return b;
}
void Hud::draw_countdown(RgbImage& image, const std::string& text, int scale) const {
    draw_banner(image, text, true, scale);
}
void Hud::draw_message(RgbImage& image, const std::string& text, int scale) const {
    draw_banner(image, text, false, scale);
}
void Hud::draw_banner(RgbImage& image, const std::string& text, bool big, int scale) const {
    if (text.empty() || scale < 1) return;
    // Fullscreen view origin 0,0 (DAT_00475394/98), size 640x480 or 320x240
    // (DAT_0047539c/a0); the rect top moves down 0x78 >> (1 - HiRes).
    // Faces from 0x00442990: big DAT_004c11ec 0x80/0x48, small DAT_004c1210
    // 0x20/0x12.
    const int w = low_ ? 320 : 640, h = low_ ? 240 : 480, shadow = low_ ? 1 : 2;
    const int height = big ? (low_ ? 0x48 : 0x80) : (low_ ? 0x12 : 0x20);
    RgbImage local(low_ ? w * scale : 0, low_ ? h * scale : 0, transparent);
    RgbImage& target = low_ ? local : image;
    TextRect rect{0, (0x78 >> (low_ ? 1 : 0)) * scale, w * scale, h * scale};
    text_.draw_box(&target, Face::badloc, height * scale, rect, 0, text, 0x4851);
    TextRect white{rect.left - shadow * scale, rect.top - shadow * scale, rect.right - shadow * scale,
                   rect.bottom - shadow * scale};
    text_.draw_box(&target, Face::badloc, height * scale, white, 0xffffff, text, 0x4851);
    if (!low_) return;
    for (int y = 0; y < local.height; ++y)
        for (int x = 0; x < local.width; ++x) {
            const uint32_t c = local.at(x, y);
            if (c == transparent) continue;
            for (int yy = 0; yy < 2; ++yy)
                for (int xx = 0; xx < 2; ++xx)
                    if (image.contains(2 * x + xx, 2 * y + yy)) image.at(2 * x + xx, 2 * y + yy) = c;
        }
}
ChannelLut identity_lut() {
    ChannelLut lut{};
    for (size_t v = 0; v < lut.size(); ++v) lut[v] = uint8_t(v);
    return lut;
}
ChannelLut half_palette_lut() {
    ChannelLut lut{};
    for (size_t v = 0; v < lut.size(); ++v) lut[v] = uint8_t(v >> 1);
    return lut;
}
ChannelLut fade_in_lut(int uploads) {
    ChannelLut lut{};
    if (uploads <= 0) return lut;
    uploads = std::min(uploads, palette_fade_uploads);
    // 0x00415d90 rebuilds entries 1..63 as table[i] = table[i-1] * 0.96
    // (fldl/fmull/fstpl, double precision) from table[0] = 1.0.
    std::array<double, 64> table{};
    table[0] = 1.0;
    for (size_t i = 1; i < table.size(); ++i) table[i] = table[i - 1] * 0.96;
    const int counter = palette_fade_uploads + 1 - uploads; // 63 on the first upload
    for (size_t v = 0; v < lut.size(); ++v)
        lut[v] = uint8_t(int(double(int(v >> 1)) * table[size_t(counter - 1)]));
    return lut;
}
ChannelLut fade_out_lut(int uploads) {
    if (uploads <= 0) return identity_lut();
    uploads = std::min(uploads, palette_fade_uploads);
    ChannelLut lut = half_palette_lut();
    for (int step = 0; step < uploads; ++step)
        for (auto& x : lut) x = uint8_t(int(double(x) * 0.96));
    return lut;
}
int Hud::cycle_mini_dash(int mode) {
    // F10 key-up in the 0x004431d0 pump: 0->1->2->0.
    if (mode < 0 || mode > 2) return 0;
    return (mode + 1) % 3;
}
void Hud::set_low_mode(bool low) {
    low_ = low;
}
NeedlePen Hud::needle_pen(bool high_res) {
    // 0x00446c30: CreatePen(PS_SOLID, HiRes+2, 0x81172), one cached HPEN per
    // HiRes slot. COLORREF 0x081172 is R 0x72, G 0x11, B 0x08.
    return NeedlePen{high_res ? 3 : 2, 0x721108};
}
NeedlePen Hud::pen() const {
    return needle_pen(!low_);
}
int Hud::numeric_speed(int32_t velocity, bool metric) {
    // 0x00446950..0x004469a4: mask before signed shifts, truncate the
    // metric sum toward zero, then clamp reverse speed. These sums fit i32.
    const int32_t speed =
        metric ? (((velocity & ~0xf00) >> 8) + ((velocity & ~0x78) >> 3) + ((velocity & ~0x1e0) >> 5)) / 10
               : (velocity >> 12) + (velocity >> 7) + (velocity >> 9);
    return std::max(0, int(speed));
}
int Hud::meter_level(int current, int base) {
    // 0x00446b00 / 0x00446b40. The original byte value is 0..31.
    if (current <= 0) return 0;
    if (base <= 1) return 31;
    return std::clamp(int(((int64_t(current) * 2 - 2) * 15) / (base - 1) + 1), 0, 31);
}
NearestOpponent nearest_opponent(const Race& race) {
    const auto& riders = race.riders();
    // +0x1f8 (0x00407410): finish DAT_004b6e58 minus +0x1c once the finish
    // is known; without one (synthetic routes) the walk runs to the route end.
    auto remaining = [&](const Rider& r) {
        const int64_t finish = race.finish_q8() ? race.finish_q8() : int64_t(race.route_of(r).size()) * 256;
        return int32_t(uint32_t(finish - int64_t(r.bike.i32(0x1c))));
    };
    if (riders.empty()) return {};
    const int32_t player = remaining(riders[0]);
    int behind_rider = -1, ahead_rider = -1;
    int32_t behind = 0x20000000, ahead = 0x20000000;
    for (size_t i = 1; i < riders.size(); ++i) {
        const int32_t d = int32_t(uint32_t(player) - uint32_t(remaining(riders[i])));
        if (d < 0) {
            if (-d < behind) {
                behind_rider = int(i);
                behind = -d;
            }
        } else if (d < ahead) {
            ahead_rider = int(i);
            ahead = d;
        }
    }
    // No target (0x00438550 leaves DAT_004b8a30 null): normalize the gap to
    // 0; no draw path reads a gap without a target (0x00446b80 clears the
    // name and meter, the entry-10 gap text is gated on the name).
    if (behind_rider < 0 && ahead_rider < 0) return {};
    if (behind * 2 < ahead) return {behind_rider, behind};
    return {ahead_rider, -ahead};
}
void Hud::draw_gap_arrow(RgbImage& out, int left, int top, bool up, uint32_t color, int scale) {
    // Native stand-in for the 18-pixel Wingdings 0xe9/0xea glyphs: an
    // 11-wide, 6-row head over a 5-wide, 6-row shaft, inside the cell.
    // At scale s every source pixel becomes an s x s block.
    if (scale < 1 || scale > 4) throw std::runtime_error("Gap arrow scale out of range 1..4");
    for (int row = 0; row < 12; ++row) {
        const int r = up ? row : 11 - row; // r: distance from the tip
        const int half = r < 6 ? r : 2;
        for (int x = 5 - half; x <= 5 + half; ++x)
            for (int yy = 0; yy < scale; ++yy)
                for (int xx = 0; xx < scale; ++xx)
                    if (out.contains(left + 3 * scale + x * scale + xx, top + 3 * scale + row * scale + yy))
                        out.at(left + 3 * scale + x * scale + xx, top + 3 * scale + row * scale + yy) = color;
    }
}
HudState Hud::state(const Race& race, const std::string& player,
                    const std::vector<std::string>& rider_names) const {
    HudState s;
    s.player = player;
    s.place = race.place(0);
    const auto& b = race.bike();
    const uint32_t velocity = uint32_t(b.i32(0xec));
    s.speed = numeric_speed(b.i32(0xec), metric_);
    const uint32_t sum = uint32_t(int32_t(velocity & 0xfffff001u) >> 1) + (velocity & 0xffffff80u) * 16 +
                         (velocity & 0xfffffe00u) * 4;
    int32_t signed_sum;
    std::memcpy(&signed_sum, &sum, 4);
    s.speed_angle = signed_sum / 200 - 1024;
    if (s.speed_angle < 0) s.speed_angle += 4096;
    s.rpm_angle = b.i32(0x2dc) / 44 - 1024;
    if (s.rpm_angle < 0) s.rpm_angle += 4096;
    // 0x00446a40 reads +0x44, the per-frame snapshot of +0x1c..+0x2c that
    // 0x00438450 copies into +0x44..+0x54 for objects in the draw window (the
    // player always is). It is the route position: distance travelled in
    // tenths of a mile (33 samples) or km, 0.0 at the grid (sample 50).
    const int32_t position = b.i32(0x1c);
    s.distance = metric_ ? ((position >> 5) - 400) / 165 : ((position >> 8) - 50) / 33;
    s.distance = int(uint8_t(s.distance));
    s.stamina = meter_level(race.riders()[0].stamina.current, race.riders()[0].stamina.base);
    s.condition = meter_level(b.i32(0x2f0), b.i32(0x2ec));
    // 0x00446b80: name (0x00446bf0) and stamina meter of the 0x00438550
    // target; both clear without one. +0x3c current, +0x34 base. The gap
    // rides with the name: no name row means no gap either, so a state with
    // an empty target always carries gap 0 (as compose assumes).
    const auto nearest = nearest_opponent(race);
    if (nearest.rider > 0 && size_t(nearest.rider) < rider_names.size()) {
        const auto& rider = race.riders().at(size_t(nearest.rider));
        s.target = rider_names[size_t(nearest.rider)];
        s.target_stamina = meter_level(rider.stamina.current, rider.stamina.base);
        s.target_gap = nearest.gap;
    }
    return s;
}
RgbImage Hud::compose(const HudState& state, const PhysicsTables& physics, DashMode mode, int scale) const {
    // Plan 19 Task 5: artwork blits nearest-neighbour x scale (plan 20 swaps
    // in upscaled art); text rasterises at height x scale with shadow x scale;
    // needles use the pen width x scale. scale = 1 keeps today's output exactly.
    if (scale < 1 || scale > 4) throw std::runtime_error("HUD scale out of range 1..4");
    RgbImage image(640 * scale, 480 * scale, transparent);
    // 0x00446c30 draws nothing at all when MiniDash == 2 (mode off).
    if (mode == DashMode::off) return image;
    const std::vector<uint8_t>& dash_art = low_ ? dashboard_low_ : dashboard_;
    const std::vector<uint8_t>& meter_art = low_ ? meter_low_ : meter_;
    const int stride = low_ ? 320 : 640, meter_stride = low_ ? 95 : 190;
    const std::array<int32_t, 55>& layout = mode == DashMode::mini ? layout_mini_ : layout_;
    const float lscale = low_ ? 0.5f : 1.f;
    auto blit = [&](const std::vector<uint8_t>& source, int src_stride, int left, int top, int right,
                    int bottom, int x, int y, bool keyed = true) {
        const int dx = low_ ? x / 2 : x, dy = low_ ? y / 2 : y;
        for (int row = top; row < bottom; ++row)
            for (int col = left; col < right; ++col) {
                if (row < 0 || col < 0 || col >= src_stride ||
                    size_t(row) * src_stride + col >= source.size())
                    continue;
                const uint8_t index = source[size_t(row) * src_stride + col];
                if (!index && keyed) continue;
                const auto* rgb = &palette_.rgbx[4 * index];
                const uint32_t pixel = index ? uint32_t(rgb[0]) << 16 | uint32_t(rgb[1]) << 8 | rgb[2] : 0;
                const int dbl = low_ ? 2 : 1;
                for (int yy = 0; yy < dbl * scale; ++yy)
                    for (int xx = 0; xx < dbl * scale; ++xx) {
                        const int px = (dx + col - left) * dbl * scale + xx,
                                  py = (dy + row - top) * dbl * scale + yy;
                        if (image.contains(px, py)) image.at(px, py) = pixel;
                    }
            }
    };
    auto bar = [&](int value, int offset) {
        // 0x0044ca50 (flags 0x10). Vertical meters: full colour column
        // {11,0,62,31}, then the black {11,32,62,63-value} empties it from the top.
        const int type = layout[offset], x = int(layout[offset + 1] * lscale),
                  y = int(layout[offset + 2] * lscale);
        value = std::clamp(value, 0, 31);
        if (!type) {
            blit(meter_art, meter_stride, 62, 16, 126, 34, x, y, false);
            blit(meter_art, meter_stride, 62, 0, 62 + value * 2, 16, x + 1, y + 1, false);
        } else {
            blit(meter_art, meter_stride, 11, 0, 62, 31, x, y, false);
            blit(meter_art, meter_stride, 11, 32, 62, 63 - value, x, y, false);
        }
    };
    // 0x00446880/0x004468e0 skip tach/speedo math when MiniDash is nonzero;
    // in mini mode (1) only the numeric speed readout is drawn, no needles/bars.
    if (mode == DashMode::mini) {
        auto label = [&](const std::string& value, int offset, uint32_t color) {
            TextRect rect{int(layout[offset + 1] * lscale) * scale, int(layout[offset + 2] * lscale) * scale,
                          int(layout[offset + 3] * lscale) * scale, int(layout[offset + 4] * lscale) * scale};
            auto shadow = rect;
            shadow.left += 2 * scale;
            shadow.top += 2 * scale;
            shadow.right += 2 * scale;
            shadow.bottom += 2 * scale;
            text_.draw_box(&image, Face::badloc, 32 * scale, shadow, 0, value, uint32_t(layout[offset]));
            text_.draw_box(&image, Face::badloc, 32 * scale, rect, color, value, uint32_t(layout[offset]));
        };
        char speed[32];
        std::snprintf(speed, sizeof speed, "%d", state.speed);
        label(speed, 21, 0xffffff);
        return image;
    }
    // 0x00446c30 draws the third (opponent) meter whenever the full dashboard
    // is up (mode 0), empty without a target; the artwork has a hole there.
    bar(state.stamina, 40);
    bar(state.condition, 45);
    bar(state.target.empty() ? 0 : state.target_stamina, 50);
    const int dashboard_height = int(dash_art.size() / stride);
    blit(dash_art, stride, 0, 0, stride, dashboard_height, 0, 480 - dashboard_height);
    const auto selected_pen = pen();
    const uint32_t pen_color = selected_pen.color;
    const int pen_band = selected_pen.width * scale;
    auto needle = [&](int angle, int offset) {
        // 0x00446c30 draws the speed needle first and the rpm needle second,
        // both with the selected pen; the rpm needle is on top. Colors are
        // identical so only the width selection is observable here.
        const int32_t sx = sine(physics, uint32_t(angle)), cy = sine(physics, uint32_t(angle + 1024));
        const int x = int(layout[offset] * lscale) * scale, y = int(layout[offset + 1] * lscale) * scale;
        line(image, x, y, x + high_product(sx, int(layout[offset + 2] * lscale)) * scale,
             y - high_product(cy, int(layout[offset + 3] * lscale)) * scale, pen_color, pen_band);
    };
    // 0x00446c30 order: speed (DAT_0049e10c) first, rpm (DAT_0049e108) second.
    needle(state.speed_angle, 21);
    needle(state.rpm_angle, 16);
    auto label = [&](const std::string& value, int offset, uint32_t color) {
        TextRect rect{int(layout[offset + 1] * lscale) * scale, int(layout[offset + 2] * lscale) * scale,
                      int(layout[offset + 3] * lscale) * scale, int(layout[offset + 4] * lscale) * scale};
        auto shadow = rect;
        shadow.left += 2 * scale;
        shadow.top += 2 * scale;
        shadow.right += 2 * scale;
        shadow.bottom += 2 * scale;
        text_.draw_box(&image, Face::badloc, 32 * scale, shadow, 0, value, uint32_t(layout[offset]));
        text_.draw_box(&image, Face::badloc, 32 * scale, rect, color, value, uint32_t(layout[offset]));
    };
    label(state.player, 0, 0xffff00);
    label(std::to_string(state.place), 25, 0xffffff);
    char distance[32];
    std::snprintf(distance, sizeof distance, "%02d.%d", state.distance / 10, state.distance % 10);
    label(distance, 30, 0xffffff);
    if (!state.target.empty()) {
        label(state.target, 5, 0xffffff);
        // 0x00446c30 with a target: gap at layout entry 10 as "%d%c%03d" of
        // (|gap|*1000 >> 4) / (metric ? 3279 : 5280), i.e. miles or km to
        // three places (one sample is 16 feet). Opponent ahead (gap <= 0):
        // COLORREF 0x10e7 and Wingdings 0xe9; behind: 0x1ca328 and 0xea.
        // The arrow rect is entry 11 offset by (-12, +11/+9) in high
        // resolution, drawn through 0x00447b20 (black, then white at -2,-2).
        const uint32_t magnitude =
            state.target_gap < 0 ? 0u - uint32_t(state.target_gap) : uint32_t(state.target_gap);
        const int32_t scaled = int32_t(magnitude * 1000u);
        const int64_t wide = int64_t(uint64_t(uint32_t(scaled >> 31)) << 32 | uint32_t(scaled >> 4));
        const int32_t value = int32_t(wide / (metric_ ? 3279 : 5280));
        char gap[32];
        std::snprintf(gap, sizeof gap, "%d.%03d", value / 1000, value % 1000);
        const bool ahead = state.target_gap < 1;
        label(gap, 10, ahead ? 0xe71000 : 0x28a31c);
        const int ax = int((layout[11] - 12) * lscale) * scale,
                  ay = int((layout[12] + (ahead ? 9 : 11)) * lscale) * scale;
        draw_gap_arrow(image, ax, ay, ahead, 0, scale);
        draw_gap_arrow(image, ax - 2 * scale, ay - 2 * scale, ahead, 0xffffff, scale);
    }
    return image;
}
uint8_t DashLut::index(uint32_t c, const Palette& pal) {
    const auto it = memo_.find(c);
    if (it != memo_.end()) return it->second;
    uint8_t best = 0;
    int best_d = INT_MAX;
    for (int i = 1; i < 256; ++i) {
        const uint8_t* rgb = &pal.rgbx[size_t(4 * i)];
        const int dr = int(rgb[0]) - int((c >> 16) & 0xff);
        const int dg = int(rgb[1]) - int((c >> 8) & 0xff);
        const int db = int(rgb[2]) - int(c & 0xff);
        const int d = dr * dr + dg * dg + db * db;
        if (d < best_d) {
            best_d = d;
            best = uint8_t(i);
        }
    }
    memo_.emplace(c, best);
    return best;
}

} // namespace rr
