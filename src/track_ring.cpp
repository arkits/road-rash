#include "track_ring.hpp"
#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace rr {
namespace {
int32_t wrap32(int64_t value) {
    const uint32_t bits = uint32_t(value);
    int32_t result;
    std::memcpy(&result, &bits, sizeof(result));
    return result;
}
int32_t lane_shift(int32_t q8) {
    return wrap32(int64_t(wrap32(int64_t(q8) * 250)) / 256);
}
const CourseNode& node_of(const Course& course, uint32_t offset) {
    const auto it = course.nodes.find(offset);
    if (it == course.nodes.end()) throw std::runtime_error("Track ring: missing course node");
    return it->second;
}
} // namespace

TrackRing::TrackRing(const Course& course, int level)
    : course_(&course), level_(level), events_(course.sections.size()),
      forward_([this] { return (*random_)(); }) {
    if (level < 0 || level > 4) throw std::runtime_error("Track ring: invalid level");
    for (size_t s = 0; s < course.sections.size(); ++s) {
        events_[s] = std::make_shared<Events>();
        events_[s]->section = uint32_t(s);
        events_[s]->records = course.sections[s].scenery_events;
    }
}

TrackRing::Events& TrackRing::events_mut(size_t node) {
    auto& e = events_.at(node);
    if (e.use_count() > 1) e = std::make_shared<Events>(*e);
    return *e;
}

// 0x00453e60: depth-first over the course graph. A road node jitters its
// section's DZHR records (0x00451ee0) and sorts them (0x00451fc0); a split
// visits +8 then +12, a merge +16. Each node is visited once.
void TrackRing::jitter_events(uint32_t offset, std::vector<uint32_t>& visited, const Random& random) {
    for (;;) {
        if (std::find(visited.begin(), visited.end(), offset) != visited.end()) return;
        const auto& node = node_of(*course_, offset);
        if (node.kind > 2) return;
        visited.push_back(offset);
        if (node.kind == 1) {
            probe_window(offset, 1);
            jitter_events(node.next, visited, random);
            offset = node.previous;
            continue;
        }
        if (node.kind == 2) {
            probe_window(offset, -1);
            offset = node.link;
            continue;
        }
        auto& records = events_mut(node.link).records;
        int32_t base = 0;
        for (auto& r : records) { // 0x00451ee0
            const uint32_t word = r[0];
            base += int32_t((word & 0x7f000000u) >> 24);
            const int32_t draw = int32_t(random());
            const int bonus = (!(word & 0x80000000u) || (word & 15) < 0xb) ? level_ : level_ - 4;
            const uint32_t sample = uint32_t(draw % int32_t(((word & 0xf0000u) >> 15) + 1) + base);
            r[0] = ((sample * 0x10000u ^ word) & 0x7fff0000u) ^ word;
            const uint32_t required = (r[1] & 0x7000u) >> 12;
            bool enabled = !(required != 0 && level_ - int(required) != -1);
            if (enabled && int(((word & 0xf00000u) >> 20) + uint32_t(bonus)) < int32_t(random()) % 10)
                enabled = false;
            if (!enabled) r[0] |= 0x3f00u;
        }
        for (size_t i = 0; i + 1 < records.size(); ++i) // 0x00451fc0 pairwise swaps
            for (size_t j = i + 1; j < records.size(); ++j)
                if ((records[j][0] & 0x7fff0000u) < (records[i][0] & 0x7fff0000u))
                    std::swap(records[i], records[j]);
        offset = node.next;
    }
}

