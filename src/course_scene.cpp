#include "course_scene.hpp"
#include <cstdlib>
#include <cstdio>
#include "upscale_import.hpp"
#include "exe_tables.hpp"
#include "terrain_ground.hpp"
#include <algorithm>
#include <cctype>
#include <cstring>
#include <set>

namespace rr {
namespace {
int32_t bits(uint32_t v) {
    int32_t r;
    std::memcpy(&r, &v, 4);
    return r;
}
int32_t wrap(int64_t v) {
    return bits(uint32_t(v));
}
// Building heights read the depth table at depth+256 (0x00454240 forward
// height) and depth+shoulder; at the farthest slice (depth up to 8448) that
// reaches index 4352, one past the 4352 positive entries at 0x0046f098. The
// original reads the two zero words that follow (0x004734a0 holds other
// data), so those indexes scale to 0. Napa never has a building at slice 32;
// HIWAY and CANYN do, and an .at() there aborted the race.
int32_t depth_scale(const DepthTable& depths, int32_t depth) {
    if (depth < 0) throw std::runtime_error("Building depth out of range");
    const size_t index = size_t(depth & ~1) / 2;
    if (index < depths.scale.size()) return depths.scale[index];
    if (index < depths.scale.size() + 2) return 0;
    throw std::runtime_error("Building depth out of range");
}
// 0x00416310: the high-resolution horizon is Images/Horizons/<name>, the name
// read through the pointer table 0x00467aa0 indexed by course number
// (City, Sierra, Penin, Napa, PCH). Course data files use other names
// (HIWAY.CRS is Sierra), so the horizon cannot reuse the .CRS name.
std::string horizon_name(const std::vector<uint8_t>& exe, const std::string& course) {
    static const char* const files[5] = {"CITY", "HIWAY", "MEDLY", "NAPA", "CANYN"}; // 0x0046df58
    const auto it = std::find(std::begin(files), std::end(files), course);
    if (it == std::end(files)) return course;
    const auto table = read_exe_data(exe, 0x00467aa0 + uint32_t(it - std::begin(files)) * 4, 4);
    uint32_t address = 0;
    for (int b = 0; b < 4; ++b) address |= uint32_t(table[size_t(b)]) << (b * 8);
    std::string name;
    for (const uint8_t c : read_exe_data(exe, address, 16)) {
        if (!c) break;
        name.push_back(char(std::toupper(c)));
    }
    return name;
}
} // namespace
CourseScene::CourseScene(const Assets& assets, const std::vector<uint8_t>& exe, const Course& course,
                         const std::string& name, uint32_t seed, int level)
    : roads_(read_road_materials(exe, assets.read("DATA/GLOBAL.RSC"))),
      logarithms_(read_exe_data(exe, 0x00476af8, 256)),
      horizon_(assets.read("IMAGES/HORIZONS/" + horizon_name(exe, name) + ".BOB")) {
    if (horizon_.size() != 720 * 320) throw std::runtime_error("Invalid course horizon");
    // 0x00441870: CEL 0x10 (WATER128) is the cliff-foot water template.
    {
        const auto global = assets.read("DATA/GLOBAL.RSC");
        for (const auto& entry : read_resources(global))
            if (entry.tag == "CEL " && entry.id == 0x10)
                water_ = read_cel(std::vector<uint8_t>(global.begin() + entry.offset,
                                                       global.begin() + entry.offset + entry.size));
    }
    {
        auto word = [](const std::vector<uint8_t>& b, size_t p) {
            uint32_t v = 0;
            for (int k = 0; k < 4; ++k) v |= uint32_t(b[p + size_t(k)]) << (k * 8);
            return bits(v);
        };
        side_street_run_ = read_exe_data(exe, 0x00465a40, 1).at(0);
        const auto table = read_exe_data(exe, 0x00465a54, 32);
        for (size_t i = 0; i < 8; ++i) arch_[i] = word(table, i * 4);
        const auto words = read_exe_data(exe, 0x004766f0, 257 * 4);
        for (size_t i = 0; i < 257; ++i) sine_words_[i] = word(words, i * 4);
        const auto bytes = read_exe_data(exe, 0x00476870, 128);
        std::copy(bytes.begin(), bytes.end(), sine_bytes_.begin());
    }
    const auto joins = read_exe_data(exe, 0x004688e0, 128);
    for (int i = 0; i < 16; ++i) {
        auto word = [&](int p) {
            uint32_t v = 0;
            for (int b = 0; b < 4; ++b) v |= uint32_t(joins[p + b]) << (b * 8);
            return bits(v);
        };
        joins_[i] = {word(i * 8), word(i * 8 + 4)};
    }
    std::set<uint32_t> needed;
    for (const auto& section : course.sections) {
        terrain_.emplace_back();
        family_slots_.emplace_back();
        tunnels_.emplace_back();
        event_selectors_.emplace_back();
        // The original terrain helpers consume CRT rand values. This isolated
        // deterministic stream has the same recurrence; full race random-call
        // interleaving is still a separate integration requirement.
        auto random = [&seed]() {
            seed = seed * 214013u + 2531011u;
            return (seed >> 16) & 0x7fffu;
        };
        TerrainIterator iterator(section.terrain, 0, random);
        std::array<uint16_t, 4> slots{};
        size_t event = 0;
        for (size_t j = 0; j < section.samples.size(); ++j) {
            if (j) iterator.step(256);
            const auto& path = section.samples[j];
            auto t = iterator.slice(-path.left - path.shoulder_left, path.right + path.shoulder_right);
            if (t.type == 4 && path.marking_flags && !(path.marking_flags & 0x40))
                for (auto& point : t.points) point[1] = 0;
            terrain_.back().push_back(t);
            // 0x00454240: marking flag 0x40 makes the sub-slice type 3, a
            // tunnel; drawn as one only on mountain samples (all disc cases),
            // whose wall values the tunnel geometry needs.
            tunnels_.back().push_back(t.type == 1 && (path.marking_flags & 0x40));
            event_selectors_.back().push_back(path.event_parameters[0]);
            // 0x00454240 makes the sub-slice type 3 for flag 0x40. The race
            // reads this slice too (0x00439b10 sidewalk heights, surface class).
            if (path.marking_flags & 0x40) terrain_.back().back().type = 3;
            // Resolve residency at the projection window's forward cursor.
            const int64_t cursor =
                std::max<int64_t>(0, int64_t(j) + view_slices + section.family_forward_bias);
            while (event < section.family_events.size() && section.family_events[event].sample <= cursor) {
                const auto& e = section.family_events[event++];
                if (e.type == 1 || e.type == 3) slots[e.slot] = e.family;
                // Keep data resident across unload/preload requests. Original
                // selectors stop referring to that slot until its replacement.
                needed.insert(e.family);
            }
            family_slots_.back().push_back(slots);
        }
        for (const auto& e : section.family_events) needed.insert(e.family);
        objects_.emplace_back(section.samples.size());
        actors_.emplace_back(section.samples.size());
        for (const auto& object : roadside_objects(section, random))
            objects_.back().at(object.sample).push_back(object);
        auto placed = place_dzhr(section, level, random);
        for (const auto& object : placed.objects) objects_.back().at(object.sample).push_back(object);
        for (const auto& actor : placed.actors) actors_.back().at(actor.sample).push_back(actor);
    }
    const std::array<std::pair<uint32_t, uint32_t>, 7> tables{{{0x0046de48, 6},
                                                               {0x0046de78, 1},
                                                               {0x0046de80, 2},
                                                               {0x0046de90, 6},
                                                               {0x0046dec0, 2},
                                                               {0x0046ded0, 6},
                                                               {0x0046df00, 6}}};
    for (size_t code = 0; code < tables.size(); ++code)
        pedestrian_tables_[code] = read_exe_data(exe, tables[code].first, tables[code].second * 8);
    const auto file = assets.read("DATA/FAMILIES.RSC");
    for (const auto& entry : read_resources(file))
        if (needed.count(entry.id) && entry.size > 24)
            families_.emplace(entry.id,
                              read_family(std::vector<uint8_t>(file.begin() + entry.offset,
                                                               file.begin() + entry.offset + entry.size)));
    for (size_t section = 0; section < terrain_.size(); ++section)
        for (size_t local = 0; local < terrain_[section].size(); ++local) {
            auto& t = terrain_[section][local];
            if (t.type != 0) continue;
            for (int side = 0; side < 2; ++side) {
                const auto selector = t.selectors[side];
                if (selector == 255) continue;
                const auto family = families_.find(family_slots_[section][local][selector >> 6]);
                if (family == families_.end()) throw std::runtime_error("Missing building family");
                const auto offset = family->second.resolve(1, selector & 63);
                if (!offset || !family->second.groups.count(*offset))
                    throw std::runtime_error("Missing building group");
                const auto& group = family->second.groups.at(*offset);
                const int height = group.nominal_height * 16;
                if (height < 17) {
                    t.selectors[side] = 255;
                    continue;
                }
                t.building_height[side] = height;
                const auto& cell = group.select(0x08000000, 0x08000000, logarithms_);
                t.building_cut_roof[side] = !cell.texture.mips.empty() &&
                                            !cell.texture.mips[0].pixels.empty() &&
                                            cell.texture.mips[0].pixels[0] == 0;
            }
        }
}
const TerrainSlice& CourseScene::terrain(uint32_t section, uint32_t local) const {
    return terrain_.at(section).at(local);
}
void CourseScene::apply_ring_terrain(uint32_t section, uint32_t local, const PathSample& path,
                                     const TerrainSlice& slice) {
    if (slice.type != 1 && slice.type != 4) return;
    auto& t = terrain_.at(section).at(local);
    base_saved_.emplace(std::make_pair(section, local), t); // keeps the first (original) value
    const uint8_t type = t.type;
    t.walls = slice.walls;
    t.points = slice.points;
    t.shades = slice.shades;
    if (slice.type == 4 && path.marking_flags && !(path.marking_flags & 0x40))
        for (auto& point : t.points) point[1] = 0;
    t.type = type;
}
const TerrainSlice& CourseScene::base_terrain(uint32_t section, uint32_t local) const {
    const auto it = base_saved_.find({section, local});
    return it != base_saved_.end() ? it->second : terrain_.at(section).at(local);
}
std::vector<uint32_t> CourseScene::terrain_section_sizes() const {
    std::vector<uint32_t> out;
    for (const auto& s : terrain_) out.push_back(uint32_t(s.size()));
    return out;
}
// The ring's sub-slice rules for a wall/hill sample (0x00454240), shared by
// the render view and the simulation overlay.
void ring_terrain_onto(TerrainSlice& t, const PathSample& path, const TerrainSlice& slice) {
    const uint8_t type = t.type;
    t.walls = slice.walls;
    t.points = slice.points;
    t.shades = slice.shades;
    if (slice.type == 4 && path.marking_flags && !(path.marking_flags & 0x40))
        for (auto& point : t.points) point[1] = 0;
    t.type = type;
}
bool CourseScene::tunnel(uint32_t section, uint32_t local) const {
    return tunnels_.at(section).at(local);
}
int32_t CourseScene::rail_height(uint32_t section, uint32_t local) const {
    const auto* group = rail_group(section, local, event_selectors_.at(section).at(local));
    return group ? ((1 << (group->row_shift & 31)) << (group->last_row & 31)) * 4 : 0;
}
const CelGroup* CourseScene::building_group(uint32_t section, uint32_t local, uint8_t selector) const {
    const uint16_t id = family_slots_.at(section).at(local)[selector >> 6];
    const auto family = families_.find(id);
    if (family == families_.end()) return nullptr;
    const auto offset = family->second.resolve(1, selector & 63);
    if (!offset) return nullptr;
    const auto it = family->second.groups.find(*offset);
    return it == family->second.groups.end() ? nullptr : &it->second;
}
const CelGroup* CourseScene::rail_group(uint32_t section, uint32_t local, uint8_t selector) const {
    // 0x0044ac00(selector, 6): category 6 entry, a cel group without frames.
    const uint16_t id = family_slots_.at(section).at(local)[selector >> 6];
    const auto family = families_.find(id);
    if (family == families_.end()) return nullptr;
    const auto offset = family->second.resolve(6, selector & 63);
    if (!offset) return nullptr;
    const auto it = family->second.groups.find(*offset);
    return it == family->second.groups.end() ? nullptr : &it->second;
}
int32_t CourseScene::sine(int32_t angle) const {
    // 0x0044f770: 512 steps per turn. Below 0x80 it reads bytes at 0x00476870,
    // which lie inside the word table at 0x004766f0, exactly as original.
    if (angle < 0) return -sine(-angle);
    const int32_t index = angle & 0x1ff;
    if (index < 0x80) return sine_bytes_[size_t(index)];
    if (index <= 0x100) return sine_words_[size_t(index)];
    return -sine(0x100 - (angle & 0xff));
}
int32_t CourseScene::approximate_angle(int32_t x, int32_t y) {
    // 0x0044f5a0: octant angle, 256 steps per turn.
    if (x == 0 && y == 0) return 0;
    const int32_t ax = x < 0 ? -x : x, ay = y < 0 ? -y : y;
    const int octant = (x < 0 ? 1 : 0) | (y < 0 ? 2 : 0) | (ax < ay ? 4 : 0);
    switch (octant) {
    case 0:
        return ay * 32 / ax;
    case 1:
        return 0x80 - ay * 32 / ax;
    case 2:
        return -(ay * 32 / ax);
    case 3:
        return ay * 32 / ax - 0x80;
    case 4:
        return 0x40 - ax * 32 / ay;
    case 5:
        return ax * 32 / ay + 0x40;
    case 6:
        return ax * 32 / ay - 0x40;
    default:
        return -0x40 - ax * 32 / ay;
    }
}
const CelTexture* CourseScene::tunnel_material(uint32_t section, uint32_t local, uint8_t selector,
                                               uint32_t lit, uint8_t kind, int32_t width,
                                               int32_t height) const {
    // 0x0040ccf0 / 0x0044ada0: category 3, entry lit (0/1), group kind; the
    // lit set falls back to the plain one when its group is missing.
    const uint16_t id = family_slots_.at(section).at(local)[selector >> 6];
    const auto family = families_.find(id);
    if (family == families_.end()) return nullptr;
    auto group = [&](uint32_t entry) -> const CelGroup* {
        const auto offset = family->second.resolve(3, selector & 63, entry);
        if (!offset) return nullptr;
        const auto node = family->second.nodes.find(*offset);
        if (node == family->second.nodes.end() || node->second.tag || kind >= node->second.children.size())
            return nullptr;
        const auto it = family->second.groups.find(node->second.children[kind]);
        return it == family->second.groups.end() ? nullptr : &it->second;
    };
    const CelGroup* chosen = lit ? group(1) : nullptr;
    if (!chosen) chosen = group(0);
    return chosen ? &chosen->select(width, height, logarithms_).texture : nullptr;
}
void CourseScene::attach_upscaled(const UpscaleCache& cache) {
    const UpscaleIndex index = cache.content_index({2, 4}, {UpscaleMethod::mmpx, UpscaleMethod::ai});
    auto one = [&](CelTexture& tex) { attach_upscaled_indexed(tex, index); };
    for (auto& [id, family] : families_)
        for (auto& [offset, group] : family.groups)
            for (auto& cell : group.cells) one(cell.texture);
    for (auto& entry : roads_.entries)
        if (entry.texture) one(*entry.texture);
    for (auto& terrain : roads_.terrain)
        if (terrain.texture) one(*terrain.texture);
    if (water_.texture) one(*water_.texture);
}
void CourseScene::clear_upscaled() {
    for (auto& [id, family] : families_)
        for (auto& [offset, group] : family.groups)
            for (auto& cell : group.cells) cell.texture.upscaled.clear();
    for (auto& entry : roads_.entries)
        if (entry.texture) entry.texture->upscaled.clear();
    for (auto& terrain : roads_.terrain)
        if (terrain.texture) terrain.texture->upscaled.clear();
    if (water_.texture) water_.texture->upscaled.clear();
}
const std::vector<SceneryObject>& CourseScene::objects(uint32_t section, uint32_t local) const {
    return objects_.at(section).at(local);
}
const AnimationFrame* CourseScene::object_frame(uint32_t section, uint32_t local,
                                                const SceneryObject& object) const {
    const uint16_t id = family_slots_.at(section).at(local)[object.selector >> 6];
    const auto family = families_.find(id);
    if (family == families_.end()) return nullptr;
    const auto offset =
        family->second.resolve(object.static_family ? 4 : 0, object.selector & 63,
                               object.static_family ? std::optional<uint32_t>(0) : std::nullopt);
    if (!offset) return nullptr;
    const auto node = family->second.nodes.find(*offset);
    if (node == family->second.nodes.end() || node->second.tag != 0x5252414e) return nullptr;
    const auto key = std::make_pair(uint32_t(id), *offset);
    auto animation = animations_.find(key);
    if (animation == animations_.end())
        animation = animations_.emplace(key, read_animation(family->second.bytes, *offset)).first;
    if (animation->second.frames.size() < 3) return nullptr;
    return &animation->second.frames[2];
}
const AnimationFrame* CourseScene::static_frame(uint32_t section, uint32_t local, uint8_t selector,
                                                bool billboard) const {
    SceneryObject object;
    object.selector = selector;
    object.static_family = billboard;
    return object_frame(section, local, object);
}
const std::vector<DzhrActor>& CourseScene::dzhr_actors(uint32_t section, uint32_t local) const {
    return actors_.at(section).at(local);
}
const PedestrianLook* CourseScene::pedestrian_look(uint32_t section, uint32_t local, uint8_t selector) const {
    const uint16_t id = family_slots_.at(section).at(local)[selector >> 6];
    const auto key = std::make_pair(uint32_t(id), uint32_t(selector & 63));
    if (const auto it = pedestrians_.find(key); it != pedestrians_.end())
        return it->second ? &*it->second : nullptr;
    auto& slot = pedestrians_[key];
    const auto family = families_.find(id);
    if (family == families_.end()) return nullptr;
    const auto& f = family->second;
    const auto& bytes = f.bytes;
    const auto graphics = f.resolve(2, selector & 63, 0), cans = f.resolve(2, selector & 63, 1);
    if (!graphics || !cans) return nullptr;
    const auto node = f.nodes.find(*graphics);
    if (node == f.nodes.end() || node->second.tag != 0x5252414e) return nullptr;
    auto word = [&](size_t p) {
        if (p + 4 > bytes.size()) throw std::runtime_error("Truncated pedestrian resource");
        return uint32_t(bytes[p]) | uint32_t(bytes[p + 1]) << 8 | uint32_t(bytes[p + 2]) << 16 |
               uint32_t(bytes[p + 3]) << 24;
    };
    // The CANS resource is five chunks (SNAC header, three cel/info chunks,
    // the code table); its extent is their summed sizes.
    size_t end = *cans;
    for (int chunk = 0; chunk < 5; ++chunk) {
        const uint32_t extent = word(end + 4);
        if (extent < 8 || end + extent > bytes.size()) return nullptr;
        end += extent;
    }
    const std::vector<uint8_t> layout(bytes.begin() + *cans, bytes.begin() + long(end));
    PedestrianLook look;
    look.graphics = read_animation(bytes, *graphics);
    look.views = read_actor_graphics(layout);
    for (const auto& table : pedestrian_tables_) {
        const auto sequence = read_actor_sequence(layout, table);
        if (!sequence) {
            look.bank.push_back(std::nullopt);
            continue;
        }
        ActorSequence frames;
        for (const auto& descriptor : *sequence) frames.push_back(descriptor.playback);
        look.bank.push_back(std::move(frames));
    }
    for (int k = 0; k < 2; ++k)
        if (const auto voice = f.resolve(2, selector & 63, uint32_t(2 + k))) {
            const uint32_t v = word(*voice + 8);
            look.voices[size_t(k)] =
                int32_t((v >> 24) | ((v >> 8) & 0xff00) | ((v << 8) & 0xff0000) | (v << 24));
        }
    slot = std::move(look);
    return &*slot;
}
const CelTexture* CourseScene::material(uint32_t section, uint32_t local, uint8_t selector, uint8_t category,
                                        std::optional<uint32_t> frame, int32_t width, int32_t height) const {
    const uint16_t id = family_slots_.at(section).at(local)[selector >> 6];
    const auto family = families_.find(id);
    if (family == families_.end()) return nullptr;
    const auto offset = family->second.resolve(category, selector & 63, frame);
    if (!offset) return nullptr;
    const auto group = family->second.groups.find(*offset);
    if (group == family->second.groups.end()) return nullptr;
    return &group->second.select(width, height, logarithms_).texture;
}
void CourseScene::draw_horizon(Framebuffer& fb, int32_t scroll, int32_t logical_width) const {
    // Plan 19 Task 4: rows and scroll offsets scale with the surface; the
    // 720x320 source is nearest-neighbour sampled until plan 20 upscales it.
    // Plan 21 Task 8: the surface is logical_w*2s x 480s; the horizon spans
    // the full wide surface (Hor+: same rows, more columns).
    const int s = fb.height() / 480;
    if (s < 1 || fb.height() != 480 * s || fb.width() != logical_width * 2 * s)
        throw std::runtime_error("Horizon needs a wide*2s x 480s surface");
    const int32_t start = int32_t(((-int64_t(scroll) % 720) + 720) % 720);
    fb.clear(0);
    // Centre the 720-wide source: the 4:3 picture is the centre crop.
    const int xoff = (720 - logical_width * 2) / 2;
    for (int y = 0; y < 320 * s; ++y)
        for (int x = 0; x < fb.width(); ++x)
            fb.row(y)[x] = horizon_[size_t(y / s) * 720 + ((x / s + xoff + start) % 720 + 720) % 720];
}
std::vector<DrawNode> CourseScene::draw_nodes(const std::vector<RouteSample>& route,
                                              const std::vector<RoadSlice>& slices, const Camera& camera,
                                              const DepthTable& depths, ActorEmitter actors,
                                              int32_t logical_width) const {
    struct Road {
        const PathSample* path;
        const TerrainSlice* terrain;
        TerrainSlice terrain_image;
        bool merge_hills = false;
        int inner_bands = 3;
        uint32_t section, local;
        HillRoad hill[2];
        BuildingRoad building[2];
        int32_t ceiling;
        int32_t wall_height[2];
        // Drawing type: the terrain type, or 3 for a mountain tunnel sample
        // (0x00454240, marking flag 0x40). Tunnel +0x50..+0x5c, +0xb4..+0xe0,
        // +0xf4/+0xf8 and 0x0040d420's angle terms.
        uint8_t kind;
        uint8_t mountain;
        int32_t offset;
        std::array<RasterPoint, 2> edge;
        std::array<int32_t, 2> curb_height;
        std::array<RasterPoint, 6> arch;
        RasterPoint tunnel_point;
        int32_t sine, cosine;
    };
    std::vector<std::array<Road, 2>> roads(slices.size());
    // Plan 19 Task 4: the ceiling stays logical (240 Q16 view rows); only
    // ViewMode::map converts it to surface rows at render time.
    int32_t ceiling = 240 * 65536;
    // 0x004ba180, reset to -1 per frame (0x00443a00) and updated by
    // 0x00444630 near to far: a hill strip makes it 0, then a plain building
    // strip 1 or a flag-16 building strip its sample. 0x0040c450 draws 16
    // side-street facades up to that sample, else 8.
    int64_t side_street_mark = -1;
    for (size_t n = 0; n < slices.size(); ++n) {
        const auto& slice = slices[n];
        const auto& sample = route.at(slice.sample);
        std::optional<MergeHillProfiles> joined;
        if (slice.count == 2) {
            size_t start = slice.sample;
            while (start > 0 && route[start - 1].dual) --start;
            if (route[start].event == RoadEvent::merge_window) {
                const bool first = sample.half == 0;
                const auto& p0 = first ? sample.path : sample.other;
                const auto& p1 = first ? sample.other : sample.path;
                const auto& t0 = terrain(first ? sample.section : sample.other_section,
                                         first ? sample.local : sample.other_local);
                const auto& t1 = terrain(first ? sample.other_section : sample.section,
                                         first ? sample.other_local : sample.local);
                if (t0.type == 4 && t1.type == 4)
                    joined =
                        merge_hill_profiles(t0, t1, p0.right + p0.shoulder_right, -p1.left - p1.shoulder_left,
                                            slice.road[1].road_x - slice.road[0].road_x);
            }
        }
        for (int r = 0; r < slice.count; ++r) {
            auto& road = roads[n][r];
            const bool own = slice.count == 1 || r == slice.own;
            road.section = own ? sample.section : sample.other_section;
            road.local = own ? sample.local : sample.other_local;
            road.path = own ? &sample.path : &sample.other;
            road.terrain_image = joined ? joined->terrain[r] : terrain(road.section, road.local);
            road.terrain = &road.terrain_image;
            road.merge_hills = joined.has_value();
            road.inner_bands = joined ? joined->bands[r] : 3;
            road.kind = tunnel(road.section, road.local) ? 3 : road.terrain->type;
            if (road.terrain->type == 4 && side_street_mark < 0)
                side_street_mark = 0;
            else if (road.terrain->type == 0 && side_street_mark == 0) {
                if (road.path->marking_flags == 0)
                    side_street_mark = 1;
                else if (road.path->marking_flags & 0x10)
                    side_street_mark = slice.sample;
            }
            road.offset = own ? sample.offset : sample.other_offset;
            const auto& s = slice.road[r];
            const int32_t world_x = s.road_x + camera.x;
            // 0x00454240 wall values (wall_profile): a tunnel halves the
            // shoulders and its empty pair-B heights become 16 (a sidewalk).
            WallProfile profile;
            std::array<int32_t, 2> outer_edge = {s.shoulder_left, s.shoulder_right};
            road.mountain = 0;
            road.curb_height = {0, 0};
            if (road.kind == 1 || road.kind == 3) {
                profile = wall_profile(*road.terrain, *road.path, road.kind == 3);
                road.mountain = profile.mountain;
            }
            if (road.kind == 3) {
                outer_edge = {profile.outer_left, profile.outer_right};
                road.curb_height =
                    road.path->height_left == 0
                        ? std::array<int32_t, 2>{16, 16}
                        : std::array<int32_t, 2>{road.path->height_left, road.path->height_right};
            }
            int32_t max_row = s.screen_y;
            HillProjection hills;
            if (road.terrain->type == 4) {
                hills = project_hills(*road.terrain, world_x, s.world_y, camera, slice.scale, logical_width);
                max_row = hills.maximum_row;
            }
            for (int side = 0; side < 2; ++side) {
                // A tunnel keeps the mountain type for its hill neighbours.
                auto& h = road.hill[side];
                h.type = road.kind == 3 ? uint8_t(1) : road.terrain->type;
                h.depth = slice.depth;
                const int32_t shoulder = outer_edge[size_t(side)];
                // 0x00454240 clears the pair B heights of type-1 sides; 0x00444630
                // keeps +0x64/+0x6c at the road row for types 1-3.
                const int32_t side_height = road.kind == 1 || road.kind == 3
                                                ? 0
                                                : (side ? road.path->height_right : road.path->height_left);
                const int32_t shoulder_y =
                    wrap(int64_t(camera.height - s.world_y - side_height) * slice.scale + 90 * 65536);
                h.world[0] = {world_x + shoulder, s.world_y};
                h.projected[0] = {wrap(slice.screen_x(r, shoulder)), shoulder_y};
                if (road.terrain->type == 0) {
                    auto& building = road.building[side];
                    building.shoulder = h.projected[0];
                    building.type = 0;
                    building.selector = road.terrain->selectors[side];
                    building.marking_flags = road.path->marking_flags;
                    const int32_t shoulder_width =
                        side ? road.path->shoulder_right : road.path->shoulder_left;
                    const int32_t ns = (building.marking_flags & 16)
                                           ? depth_scale(depths, slice.depth + shoulder_width)
                                           : slice.scale;
                    const int32_t fd =
                        slice.depth + 256 - ((building.marking_flags & 8) ? shoulder_width : 0);
                    building.height = wrap(int64_t(road.terrain->building_height[side]) * ns);
                    building.forward_height =
                        wrap(int64_t(road.terrain->building_height[side]) * depth_scale(depths, fd));
                    building.roof_offset = (side ? 1 : -1) * (building.height / 2);
                    building.forward_roof_offset = (side ? 1 : -1) * (building.forward_height / 2);
                    building.cut_roof = road.terrain->building_cut_roof[side];
                } else {
                    road.building[side].type = road.terrain->type;
                    road.building[side].shoulder = h.projected[0];
                }
                for (int band = 0; band < 3; ++band) {
                    const auto& point = road.terrain->points[band * 2 + side];
                    h.world[band + 1] = {world_x + point[0], s.world_y + point[1]};
                    h.projected[band + 1] = {hills.points[band * 2 + side][0],
                                             hills.points[band * 2 + side][1]};
                }
                road.wall_height[side] = 0;
                if (road.kind == 1 || road.kind == 3) {
                    // 0x00444630 projects the base at the road row and the top
                    // height*scale above it (+0x70/+0x80).
                    const int32_t base = profile.base[size_t(side)], top = profile.top[size_t(side)],
                                  height = profile.height[size_t(side)];
                    road.wall_height[side] = height;
                    h.world[1] = {world_x + base, s.world_y};
                    h.world[2] = h.world[3] = {world_x + top, s.world_y + height};
                    h.projected[1] = {wrap(slice.screen_x(r, base)), s.screen_y};
                    h.projected[2] = h.projected[3] = {wrap(slice.screen_x(r, top)),
                                                       wrap(s.screen_y - int64_t(height) * slice.scale)};
                }
            }
            if (road.kind == 1 || road.kind == 3) {
                // 0x00444630 types 1-3 occlusion row (param_3[7]): with a right
                // side below the road, the lower of the two wall tops; else the
                // road row, or the left top when only the left drops.
                const int32_t left_top = road.hill[0].projected[2].y, right_top = road.hill[1].projected[2].y;
                if (road.wall_height[1] < 0)
                    max_row = std::max(left_top, right_top);
                else
                    max_row = road.wall_height[0] >= 0 ? s.screen_y : left_top;
            }
            if (road.kind == 3) {
                // 0x00444630 type 3: arch points from the 0x00465a54 table
                // (heights) at road-width fractions, +0xf4/+0xf8 half a lane
                // beyond the open side's shoulder at the arch height.
                auto row = [&](int32_t h) { return wrap(s.screen_y - int64_t(h) * slice.scale); };
                auto across = [&](int32_t fraction) {
                    const int32_t v = wrap(int64_t(fraction) * (s.right - s.left));
                    return wrap(int64_t((v + ((v >> 31) & 0xff)) >> 8) * slice.scale +
                                slice.screen_x(r, s.left));
                };
                const int32_t left_x = road.hill[0].projected[0].x, right_x = road.hill[1].projected[0].x;
                road.edge = {RasterPoint{wrap(slice.screen_x(r, s.left)), row(road.curb_height[0])},
                             RasterPoint{wrap(slice.screen_x(r, s.right)), row(road.curb_height[1])}};
                road.arch = {RasterPoint{left_x, road.edge[0].y},
                             {left_x, row(arch_[0])},
                             {across(arch_[1]), row(arch_[2])},
                             {across(arch_[3]), row(arch_[4])},
                             {right_x, row(arch_[6])},
                             {right_x, road.edge[1].y}};
                const int32_t half_lane = wrap(int64_t(250) * slice.scale) / 2;
                road.tunnel_point = road.wall_height[1] > 0
                                        ? RasterPoint{wrap(int64_t(left_x) - half_lane), road.arch[2].y}
                                        : RasterPoint{wrap(int64_t(right_x) + half_lane), road.arch[3].y};
                // 0x0040d420's per-sample portal lean (0x0047ea84/0x0047ea80).
                const int32_t span = wrap(int64_t(outer_edge[1] - outer_edge[0]) * arch_[1]);
                const int32_t angle =
                    approximate_angle((span + ((span >> 31) & 0xff)) >> 8, arch_[2] - arch_[0]);
                const int32_t lean = wrap((int64_t(angle) * -65536 + 0xc00000) / 2 - 0x400000);
                road.sine = wrap(int64_t(sine(lean >> 14)) * 256);
                road.cosine = wrap(int64_t(sine(wrap(0x200000 - int64_t(lean)) >> 14)) * 256);
            }
            road.ceiling = ceiling;
            ceiling = std::min(ceiling, max_row);
        }
    }
    for (size_t n = 0; n + 1 < slices.size(); ++n)
        for (int r = 0; r < std::min(slices[n].count, slices[n + 1].count); ++r) {
            auto& near = roads[n][r];
            const auto& far = roads[n + 1][r];
            if (near.terrain->type != 0 || !(near.path->marking_flags & 24)) continue;
            for (int side = 0; side < 2; ++side) {
                const int32_t width = side ? near.path->shoulder_right : near.path->shoulder_left;
                const int32_t fraction = (near.path->marking_flags & 8) ? 256 - width : width;
                const int32_t depth = slices[n].depth + fraction;
                const int32_t scale = depth_scale(depths, depth);
                const int32_t nx = near.hill[side].world[0].x, fx = far.hill[side].world[0].x;
                const int32_t ny = near.hill[side].world[0].y +
                                   (side ? near.path->height_right : near.path->height_left),
                              fy = far.hill[side].world[0].y +
                                   (side ? far.path->height_right : far.path->height_left);
                const int32_t x = nx + (wrap(int64_t(fx - nx) * fraction) >> 8);
                const int32_t y = ny + (wrap(int64_t(fy - ny) * fraction) >> 8);
                near.building[side].transition = {
                    wrap(int64_t(x - camera.x) * scale + (logical_width & ~1) * 32768),
                    wrap(int64_t(camera.height - y) * scale + 90 * 65536)};
            }
        }
    std::vector<DrawNode> result;
    auto append = [&](const std::vector<DrawNode>& nodes) {
        result.insert(result.end(), nodes.begin(), nodes.end());
    };
    // Native ordering (not yet an original-golden match): actors in the front
    // half of a sample (+0x1c low byte < 0x80) are emitted after the road
    // surface of the next nearer pair. A rider standing on a sample's near
    // edge (start grid: low byte 0) reaches below that edge, and the nearer
    // road strip drawn afterwards otherwise cut its tyres along a scanline.
    std::vector<DrawNode> front;
    // 0x0041d260: a hill/building side's shoulder ends at its shoulder edge
    // (+0x60); a mountain side's at its wall base (+0x70, world +0x14c).
    auto outer = [](const Road& road) { return road.kind == 0 || road.kind == 4 ? 0 : 1; };
    // 0x00443d70: the camera sample's sub-slice is a tunnel.
    const bool camera_in_tunnel = !slices.empty() && roads[0][0].kind == 3;
    std::vector<DrawNode> rails;
    for (size_t n = slices.size(); n-- > 1;) {
        const auto& near = slices[n - 1];
        const auto& far = slices[n];
        const int count = std::min(near.count, far.count);
        std::vector<DrawNode> pending;
        pending.swap(front);
        for (int i = 0; i < count; ++i) {
            const int r = count == 2 ? (i == 0 ? 1 - near.own : near.own) : 0;
            const auto& a = far.road[r];
            const auto& b = near.road[r];
            const auto& road = roads[n - 1][r];
            const auto& old = roads[n][r];
            const bool visible = b.screen_y >= a.screen_y && road.ceiling >= a.screen_y;
            const auto& path = *road.path;
            const int32_t whole_left = -((path.left_q8 + 255) / 256) * 250;
            RoadPanelInput panels;
            // 0x00444630 updates the previous strip's +0x30 using that
            // strip's own +0xd8 lane origin at the new far scale. Both ends
            // must share its lane grid: using the next sample's rounded left
            // count shifts every far endpoint by a lane at width transitions.
            const int32_t far_origin = wrap(int64_t(whole_left) + road.offset - old.offset);
            panels.first_left = wrap(far.screen_x(r, far_origin));
            panels.first_y = a.screen_y;
            panels.first_lane_width = wrap(int64_t(250) * far.scale);
            panels.second_left = wrap(near.screen_x(r, whole_left));
            panels.second_y = b.screen_y;
            panels.second_lane_width = wrap(int64_t(250) * near.scale);
            panels.near_depth_q8 = near.depth;
            panels.far_depth_q8 = far.depth;
            panels.left_lanes_q8 = path.left_q8;
            panels.right_lanes_q8 = path.right_q8;
            panels.left_lane_count = uint8_t((path.left_q8 + 255) / 256);
            panels.right_lane_count = uint8_t((path.right_q8 + 255) / 256);
            panels.marking_flags = path.marking_flags;
            panels.terrain_type = road.terrain->type;
            panels.next_terrain_type = old.terrain->type;
            panels.visible = visible;
            append(road_draw_nodes(road_panels(panels, logical_width), roads_));
            // 0x0043bef0 draws marked ground (0x0043b970) only for types 0 and 4.
            if ((path.marking_flags & 32) && road.kind != 1 && road.kind != 3)
                for (int side = 0; side < 2; ++side) {
                    MarkedGroundInput ground;
                    ground.near_edge = {wrap(near.screen_x(r, side ? b.right : b.left)), b.screen_y};
                    ground.far_edge = {wrap(far.screen_x(r, side ? a.right : a.left)), a.screen_y};
                    ground.near_scale = near.scale;
                    ground.far_scale = far.scale;
                    ground.logical_width = logical_width;
                    ground.side = uint8_t(side);
                    ground.marking_flags = path.marking_flags;
                    ground.strips = road.terrain->type == 0 ? 8 : 16;
                    ground.near_depth_q8 = near.depth;
                    ground.far_depth_q8 = far.depth;
                    append(marked_ground_nodes(ground, [&](uint8_t index) {
                        return index == 3 ? &*roads_.entries[0].texture : &*roads_.terrain.at(index).texture;
                    }));
                }
            if (i == count - 1) {
                append(pending);
                pending.clear();
            }
            if (!(path.marking_flags & 32) && road.terrain->type == 4)
                for (int side = 0; side < 2; ++side) {
                    if (visible) {
                        ShoulderPanelInput shoulder;
                        shoulder.near_depth = near.depth;
                        shoulder.has_lanes = panels.left_lane_count + panels.right_lane_count != 0;
                        shoulder.strip.side = uint8_t(side);
                        shoulder.strip.far_depth = far.depth;
                        shoulder.logical_width = logical_width;
                        shoulder.strip.world = {
                            {{a.road_x + camera.x + (side ? a.right : a.left), a.world_y},
                             old.hill[side].world[outer(old)],
                             road.hill[side].world[outer(road)],
                             {b.road_x + camera.x + (side ? b.right : b.left), b.world_y}}};
                        shoulder.strip.projected = {
                            {{wrap(far.screen_x(r, side ? a.right : a.left)), old.hill[side].projected[0].y},
                             old.hill[side].projected[outer(old)],
                             road.hill[side].projected[outer(road)],
                             {wrap(near.screen_x(r, side ? b.right : b.left)),
                              road.hill[side].projected[0].y}}};
                        append(shoulder_draw_nodes(shoulder, camera, depths, [&](int32_t w, int32_t h) {
                            return material(road.section, road.local, path.side_flags[side], 5, 0, w, h);
                        }));
                    }
                    if (path.marking_flags & 24) {
                        // 0x0043bef0 type 4, flags 8/16: shoulder followed
                        // by 0x0041d110's cross road, rather than hillside.
                        if (visible) {
                            HillIntersectionInput crossing;
                            crossing.side = uint8_t(side);
                            crossing.logical_width = logical_width;
                            crossing.near_edge = {road.hill[side].projected[0].x, b.screen_y};
                            crossing.far_edge = {old.hill[side].projected[0].x, a.screen_y};
                            crossing.near_scale = near.scale;
                            crossing.far_scale = far.scale;
                            crossing.near_depth_q8 = near.depth;
                            crossing.far_depth_q8 = far.depth;
                            append(hill_intersection_nodes(crossing, [&](int32_t w, int32_t h) {
                                return material(road.section, road.local, path.side_flags[side], 5, 2, w, h);
                            }));
                        }
                        continue;
                    }
                    HillPanelInput hills;
                    hills.near = road.hill[side];
                    hills.far = old.hill[side];
                    hills.side = uint8_t(side);
                    hills.occlusion_row = road.ceiling;
                    hills.overlap = true;
                    hills.join_offsets = joins_;
                    hills.logical_width = logical_width;
                    if (road.merge_hills && side == 1 - r) {
                        // 0x00443cd0 / 0x0041d500: inward profiles meet the
                        // neighboring branch. Only exterior hills extend to
                        // the viewport boundary.
                        hills.extend_outer = false;
                        hills.band_count = std::max(road.inner_bands, old.inner_bands);
                        if (i == count - 1 && far.count == 2 &&
                            std::max(roads[n - 1][0].inner_bands, roads[n][0].inner_bands) > 3) {
                            const auto& h0 = roads[n - 1][0];
                            append(merge_hill_bridge(
                                {roads[n][0].hill[1].projected[3], roads[n][1].hill[0].projected[3],
                                 roads[n - 1][1].hill[0].projected[3], h0.hill[1].projected[3]},
                                far.depth, near.depth, logical_width, [&](int32_t w, int32_t h) {
                                    return material(h0.section, h0.local, h0.terrain->selectors[1], 1, 2, w,
                                                    h);
                                }));
                        }
                    }
                    append(hill_draw_nodes(hills, camera, depths, [&](uint8_t band, int32_t w, int32_t h) {
                        return material(road.section, road.local, road.terrain->selectors[side], 1, band, w,
                                        h);
                    }));
                }
            // 0x0043bef0 types 1-3: shoulder while the pair is visible (not
            // between two tunnel samples), then the mountain side; on the
            // tunnel's mountain side also the arch (0x0040dac0) and, at the
            // mouth, the portal (0x0040d420).
            if (road.kind == 1 || road.kind == 3)
                for (int side = 0; side < 2; ++side) {
                    const bool tunnel_pair = road.kind == 3 && old.kind == 3;
                    if (visible && !tunnel_pair) {
                        ShoulderPanelInput shoulder;
                        shoulder.near_depth = near.depth;
                        shoulder.has_lanes = panels.left_lane_count + panels.right_lane_count != 0;
                        shoulder.strip.side = uint8_t(side);
                        shoulder.strip.far_depth = far.depth;
                        shoulder.logical_width = logical_width;
                        shoulder.strip.world = {
                            {{a.road_x + camera.x + (side ? a.right : a.left), a.world_y},
                             old.hill[side].world[outer(old)],
                             road.hill[side].world[outer(road)],
                             {b.road_x + camera.x + (side ? b.right : b.left), b.world_y}}};
                        shoulder.strip.projected = {
                            {{wrap(far.screen_x(r, side ? a.right : a.left)), old.hill[side].projected[0].y},
                             old.hill[side].projected[outer(old)],
                             road.hill[side].projected[outer(road)],
                             {wrap(near.screen_x(r, side ? b.right : b.left)),
                              road.hill[side].projected[0].y}}};
                        append(shoulder_draw_nodes(shoulder, camera, depths, [&](int32_t w, int32_t h) {
                            return material(road.section, road.local, path.side_flags[side], 5, 0, w, h);
                        }));
                    }
                    if (old.kind != 0 && old.kind != 1 && old.kind != 3 && old.kind != 4) continue;
                    WallPanelInput wall;
                    wall.near = road.hill[side];
                    wall.far = old.hill[side];
                    wall.side = uint8_t(side);
                    wall.height = road.wall_height[side];
                    wall.near.type = road.kind;
                    wall.far.type = old.kind;
                    // A building neighbour: 0x00444630 case 0 leaves its facade
                    // offsets/heights at +0x70/+0x80, and 0x0040dfd0/0x0040d030
                    // read them as that end's base and top.
                    if (old.kind == 0) {
                        const auto& facade = old.building[side];
                        wall.far.projected[1] = {facade.roof_offset, facade.height};
                        wall.far.projected[2] = {facade.forward_roof_offset, facade.forward_height};
                    }
                    wall.near_lane_width = wrap(int64_t(250) * near.scale);
                    wall.far_lane_width = wrap(int64_t(250) * far.scale);
                    wall.logical_width = logical_width;
                    wall.near_tunnel = road.kind == 3;
                    wall.far_tunnel = old.kind == 3;
                    wall.camera_in_tunnel = camera_in_tunnel;
                    wall.near_tunnel_point = road.tunnel_point;
                    wall.far_tunnel_point = old.tunnel_point;
                    append(wall_draw_nodes(
                        wall,
                        [&](uint8_t frame, int32_t w, int32_t h) {
                            return material(road.section, road.local, road.terrain->selectors[side], 1, frame,
                                            w, h);
                        },
                        water_.texture ? &*water_.texture : nullptr));
                    if (road.wall_height[side] < 1 || !tunnel_pair || near.depth <= 0x3c) continue;
                    // 0x0040dac0: raised sidewalks (0x0040bde0 with category-3
                    // frames 2/3 of the CESR selector +0x39), then the arch.
                    const uint8_t selector = path.event_parameters[0];
                    for (int walk = 0; walk < 2; ++walk) {
                        BuildingShoulderInput sidewalk;
                        sidewalk.near_depth = near.depth;
                        sidewalk.far_base_y = a.screen_y;
                        sidewalk.near_base_y = b.screen_y;
                        sidewalk.far_world_base_y = a.world_y;
                        sidewalk.near_world_base_y = b.world_y;
                        sidewalk.logical_width = logical_width;
                        sidewalk.top.side = uint8_t(walk);
                        sidewalk.top.far_depth = far.depth;
                        sidewalk.top.occlusion_row = road.ceiling;
                        const int32_t fy = old.curb_height[size_t(walk)], ny = road.curb_height[size_t(walk)];
                        sidewalk.top.projected = {
                            {old.edge[size_t(walk)],
                             {old.hill[walk].projected[0].x, old.edge[size_t(walk)].y},
                             {road.hill[walk].projected[0].x, road.edge[size_t(walk)].y},
                             road.edge[size_t(walk)]}};
                        sidewalk.top.world = {
                            {{a.road_x + camera.x + (walk ? a.right : a.left), a.world_y + fy},
                             {old.hill[walk].world[0].x, a.world_y + fy},
                             {road.hill[walk].world[0].x, b.world_y + ny},
                             {b.road_x + camera.x + (walk ? b.right : b.left), b.world_y + ny}}};
                        append(building_shoulder_nodes(sidewalk, camera, depths,
                                                       [&](uint8_t frame, int32_t w, int32_t h) {
                                                           return material(road.section, road.local, selector,
                                                                           3, uint32_t(frame + 2), w, h);
                                                       }));
                    }
                    TunnelArchInput arch;
                    arch.near = road.arch;
                    arch.far = old.arch;
                    arch.near_row = b.screen_y;
                    arch.near_depth = near.depth;
                    arch.far_depth = far.depth;
                    arch.logical_width = logical_width;
                    arch.backdrop = n + 1 == slices.size();
                    // 0x0040dac0 picks set ((sample&3)+1)>>2, but 0x0040ccf0 copies
                    // the plain entries over set 1 unless its group's word at -8
                    // is 'CLGP'. In every captured tunnel strip (88, capture
                    // pch-tunnel-20261016) set 1 equals set 0, so the plain set
                    // is always drawn.
                    const uint32_t lit = 0;
                    append(tunnel_arch_nodes(arch, [&](uint8_t kind, int32_t w, int32_t h) {
                        return tunnel_material(road.section, road.local, selector, lit, kind, w, h);
                    }));
                    // 0x0043bef0: the mouth is the pair whose nearer neighbour
                    // (sub-slice +0x18) is not a tunnel sample.
                    const auto& before = route.at(near.sample > 0 ? near.sample - 1 : near.sample);
                    if (near.sample > 0 && tunnel(before.section, before.local)) continue;
                    TunnelPortalInput portal;
                    portal.mountain = road.mountain;
                    portal.base = {road.hill[0].projected[1], road.hill[1].projected[1]};
                    portal.top = {road.hill[0].projected[2], road.hill[1].projected[2]};
                    portal.tunnel_point = road.tunnel_point;
                    portal.arch = road.arch;
                    portal.edge = road.edge;
                    portal.shoulder_x = {road.hill[0].projected[0].x, road.hill[1].projected[0].x};
                    portal.row = b.screen_y;
                    portal.depth = near.depth;
                    portal.logical_width = logical_width;
                    portal.sine = road.sine;
                    portal.cosine = road.cosine;
                    portal.fractions = {arch_[1], arch_[3]};
                    append(tunnel_portal_nodes(portal, [&](uint8_t part, int32_t w, int32_t h) {
                        if (part == 0)
                            return material(road.section, road.local, road.terrain->selectors[road.mountain],
                                            1, 0, w, h);
                        return material(road.section, road.local, selector, 3, part == 1 ? 5u : 4u, w, h);
                    }));
                }
            if (!(path.marking_flags & 32) && road.terrain->type == 0)
                for (int side = 0; side < 2; ++side) {
                    // 0x0043bef0 type 0: 0x0040bde0, then 0x0040ba10 for flags 8/16,
                    // the facade 0x0040c770, and 0x0040c450 for flag 16.
                    BuildingShoulderInput shoulder;
                    shoulder.near_depth = near.depth;
                    shoulder.far_base_y = a.screen_y;
                    shoulder.near_base_y = b.screen_y;
                    shoulder.far_world_base_y = a.world_y;
                    shoulder.near_world_base_y = b.world_y;
                    shoulder.logical_width = logical_width;
                    shoulder.top.side = uint8_t(side);
                    shoulder.top.far_depth = far.depth;
                    shoulder.top.occlusion_row = road.ceiling;
                    const int32_t fy = side ? old.path->height_right : old.path->height_left,
                                  ny = side ? path.height_right : path.height_left;
                    shoulder.top.projected = {
                        {{wrap(far.screen_x(r, side ? a.right : a.left)), old.hill[side].projected[0].y},
                         old.hill[side].projected[0],
                         road.hill[side].projected[0],
                         {wrap(near.screen_x(r, side ? b.right : b.left)), road.hill[side].projected[0].y}}};
                    shoulder.top.world = {
                        {{a.road_x + camera.x + (side ? a.right : a.left), a.world_y + fy},
                         {old.hill[side].world[0].x, a.world_y + fy},
                         {road.hill[side].world[0].x, b.world_y + ny},
                         {b.road_x + camera.x + (side ? b.right : b.left), b.world_y + ny}}};
                    const auto side_material = [&](uint8_t frame, int32_t w, int32_t h) {
                        return material(road.section, road.local, path.side_flags[side], 5, frame, w, h);
                    };
                    append(building_shoulder_nodes(shoulder, camera, depths, side_material));
                    if (path.marking_flags & 0x10)
                        append(side_street_curb_nodes(
                            uint8_t(side), wrap(near.screen_x(r, side ? b.right : b.left)),
                            road.hill[side].projected[0].y, road.hill[side].projected[0].x, b.screen_y,
                            road.ceiling, side_material));
                    const int32_t shoulder_width = std::abs(side ? path.shoulder_right : path.shoulder_left);
                    if (path.marking_flags & 24) {
                        SideStreetInput street;
                        street.side = uint8_t(side);
                        street.marking_flags = path.marking_flags;
                        street.transition = road.building[side].transition;
                        street.near_shoulder_x = road.hill[side].projected[0].x;
                        street.near_curb_y = road.hill[side].projected[0].y;
                        street.near_row = b.screen_y;
                        street.far_shoulder_x = old.hill[side].projected[0].x;
                        street.far_curb_y = old.hill[side].projected[0].y;
                        street.near_scale = depth_scale(depths, near.depth);
                        street.near_shoulder_scale = depth_scale(depths, near.depth + shoulder_width);
                        street.far_scale = depth_scale(depths, far.depth);
                        street.far_shoulder_scale =
                            depth_scale(depths, std::max(0, far.depth - shoulder_width));
                        if (n >= 2) {
                            street.nearer = true;
                            street.nearer_curb_y = roads[n - 2][r].hill[side].projected[0].y;
                        }
                        street.logical_width = logical_width;
                        append(side_street_nodes(street, side_material));
                    }
                    BuildingPanelInput building{road.building[side], old.building[side], uint8_t(side)};
                    building.near_depth_q8 = near.depth;
                    building.far_depth_q8 = far.depth;
                    building.logical_width = logical_width;
                    // The end face (0x0040cbb0) passes quality 6 to 0x0040c3f0, which resets
                    // the template to the group's first cell (0x0044a9b0).
                    append(building_draw_nodes(
                        building, [&](bool use_far, int32_t w, int32_t h) -> const CelTexture* {
                            if (!use_far)
                                return material(road.section, road.local, road.terrain->selectors[side], 1,
                                                std::nullopt, w, h);
                            const auto* group =
                                building_group(old.section, old.local, old.terrain->selectors[side]);
                            return group && !group->cells.empty() ? &group->cells.front().texture : nullptr;
                        }));
                    if (path.marking_flags & 0x10) {
                        SideFacadeInput facade;
                        facade.side = uint8_t(side);
                        facade.selector = road.terrain->selectors[side];
                        facade.has_building = road.building[side].height != 0;
                        facade.transition = road.building[side].transition;
                        facade.scale = depth_scale(depths, near.depth + shoulder_width);
                        facade.count = int64_t(near.sample) <= side_street_mark ? 16 : 8;
                        facade.run_length = side_street_run_;
                        facade.logical_width = logical_width;
                        append(side_facade_nodes(
                            facade,
                            [&](uint32_t index) -> const CelGroup* {
                                const uint16_t id = family_slots_.at(road.section)
                                                        .at(road.local)[(facade.selector & 0xc0) >> 6];
                                const auto family = families_.find(id);
                                if (family == families_.end()) return nullptr;
                                const auto offset = family->second.resolve(1, index);
                                if (!offset) return nullptr;
                                const auto it = family->second.groups.find(*offset);
                                return it == family->second.groups.end() ? nullptr : &it->second;
                            },
                            logarithms_));
                    }
                }
            // 0x00443af0 draws a strip's scenery (0x00443b40) after the next
            // nearer strip's terrain: the previous pair's guard rails go here.
            if (i == count - 1) {
                append(rails);
                rails.clear();
            }
            if (i == count - 1 && path.event_type == 5) {
                // 0x00443c00/0x0041c200: CESR event 5 guard rails. Parameter 0
                // selects the category-6 group, 1 is a signed lateral offset
                // from the shoulder, 2 bits 0/1 enable the left/right rail.
                const uint8_t selector = path.event_parameters[0];
                const auto* group = rail_group(road.section, road.local, selector);
                const int32_t half = (logical_width >> 1) * 65536;
                const std::array<int, 2> order =
                    road.hill[0].projected[0].x < half ? std::array<int, 2>{0, 1} : std::array<int, 2>{1, 0};
                for (const int side : order) {
                    if (!group || !(path.event_parameters[2] & (1 << side))) continue;
                    const int32_t lateral =
                        side ? int8_t(path.event_parameters[1]) : -int8_t(path.event_parameters[1]);
                    const int32_t height = ((1 << (group->row_shift & 31)) << (group->last_row & 31)) * 4;
                    const int32_t near_x =
                        wrap(int64_t(lateral) * near.scale + road.hill[side].projected[0].x);
                    const int32_t far_x = wrap(int64_t(lateral) * far.scale + old.hill[side].projected[0].x);
                    const int32_t near_top = wrap(b.screen_y - int64_t(height) * near.scale),
                                  far_top = wrap(a.screen_y - int64_t(height) * far.scale);
                    const int32_t width = logical_width * 65536;
                    if (!((near_x <= width || far_x <= width) && (near_x >= 0 || far_x >= 0))) continue;
                    const int32_t span = near_x > far_x ? near_x - far_x : far_x - near_x;
                    const auto& cell = group->select(span, wrap(int64_t(height) * near.scale), logarithms_);
                    DrawNode rail{{RasterPoint{near_x, near_top}, RasterPoint{far_x, far_top},
                                   RasterPoint{far_x, a.screen_y}, RasterPoint{near_x, b.screen_y}},
                                  &cell.texture,
                                  std::nullopt,
                                  false,
                                  false};
                    rail.flip_u = rail.view_points[1].x < rail.view_points[0].x ||
                                  rail.view_points[2].x < rail.view_points[3].x;
                    rail.flip_v = rail.view_points[3].y < rail.view_points[0].y ||
                                  rail.view_points[2].y < rail.view_points[1].y;
                    rail.view_depth = {near.depth, far.depth, far.depth, near.depth};
                    rails.push_back(rail);
                }
            }
            if (actors) append(actors(near, far, r, road.ceiling, true));
            if (near.depth > 146)
                for (const auto& object : objects(road.section, road.local)) {
                    const auto* frame = object_frame(road.section, road.local, object);
                    if (!frame || !frame->texture) continue;
                    int32_t ground =
                        terrain_height(*road.terrain, path, *old.terrain, *old.path, object.lateral * 256,
                                       128, a.road_x - b.road_x, road.offset);
                    if (ground > 998) ground = 0;
                    auto draw = scenery_node(*frame, object, (a.road_x + b.road_x) / 2 + camera.x,
                                             (a.world_y + b.world_y) / 2, ground, near.depth, camera, depths,
                                             logical_width);
                    if (draw.view_points[0].y < road.ceiling) result.push_back(draw);
                }
            if (actors) {
                const auto nodes = actors(near, far, r, road.ceiling, false);
                front.insert(front.end(), nodes.begin(), nodes.end());
            }
        }
        append(pending);
    }
    append(rails);
    append(front);
    return result;
}
} // namespace rr
