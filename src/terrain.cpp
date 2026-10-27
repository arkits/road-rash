#include "terrain.hpp"
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <string>
namespace rr {
namespace {
int32_t signed_bits(uint32_t v) {
    int32_t r;
    std::memcpy(&r, &v, 4);
    return r;
}
int32_t add(int32_t a, int32_t b) {
    return signed_bits(uint32_t(a) + uint32_t(b));
}
int32_t sub(int32_t a, int32_t b) {
    return signed_bits(uint32_t(a) - uint32_t(b));
}
int32_t mul(int32_t a, int32_t b) {
    return signed_bits(uint32_t(a) * uint32_t(b));
}
int32_t sample_of(int32_t q8) {
    return q8 >= 0 ? q8 / 256 : int32_t((int64_t(q8) - 255) / 256);
}
} // namespace
TerrainDefinition read_terrain(const std::vector<uint8_t>& b, uint32_t samples) {
    auto range = [&](size_t p, size_t n) {
        if (p > b.size() || n > b.size() - p) throw std::runtime_error("Truncated terrain payload");
    };
    auto word = [&](size_t p) {
        range(p, 4);
        return uint32_t(b[p]) | uint32_t(b[p + 1]) << 8 | uint32_t(b[p + 2]) << 16 | uint32_t(b[p + 3]) << 24;
    };
    range(0, 16);
    if (word(4) != b.size()) throw std::runtime_error("Terrain size mismatch");
    const std::string tag(b.begin(), b.begin() + 4);
    TerrainDefinition d;
    size_t selectors = 0;
    if (tag == "DLBR") {
        d.kind = TerrainDefinition::Kind::building;
        selectors = 16;
    } else if (tag == "NTMR" || tag == "DWDR") {
        d.kind = TerrainDefinition::Kind::wall;
        range(0, 24);
        size_t p = 24;
        for (int side = 0; side < 2; ++side) {
            const auto count = word(12 + 4 * side);
            if (!count || count > (b.size() - p) / 16) throw std::runtime_error("Invalid wall record count");
            for (uint32_t i = 0; i < count; ++i, p += 16) {
                TerrainWallRecord r;
                r.start = word(p);
                r.base_height = signed_bits(word(p + 12));
                std::copy_n(b.begin() + p + 4, 8, r.parameters.begin());
                // Disc profiles include out-of-order boundaries; preserve physical order.
                d.walls[side].push_back(r);
            }
        }
        selectors = p;
    } else if (tag == "LIHR") {
        d.kind = TerrainDefinition::Kind::hill;
        range(0, 20);
        const auto count = word(8);
        if (!count || count > (b.size() - 20) / 76) throw std::runtime_error("Invalid hill record count");
        for (uint32_t i = 0; i < count; ++i) {
            const size_t p = 20 + 76 * i;
            TerrainHillRecord r;
            r.start = signed_bits(word(p));
            r.end = signed_bits(word(p + 4));
            // CITY intentionally has a first interval 0..-6 followed by -6..12.
            // Keep signed endpoints and original transition behavior.
            r.selectors = {b[p + 8], b[p + 9]};
            for (int j = 0; j < 8; ++j) {
                const size_t q = p + 12 + 8 * j;
                auto& a = r.points[j];
                a = {b[q],     b[q + 1], b[q + 2], int8_t(b[q + 3]), int8_t(b[q + 4]),
                     b[q + 5], b[q + 6], b[q + 7]};
                if (a.min_x > a.max_x || a.min_height > a.max_height || !a.min_length ||
                    a.min_length > a.max_length)
                    throw std::runtime_error("Invalid hill point bounds");
            }
            d.hills.push_back(r);
        }
        return d;
    } else
        throw std::runtime_error("Unknown terrain tag: " + tag);
    if (word(8) != samples) throw std::runtime_error("Terrain/path sample count mismatch");
    range(selectors, size_t(samples) * 2);
    for (uint32_t i = 0; i < samples; ++i)
        d.selectors.push_back({b[selectors + 2 * i], b[selectors + 2 * i + 1]});
    return d;
}
TerrainIterator::TerrainIterator(const TerrainDefinition& d, int32_t position,
                                 std::function<uint32_t()> random)
    : definition_(&d), random_(std::move(random)), position_(position), last_sample_(sample_of(position)) {
    if (!random_) throw std::runtime_error("Terrain requires random source");
    if (d.kind == TerrainDefinition::Kind::wall) {
        for (int side = 0; side < 2; ++side) {
            if (d.walls[side].empty()) throw std::runtime_error("Empty wall profile");
            wall_record_[side] = position ? d.walls[side].size() - 1 : 0;
            const auto& p = d.walls[side][wall_record_[side]].parameters;
            wall_values_[side] = {p[2], p[7] * 2, p[4] * 2};
        }
    } else if (d.kind == TerrainDefinition::Kind::hill) {
        if (d.hills.empty()) throw std::runtime_error("Empty hill profile");
        hill_record_ = position ? d.hills.size() - 1 : 0;
        for (int i = 0; i < 8; ++i) {
            const auto& p = d.hills[hill_record_].points[i];
            auto& v = points_[i];
            v.start = v.end = position; // original initializer uses Q8 value here
            v.x = int32_t(random_() % uint32_t((p.max_x - p.min_x) * 32 + 1)) + p.min_x * 32;
            v.height =
                p.min_height * 16 + int32_t(random_() % uint32_t((p.max_height - p.min_height) * 16 + 1));
        }
    }
}
void TerrainIterator::hill_slopes(int32_t sample, int32_t delta) {
    const auto& record = definition_->hills[hill_record_];
    for (int i = 0; i < 8; ++i) {
        auto& v = points_[i];
        const auto& p = record.points[i];
        if (v.end > sample && sample > v.start) continue;
        int32_t length = p.min_length + int32_t(random_() % uint32_t(p.max_length - p.min_length + 1));
        if (delta < 1) {
            length = -length;
            if (int64_t(sample) + length < record.start) length = int32_t(record.start) - sample;
            v.end = sample;
            v.start = add(sample, length);
        } else {
            if (int64_t(sample) + length > record.end) length = int32_t(record.end) - sample;
            v.start = sample;
            v.end = add(sample, length);
        }
        if (!length) return; // original returns without updating remaining points
        int32_t slope = int32_t(random_() % uint32_t(p.x_variation * 16 + 1)) - p.x_variation * 8;
        int32_t target = add(v.x, mul(slope, length));
        if (target < p.min_x * 32)
            slope = (p.min_x * 32 - v.x) / length;
        else if (target > p.max_x * 32)
            slope = (p.max_x * 32 - v.x) / length;
        v.dx = slope;
        slope = int32_t(random_() % uint32_t(p.height_variation * 16 + 1)) - p.height_variation * 8;
        target = add(v.height, mul(slope, length));
        if (target < p.min_height * 16)
            slope = (p.min_height * 16 - v.height) / length;
        else if (target > p.max_height * 16)
            slope = (p.max_height * 16 - v.height) / length;
        v.dh = slope;
    }
}
void TerrainIterator::step(int32_t delta) {
    position_ = add(position_, delta);
    const int32_t sample = sample_of(position_);
    if (definition_->kind == TerrainDefinition::Kind::wall && last_sample_ != sample) {
        for (int side = 0; side < 2; ++side) {
            const auto& records = definition_->walls[side];
            auto& index = wall_record_[side];
            if (records.size() > 1) {
                if (delta < 1 && uint32_t(sample) < records[index].start) {
                    if (!index) throw std::runtime_error("Wall before profile");
                    --index;
                } else if (delta > 0 && index + 1 < records.size() &&
                           records[index + 1].start <= uint32_t(sample))
                    ++index;
            }
            const auto& p = records[index].parameters;
            auto& v = wall_values_[side];
            v[0] = add(v[0], int32_t(random_() % uint32_t(p[1] * 2 + 1)) - p[1]);
            v[1] = add(v[1], int32_t(random_() % uint32_t(p[6] * 2 + 1)) - p[6]);
            v[2] = add(v[2], int32_t(random_() % uint32_t(p[3] * 2 + 1)) - p[3]);
            v[0] = std::clamp(v[0], 0, int(p[2]) * 2);
            v[1] = std::clamp(v[1], 0, int(p[7]) * 4);
            v[2] = std::clamp(v[2], 0, int(p[4]) * 4);
        }
    } else if (definition_->kind == TerrainDefinition::Kind::hill && definition_->hills.size() > 1) {
        bool advance = false;
        if (delta < 0 && hill_record_ > 0) {
            if (sample < int32_t(definition_->hills[hill_record_].start)) --hill_record_;
            advance = true;
        } else if (delta > 0 && hill_record_ + 1 < definition_->hills.size()) {
            if (int32_t(definition_->hills[hill_record_ + 1].start) <= sample) ++hill_record_;
            advance = true;
        }
        if (advance) {
            hill_slopes(sample, delta);
            const int64_t steps = delta < 0 ? int64_t(last_sample_) - sample : int64_t(sample) - last_sample_;
            if (steps > 0)
                for (auto& v : points_) {
                    v.x = add(v.x, mul(v.dx, int32_t(delta < 0 ? -steps : steps)));
                    v.height = add(v.height, mul(v.dh, int32_t(delta < 0 ? -steps : steps)));
                }
        }
    }
    last_sample_ = sample;
}
TerrainIteratorState TerrainIterator::diagnostic_state() const {
    return {position_, last_sample_, hill_record_, points_};
}
TerrainSlice TerrainIterator::slice(int32_t left, int32_t right) const {
    TerrainSlice s;
    if (definition_->kind == TerrainDefinition::Kind::none) return s;
    if (definition_->kind == TerrainDefinition::Kind::hill) {
        s.type = 4;
        s.selectors = definition_->hills[hill_record_].selectors;
        for (int side = 0; side < 2; ++side) {
            int32_t previous = side ? right : left, previous_height = 0;
            for (int i = 0; i < 3; ++i) {
                const auto& p = points_[side * 4 + i];
                auto& v = s.points[i * 2 + side];
                v = {side ? add(right, p.x) : sub(left, p.x), p.height};
                const int64_t dx = std::abs(int64_t(sub(v[0], previous))),
                              dy = std::abs(int64_t(sub(v[1], previous_height)));
                const int64_t denominator = (std::min(dx, dy) / 2) - dy - dx;
                if (!denominator) throw std::runtime_error("Degenerate hill shading segment");
                const int32_t numerator = signed_bits(uint32_t(dy) << 16);
                const int64_t shade = std::max<int64_t>(0, int64_t(numerator) / denominator + 65536);
                s.shades[i * 2 + side] = int32_t(std::min<int64_t>(31, (shade * 32) / 65536));
                previous = v[0];
                previous_height = v[1];
            }
        }
    } else {
        const int32_t sample = sample_of(position_);
        if (sample >= 0 && size_t(sample) == definition_->selectors.size() && definition_->end_selectors)
            s.selectors = *definition_->end_selectors;
        else {
            if (sample < 0 || size_t(sample) >= definition_->selectors.size())
                throw std::runtime_error("Terrain selector beyond profile");
            s.selectors = definition_->selectors[sample];
        }
        s.type = definition_->kind == TerrainDefinition::Kind::wall ? 1 : 0;
        if (s.type == 1)
            for (int side = 0; side < 2; ++side) {
                const auto& r = definition_->walls[side][wall_record_[side]];
                const auto& p = r.parameters;
                const auto& v = wall_values_[side];
                s.walls[side] = {v[0], add(v[2], (p[2] + p[5] * 2) * 2), add(r.base_height, v[1])};
            }
    }
    return s;
}
} // namespace rr