// 0x00453b60: 0x00453e60 walks a scratch walker 0x51 slices through every
// split (forward from the end of the road before it) and merge (backward
// from the start of the road after it) to measure the halves' separation.
// Only its terrain iterators draw from the stream. The walker starts with
// 0x00452f50 (enter at 0), re-enters (at the end for a split, at 0 for a
// merge), and its first step reaches the split/merge: 0x00453030 enters
// both halves (+8, +12) and steps each. Later steps move the node's +0x19
// half first. Both halves stay well inside their sections on the original
// courses; anything else marks the ring unsupported.
void TrackRing::probe_window(uint32_t offset, int32_t direction) {
    const auto& window = node_of(*course_, offset);
    std::array<SectionWalker, 2> half;
    enter(half[0], window.link, true);
    enter(half[0], window.link, direction < 0);
    const int32_t delta = direction * 0x100;
    if (step_section(half[0], delta) != 2) {
        unsupported_ = true;
        return;
    }
    const auto& reached = node_of(*course_, direction > 0 ? node_of(*course_, half[0].node).next
                                                          : node_of(*course_, half[0].node).previous);
    if (reached.offset != offset) {
        unsupported_ = true;
        return;
    }
    const auto before = path_of(half[0]);
    enter(half[0], window.next, direction > 0);
    enter(half[1], window.previous, direction > 0);
    // Forward: the outer position's fraction (0x100 & 0xff = 0); backward:
    // (0 & 0xff) - 0x100.
    const int32_t first = direction > 0 ? 0 : delta;
    step_section(half[0], first);
    step_section(half[1], first);
    half[0].offset = lane_shift(path_of(half[0]).left_q8 - before.left_q8);
    half[1].offset = lane_shift(before.right_q8 - path_of(half[1]).right_q8);
    const int active = window.primary ? 1 : 0;
    int32_t dx_sep = path_of(half[1]).bend - path_of(half[0]).bend;
    int32_t x_sep = 0;
    if (direction < 0) {
        dx_sep = -dx_sep;
        x_sep = -dx_sep;
    }
    int32_t threshold = 80;
    bool separated = false;
    auto measure = [&](int32_t sample) {
        if (separated || x_sep <= 255) return;
        int32_t clearance = 0;
        const auto& section = course_->sections.at(node_of(*course_, half[0].node).link);
        if (section.terrain.kind != TerrainDefinition::Kind::none)
            clearance = path_of(half[0]).shoulder_right + path_of(half[1]).shoulder_left;
        if (clearance + 256 <= x_sep) {
            separated = true;
            threshold = sample;
        }
    };
    measure(0);
    for (int i = 1; i <= 0x50; ++i) {
        if (direction < 0) {
            dx_sep = wrap32(int64_t(dx_sep) + path_of(half[0]).bend - path_of(half[1]).bend);
            x_sep = wrap32(int64_t(x_sep) - dx_sep);
        }
        const int moved = step_section(half[active], delta);
        if (moved == 2) {
            unsupported_ = true;
            return;
        }
        if (moved == 3) continue; // 0x00453030 returns before the other half
        step_section(half[active ^ 1], delta);
        if (direction > 0) {
            x_sep = wrap32(int64_t(x_sep) + dx_sep);
            dx_sep = wrap32(int64_t(dx_sep) + path_of(half[1]).bend - path_of(half[0]).bend);
        }
        measure(i);
    }
    windows_[offset] = {std::move(half), uint8_t(active), dx_sep, x_sep, threshold};
}

void TrackRing::Borr::load(int32_t index) {
    const uint32_t word = (*words).at(size_t(index));
    const uint8_t top = uint8_t(word >> 24);
    alternate = top >> 7;
    left = (top >> 4) & 1;
    selector = uint8_t(word >> 16);
    random = (word >> 11) & 1;
    count = (word >> 8) & 7;
    units = top & 15;
    if ((word & 0x0f000000u) == 0) {
        const int32_t amount = int32_t(((word & 0xf000u) >> 8) + (word & 15u));
        spacing = (word & 0x20000000u) ? (amount + 0x20) * 0x20 : amount * 8;
        period = 0;
    } else {
        spacing = (word & 0x20000000u) ? int32_t((word & 15u) + 4) * 0x100 : int32_t(word & 15u) << 6;
        period = (word >> 12) & 15;
    }
}

void TrackRing::Borr::init(const std::vector<uint32_t>* data, int32_t samples, bool at_start) {
    words = data && !data->empty() ? data : nullptr;
    if (!words) return;
    record = at_start ? 0 : int32_t(words->size()) - 1;
    position = at_start ? 0 : (samples - 1) * 0x100;
    load(record);
    current = position >> 8;
    start = at_start ? 0 : current - (period + 1) * units;
    end = at_start ? (period + 1) * units : current;
}

void TrackRing::Borr::step(int32_t delta) {
    if (!words) return;
    const int32_t old = position;
    position += delta;
    const int32_t sample = position >> 8;
    if (sample == old >> 8) return;
    current = sample;
    if (delta < 1) {
        if (uint32_t(sample) < uint32_t(start)) {
            if (record < 1) {
                selector = 0x3f;
                return;
            }
            load(--record);
            end = sample;
            start = sample - (period + 1) * units;
        }
        return;
    }
    if (uint32_t(end) < uint32_t(sample)) {
        if (int32_t(words->size()) - 1 <= record) {
            selector = 0x3f;
            return;
        }
        load(++record);
        start = position >> 8;
        end = (period + 1) * units + (position >> 8);
    }
}

