#include "assets.hpp"
#include "course.hpp"
#include "exe_tables.hpp"
#include "golden.hpp"
#include "race_setup.hpp" // complete rr::RaceBounds (defined by the race-setup owner)
#include <filesystem>
#include <iostream>
#include <set>
#include <string>
#include <vector>
int main() {
    int failures = 0;
    auto check = [&](bool ok, const char* message) {
        if (!ok) {
            ++failures;
            std::cerr << message << '\n';
        }
    };
    // Original 0x00453030 enters node.next as half0 and node.previous
    // as half1. Whichever route is selected, its other slice must refer
    // to the opposite section and carry that section's events.
    {
        rr::Course c;
        c.root = 20;
        rr::CourseSection trunk, left, right;
        trunk.samples.resize(300, rr::PathSample{0, 0, 500, 500, 512, 512});
        left.samples.resize(300, rr::PathSample{1, 0, 250, 500, 256, 512});
        right.samples.resize(325, rr::PathSample{3, 0, 500, 250, 512, 256});
        right.samples[29].event_type = 3;
        right.samples[29].zone_end = true;
        right.samples[29].event_start = 10;
        right.samples[29].event_end = 29;
        c.sections = {trunk, left, right, trunk};
        c.nodes.emplace(20, rr::CourseNode{20, 0, 40, 160, 0, 300});
        c.nodes.emplace(40, rr::CourseNode{40, 1, 60, 80, 20});
        c.nodes.emplace(60, rr::CourseNode{60, 0, 100, 40, 1, 300});
        c.nodes.emplace(80, rr::CourseNode{80, 0, 100, 40, 2, 325});
        c.nodes.emplace(100, rr::CourseNode{100, 2, 60, 80, 120});
        c.nodes.emplace(120, rr::CourseNode{120, 0, 140, 100, 3, 300});
        c.nodes.emplace(140, rr::CourseNode{140, 4});
        c.nodes.emplace(160, rr::CourseNode{160, 3});
        for (bool second : {false, true}) {
            const auto route = rr::make_route(c, second);
            const auto& entry = route[300];
            check(entry.dual && entry.other_node == (second ? 60u : 80u) &&
                      entry.other_section == (second ? 1u : 2u) && entry.other.bend == (second ? 1 : 3),
                  "Split annotation retains the opposite branch for both route choices");
            const auto& event = route[329];
            check(second ? (event.path.zone_end && !event.other.zone_end)
                         : (!event.path.zone_end && event.other.zone_end && event.other.event_type == 3),
                  "Opposite-half intersection endpoint survives the route annotation");
        }
    }
    rr::Assets assets(RR_SOURCE_DIR "/../cdrom/ROADRASH");
    if (!assets.exists("DATA/COURSES/NAPA.CRS")) {
        std::cout << "No imported course corpus\n";
        return failures ? 1 : 0;
    }
    size_t route_count = 0, dual_count = 0, event_count = 0;
    // Plan 17 prerequisites (docs/research/post-11a-2026-10-12/simulation-and-courses.md):
    // split-node census (sequence of choices keyed to split identity) and
    // level-tagged finish recovery 0x004525c0 (first raw-type-6 event whose
    // parameter[0]-level==1). Expected graph-relative event-start samples from
    // analysis/oracle/post11a-research/census.json (26 branch combos total).
    struct ComboExpect {
        uint32_t split;
        int branch;
    };
    struct CourseExpect {
        const char* name;
        std::vector<std::vector<int>> combos;
        size_t samples;
        int finish[5];
    };
    const std::vector<CourseExpect> census{
        {"CITY", {{0}, {1}}, 5606, {1821, 2840, 3865, 4510, 5419}},
        {"HIWAY", {{0, 0}, {1, 0}, {0, 1}, {1, 1}}, 0, {0, 0, 0, 0, 0}},
        {"MEDLY",
         {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {1, 1, 0}, {0, 0, 1}, {1, 0, 1}, {0, 1, 1}, {1, 1, 1}},
         0,
         {0, 0, 0, 0, 0}},
        {"NAPA",
         {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {1, 1, 0}, {0, 0, 1}, {1, 0, 1}, {0, 1, 1}, {1, 1, 1}},
         0,
         {0, 0, 0, 0, 0}},
        {"CANYN", {{0, 0}, {1, 0}, {0, 1}, {1, 1}}, 0, {0, 0, 0, 0, 0}},
    };
    // Full per-route expectations from analysis/oracle/post11a-research/census.json:
    // {route length, level 1..5 first-match finish event-start samples}.
    struct RouteExpect {
        std::vector<int> combo;
        size_t samples;
        int finish[5];
    };
    const std::vector<RouteExpect> hiway{{{{0, 0}}, 6005, {1810, 2650, 3295, 4205, 5566}},
                                         {{{1, 0}}, 5985, {1790, 2630, 3275, 4185, 5546}},
                                         {{{0, 1}}, 6095, {1810, 2650, 3295, 4295, 5656}},
                                         {{{1, 1}}, 6075, {1790, 2630, 3275, 4275, 5636}}};
    const std::vector<RouteExpect> medly{{{{0, 0, 0}}, 5634, {1818, 2719, 3642, 4963, 5532}},
                                         {{{1, 0, 0}}, 5618, {1802, 2703, 3626, 4947, 5516}},
                                         {{{0, 1, 0}}, 5681, {1818, 2719, 3689, 5010, 5579}},
                                         {{{1, 1, 0}}, 5665, {1802, 2703, 3673, 4994, 5563}},
                                         {{{0, 0, 1}}, 5614, {1818, 2719, 3642, 4943, 5512}},
                                         {{{1, 0, 1}}, 5598, {1802, 2703, 3626, 4927, 5496}},
                                         {{{0, 1, 1}}, 5661, {1818, 2719, 3689, 4990, 5559}},
                                         {{{1, 1, 1}}, 5645, {1802, 2703, 3673, 4974, 5543}}};
    const std::vector<RouteExpect> napa{{{{0, 0, 0}}, 6224, {1715, 2565, 3570, 4280, 6164}},
                                        {{{1, 0, 0}}, 6249, {1740, 2590, 3595, 4305, 6189}},
                                        {{{0, 1, 0}}, 6294, {1715, 2565, 3640, 4350, 6234}},
                                        {{{1, 1, 0}}, 6319, {1740, 2590, 3665, 4375, 6259}},
                                        {{{0, 0, 1}}, 6224, {1715, 2565, 3570, 4280, 6164}},
                                        {{{1, 0, 1}}, 6249, {1740, 2590, 3595, 4305, 6189}},
                                        {{{0, 1, 1}}, 6294, {1715, 2565, 3640, 4350, 6234}},
                                        {{{1, 1, 1}}, 6319, {1740, 2590, 3665, 4375, 6259}}};
    const std::vector<RouteExpect> canyn{{{{0, 0}}, 5423, {1862, 2719, 3685, 4215, 5361}},
                                         {{{1, 0}}, 5443, {1882, 2739, 3705, 4235, 5381}},
                                         {{{0, 1}}, 5423, {1862, 2719, 3685, 4215, 5361}},
                                         {{{1, 1}}, 5443, {1882, 2739, 3705, 4235, 5381}}};
    for (const auto& expect : census) {
        const auto course = rr::read_course(assets.read(std::string("DATA/COURSES/") + expect.name + ".CRS"));
        const auto splits = rr::split_nodes(course);
        const std::string tag = expect.name;
        check(splits.size() == expect.combos[0].size(), ("Split count for " + tag).c_str());
        size_t combos = 1 << splits.size();
        check(expect.combos.size() == combos, ("Branch combination count for " + tag).c_str());
        std::set<size_t> lengths;
        const std::vector<RouteExpect>* table = nullptr;
        if (tag == "HIWAY")
            table = &hiway;
        else if (tag == "MEDLY")
            table = &medly;
        else if (tag == "NAPA")
            table = &napa;
        else if (tag == "CANYN")
            table = &canyn;
        for (const auto& combo : expect.combos) {
            const auto route = rr::make_route_choices(course, combo);
            lengths.insert(route.size());
            for (int level = 1; level <= 5; ++level) {
                const auto at = rr::finish_event_sample(course, route, level - 1);
                if (expect.finish[0])
                    check(at == expect.finish[level - 1], ("Level-tagged finish for " + tag).c_str());
                else
                    check(at > 0, ("Positive level-tagged finish for " + tag).c_str());
            }
            if (table)
                for (const auto& row : *table)
                    if (row.combo == combo) {
                        check(route.size() == row.samples, ("Route length for " + tag).c_str());
                        for (int level = 1; level <= 5; ++level)
                            check(rr::finish_event_sample(course, route, level - 1) == row.finish[level - 1],
                                  ("Route finish L" + std::to_string(level) + " for " + tag).c_str());
                    }
        }
        if (tag == "CITY")
            check(lengths == std::set<size_t>{5606}, "CITY route length");
        else
            check(lengths.size() > 1 || splits.size() == 1, "Distinct physical route lengths differ");
    }
    // Plan 17 Task 1: CLI-to-original course mapping (plan 17 global
    // constraints; original order from the 0x0046df58 string table).
    check(rr::original_course_index(0) == 4, "CLI NAPA maps to original 4");
    check(rr::original_course_index(1) == 1, "CLI CITY maps to original 1");
    check(rr::original_course_index(2) == 2, "CLI HIWAY maps to original 2");
    check(rr::original_course_index(3) == 5, "CLI CANYN maps to original 5");
    check(rr::original_course_index(4) == 3, "CLI MEDLY maps to original 3");
    for (int bad : {-1, 5, 6}) {
        bool threw = false;
        try {
            (void)rr::original_course_index(bad);
        } catch (const std::runtime_error&) {
            threw = true;
        }
        check(threw, ("CLI course rejects " + std::to_string(bad)).c_str());
    }
    if (!assets.exists("ROADRASH.EXE"))
        std::cout << "bounds checks skipped, no EXE\n";
    else {
        // Per-course/per-level start+finish bounds on route A: every split
        // takes branch 0, enumerated explicitly per split (not the
        // all-false/all-true shorthand). Route-A finish samples repeat the
        // per-route expectations above (CITY row, hiway/medly/napa/canyn
        // all-false rows); start is the 0x00464218 player-grid row
        // (0x3200 levels 0..3, 0x3300 level 4).
        const auto exe = assets.read("ROADRASH.EXE");
        struct BoundsCourse {
            const char* name;
            int original;
            int route_a_finish[5];
        };
        const std::vector<BoundsCourse> bounds_courses{
            {"CITY", 1, {1821, 2840, 3865, 4510, 5419}},  {"HIWAY", 2, {1810, 2650, 3295, 4205, 5566}},
            {"MEDLY", 3, {1818, 2719, 3642, 4963, 5532}}, {"NAPA", 4, {1715, 2565, 3570, 4280, 6164}},
            {"CANYN", 5, {1862, 2719, 3685, 4215, 5361}},
        };
        auto exe_word = [&](uint32_t address) {
            const auto bytes = rr::read_exe_data(exe, address, 4);
            return uint32_t(bytes[0]) | uint32_t(bytes[1]) << 8 | uint32_t(bytes[2]) << 16 |
                   uint32_t(bytes[3]) << 24;
        };
        for (const auto& bounds_course : bounds_courses) {
            const std::string tag = bounds_course.name;
            const auto course = rr::read_course(assets.read(std::string("DATA/COURSES/") + tag + ".CRS"));
            const auto splits = rr::split_nodes(course);
            const auto route_a = rr::make_route_choices(course, std::vector<int>(splits.size(), 0));
            for (int level = 0; level <= 4; ++level) {
                const rr::RaceBounds bounds = rr::race_bounds(exe, course, bounds_course.original, level);
                const std::string where = tag + " L" + std::to_string(level + 1);
                const uint32_t want_start = level < 4 ? 0x3200u : 0x3300u;
                check(bounds.start_position_q8 == want_start, ("Bounds start grid " + where).c_str());
                check(bounds.route_hint == 0, ("Bounds route hint " + where).c_str());
                check(bounds.finish_position_q8 == (uint32_t(bounds_course.route_a_finish[level]) << 8),
                      ("Bounds finish event line " + where).c_str());
                check(bounds.start_position_q8 < bounds.finish_position_q8,
                      ("Bounds start < finish " + where).c_str());
                check(bounds.finish_position_q8 <= (uint32_t(route_a.size() - 1) << 8),
                      ("Bounds finish on route A " + where).c_str());
                // Menu mileage 0x00468f58[(original-1)*5+level] (tenths) is
                // display text, not race bounds
                // (analysis/course-finish-notes.md): reported, not asserted.
                const uint32_t tenths =
                    exe_word(0x00468f58u + 4u * uint32_t((bounds_course.original - 1) * 5 + level));
                std::cout << where << ": start " << bounds.start_position_q8 << " finish "
                          << bounds.finish_position_q8 << " ("
                          << (bounds.finish_position_q8 - bounds.start_position_q8) / 256 << " samples) menu "
                          << tenths << " tenths\n";
            }
            for (int bad_original : {0, 6}) {
                bool threw = false;
                try {
                    (void)rr::race_bounds(exe, course, bad_original, 0);
                } catch (const std::runtime_error&) {
                    threw = true;
                }
                check(threw,
                      ("Bounds rejects original " + std::to_string(bad_original) + " on " + tag).c_str());
            }
            for (int bad_level : {-1, 5}) {
                bool threw = false;
                try {
                    (void)rr::race_bounds(exe, course, bounds_course.original, bad_level);
                } catch (const std::runtime_error&) {
                    threw = true;
                }
                check(threw, ("Bounds rejects level " + std::to_string(bad_level) + " on " + tag).c_str());
            }
        }
    }
    for (const char* name : {"CITY", "HIWAY", "MEDLY", "NAPA", "CANYN"}) {
        const auto course = rr::read_course(assets.read(std::string("DATA/COURSES/") + name + ".CRS"));
        for (const auto& section : course.sections) event_count += section.family_events.size();
        for (bool second : {false, true})
            for (const auto& sample : rr::make_route(course, second)) {
                ++route_count;
                check(sample.local < course.sections.at(sample.section).samples.size(),
                      "Own route section/local metadata");
                if (!sample.dual) continue;
                ++dual_count;
                const auto& node = course.nodes.at(sample.other_node);
                check(node.kind == 0 && node.link == sample.other_section && sample.other_local < node.length,
                      "Other route section/node/local metadata");
                const auto& other = course.sections.at(sample.other_section).samples.at(sample.other_local);
                check(other.bend == sample.other.bend && other.rise == sample.other.rise &&
                          other.left_q8 == sample.other.left_q8 && other.right_q8 == sample.other.right_q8,
                      "Other roadway path corresponds to terrain source");
            }
    }
    std::cout << route_count << " route samples, " << dual_count << " dual references, " << event_count
              << " family records\n";
    const std::string path = rr::golden::oracle_dir() + "/napa-events.tsv";
    if (std::filesystem::exists(path)) {
        try {
            const auto capture = rr::golden::load(path);
            if (capture.exe != "73d7f9744b5b1a9482b3909950cdf3fe34119fe13d023b031e9c6739a60f18a2" ||
                capture.frames.size() != 1)
                throw std::runtime_error("Wrong course event oracle");
            const auto& fields = capture.frames.at(0);
            const auto course = rr::read_course(assets.read("DATA/COURSES/NAPA.CRS"));
            check(fields.at("course.events.sections") == int64_t(course.sections.size()),
                  "Original course section count");
            size_t compared = 0;
            for (size_t section = 0; section < course.sections.size(); ++section) {
                const auto prefix = "course.events." + std::to_string(section) + ".";
                const auto& samples = course.sections[section].samples;
                check(fields.at(prefix + "count") == int64_t(samples.size()),
                      "Original section sample count");
                for (size_t j = 0; j < samples.size(); ++j) {
                    const auto key = prefix + std::to_string(j) + ".";
                    const auto& sample = samples[j];
                    ++compared;
                    if (sample.marking_flags != fields.at(key + "flags") ||
                        sample.event_type != fields.at(key + "value.0")) {
                        check(false, "Original course event type/marking mismatch");
                        std::cerr << section << ':' << j << '\n';
                    }
                    for (int v = 0; v < 3; ++v)
                        check(sample.event_parameters[v] == fields.at(key + "value." + std::to_string(v + 1)),
                              "Original event parameter mismatch");
                }
            }
            std::cout << compared << " original Napa event samples compared\n";
        } catch (const std::exception& e) {
            std::cerr << e.what() << '\n';
            return 1;
        }
    }
    std::cout << failures << " failures\n";
    return failures ? 1 : 0;
}
