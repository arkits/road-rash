#pragma once
#include "course.hpp"
#include "terrain.hpp"
#include <array>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

namespace rr {
// The random-consuming half of the original track ring: the two course
// walkers 0x0049ee30 (front) and 0x0049ea20 (back) that 0x004552e0 builds
// and 0x00454120 steps one slice at a time. Each walker carries the section
// feature iterators that draw from the shared CRT stream:
//   terrain  0x004510c0/0x004516e0 (LIHR init 0x00450fc0, steps 0x00451380;
//            NTMR/DWDR steps 0x00451220), via TerrainIterator;
//   BORR     0x00451b60/0x00451e20, slot walk 0x00451cb0 (run twice per
//            slice build by 0x00454240, drawing when spacing is random);
//   DZHR     0x00452060/0x00452230, actor spawns 0x004520d0 -> 0x004409e0.
// 0x00453e60 jitters and sorts every section's DZHR records (0x00451ee0,
// 0x00451fc0) in course-graph order before the walkers start.
// Split/merge traversal retains two section iterators and builds the
// active half before its opposite.
// One DZHR actor record fired by a slice build: ring_sample is the slice's
// route sample (ring position >> 8), position_q8 the record's position.
struct RingActor {
    int32_t ring_sample = 0;
    uint32_t position_q8 = 0, word0 = 0, word1 = 0, section = 0, local = 0;
};
// Value-only walker snapshots for optional original terrain diagnostics.
// Collidable roadside objects of one ring slice (0x00454240): the BORR
// slots of its second 0x00451cb0 walk (+0x60 count, +0x63 selector, +0x6c
// laterals) and up to two 0x004520d0 billboards (+0x3d, entries +0x40).
// Laterals are units relative to the road centre (slice +0xcc).
struct RingStatics {
    uint8_t borr_count = 0, borr_selector = 0x3f;
    std::array<int32_t, 8> borr{};
    uint8_t billboards = 0;
    struct Billboard {
        uint8_t type = 0, selector = 0;
        int32_t offset = 0;
    };
    std::array<Billboard, 2> billboard{};
};
struct TrackWalkerState {
    int32_t position = 0;
    uint32_t node = 0;
    int32_t borr_position = 0, borr_record = 0, borr_start = 0, borr_end = 0;
    int32_t dzhr_position = 0, dzhr_index = 0;
    int32_t offset = 0; // section +0x0c, world units
    std::optional<TerrainIteratorState> terrain;
};
struct TrackRingState {
    TrackWalkerState front, back;
    TrackWalkerState front_other, back_other;
    bool front_dual = false, back_dual = false;
    uint8_t front_active = 0, back_active = 0;
};
class TrackRing {
  public:
    using Random = std::function<uint32_t()>;
    // Called for each DZHR actor record 0x004520d0 fires, with its lateral
    // jitter draw (lateral = (centre + signed 12 bits) * 256 - 2s + jitter %
    // (4s + 1), s = (word1 & 0xff0000) >> 14). The sink makes the 0x004409e0
    // draws (pool allocation, police).
    using ActorSink = std::function<void(const RingActor&, uint32_t jitter)>;
    // Called at each slice build with the front walker's terrain slice
    // (0x00451910 into the ring slice, 0x00454240), keyed by section and
    // local sample. The race reads ground heights from these slices.
    using SliceSink =
        std::function<void(uint32_t section, uint32_t local, const PathSample&, const TerrainSlice&)>;
    TrackRing(const Course&, int level);
    void set_slice_sink(SliceSink sink) { slice_sink_ = std::move(sink); }
    // Called at each slice build with its collidable statics, keyed by the
    // slice's route sample.
    using StaticSink =
        std::function<void(int32_t ring_sample, uint32_t section, uint32_t local, const RingStatics&)>;
    void set_static_sink(StaticSink sink) { static_sink_ = std::move(sink); }
    // Copyable for race snapshots (plan 34 Task 3): course data is shared,
    // the per-node DZHR record lists are copy-on-write.
    TrackRing(const TrackRing&) = default;
    TrackRing& operator=(const TrackRing&) = default;
    // 0x004552e0 (random part): DZHR jitter, walker init, walk to camera -
    // 0x3000, then 128 slice builds from the front walker.
    void setup(int32_t camera_q8, const Random&, const ActorSink&);
    // 0x00454120 with one slice crossing: back walker step, slice build
    // (old front forward, new back backward), then front walker step.
    // Smaller moves only advance the ring cursors.
    void advance(int32_t delta_q8, const Random&, const ActorSink&);
    int32_t front_q8() const { return front_; }
    int32_t back_q8() const { return back_; }
    bool unsupported() const { return unsupported_; }
    void commit_branch(uint8_t half); // 0x004540e0, shared front/back choice
    TrackRingState diagnostic_state() const;
    // Terrain slice exported by a walker at its current sample.
    TerrainSlice front_terrain(int32_t left_edge, int32_t right_edge) const;