void TrackRing::Borr::slots(const PathSample& path, const Random& draw, RingStatics& out) const {
    // 0x00451cb0: +0x60 = 0, then (gates passing) count+1 slots stepping
    // outward from 224 units inside the road edge (+0xe0/+0xe4).
    out.borr_count = 0;
    if (!words) return;
    if (period != 0 && (current - start) % (period + 1) != 0) return;
    if ((selector & 0x3f) == 0x3f) return;
    const bool left_side = alternate == 0 ? left != 0 : ((current - start) & 1) == 0 ? left == 0 : left != 0;
    int32_t x = left_side ? -int32_t(path.left) - spacing + 0xe0 : int32_t(path.right) - 0xe0 + spacing;
    for (int i = 0; i <= count; ++i) {
        out.borr[size_t(i)] = x;
        int32_t step = spacing;
        if (random) {
            const uint32_t half = uint32_t(spacing) >> 1;
            step = int32_t(half + draw() % (uint32_t(spacing) - half + 1)); // unsigned divl
        }
        x += left_side ? -step : step;
    }
    out.borr_count = uint8_t(count + 1);
    out.borr_selector = selector;
}

void TrackRing::enter(SectionWalker& w, uint32_t offset, bool start) {
    // 0x00452cb0: road cursor at 0 or length; BORR/DZHR at length-1.
    const auto& node = node_of(*course_, offset);
    const auto& section = course_->sections.at(node.link);
    w.node = offset;
    w.position = start ? 0 : int32_t(node.length) * 0x100;
    w.borr.init(&section.roadside_profiles, int32_t(section.samples.size()), start);
    w.terrain.reset();
    if (section.terrain.kind != TerrainDefinition::Kind::none)
        w.terrain.emplace(section.terrain, w.position, forward_);
    w.dzhr = {size_t(node.link), start ? 0 : (int32_t(section.samples.size()) - 1) * 0x100,
              start ? 0 : int32_t(events_.at(node.link)->records.size()) - 1};
}

void TrackRing::commit_branch(uint8_t half) {
    if (half > 1) throw std::runtime_error("Track ring: invalid branch half");
    chosen_half_ = half;
    branch_committed_ = true;
    for (Walker* w : {&front_walker_, &back_walker_})
        if (w->dual) {
            w->active = half;
            w->committed = true;
        }
}

void TrackRing::collapse(Walker& w) {
    if (!branch_committed_) return;
    if (w.active) w.halves[0] = w.halves[1];
    w.active = 0;
    w.dual = false;
    w.committed = w.collapse_pending = false;
    w.window = 0;
}

