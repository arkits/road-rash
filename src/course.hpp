#pragma once
#include "resource.hpp"
#include "terrain.hpp"
#include <array>
#include <cstdint>
#include <map>
#include <vector>

namespace rr {
// Per-sample path bytes and road half-widths in world units (250 per lane),
// truncated as in 0x00454240.
struct PathSample {
    int bend, rise;
    int32_t left, right, left_q8, right_q8;
    // RSLD sides (0x00452b00), world units: shoulder width beyond each road edge
    // (0x00454240) and shoulder height (0x00444630). flags: record bytes +16/+17.
    int32_t shoulder_left = 0, shoulder_right = 0, height_left = 0, height_right = 0;
    uint8_t side_flags[2] = {0, 0};
    uint8_t marking_flags = 0, event_type = 0;
    std::array<uint8_t, 3> event_parameters{};
    // 0x00452710 returns 0x80000000 (beyond the byte kept in marking_flags)
    // on an event's end sample, except types 4, 5 and 7. When the forward
    // ring build reaches it, 0x00454240 calls 0x0041ec70: crossing traffic
    // for types 1..3 (0x0041eca0) and traffic density event_parameters[1]
    // (0x0044dd00; 0 = default 6, negative = spawn rate and cruise speed).
    bool zone_end = false;
    // Section-local interval of the event record this sample sits in (0x00452710's
    // record +0/+4); 0x0041eca0 builds the intersection from its start.
    int32_t event_start = 0, event_end = 0;
    // Absolute road height at this sample: HTPR iterator +4 (0x00451970 seeds
    // the header short +0xc, 0x004519f0 adds each passed sample's rise).
    // 0x00454240 copies it to sub-slice +0xc0; a mountain side whose height is
    // below 1 becomes -elevation, a cliff down to height 0 (0x0040d030).
    int32_t elevation = 0;
};
struct CourseEvent {
    int32_t start = 0, end = 0; // CESR interval, section-local samples (0x00452690)
    uint8_t raw_type = 0;       // record byte +8; runtime type is raw+1 (finish = raw 6)
    uint8_t params[3] = {};     // record bytes +9..+11; params[0] is the level tag
};
struct FamilyEvent {
    int32_t sample = 0;
    uint8_t type = 0, slot = 0;
    uint16_t family = 0;
};
struct CourseSection {
    std::vector<PathSample> samples;
    TerrainDefinition terrain;
    std::vector<CourseEvent> events; // raw CESR records in original order (0x004525c0 scans these)
    std::vector<FamilyEvent> family_events;
    int32_t family_forward_bias = 0;
    std::vector<uint32_t> roadside_profiles;             // BORR packed words, in original order
    std::vector<std::array<uint32_t, 2>> scenery_events; // DZHR packed records, before random placement
    // 0x00452230 can inspect one word after the last DZHR record on rewind.
    // Preserve that word from SGS rather than reading past the native vector.
    uint32_t scenery_following_word = 0;
    // 0x00452cb0(...,0) exposes one slice at position length before rewinding.
    PathSample backward_endpoint{0, 0, 500, 500, 512, 512};
};
struct CourseNode {
    uint32_t offset, kind, next = 0, previous = 0, link = 0, length = 0;
    // Split/merge byte +0x19: the half 0x00453030 steps first (traversal [1]).
    int8_t primary = 0;
};
struct Course {
    uint32_t root;
    std::vector<CourseSection> sections;
    std::map<uint32_t, CourseNode> nodes;
};
// Split/merge event recorded in traversal byte +0xb (0x00453030).
enum class RoadEvent : uint8_t { none = 0, merged = 1, split = 2, merge_window = 6 };
struct RouteSample {
    PathSample path;
    uint32_t node, section, local;
    int32_t offset = 0; // lateral centre of this road ([0xd])
    // Second roadway alongside this sample (ring slice kind 2). Halves are
    // numbered by split/merge node fields +8 (0) and +12 (1).
    bool dual = false;
    uint8_t half = 0; // half this route follows
    RoadEvent event = RoadEvent::none;
    PathSample other{};
    uint32_t other_node = 0, other_section = 0, other_local = 0;
    int32_t other_offset = 0;
    int32_t dx_sep = 0, x_sep = 0; // half 1 minus half 0: traversal [5], [6]
};
// Select one physical sub-slice while preserving the opposite half's metadata.
RouteSample route_half(const RouteSample& sample, uint8_t half);
Course read_course(const std::vector<uint8_t>& bytes);
// Split node offsets in traversal order (kind 1 nodes reached from root).
// Census key: one branch bit per split, in this order (26 combos total:
// CITY 2, HIWAY 4, MEDLY 8, NAPA 8, CANYN 4).
std::vector<uint32_t> split_nodes(const Course& course);
// Route with per-split branch choices (0 = node.next, 1 = node.previous).
// make_route(course,second) keeps the all-false/all-true shorthand.
std::vector<RouteSample> make_route_choices(const Course& course, const std::vector<int>& choices);
// At each split choose its first or second outgoing edge. Merge follows link.
// Annotates split/merge windows as the original lookahead traversal would,
// assuming the rider has already committed to the chosen branch (0x00476cd4).
std::vector<RouteSample> make_route(const Course& course, bool second_branch);
// First-match finish recovery 0x004525c0: graph-relative event-start sample
// of the first raw-type-6 event on this route whose parameter[0]-level==1
// (level 1..5; level is 0-based DAT_004b8a12). -1 when no route sample
// carries a qualifying event.
int32_t finish_event_sample(const Course& course, const std::vector<RouteSample>& route, int level);
// Plan 17 Task 1: original per-course/per-level start and finish. RaceBounds
// is defined in race_setup.hpp (the race-setup owner's file, which includes
// this header); forward-declared here so course code and tests can name the
// bounds without depending on the full race-setup stack. Callers need the
// complete type (include race_setup.hpp where the value is used).
struct RaceBounds;
// CLI course (rr-game --course: NAPA 0, CITY 1, HIWAY 2, CANYN 3, MEDLY 4)
// to original course number (0x0046df58/0x004160c0: 1 CITY, 2 HIWAY, 3 MEDLY,
// 4 NAPA, 5 CANYN). Throws std::runtime_error on any other value.
int original_course_index(int cli_course);
// Original per-course/per-level start and finish (plan 17 Task 1 Step 1
// trace): start is the 0x00405600 player-grid position for the level
// (0x00464218 row: 0x3200 at levels 0..3, 0x3300 at level 4); finish is the
// level-tagged CESR finish line (0x004525c0/0x00453e60/0x00454240, global
// DAT_004b6e58) on route A (all-false branch choices, route_hint 0).
// Throws std::runtime_error if the course/level is out of range, the EXE
// tables are unreadable, or no level-tagged finish exists on route A.
RaceBounds race_bounds(const std::vector<uint8_t>& exe, const Course& course, int original_course, int level);
} // namespace rr
