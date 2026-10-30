#include "course.hpp"
#include "exe_tables.hpp"
#include "race_setup.hpp"
#include <algorithm>
#include <set>

namespace rr {
namespace {
struct Reader {
    const std::vector<uint8_t>& b;
    size_t base, size;
    void range(size_t p, size_t n) const {
        if (p > size || n > size - p) throw std::runtime_error("Course payload range outside resource");
    }
    uint32_t u32(size_t p) const {
        range(p, 4);
        p += base;
        return uint32_t(b[p]) | uint32_t(b[p + 1]) << 8 | uint32_t(b[p + 2]) << 16 | uint32_t(b[p + 3]) << 24;
    }
    int64_t s32(size_t p) const {
        auto v = u32(p);
        return v <= INT32_MAX ? int64_t(v) : int64_t(v) - 0x100000000LL;
    }
    int byte(size_t p) const {
        range(p, 1);
        return b[base + p];
    }
    int signed_byte(size_t p) const {
        int v = byte(p);
        return v < 128 ? v : v - 256;
    }
    void magic(size_t p, const char* tag) const {
        range(p, 4);
        if (!std::equal(tag, tag + 4, b.begin() + base + p))
            throw std::runtime_error("Unexpected course chunk tag");
    }
    void chunk(size_t p, const char* tag, size_t header, size_t stride) const {
        magic(p, tag);
        range(p, header);
        uint32_t extent = u32(p + 4), count = u32(p + 8);
        if (extent < header || count > (extent - header) / stride)
            throw std::runtime_error("Invalid course chunk count/size");
        range(p, extent);
    }
};

// Forward walk of an interval table exactly as the per-sample iterators step it
// (0x00452920 lanes, 0x00452b70 sides): records {start, end, bytes..., Q16 slopes}.
// Values start at record 0's start bytes. Stepping to sample j: within a record,
// j < end adds the slopes and j == end loads the end bytes; past it, the next record
// is entered only when j equals its start, keeping the current values. The last
// record is the terminal marker that 0x00447c20 rewrites to the node length.
struct Field {
    size_t first, last, slope;
};
template <class Emit>
void walk_intervals(const Reader& r, size_t chunk, const char* tag, size_t stride,
                    std::initializer_list<Field> fields, uint32_t count, Emit emit) {
    r.chunk(chunk, tag, 16, stride);
    const uint32_t n = r.u32(chunk + 8);
    if (!n) throw std::runtime_error("Empty interval table");
    auto record = [&](uint32_t k) { return chunk + 16 + stride * k; };
    for (uint32_t k = 0; k < n; ++k)
        if (r.u32(record(k)) > r.u32(record(k) + 4)) throw std::runtime_error("Inverted interval");
    uint32_t k = 0;
    std::vector<int64_t> value;
    for (const auto& f : fields) value.push_back(int64_t(r.byte(record(0) + f.first)) * 65536);
    for (uint32_t j = 0; j < count; ++j) {
        if (j) {
            const uint32_t end = r.u32(record(k) + 4);
            if (j < end) {
                size_t i = 0;
                for (const auto& f : fields) value[i++] += r.s32(record(k) + f.slope);
            } else if (j == end) {
                size_t i = 0;
                for (const auto& f : fields) value[i++] = int64_t(r.byte(record(k) + f.last)) * 65536;
            } else if (k + 1 < n && r.u32(record(k + 1)) == j)
                ++k;
        }
        for (auto v : value)
            if (v < 0 || v > 255LL * 65536 + 65535)
                throw std::runtime_error(std::string(tag) + " value outside byte range");
        emit(j, value, record(k));
    }
}
} // namespace

Course read_course(const std::vector<uint8_t>& bytes) {
    auto resources = read_resources(bytes);
    auto get = [&](const char* tag) {
        auto it = std::find_if(resources.begin(), resources.end(),
                               [&](const Resource& r) { return r.tag == tag && r.id == 1; });
        if (it == resources.end()) throw std::runtime_error(std::string("Missing course resource ") + tag);
        return Reader{bytes, it->offset, it->size};
    };
    auto segments = get("SGS ");
    auto graph = get("NOD ");
    segments.chunk(0, "SGSR", 16, 52);
    graph.magic(0, "DONR");
    graph.range(0, 20);
    if (graph.u32(4) != graph.size) throw std::runtime_error("Node resource size mismatch");
    Course course{graph.u32(16), {}, {}};
    for (size_t i = 0; i < segments.u32(8); ++i) {
        size_t row = 16 + i * 52, path = segments.u32(row + 4), sides = segments.u32(row + 0x14),
               lanes = segments.u32(row + 0x18);
        segments.chunk(path, "HTPR", 16, 2);
        const uint32_t count = segments.u32(path + 8);
        CourseSection section;
        const size_t roadside = segments.u32(row + 8), scenery = segments.u32(row + 0x1c);
        if (roadside) {
            segments.chunk(roadside, "BORR", 20, 4);
            for (uint32_t j = 0; j < segments.u32(roadside + 8); ++j)
                section.roadside_profiles.push_back(segments.u32(roadside + 20 + 4 * j));
        }
        if (scenery) {
            segments.chunk(scenery, "DZHR", 16, 8);
            for (uint32_t j = 0; j < segments.u32(scenery + 8); ++j)
                section.scenery_events.push_back(
                    {segments.u32(scenery + 16 + 8 * j), segments.u32(scenery + 20 + 8 * j)});
            const size_t following = scenery + 16 + size_t(8) * segments.u32(scenery + 8);
            if (following <= segments.size && segments.size - following >= 4)
                section.scenery_following_word = segments.u32(following);
        }
        const int32_t start_height =
            int16_t(uint16_t(segments.byte(path + 12) | segments.byte(path + 13) << 8));
        for (uint32_t j = 0; j < count; ++j) {
            // No lane table: 0x004528f0 reports 0x200 (two lanes) each side.
            section.samples.push_back({segments.signed_byte(path + 16 + 2 * j),
                                       segments.signed_byte(path + 17 + 2 * j), 500, 500, 512, 512});
            section.samples.back().elevation =
                j ? section.samples[j - 1].elevation + section.samples[j - 1].rise : start_height;
        }
        // RLAN widths in lane units (0x00452820 / 0x00452920); 0x00454240 converts
        // Q8 lane units to 250-unit lanes.
        if (lanes)
            walk_intervals(segments, lanes, "NALR", 20, {{8, 10, 12}, {9, 11, 16}}, count,
                           [&](uint32_t j, const std::vector<int64_t>& q16, size_t) {
                               for (int side = 0; side < 2; ++side) {
                                   const int64_t q8 = q16[side] / 256;
                                   (side ? section.samples[j].right : section.samples[j].left) =
                                       int32_t(q8 * 250 / 256);
                                   (side ? section.samples[j].right_q8 : section.samples[j].left_q8) =
                                       int32_t(q8);
                               }
                           });
        // RSLD sides in world units (0x00452a00 / 0x00452b70, read back >>16 by 0x00452b00).
        if (sides)
            walk_intervals(segments, sides, "DLSR", 36,
                           {{8, 10, 20}, {9, 11, 24}, {12, 14, 28}, {13, 15, 32}}, count,
                           [&](uint32_t j, const std::vector<int64_t>& q16, size_t record) {
                               auto& sample = section.samples[j];
                               sample.shoulder_left = int32_t(q16[0] >> 16);
                               sample.shoulder_right = int32_t(q16[1] >> 16);
                               sample.height_left = int32_t(q16[2] >> 16);
                               sample.height_right = int32_t(q16[3] >> 16);
                               sample.side_flags[0] = uint8_t(segments.byte(record + 16));
                               sample.side_flags[1] = uint8_t(segments.byte(record + 17));
                           });
        const size_t terrain = segments.u32(row + 0x10);
        if (terrain) {
            const size_t extent = segments.u32(terrain + 4);
            segments.range(terrain, extent);
            section.terrain =
                read_terrain(std::vector<uint8_t>(bytes.begin() + segments.base + terrain,
                                                  bytes.begin() + segments.base + terrain + extent),
                             count);
        }
        // CESR events, 0x00452690/0x00452710/0x004527d0. The initial
        // iterator has no active type until its first forward step.
        const size_t events = segments.u32(row + 0x24);
        if (events) {
            segments.chunk(events, "CESR", 20, 12);
            const uint32_t n = segments.u32(events + 8);
            for (uint32_t k = 0; k < n; ++k) {
                const size_t p = events + 20 + k * 12;
                if (segments.s32(p) > segments.s32(p + 4) || (k && segments.s32(p) < segments.s32(p - 12)))
                    throw std::runtime_error("Invalid course event interval");
                CourseEvent e;
                e.start = segments.s32(p);
                e.end = segments.s32(p + 4);
                e.raw_type = segments.byte(p + 8);
                for (int v = 0; v < 3; ++v) e.params[v] = segments.byte(p + 9 + v);
                section.events.push_back(e);
            }
            uint32_t k = 0;
            for (uint32_t j = 0; n && j < count; ++j) {
                if (j && k + 1 < n && segments.s32(events + 20 + (k + 1) * 12) <= j) ++k;
                const size_t p = events + 20 + k * 12;
                const int64_t start = segments.s32(p), end = segments.s32(p + 4);
                auto& sample = section.samples[j];
                for (int v = 0; v < 3; ++v) sample.event_parameters[v] = uint8_t(segments.byte(p + 9 + v));
                if (j < start || j > end) continue;
                sample.event_start = int32_t(start);
                sample.event_end = int32_t(end);
                if (j) sample.event_type = uint8_t(segments.byte(p + 8) + 1);
                sample.zone_end =
                    j == end && sample.event_type != 4 && sample.event_type != 5 && sample.event_type != 7;
                if (sample.event_type == 4)
                    sample.marking_flags = 0x40;
                else if (sample.event_type == 5 || sample.event_type == 7)
                    sample.marking_flags = 0;
                else if (j == end)
                    sample.marking_flags = 0; // end marker 0x80000000 has a zero low byte
                else if (j == start)
                    sample.marking_flags = 8;
                else if (end - j == 1)
                    sample.marking_flags = 16;
                else if (sample.event_type == 1) {
                    const int64_t middle = int64_t(end) - 2 * int64_t(j) + start;
                    sample.marking_flags =
                        middle == 0 || middle == 1 ? 0x22 : uint8_t(0x20 + (end - j < j - start + 1));
                } else
                    sample.marking_flags = 0x21;
            }
        }
        // MSRR family load/unload requests. Preserve their actual ordering,
        // including records at the same sample and the original forward bias.
        const size_t families = segments.u32(row + 0xc);
        if (families) {
            segments.chunk(families, "MSRR", 24, 12);
            section.family_forward_bias = segments.byte(families + 14) - 0x58;
            for (uint32_t k = 0; k < segments.u32(families + 8); ++k) {
                const size_t p = families + 24 + k * 12;
                const uint8_t type = uint8_t(segments.byte(p + 4)), slot = uint8_t(segments.byte(p + 5));
                if (slot > 3 || type < 1 || type > 4 || (k && segments.s32(p) < segments.s32(p - 12)))
                    throw std::runtime_error("Invalid family event record");
                section.family_events.push_back(
                    {int32_t(segments.s32(p)), type, slot, uint16_t(segments.u32(p + 8))});
            }
        }
        // 0x00452cb0(...,0) initializes at the road end. These are direct
        // backwards initializer values, not the final forward sample's slopes.
        auto& endpoint = section.backward_endpoint;
        endpoint.elevation = int16_t(uint16_t(segments.byte(path + 14) | segments.byte(path + 15) << 8));
        const size_t after_path = path + 16 + size_t(count) * 2;
        if (after_path <= segments.size && segments.size - after_path >= 2) {
            endpoint.bend = segments.signed_byte(after_path);
            endpoint.rise = segments.signed_byte(after_path + 1);
        }
        if (lanes && segments.u32(lanes + 8)) {
            const size_t last = lanes + 16 + size_t(segments.u32(lanes + 8) - 1) * 20;
            endpoint.left_q8 = segments.byte(last + 10) * 256;
            endpoint.right_q8 = segments.byte(last + 11) * 256;
            endpoint.left = endpoint.left_q8 * 250 / 256;
            endpoint.right = endpoint.right_q8 * 250 / 256;
        }
        if (sides && segments.u32(sides + 8)) {
            const size_t last = sides + 16 + size_t(segments.u32(sides + 8) - 1) * 36;
            endpoint.shoulder_left = segments.byte(last + 10);
            endpoint.shoulder_right = segments.byte(last + 11);
            endpoint.height_left = segments.byte(last + 14);
            endpoint.height_right = segments.byte(last + 15);
            endpoint.side_flags[0] = uint8_t(segments.byte(last + 16));
            endpoint.side_flags[1] = uint8_t(segments.byte(last + 17));
        }
        if (terrain && section.terrain.kind != TerrainDefinition::Kind::hill) {
            const size_t selectors =
                section.terrain.kind == TerrainDefinition::Kind::building
                    ? 16
                    : 24 + 16 * (section.terrain.walls[0].size() + section.terrain.walls[1].size());
            const size_t after = terrain + selectors + size_t(count) * 2;
            if (after <= segments.size && segments.size - after >= 2)
                section.terrain.end_selectors =
                    std::array<uint8_t, 2>{uint8_t(segments.byte(after)), uint8_t(segments.byte(after + 1))};
        }
        course.sections.push_back(std::move(section));
    }
    // 0x00447b80 links +8/+12 for kind 0; +8/+12/+16 for kinds 1/2.
    std::vector<uint32_t> pending{course.root};
    while (!pending.empty()) {
        auto offset = pending.back();
        pending.pop_back();
        if (course.nodes.count(offset)) continue;
        graph.range(offset, 8);
        if (offset < 20 || offset % 4) throw std::runtime_error("Invalid course node offset");
        CourseNode node{offset, graph.u32(offset)};
        if (node.kind > 4)
            throw std::runtime_error("Unknown course node kind " + std::to_string(node.kind) + " at " +
                                     std::to_string(offset));
        if (node.kind < 3) {
            graph.range(offset, 32);
            node.next = graph.u32(offset + 8);
            node.previous = graph.u32(offset + 12);
            node.link = graph.u32(offset + 16);
            node.length = graph.u32(offset + 20);
            node.primary = int8_t(graph.u32(offset + 24) >> 8);
            pending.push_back(node.next);
            pending.push_back(node.previous);
            if (node.kind)
                pending.push_back(node.link);
            else if (node.link >= course.sections.size() || !node.length ||
                     node.length > course.sections[node.link].samples.size())
                throw std::runtime_error("Invalid section reference or node length");
        }
        course.nodes.emplace(offset, node);
        if (course.nodes.size() > graph.u32(8)) throw std::runtime_error("Node count exceeds header");
    }
    if (course.nodes.size() != graph.u32(8))
        throw std::runtime_error("Unreachable nodes or incorrect graph count");
    return course;
}

namespace {
// World-unit edge shift for a Q8 lane-width change: trunc(q8 * 250 / 256).
int32_t lane_shift(int32_t q8) {
    return int32_t(int64_t(q8) * 250 / 256);
}

const CourseNode& node_at(const Course& course, uint32_t offset) {
    auto found = course.nodes.find(offset);
    if (found == course.nodes.end()) throw std::runtime_error("Missing route node");
    return found->second;
}

// Samples of a branch from road node `offset` up to the next split/merge node.
std::vector<RouteSample> branch_forward(const Course& course, uint32_t offset) {
    std::vector<RouteSample> samples;
    for (size_t guard = 0; guard <= course.nodes.size(); ++guard) {
        const auto& node = node_at(course, offset);
        if (node.kind != 0) return samples;
        const auto& section = course.sections.at(node.link);
        for (uint32_t i = 0; i < node.length; ++i)
            samples.push_back({section.samples.at(i), offset, node.link, i});
        offset = node.next;
    }
    throw std::runtime_error("Cycle in course branch");
}

// Samples of a branch ending at road node `offset`, walking back to the previous split.
std::vector<RouteSample> branch_backward(const Course& course, uint32_t offset) {
    std::vector<std::vector<RouteSample>> parts;
    for (size_t guard = 0; guard <= course.nodes.size(); ++guard) {
        const auto& node = node_at(course, offset);
        if (node.kind != 0) {
            std::vector<RouteSample> samples;
            for (auto part = parts.rbegin(); part != parts.rend(); ++part)
                samples.insert(samples.end(), part->begin(), part->end());
            return samples;
        }
        const auto& section = course.sections.at(node.link);
        parts.emplace_back();
        for (uint32_t i = 0; i < node.length; ++i)
            parts.back().push_back({section.samples.at(i), offset, node.link, i});
        offset = node.previous;
    }
    throw std::runtime_error("Cycle in course branch");
}

// 0x00453b60 walks 0x51 samples from each split/merge and rewrites node +0x14 to 0x5100.
constexpr uint32_t window = 0x51;
} // namespace
std::vector<RouteSample> make_route_indexed(const Course& course, const std::vector<int>& choices,
                                            const std::vector<uint32_t>& splits_by_node, int global_branch);

std::vector<uint32_t> split_nodes(const Course& course) {
    std::vector<uint32_t> splits;
    std::set<uint32_t> visited;
    std::vector<uint32_t> pending{course.root};
    while (!pending.empty()) {
        uint32_t offset = pending.back();
        pending.pop_back();
        if (!visited.insert(offset).second) continue;
        const auto& node = node_at(course, offset);
        if (node.kind >= 3) continue;
        if (node.kind == 0) {
            pending.push_back(node.next);
            pending.push_back(node.previous);
        } else if (node.kind == 1) {
            splits.push_back(offset);
            pending.push_back(node.next);
            pending.push_back(node.previous);
        } else
            pending.push_back(node.link);
    }
    std::sort(splits.begin(), splits.end()); // total-order: integers
    return splits;
}

std::vector<RouteSample> make_route_choices(const Course& course, const std::vector<int>& choices) {
    const auto splits = split_nodes(course);
    if (choices.size() != splits.size())
        throw std::runtime_error("Branch choice count must match split count");
    for (int c : choices)
        if (c != 0 && c != 1) throw std::runtime_error("Branch choice must be 0 or 1");
    return make_route_indexed(course, choices, splits, -1);
}
std::vector<RouteSample> make_route(const Course& course, bool second_branch) {
    return make_route_indexed(course, {}, split_nodes(course), second_branch ? 1 : 0);
}

int original_course_index(int cli_course) {
    // Native CLI order (plan 17 global constraints): NAPA 0, CITY 1,
    // HIWAY 2, CANYN 3, MEDLY 4. Original order (0x0046df58 string table,
    // 0x004160c0 course word): 1 CITY, 2 HIWAY, 3 MEDLY, 4 NAPA, 5 CANYN.
    switch (cli_course) {
    case 0:
        return 4;
    case 1:
        return 1;
    case 2:
        return 2;
    case 3:
        return 5;
    case 4:
        return 3;
    default:
        break;
    }
    throw std::runtime_error("CLI course out of range (0..4)");
}

RaceBounds race_bounds(const std::vector<uint8_t>& exe, const Course& course, int original_course,
                       int level) {
    if (original_course < 1 || original_course > 5)
        throw std::runtime_error("Original course out of range (1..5)");
    if (level < 0 || level > 4) throw std::runtime_error("Race level out of range (0..4)");
    // Player start: 0x00405600 builds the player from the start-grid row at
    // 0x00464218 + slot*8 (8 bytes: lateral, position), slot = network seat
    // DAT_004b8a10 (0 offline; plan 34 Task 2 correction). Reading it by
    // level, as here, gives seat `level`'s position: 0x3200 for 0..3,
    // seat 4's 0x3300 for level 4. build_race keeps 0x3200 (seat 0).
    // read_exe_data throws when the table is missing/unreadable.
    const uint32_t row = 0x00464218u + 8u * uint32_t(level);
    const auto position = read_exe_data(exe, row + 4, 4);
    const uint32_t start = uint32_t(position[0]) | uint32_t(position[1]) << 8 | uint32_t(position[2]) << 16 |
                           uint32_t(position[3]) << 24;
    // Finish: first-match level-tagged CESR finish (0x004525c0 scan,
    // 0x00453e60 section+0x2c, 0x00454240 global DAT_004b6e58 =
    // section_base + finish*0x100) on route A. Branch bits are enumerated
    // explicitly (all false) rather than via the shorthand overload.
    const auto splits = split_nodes(course);
    const auto route = make_route_choices(course, std::vector<int>(splits.size(), 0));
    const int32_t finish_sample = finish_event_sample(course, route, level);
    if (finish_sample < 0) throw std::runtime_error("No level-tagged finish event on route A");
    const uint32_t finish = uint32_t(finish_sample) << 8;
    if (!(start < finish) || finish > (uint32_t(route.size() - 1) << 8))
        throw std::runtime_error("Finish line outside route A");
    return RaceBounds{start, finish, 0};
}

int32_t finish_event_sample(const Course& course, const std::vector<RouteSample>& route, int level) {
    // 0x004525c0 scans CESR entries (header20, stride12) for raw type 6 with
    // signed parameter byte +9 such that parameter-level==1; it returns the
    // first matching local start sample, stored at section+0x2c by 0x00453e60.
    // Route samples carry section/local metadata, so scan them in order.
    if (level < 0 || level > 4) throw std::runtime_error("Finish level out of range");
    for (size_t i = 0; i < route.size(); ++i) {
        const auto& s = route[i];
        const auto& section = course.sections.at(s.section);
        for (const auto& e : section.events) {
            if (e.raw_type != 6 || int(e.params[0]) - level != 1) continue;
            if (int32_t(s.local) != e.start) continue;
            // Graph-relative event-start sample = route index of the matching local start.
            return int32_t(i);
        }
    }
    return -1;
}

std::vector<RouteSample> make_route_indexed(const Course& course, const std::vector<int>& choices,
                                            const std::vector<uint32_t>& splits_by_node, int global_branch) {
    std::vector<RouteSample> route;
    std::set<uint32_t> visited;
    struct Split {
        size_t index;
        uint32_t other;
        int branch;
    };
    struct Merge {
        size_t index;
        uint8_t half;
        uint32_t other;
    };
    std::vector<Split> splits;
    std::vector<Merge> merges;
    uint32_t offset = course.root, arrived = 0;
    for (size_t guard = 0; guard <= course.nodes.size(); ++guard) {
        const auto& node = node_at(course, offset);
        if (node.kind >= 3) break;
        if (node.kind == 0) {
            const auto& section = course.sections.at(node.link);
            for (uint32_t i = 0; i < node.length; ++i)
                route.push_back({section.samples.at(i), offset, node.link, i});
            arrived = offset;
            offset = node.next;
        } else if (node.kind == 1) {
            auto it = std::find(splits_by_node.begin(), splits_by_node.end(), offset);
            if (it == splits_by_node.end()) throw std::runtime_error("Split node missing from census order");
            const int branch = choices.empty() ? global_branch : choices[size_t(it - splits_by_node.begin())];
            splits.push_back({route.size(), branch ? node.previous : node.next, branch});
            offset = branch ? node.previous : node.next;
        } else {
            const uint8_t half = arrived == node.next ? 0 : 1;
            merges.push_back({route.size(), half, half ? node.next : node.previous});
            offset = node.link;
        }
        if (guard == course.nodes.size()) throw std::runtime_error("Cycle in forward course route");
    }
    if (route.empty()) throw std::runtime_error("Empty course route");
    // Replay the lookahead traversal (0x00453030) along the route.
    auto merge_at = [&](size_t i) {
        for (auto& m : merges)
            if (m.index == i) return &m;
        return (Merge*)nullptr;
    };
    auto split_at = [&](size_t i) {
        for (auto& s : splits)
            if (s.index == i) return &s;
        return (Split*)nullptr;
    };
    bool dual = false, after_split = false;
    uint8_t primary = 0;
    int32_t own_offset = 0, other_offset = 0, dx_sep = 0, x_sep = 0;
    std::vector<RouteSample> other;
    size_t other_index = 0;
    // Merge window states from the backward walk of 0x00453b60, indexed by distance to merge.
    struct Snapshot {
        size_t other_index;
        int32_t dx_sep, x_sep;
    };
    std::vector<Snapshot> snapshot;
    int32_t snapshot_offset = 0;
    const Merge* pending = nullptr;
    for (size_t i = 0; i < route.size(); ++i) {
        auto& sample = route[i];
        auto halves = [&](const PathSample& own, const PathSample& o) {
            return primary ? std::pair<const PathSample*, const PathSample*>{&o, &own}
                           : std::pair<const PathSample*, const PathSample*>{&own, &o};
        };
        if (const Merge* m = merge_at(i)) {
            // Merge reached: keep one road; the kept edge stays aligned.
            const auto& before = route[i - 1].path;
            own_offset += primary ? lane_shift(before.right_q8 - sample.path.right_q8)
                                  : lane_shift(sample.path.left_q8 - before.left_q8);
            dual = false;
            after_split = false;
            pending = nullptr;
            (void)m;
            sample.event = RoadEvent::merged;
        } else if (const Split* s = split_at(i)) {
            other = branch_forward(course, s->other);
            if (other.empty()) throw std::runtime_error("Empty split branch");
            primary = uint8_t(s->branch);
            other_index = 0;
            const auto& before = route[i - 1].path;
            auto [h0, h1] = halves(sample.path, other[0].path);
            const int32_t o0 = own_offset + lane_shift(h0->left_q8 - before.left_q8);
            const int32_t o1 = own_offset + lane_shift(before.right_q8 - h1->right_q8);
            own_offset = primary ? o1 : o0;
            other_offset = primary ? o0 : o1;
            dx_sep = h1->bend - h0->bend;
            x_sep = 0;
            dual = true;
            after_split = true;
            sample.event = RoadEvent::split;
        } else if (dual) {
            // Primary crossing into a new road node, or past the window, drops the other road (0x00452fd0).
            if (after_split && (sample.node != route[i - 1].node || (sample.local << 8) > (window << 8))) {
                dual = false;
                after_split = false;
                primary = 0;
            } else {
                if (other_index + 1 < other.size()) ++other_index;
                auto [h0, h1] = halves(sample.path, other[other_index].path);
                x_sep += dx_sep;
                dx_sep += h1->bend - h0->bend;
            }
        } else {
            // 81 samples before a merge the traversal restores its precomputed dual snapshot.
            const auto& node = node_at(course, sample.node);
            const auto& next = node_at(course, node.next);
            if (next.kind == 2 && node.length - sample.local == window) {
                pending = nullptr;
                for (auto& m : merges)
                    if (m.index > i && (!pending || m.index < pending->index)) pending = &m;
                if (!pending || pending->index != i + window)
                    throw std::runtime_error("Merge window mismatch");
                primary = pending->half;
                other = branch_backward(course, pending->other);
                if (other.empty()) throw std::runtime_error("Empty merge branch");
                // Backward walk from the merge (0x00453b60 -> 0x00453030, negative steps).
                std::vector<const PathSample*> mine;
                for (size_t k = i; k < pending->index; ++k) mine.push_back(&route[k].path);
                snapshot.assign(window, {});
                auto other_at = [&](size_t d) { return other.size() > d ? other.size() - 1 - d : 0; };
                auto d_at = [&](size_t d) {
                    auto [h0, h1] = halves(*mine[mine.size() - 1 - d], other[other_at(d)].path);
                    return h1->bend - h0->bend;
                };
                int32_t s5 = -d_at(0), s6 = 0;
                for (size_t d = 0; d < window; ++d) {
                    snapshot[d] = {other_at(d), s5, s6};
                    s5 -= d_at(d);
                    s6 -= s5;
                }
                // Offsets relative to the outgoing road, as the backward merge entry aligns them.
                const auto& out = route[pending->index].path;
                auto [h0, h1] = halves(*mine.back(), other.back().path);
                const int32_t o0 = lane_shift(h0->left_q8 - out.left_q8),
                              o1 = lane_shift(out.right_q8 - h1->right_q8);
                snapshot_offset = primary ? o0 - o1 : o1 - o0;
                other_offset = own_offset + snapshot_offset;
                dual = true;
                after_split = false;
                sample.event = RoadEvent::merge_window;
            }
        }
        if (dual && pending && !after_split) {
            const auto& snap = snapshot[pending->index - 1 - i];
            other_index = snap.other_index;
            dx_sep = snap.dx_sep;
            x_sep = snap.x_sep;
        }
        sample.offset = own_offset;
        if (dual) {
            sample.dual = true;
            sample.half = primary;
            sample.other = other[other_index].path;
            sample.other_node = other[other_index].node;
            sample.other_section = other[other_index].section;
            sample.other_local = other[other_index].local;
            sample.other_offset = other_offset;
            sample.dx_sep = dx_sep;
            sample.x_sep = x_sep;
        }
    }
    return route;
}
} // namespace rr