void TrackRing::step(Walker& w, int32_t delta) {
    if (!delta || w.stopped) return;
    const int32_t next = w.position + delta;
    const bool crossing = ((w.position ^ next) & ~0xff) != 0;
    if (crossing) w.event = 0;
    if (w.dual && delta < 0 && crossing) {
        w.dx_sep = wrap32(int64_t(w.dx_sep) + path_of(w.halves[0]).bend - path_of(w.halves[1]).bend);
        w.x_sep = wrap32(int64_t(w.x_sep) - w.dx_sep);
    }
    auto& selected = w.halves[w.active];
    const uint32_t old_node = selected.node;
    const int32_t old_position = selected.position;
    const auto before = path_of(selected);
    const int32_t old_offset = selected.offset;
    const uint8_t old_active = w.active;
    const int result = step_section(selected, delta);
    if (result == 3) return;
    if (result == 2) {
        const auto& road = node_of(*course_, old_node);
        const auto& window = node_of(*course_, delta > 0 ? road.next : road.previous);
        w.window = window.offset;
        const auto cache = windows_.find(window.offset);
        w.countdown = cache == windows_.end() ? 0 : cache->second.threshold;
        if ((delta > 0 && window.kind == 1) || (delta < 0 && window.kind == 2)) {
            enter(w.halves[0], window.next, delta > 0);
            enter(w.halves[1], window.previous, delta > 0);
            w.position = next;
            const int32_t fraction = delta > 0 ? (next & 0xff) : ((next - delta) & 0xff) + delta;
            step_section(w.halves[0], fraction);
            step_section(w.halves[1], fraction);
            w.halves[0].offset =
                wrap32(int64_t(old_offset) + lane_shift(path_of(w.halves[0]).left_q8 - before.left_q8));
            w.halves[1].offset =
                wrap32(int64_t(old_offset) + lane_shift(before.right_q8 - path_of(w.halves[1]).right_q8));
            w.active = branch_committed_ ? chosen_half_ : uint8_t(window.primary != 0);
            w.committed = branch_committed_;
            w.dual = true;
            w.event = delta > 0 ? 2 : 4;
            w.dx_sep = path_of(w.halves[1]).bend - path_of(w.halves[0]).bend;
            w.x_sep = 0;
            if (delta < 0) {
                w.dx_sep = -w.dx_sep;
                w.x_sep = -w.dx_sep;
            }
        } else {
            if (w.active) w.halves[0] = w.halves[w.active];
            w.active = 0;
            w.dual = w.committed = false;
            w.event = delta > 0 ? 1 : 3;
            enter(w.halves[0], window.link, delta > 0);
            const auto& common = path_of(w.halves[0]);
            w.halves[0].offset =
                wrap32(int64_t(old_offset) + lane_shift(old_active == 0 ? common.left_q8 - before.left_q8
                                                                        : before.right_q8 - common.right_q8));
            const int32_t residual =
                delta > 0 ? old_position + delta - int32_t(road.length) * 256 : old_position + delta;
            step_section(w.halves[0], residual);
            w.position = next;
        }
        return;
    }
    if (result == 1) w.collapse_pending = true;
    if (w.dual) {
        step_section(w.halves[w.active ^ 1], delta);
        if (crossing) {
            const auto& window = node_of(*course_, w.window);
            const auto& half = w.halves[w.active];
            if (delta < 0) {
                w.event = window.kind == 1 ? 5 : 6;
                w.countdown += window.kind == 1 ? 1 : -1;
                if (window.kind == 1 && (half.position & ~0xff) == 0) w.event = 2;
                if (window.kind == 2 &&
                    int32_t(node_of(*course_, half.node).length) * 256 - half.position > 0x5100)
                    w.collapse_pending = true;
            } else {
                w.x_sep = wrap32(int64_t(w.x_sep) + w.dx_sep);
                w.dx_sep = wrap32(int64_t(w.dx_sep) + path_of(w.halves[1]).bend - path_of(w.halves[0]).bend);
                w.countdown += window.kind == 1 ? -1 : 1;
                if (window.kind == 1 && half.position > 0x5100) w.collapse_pending = true;
                if (window.kind == 2 &&
                    int32_t(node_of(*course_, half.node).length) - (half.position >> 8) == 1)
                    w.event = 4;
            }
            if (w.collapse_pending) collapse(w);
        }
    } else if (crossing && result == 0) {
        const auto& half = w.halves[0];
        const auto& road = node_of(*course_, half.node);
        const uint32_t boundary = delta > 0 ? road.next : road.previous;
        if (boundary) {
            const auto& window = node_of(*course_, boundary);
            const int32_t sample = half.position >> 8;
            if (delta > 0 && window.kind == 1 && int32_t(road.length) - sample == 1) w.event = 3;
            if (delta < 0 && window.kind == 2 && (half.position & ~0xff) == 0) w.event = 1;
            const bool reenter = delta > 0 ? window.kind == 2 && int32_t(road.length) - 81 == sample
                                           : window.kind == 1 && (half.position & ~0xff) == 0x5100;
            if (reenter) {
                const auto& cache = windows_.at(window.offset);
                const uint8_t active = uint8_t(window.previous == half.node);
                if (active) w.halves[1] = w.halves[0];
                w.halves[active ^ 1] = cache.halves[active ^ 1];
                w.halves[active ^ 1].offset =
                    wrap32(int64_t(w.halves[active].offset) + cache.halves[active ^ 1].offset -
                           cache.halves[active].offset);
                step_section(w.halves[active ^ 1], next & 0xff);
                w.active = chosen_half_ = active;
                w.window = window.offset;
                w.countdown = cache.threshold + (delta > 0 ? sample - int32_t(road.length) : -sample);
                w.dx_sep = cache.dx_sep;
                w.x_sep = cache.x_sep;
                w.dual = w.committed = true;
                w.collapse_pending = false;
                w.event = delta > 0 ? 6 : 5;
                branch_committed_ = true; // 0x5100 exceeds either original front/back window limit
            }
        }
    }
    if (w.collapse_pending) collapse(w);
    w.position = next;
}