  private:
    struct Borr {
        const std::vector<uint32_t>* words = nullptr;
        int32_t position = 0;       // [2] Q8
        int32_t record = 0;         // [3]
        int32_t start = 0, end = 0; // [9], [10] samples
        int32_t current = 0;        // [11] sample
        uint8_t alternate = 0, left = 0, selector = 0x3f, random = 0, count = 0, units = 0, period = 0;
        int32_t spacing = 0;                                                  // [8]
        void load(int32_t index);                                             // 0x00451a90
        void init(const std::vector<uint32_t>*, int32_t samples, bool start); // 0x00451b60
        void step(int32_t delta);                                             // 0x00451e20
        void slots(const PathSample&, const Random&, RingStatics&) const;     // 0x00451cb0
    };
    struct Dzhr {
        size_t node = 0;      // index into events_ (per course node)
        int32_t position = 0; // [1] Q8
        int32_t index = 0;    // [2]
    };
    struct SectionWalker {
        uint32_t node = 0;
        int32_t position = 0; // Q8 within the node's section
        int32_t offset = 0;   // section +0x0c, road lateral centre
        std::optional<TerrainIterator> terrain;
        Borr borr;
        Dzhr dzhr;
    };
    struct Walker {
        std::array<SectionWalker, 2> halves;
        int32_t position = 0; // traversal +0: route Q8, independent of section position
        uint8_t active = 0;   // traversal +4: half stepped and built first
        uint32_t window = 0;
        uint8_t event = 0;
        bool front = false;
        int32_t countdown = 0, dx_sep = 0, x_sep = 0;
        bool committed = false, collapse_pending = false;
        bool dual = false;
        bool stopped = false;
    };
    // 0x00453b60 retains the fully advanced traversal at window+0x1c;
    // late entry into a split/merge clones its section iterator state.
    struct Window {
        std::array<SectionWalker, 2> halves;
        uint8_t active = 0;
        int32_t dx_sep = 0, x_sep = 0, threshold = 80;
    };
    struct Events {
        uint32_t section = 0;
        std::vector<std::array<uint32_t, 2>> records;
    };
    void probe_window(uint32_t node, int32_t direction);                              // 0x00453b60 draws
    void jitter_events(uint32_t node, std::vector<uint32_t>& visited, const Random&); // 0x00453e60
    void enter(SectionWalker&, uint32_t node, bool start = true);                     // 0x00452cb0
    const PathSample& path_of(const SectionWalker&) const;
    int step_section(SectionWalker&, int32_t delta);                    // 0x00452e10 section result (0/1/2/3)
    void collapse(Walker&);                                             // 0x00452fd0
    void step(Walker&, int32_t delta);                                  // 0x00453030 -> 0x00452e10
    void build_half(SectionWalker&, int32_t ring_q8, const ActorSink&); // 0x00454240
    void build_slice(Walker&, int32_t ring_q8, const ActorSink&);       // 0x00454c80 -> 0x00454240
    const Course* course_;
    int level_;
    std::vector<std::shared_ptr<Events>> events_;          // one per node, DFS order (copy-on-write)
    Events& events_mut(size_t node);                       // clones a node's list shared with a snapshot
    std::vector<std::pair<uint32_t, size_t>> event_index_; // node offset -> events_
    const Random* random_ = nullptr;                       // stream for the current call
    Random forward_;                                       // TerrainIterator's source
    Walker front_walker_, back_walker_;
    std::map<uint32_t, Window> windows_;
    int32_t front_ = 0, back_ = 0;
    bool branch_committed_ = false;
    uint8_t chosen_half_ = 0;
    std::array<uint8_t, 128> slice_events_{};
    bool unsupported_ = false;
    SliceSink slice_sink_;
    StaticSink static_sink_;
};
} // namespace rr