const PathSample& TrackRing::path_of(const SectionWalker& half) const {
    const auto& section = course_->sections.at(node_of(*course_, half.node).link);
    const size_t sample = size_t(half.position >> 8);
    return sample == section.samples.size() ? section.backward_endpoint : section.samples.at(sample);
}

int TrackRing::step_section(SectionWalker& w, int32_t delta) {
    // 0x00452e10: 0 within a road, 1 after changing roads, 2 at a
    // split/merge and 3 at a terminal link. Boundaries do not mutate the
    // section iterators; the enclosing 0x00453030 traversal handles them.
    const auto& node = node_of(*course_, w.node);
    int32_t next = w.position + delta;
    int result = 0;
    auto neighbour = [&](uint32_t offset) {
        if (!offset) return 3;
        const auto kind = node_of(*course_, offset).kind;
        return kind == 0 ? 0 : kind <= 2 ? 2 : 3;
    };
    if (next < 0) {
        if (const int boundary = neighbour(node.previous)) return boundary;
        // 0x00452f1d uses minus the OLD position, not the residual delta.
        const int32_t old = w.position;
        enter(w, node.previous, false);
        delta = -old;
        next = w.position + delta;
        result = 1;
    } else if (next >= int32_t(node.length) * 0x100) {
        if (const int boundary = neighbour(node.next)) return boundary;
        next -= int32_t(node.length) * 0x100;
        enter(w, node.next);
        delta = next;
        result = 1;
    }
    w.position = next;
    w.borr.step(delta);                    // 0x00451e20
    if (w.terrain) w.terrain->step(delta); // 0x004516e0
    // 0x00452230: skip records before the walker's sample.
    w.dzhr.position += delta;
    const auto& records = events_.at(w.dzhr.node)->records;
    // 0x00452230 skips a null DZHR stream. In native sections an
    // absent stream has no records; reverse entry's count-1 is -1.
    if (records.empty()) return result;
    if (delta > 0) {
        const uint32_t sample = uint32_t(w.dzhr.position >> 8);
        // The index may end one past the last record (loop test after ++).
        while (w.dzhr.index < int32_t(records.size())) {
            if (sample <= (records[size_t(w.dzhr.index)][0] & 0x7fff0000u) >> 16) break;
            ++w.dzhr.index;
        }
    } else if (delta < 0) {
        const uint32_t sample = uint32_t(w.dzhr.position >> 8);
        while (w.dzhr.index > 0) {
            // Forward traversal can leave the index one past the records;
            // 0x00452256 then reads the following SGS word before decrementing.
            const uint32_t word = w.dzhr.index == int32_t(records.size())
                                      ? course_->sections.at(w.dzhr.node).scenery_following_word
                                      : records.at(size_t(w.dzhr.index))[0];
            if (sample >= (word & 0x7fff0000u) >> 16) break;
            --w.dzhr.index;
        }
    }
    return result;
}

void TrackRing::build_slice(Walker& w, int32_t ring_q8, const ActorSink& sink) {
    if (w.stopped) return;
    slice_events_[size_t((uint32_t(ring_q8) >> 8) & 127)] = w.event;
    // 0x00454c80 builds the selected half before its opposite.
    build_half(w.halves[w.active], ring_q8, sink);
    if (w.dual) build_half(w.halves[w.active ^ 1], ring_q8, sink);
}

void TrackRing::build_half(SectionWalker& w, int32_t ring_q8, const ActorSink& sink) {
    // 0x00454240: two 0x00451cb0 walks, then 0x004520d0.
    const auto& node = node_of(*course_, w.node);
    const auto& section = course_->sections.at(node.link);
    const uint32_t local = uint32_t(w.position >> 8);
    const auto& path =
        local == section.samples.size() ? section.backward_endpoint : section.samples.at(local);
    RingStatics statics;
    w.borr.slots(path, *random_, statics); // first walk: stale edges, positions overwritten
    if (slice_sink_ && w.terrain)          // 0x00451910 (draw-free)
        slice_sink_(node.link, local, path,
                    w.terrain->slice(-path.left - path.shoulder_left, path.right + path.shoulder_right));
    w.borr.slots(path, *random_, statics);
    const std::vector<std::array<uint32_t, 2>>* list = &events_.at(w.dzhr.node)->records;
    for (size_t i = size_t(std::max(0, w.dzhr.index)); i < list->size(); ++i) {
        const auto& r = (*list)[i];
        if ((r[0] & 0x7fff0000u) >> 16 != uint32_t(w.dzhr.position >> 8)) break;
        if (((r[0] >> 8) & 0x3f) == 0x3f) continue;
        if (!(r[0] & 0x80000000u)) { // billboard (+0x40 + 16n, two at most): no draw
            if (statics.billboards < 2) {
                const int32_t offset = (r[1] & 0x800) ? int32_t(r[1] | 0xfffff000u) : int32_t(r[1] & 0xfff);
                statics.billboard[statics.billboards++] = {uint8_t(r[0] & 15), uint8_t(r[0] >> 8), offset};
            }
            continue;
        }
        const uint32_t jitter = (*random_)();
        const RingActor actor{ring_q8 >> 8, local * 0x100u + (r[1] >> 24), r[0], r[1], node.link, local};
        if (sink) sink(actor, jitter);
        auto& mine = events_mut(w.dzhr.node).records; // copy-on-write: a snapshot may share it
        mine[i][0] |= 0x3f00u;
        list = &mine;
    }
    if (static_sink_) static_sink_(ring_q8 >> 8, node.link, local, statics);
}

void TrackRing::setup(int32_t camera, const Random& random, const ActorSink& sink) {
    random_ = &random;
    std::vector<uint32_t> visited;
    jitter_events(course_->root, visited, random);
    front_walker_.front = true;
    enter(front_walker_.halves[0], course_->root); // 0x00452f50 front, then back
    enter(back_walker_.halves[0], course_->root);
    front_ = camera + 0x5000;
    back_ = camera - 0x3000;
    auto walk = [&](Walker& w, int32_t to) { // 0x00453ae0 forward
        int32_t d = to - w.position;
        if (d <= 0) {
            if (d < 0) w.stopped = unsupported_ = true;
            return;
        }
        for (int32_t n = d >> 8; n > 0; --n) step(w, 0x100);
        step(w, d & 0xff);
    };
    walk(front_walker_, back_);
    walk(back_walker_, back_);
    for (int32_t k = 0; k < 0x8000; k += 0x100) {
        build_slice(front_walker_, back_ + k, sink);
        step(front_walker_, 0x100);
    }
    random_ = nullptr;
}

void TrackRing::advance(int32_t delta, const Random& random, const ActorSink& sink) {
    if (delta == 0) return;
    random_ = &random;
    const int32_t old_back = back_;
    back_ += delta;
    front_ += delta;
    if (((old_back ^ back_) & ~0xff) != 0) {
        if (delta < 1) {
            step(back_walker_, -0x100);
            build_slice(back_walker_, back_, sink);
            step(front_walker_, -0x100);
        } else {
            step(back_walker_, 0x100);
            build_slice(front_walker_, front_ - delta, sink);
            step(front_walker_, 0x100);
        }
    }
    const auto event = slice_events_[size_t((uint32_t(front_ - 0x5000) >> 8) & 127)];
    if ((delta > 0 && event == 1) || (delta < 0 && event == 3)) branch_committed_ = false;
    random_ = nullptr;
}

TrackRingState TrackRing::diagnostic_state() const {
    auto state = [](const SectionWalker& walker) {
        TrackWalkerState out;
        out.position = walker.position;
        out.node = walker.node;
        out.offset = walker.offset;
        out.borr_position = walker.borr.position;
        out.borr_record = walker.borr.record;
        out.borr_start = walker.borr.start;
        out.borr_end = walker.borr.end;
        out.dzhr_position = walker.dzhr.position;
        out.dzhr_index = walker.dzhr.index;
        if (walker.terrain) out.terrain = walker.terrain->diagnostic_state();
        return out;
    };
    TrackRingState out{state(front_walker_.halves[front_walker_.active]),
                       state(back_walker_.halves[back_walker_.active])};
    out.front_dual = front_walker_.dual;
    out.back_dual = back_walker_.dual;
    out.front_active = front_walker_.active;
    out.back_active = back_walker_.active;
    if (out.front_dual) out.front_other = state(front_walker_.halves[front_walker_.active ^ 1]);
    if (out.back_dual) out.back_other = state(back_walker_.halves[back_walker_.active ^ 1]);
    return out;
}

TerrainSlice TrackRing::front_terrain(int32_t left, int32_t right) const {
    const auto& half = front_walker_.halves[front_walker_.active];
    return half.terrain ? half.terrain->slice(left, right) : TerrainSlice{};
}
} // namespace rr
