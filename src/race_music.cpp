#include "race_music.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>

#include "assets.hpp"
#include "det_math.hpp"
#include "mixer.hpp"

namespace rr {
namespace {

uint16_t u16le(const std::vector<uint8_t>& b, size_t p) {
    if (p + 2 > b.size()) throw std::runtime_error("MIDS/SF1: truncated u16");
    return uint16_t(b[p]) | (uint16_t(b[p + 1]) << 8);
}

uint32_t u32le(const std::vector<uint8_t>& b, size_t p) {
    if (p + 4 > b.size()) throw std::runtime_error("MIDS/SF1: truncated u32");
    return uint32_t(b[p]) | (uint32_t(b[p + 1]) << 8) | (uint32_t(b[p + 2]) << 16) |
           (uint32_t(b[p + 3]) << 24);
}

// Bounds-checked RIFF chunk walk.
struct Chunk {
    std::string tag;
    size_t payload = 0;
    uint32_t size = 0;
};

std::vector<Chunk> riff_chunks(const std::vector<uint8_t>& b, size_t first, size_t end) {
    std::vector<Chunk> out;
    while (first < end) {
        if (first + 8 > end) throw std::runtime_error("MIDS/SF1: truncated chunk header");
        char tag[5] = {};
        memcpy(tag, b.data() + first, 4);
        uint32_t size = u32le(b, first + 4);
        size_t last = first + 8 + size;
        if (last > end) throw std::runtime_error("MIDS/SF1: chunk overruns parent");
        out.push_back({tag, first + 8, size});
        first = last + (size & 1); // word-aligned
        if (first > end) throw std::runtime_error("MIDS/SF1: truncated chunk pad");
    }
    if (first != end) throw std::runtime_error("MIDS/SF1: chunk list overrun");
    return out;
}

bool ieq_prefix(const std::string& s, const char* prefix, size_t n) {
    if (s.size() < n) return false;
    for (size_t i = 0; i < n; ++i)
        if (tolower((unsigned char)s[i]) != tolower((unsigned char)prefix[i])) return false;
    return true;
}

// SoundFont generator numbers used below (SF2 numbering, shared by SF1).
constexpr int kGenCount = 61;
constexpr double kPi = 3.141592653589793;

// Platform-independent maths (plan 33 A11): det_math plus a series sine, so
// renders are identical on every target.
double pow2(double x) {
    return det_exp(x * 0.6931471805599453);
}
double log10d(double x) {
    return det_log(x) / 2.302585092994046;
}
double sin_series(double x) {
    x -= 2.0 * kPi * std::floor((x + kPi) / (2.0 * kPi)); // [-pi, pi)
    const double x2 = x * x;
    double term = x, sum = x;
    for (int n = 1; n <= 11; ++n) { // |error| < 1e-10 on [-pi, pi]
        term *= -x2 / double((2 * n) * (2 * n + 1));
        sum += term;
    }
    return sum;
}
double cos_series(double x) {
    return sin_series(x + kPi / 2.0);
}

// SBK -> SF2 unit conversions, one per generator class (rules as in
// TiMidity's sbkconv.c, read as a reference; docs/REVERSE_ENGINEERING.md
// "SBK generator semantics"). Integer truncation follows that reader.
int sbk_time_tc(int ms) { // milliseconds -> timecents
    if (ms <= 0) ms = 1;
    return int(det_log2(double(ms) / 1000.0) * 1200.0);
}
float tc_seconds(int tc) {
    return float(pow2(double(std::clamp(tc, -12000, 5000)) / 1200.0));
}
int sbk_freq_ac(int gen, int v) { // LFO rate -> absolute cents
    if (v == 0) return gen == 22 ? -725 : -15600;
    return int(1200 * det_log2(double(v)) - 7925.0);
}
float ac_hz(int ac) {
    return float(8.176 * pow2(double(std::clamp(ac, -16000, 4500)) / 1200.0));
}
int sbk_pshift(int v) {
    return (1200 * v / 64 + 1) / 2;
}
int sbk_atten_cb(int v) {
    if (v <= 0) return 1000;
    return std::clamp(int(-200.0 * log10d(double(v) / 127.0) * 10), 0, 1440);
}

Sf1Zone convert_sbk_zone(const int32_t* val, const bool* set, uint32_t hs, uint32_t he, uint32_t hls,
                         uint32_t hle, size_t sample_count) {
    auto get = [&](int g, int def) { return set[g] ? int(val[g]) : def; };
    Sf1Zone z;
    if (set[43]) {
        z.low_key = uint8_t(val[43] & 0xff);
        z.high_key = uint8_t((val[43] >> 8) & 0xff);
    }
    if (set[44]) {
        z.low_vel = uint8_t(val[44] & 0xff);
        z.high_vel = uint8_t((val[44] >> 8) & 0xff);
    }
    if (z.low_key > z.high_key || z.high_key > 127) throw std::runtime_error("SF1: inverted key range");
    if (z.low_vel > z.high_vel || z.high_vel > 127) throw std::runtime_error("SF1: inverted velocity range");

    // Root: samplePitch (55) gives root = v/100 and tune = -(v%100), rounded
    // to the nearer key; overridingRootKey (58) replaces the key only.
    int root = 60, tune = 0;
    if (set[55]) {
        root = val[55] / 100;
        tune = -(val[55] % 100);
        if (tune <= -50) {
            ++root;
            tune += 100;
        }
    }
    if (set[58]) root = val[58];
    z.root_key = uint8_t(std::clamp(root, 0, 127));
    z.pitch_correction = int16_t(tune);
    z.coarse_tune = int8_t(std::clamp(get(51, 0), -120, 120));
    z.fine_tune = int8_t(std::clamp(get(52, 0), -99, 99));
    z.scale_tuning = int16_t(set[56] ? (val[56] ? 50 : 100) : 100);
    if (set[46] && val[46] >= 0 && val[46] <= 127) z.fixed_key = int8_t(val[46]);
    if (set[47] && val[47] > 0 && val[47] <= 127) z.fixed_velocity = int8_t(val[47]);
    z.attenuation_cb = int16_t(set[48] ? sbk_atten_cb(val[48]) : 0);

    // Addresses: header plus offset generators (coarse = 32768 samples), and
    // the SF1 loop correction (+1 start, +2 end) of TiMidity's correct_samples.
    auto addr = [&](int64_t base, int fine, int coarse) {
        return base + int64_t(get(fine, 0)) + int64_t(get(coarse, 0)) * 32768;
    };
    const int64_t n = int64_t(sample_count);
    const int64_t start = std::clamp<int64_t>(addr(hs, 0, 4), 0, n);
    const int64_t end = std::clamp<int64_t>(addr(he, 1, 12), start, n);
    int64_t ls = addr(int64_t(hls) + 1, 2, 45);
    int64_t le = addr(int64_t(hle) + 2, 3, 50);
    le = std::min(le, std::min(end + 1, n)); // TiMidity: loop end <= len + 1
    ls = std::clamp<int64_t>(ls, start, end);
    if (ls >= le) ls = le = end;
    z.sample_start = uint32_t(start);
    z.sample_end = uint32_t(end);
    z.loop_start = uint32_t(ls);
    z.loop_end = uint32_t(le);
    z.sample_modes = uint8_t(get(54, 0) & 3);
    z.sample_rate = 44100;

    z.pan = int16_t(set[17] ? std::clamp(val[17] * 1000 / 127 - 500, -500, 500) : 0);
    z.chorus_send = int16_t(set[15] ? std::clamp(val[15] * 1000 / 256, 0, 1000) : 0);
    z.reverb_send = int16_t(set[16] ? std::clamp(val[16] * 1000 / 256, 0, 1000) : 0);

    // Envelopes: times are milliseconds; unset times are -12000 timecents.
    auto t = [&](int g) { return tc_seconds(set[g] ? sbk_time_tc(val[g]) : -12000); };
    auto key_scale = [&](int g) { return int16_t(set[g] ? std::clamp(int(val[g] * 5.55), -1200, 1200) : 0); };
    z.mod_env = {t(25), t(26), t(27), t(28), t(30), 0.0f, key_scale(31), key_scale(32)};
    z.vol_env = {t(33), t(34), t(35), t(36), t(38), 0.0f, key_scale(39), key_scale(40)};
    // Sustain levels are 0..127; unset sustain is TiMidity's SBK default
    // 1000 (silent / zero level), so a zone with only a decay dies away.
    const int ms = set[29] ? (val[29] < 96 ? 1000 * (96 - val[29]) / 96 : 0) : 1000;
    const int vs = set[37] ? (val[37] < 96 ? (2000 - 21 * val[37]) / 2 : 0) : 1000;
    z.mod_env.sustain = 1.0f - float(std::clamp(ms, 0, 1000)) / 1000.0f;
    z.vol_env.sustain = float(std::clamp(vs, 0, 1440));

    z.mod_lfo_delay = t(21);
    z.vib_lfo_delay = t(23);
    z.mod_lfo_hz = ac_hz(set[22] ? sbk_freq_ac(22, val[22]) : -725);
    z.vib_lfo_hz = ac_hz(set[24] ? sbk_freq_ac(24, val[24]) : -15600);
    z.mod_lfo_to_pitch = int16_t(set[5] ? std::clamp(sbk_pshift(val[5]), -12000, 12000) : 0);
    z.vib_lfo_to_pitch = int16_t(set[6] ? std::clamp(sbk_pshift(val[6]), -12000, 12000) : 0);
    z.mod_env_to_pitch = int16_t(set[7] ? std::clamp(sbk_pshift(val[7]), -12000, 12000) : 0);
    // Filter: 127 = open (14400, outside TiMidity's 1500..13500 window).
    if (set[8]) {
        const int fc = val[8] == 127 ? 14400 : 59 * val[8] + 4366;
        z.filter_fc = int16_t(std::clamp(fc, 0, 14400));
        z.filter_on = fc >= 1500 && fc <= 13500;
    }
    z.filter_q_cb = int16_t(set[9] ? std::clamp(val[9] * 3 / 2, 0, 960) : 0);
    z.mod_lfo_to_fc = int16_t(set[10] ? std::clamp(1200 * 3 * val[10] / 64, -12000, 12000) : 0);
    z.mod_env_to_fc = int16_t(set[11] ? std::clamp(1200 * 6 * val[11] / 64, -12000, 12000) : 0);
    z.mod_lfo_to_volume = int16_t(set[13] ? std::clamp(120 * val[13] / 64, -960, 960) : 0);
    z.exclusive_class = uint8_t(set[57] ? std::clamp(int(val[57]), 0, 127) : 0);
    return z;
}

} // namespace

bool race_song_is_klown(const std::string& alias) {
    // 0x00433bb0: strnicmp(alias, "krusty", 6) / strnicmp(alias, "bozo", 4).
    return ieq_prefix(alias, "krusty", 6) || ieq_prefix(alias, "bozo", 4);
}

std::string race_song_name(int original_course_index, const std::string& alias) {
    if (race_song_is_klown(alias)) return "KLOWN";
    // music_generic table order: 1 CITY .. 5 CANYN -> Grunge1..5.
    if (original_course_index < 1 || original_course_index > 5)
        throw std::runtime_error("race song: course index out of range");
    return "SBKGRNG" + std::to_string(original_course_index);
}

MidsSong read_mids(const std::vector<uint8_t>& bytes) {
    if (bytes.size() < 12) throw std::runtime_error("MIDS: truncated RIFF header");
    if (memcmp(bytes.data(), "RIFF", 4) != 0) throw std::runtime_error("MIDS: missing RIFF tag");
    uint32_t riff_size = u32le(bytes, 4);
    if (riff_size + 8 > bytes.size()) throw std::runtime_error("MIDS: truncated RIFF");
    if (memcmp(bytes.data() + 8, "MIDS", 4) != 0) throw std::runtime_error("MIDS: not a MIDS form");
    const size_t form_end = size_t(8) + riff_size;
    if (form_end > bytes.size()) throw std::runtime_error("MIDS: truncated RIFF");

    MidsSong song;
    size_t fmt_off = 0, fmt_sz = 0, data_off = 0, data_sz = 0;
    bool have_fmt = false, have_data = false;
    for (const Chunk& c : riff_chunks(bytes, 12, form_end)) {
        if (c.tag == "fmt " && !have_fmt) {
            fmt_off = c.payload;
            fmt_sz = c.size;
            have_fmt = true;
        } else if (c.tag == "data" && !have_data) {
            data_off = c.payload;
            data_sz = c.size;
            have_data = true;
        }
    }
    if (!have_fmt) throw std::runtime_error("MIDS: missing fmt chunk");
    if (!have_data) throw std::runtime_error("MIDS: missing data chunk");
    if (fmt_sz != 12) throw std::runtime_error("MIDS: bad fmt size");
    song.division = u32le(bytes, fmt_off);
    const uint32_t max_buffer = u32le(bytes, fmt_off + 4);
    const uint32_t flags = u32le(bytes, fmt_off + 8);
    if (song.division == 0 || song.division > 32768) throw std::runtime_error("MIDS: bad division");
    if (max_buffer == 0 || max_buffer > (1 << 24)) throw std::runtime_error("MIDS: bad max_buffer");
    if (flags & ~1u) throw std::runtime_error("MIDS: unknown flags");

    size_t p = data_off;
    const size_t data_end = data_off + data_sz;
    if (data_end > bytes.size()) throw std::runtime_error("MIDS: truncated data");
    if (p + 4 > data_end) throw std::runtime_error("MIDS: truncated block count");
    const uint32_t blocks = u32le(bytes, p);
    p += 4;
    if (blocks > (1 << 20)) throw std::runtime_error("MIDS: absurd block count");
    const size_t stride = (flags & 1) ? 8 : 12;
    uint32_t tick = 0;
    for (uint32_t i = 0; i < blocks; ++i) {
        if (p + 8 > data_end) throw std::runtime_error("MIDS: truncated block header");
        const uint32_t size = u32le(bytes, p + 4); // p[0..4): start tick (informational)
        p += 8;
        if (size > max_buffer) throw std::runtime_error("MIDS: block exceeds max_buffer");
        if (p + size > data_end) throw std::runtime_error("MIDS: truncated block");
        const size_t end = p + size;
        while (p < end) {
            if (p + stride > end) throw std::runtime_error("MIDS: truncated event");
            const uint32_t delta = u32le(bytes, p);
            const uint32_t event = u32le(bytes, p + stride - 4);
            p += stride;
            // 32-bit tick wraparound matches the original's DWORD accumulation.
            tick += delta;
            const uint32_t kind = (event >> 24) & 191;
            if (event & 0x80000000u) {
                const uint32_t length = event & 0xffffffu;
                const size_t padded = (size_t(length) + 3) & ~size_t(3);
                if (p + padded > end) throw std::runtime_error("MIDS: truncated long event");
                p += padded; // SysEx/meta payload: counted, not sequenced
            } else if (kind == 1) {
                song.tempo.emplace_back(tick, event & 0xffffffu);
            } else if (kind == 0) {
                const uint8_t status = event & 0xff, a = (event >> 8) & 0xff, v = (event >> 16) & 0xff;
                if (status < 0x80 || status >= 0xF0 || a >= 0x80 || v >= 0x80)
                    throw std::runtime_error("MIDS: bad short event");
                song.events.push_back({tick, status, a, v});
            } else {
                throw std::runtime_error("MIDS: unknown short event kind");
            }
        }
        if (p != end) throw std::runtime_error("MIDS: block overrun");
    }
    if (p != data_end) throw std::runtime_error("MIDS: trailing data");
    song.total_ticks = tick;
    return song;
}

std::shared_ptr<const Sf1Bank> read_sf1(const std::vector<uint8_t>& bytes, bool melodic) {
    if (bytes.size() < 12) throw std::runtime_error("SF1: truncated RIFF header");
    if (memcmp(bytes.data(), "RIFF", 4) != 0) throw std::runtime_error("SF1: missing RIFF tag");
    uint32_t riff_size = u32le(bytes, 4);
    if (riff_size + 8 > bytes.size()) throw std::runtime_error("SF1: truncated RIFF");
    if (memcmp(bytes.data() + 8, "sfbk", 4) != 0) throw std::runtime_error("SF1: not an sfbk form");
    const size_t form_end = size_t(8) + riff_size;

    // Flatten the two-level LIST tree into path-keyed payloads.
    struct Entry {
        std::string path;
        size_t off;
        uint32_t size;
    };
    std::vector<Entry> flat;
    for (const Chunk& top : riff_chunks(bytes, 12, form_end)) {
        if (top.tag != "LIST") throw std::runtime_error("SF1: expected LIST");
        if (top.size < 4) throw std::runtime_error("SF1: truncated LIST");
        char kind[5] = {};
        memcpy(kind, bytes.data() + top.payload, 4);
        std::string prefix = std::string(kind) + "/";
        for (const Chunk& c : riff_chunks(bytes, top.payload + 4, top.payload + top.size)) {
            if (c.tag == "LIST") throw std::runtime_error("SF1: nested LIST");
            flat.push_back({prefix + c.tag, c.payload, c.size});
        }
    }
    auto find = [&](const char* path) -> const Entry* {
        for (const auto& e : flat)
            if (e.path == path) return &e;
        return nullptr;
    };
    const Entry* ifil = find("INFO/ifil");
    const Entry* smpl = find("sdta/smpl");
    const Entry* phdr = find("pdta/phdr");
    const Entry* pbag = find("pdta/pbag");
    const Entry* pgen = find("pdta/pgen");
    const Entry* inst = find("pdta/inst");
    const Entry* ibag = find("pdta/ibag");
    const Entry* igen = find("pdta/igen");
    const Entry* shdr = find("pdta/shdr");
    if (!ifil || !smpl || !phdr || !pbag || !pgen || !inst || !ibag || !igen || !shdr)
        throw std::runtime_error("SF1: missing chunk");
    if (ifil->size != 4) throw std::runtime_error("SF1: bad ifil");
    if (u16le(bytes, ifil->off) != 1) throw std::runtime_error("SF1: not SoundFont 1");
    if (smpl->size % 2 != 0) throw std::runtime_error("SF1: odd smpl size");
    if (phdr->size % 38 != 0 || pbag->size % 4 != 0 || pgen->size % 4 != 0 || inst->size % 22 != 0 ||
        ibag->size % 4 != 0 || igen->size % 4 != 0)
        throw std::runtime_error("SF1: bad pdta alignment");

    auto bank = std::make_shared<Sf1Bank>();
    bank->samples.resize(smpl->size / 2);
    for (size_t i = 0; i < bank->samples.size(); ++i) {
        const size_t o = smpl->off + 2 * i;
        bank->samples[i] = int16_t(bytes[o] | (bytes[o + 1] << 8));
    }
    const size_t sample_count = bank->samples.size();

    struct Gen {
        uint16_t op = 0, amount = 0;
    };
    auto gen_list = [&](const Entry* e) {
        std::vector<Gen> v;
        for (uint32_t i = 0; i < e->size; i += 4)
            v.push_back({u16le(bytes, e->off + i), u16le(bytes, e->off + i + 2)});
        return v;
    };
    const std::vector<Gen> pgens = gen_list(pgen);
    const std::vector<Gen> igens = gen_list(igen);
    auto bag_list = [&](const Entry* e) {
        std::vector<std::pair<uint16_t, uint16_t>> v;
        for (uint32_t i = 0; i < e->size; i += 4)
            v.push_back({u16le(bytes, e->off + i), u16le(bytes, e->off + i + 2)});
        return v;
    };
    const auto pbags = bag_list(pbag);
    const auto ibags = bag_list(ibag);

    const uint32_t n_presets = phdr->size / 38;
    const uint32_t n_inst = inst->size / 22;
    // Bag lists hold one entry per zone plus a terminal entry; their counts
    // are independent of the preset/instrument counts.
    if (n_presets < 1 || pbags.empty()) throw std::runtime_error("SF1: bad preset bags");
    if (n_inst < 1 || ibags.empty()) throw std::runtime_error("SF1: bad instrument bags");
    // Sample headers in these banks are a compact 16 bytes: start, end,
    // loopStart, loopEnd (no name/rate/pitch fields). The rate is 44100 Hz
    // (TiMidity's SBK assumption; the GRUNGE2/5 bass zones that set only
    // samplePitch 4281/4782 measure 42.8/47.9 semitones at that rate) and the
    // root pitch comes from samplePitch (55), overridden by 58.
    if (shdr->size % 16 != 0) throw std::runtime_error("SF1: bad shdr alignment");
    const uint32_t n_samples = shdr->size / 16;

    auto sample_range = [&](uint32_t s) {
        if (s >= n_samples) throw std::runtime_error("SF1: sample id out of range");
        const size_t o = shdr->off + 16 * size_t(s);
        const uint32_t start = u32le(bytes, o);
        const uint32_t end = u32le(bytes, o + 4);
        const uint32_t loop_start = u32le(bytes, o + 8);
        const uint32_t loop_end = u32le(bytes, o + 12);
        if (end > sample_count || loop_end > sample_count)
            throw std::runtime_error("SF1: sample range out of range");
        if (start > end) throw std::runtime_error("SF1: inverted sample range");
        struct R {
            uint32_t s, e, ls, le;
        };
        return R{start, end, loop_start, loop_end};
    };

    // Generator table for one zone: set flags plus raw SBK amounts.
    struct Table {
        int32_t val[kGenCount] = {};
        bool set[kGenCount] = {};
    };
    auto apply = [&](Table& t, const std::vector<Gen>& gens, uint32_t lo, uint32_t hi) {
        for (uint32_t i = lo; i < hi; ++i) {
            if (i >= gens.size()) throw std::runtime_error("SF1: generator index out of range");
            if (gens[i].op == 0 || gens[i].op >= kGenCount) continue;
            t.val[gens[i].op] = int16_t(gens[i].amount);
            t.set[gens[i].op] = true;
        }
    };
    auto has = [&](const std::vector<Gen>& gens, uint32_t lo, uint32_t hi, uint16_t op) {
        for (uint32_t i = lo; i < hi && i < gens.size(); ++i)
            if (gens[i].op == op) return true;
        return false;
    };
    auto bag_gens = [&](const std::vector<std::pair<uint16_t, uint16_t>>& bags, const std::vector<Gen>& gens,
                        uint32_t bi) {
        const uint32_t lo = bags[bi].first;
        const uint32_t hi = (bi + 1 < bags.size()) ? bags[bi + 1].first : uint32_t(gens.size());
        if (lo > hi || hi > gens.size()) throw std::runtime_error("SF1: generator bag out of range");
        return std::pair<uint32_t, uint32_t>{lo, hi};
    };

    for (uint32_t pi = 0; pi + 1 < n_presets; ++pi) {
        const size_t o = phdr->off + 38 * size_t(pi);
        const uint16_t program = u16le(bytes, o + 20);
        const uint16_t bank_id = u16le(bytes, o + 22);
        const uint16_t bag_lo = u16le(bytes, o + 24);
        const uint16_t bag_hi = u16le(bytes, o + 38 + 24);
        const bool want = melodic ? (bank_id == 0) : (bank_id == 128);
        if (!want) continue;
        if (bag_lo > bag_hi || bag_hi > pbags.size())
            throw std::runtime_error("SF1: preset bag out of range");
        // A first preset zone without an instrument is the preset's global zone.
        Table pglobal;
        uint32_t first = bag_lo;
        if (first < bag_hi) {
            const auto [g_lo, g_hi] = bag_gens(pbags, pgens, first);
            if (!has(pgens, g_lo, g_hi, 41)) {
                apply(pglobal, pgens, g_lo, g_hi);
                ++first;
            }
        }
        for (uint32_t bi = first; bi < bag_hi; ++bi) {
            const auto [g_lo, g_hi] = bag_gens(pbags, pgens, bi);
            Table pzone = pglobal;
            apply(pzone, pgens, g_lo, g_hi);
            if (!pzone.set[41]) continue; // later zones without an instrument are ignored
            const uint32_t inst_id = uint16_t(pzone.val[41]);
            if (inst_id >= n_inst) throw std::runtime_error("SF1: instrument out of range");
            const uint16_t ib_lo = u16le(bytes, inst->off + 22 * size_t(inst_id) + 20);
            const uint16_t ib_hi = (inst_id + 1 < n_inst)
                                       ? u16le(bytes, inst->off + 22 * size_t(inst_id + 1) + 20)
                                       : uint16_t(ibags.size() - 1);
            if (ib_lo > ib_hi || ib_hi > ibags.size())
                throw std::runtime_error("SF1: instrument bag out of range");
            // Instrument global zone (first zone without a sampleID): defaults.
            Table iglobal;
            uint32_t ifirst = ib_lo;
            if (ifirst < ib_hi) {
                const auto [h_lo, h_hi] = bag_gens(ibags, igens, ifirst);
                if (!has(igens, h_lo, h_hi, 53)) {
                    apply(iglobal, igens, h_lo, h_hi);
                    ++ifirst;
                }
            }
            for (uint32_t bj = ifirst; bj < ib_hi; ++bj) {
                const auto [h_lo, h_hi] = bag_gens(ibags, igens, bj);
                Table t = iglobal;
                apply(t, igens, h_lo, h_hi);
                if (!t.set[53]) continue; // zones without a sample are skipped
                // SF1 merge (TiMidity merge_table, version 1): preset values
                // fill only unset instrument generators; ranges always copy.
                for (int g = 1; g < kGenCount; ++g) {
                    if (!pzone.set[g] || g == 41) continue;
                    if (!t.set[g] || g == 43 || g == 44) {
                        t.val[g] = pzone.val[g];
                        t.set[g] = true;
                    }
                }
                const auto r = sample_range(uint16_t(t.val[53]));
                Sf1Zone z = convert_sbk_zone(t.val, t.set, r.s, r.e, r.ls, r.le, sample_count);
                z.program = uint8_t(program & 127);
                z.bank = uint8_t(bank_id == 128 ? 128 : 0);
                bank->zones.push_back(z);
            }
        }
    }
    return bank;
}

namespace {
constexpr size_t kControlFrames = 32; // envelope/LFO/filter update period
// Output headroom for up to 32 summed voices. The EMU8000 mix level is
// unmeasured; -12 dB keeps the five AWE corpus songs below full scale.
constexpr double kMasterGain = 0.25;

bool zone_matches(const Sf1Zone& z, bool drums, uint8_t program, uint8_t key, uint8_t vel) {
    if (!drums && z.program != program) return false;
    return key >= z.low_key && key <= z.high_key && vel >= z.low_vel && vel <= z.high_vel;
}

bool bank_covers(const Sf1Bank* bank, bool drums, uint8_t program, uint8_t key, uint8_t vel) {
    if (!bank) return false;
    for (const Sf1Zone& z : bank->zones)
        if (zone_matches(z, drums, program, key, vel)) return true;
    return false;
}

double cb_amp(double cb) {
    return det_pow10(-cb / 200.0);
}

// SF2 default-modulator concave curve (velocity, CC7, CC11): 960 cB range,
// amplitude proportional to the square of the normalised controller value.
double concave_cb(double x) {
    return x <= 0.0 ? 960.0 : std::min(960.0, -400.0 * log10d(x));
}

// SF2 LFO: triangle starting at 0 and rising.
double triangle(double phase) {
    phase -= std::floor(phase);
    if (phase < 0.25) return 4.0 * phase;
    if (phase < 0.75) return 2.0 - 4.0 * phase;
    return 4.0 * phase - 4.0;
}

double key_scaled(float seconds, int16_t tc_per_key, int key) {
    return double(seconds) * pow2(double(tc_per_key) * double(60 - key) / 1200.0);
}
} // namespace

// Synthesized factory timbres (no disc bytes): one looped waveform per
// census-observed missing melodic program. Waveforms are deliberately plain
// (sine + 2nd/3rd harmonic, -6 dB steps): they mark "factory instrument
// present" audibly without claiming to reproduce the AWE ROM timbre.
const Sf1Bank& factory_bank() {
    static const Sf1Bank bank = [] {
        Sf1Bank b;
        constexpr uint8_t kPrograms[] = {11, 12, 18, 21, 33, 35, 56, 71};
        constexpr int kLen = 256;
        for (uint8_t p : kPrograms) {
            Sf1Zone z;
            z.program = p;
            z.bank = 0;
            z.low_key = 0;
            z.high_key = 127;
            z.root_key = 60;
            z.sample_rate = 22050;
            z.sample_modes = 1;
            z.vol_env.release = 0.005f; // the previous renderer's 5 ms release
            z.sample_start = uint32_t(b.samples.size());
            for (int i = 0; i < kLen; ++i) {
                const double ph = double(i) / kLen * 2.0 * kPi;
                // Program-parametrised harmonic mix keeps timbres distinct.
                const double h2 = (p % 3 + 1) / 12.0, h3 = (p % 5 + 1) / 24.0;
                const double v = sin(ph) + h2 * sin(2 * ph) + h3 * sin(3 * ph);
                b.samples.push_back(int16_t(lrint(v / 1.5 * 20000.0)));
            }
            z.sample_end = uint32_t(b.samples.size());
            z.loop_start = z.sample_start;
            z.loop_end = z.sample_end;
            b.zones.push_back(z);
        }
        return b;
    }();
    return bank;
}

bool factory_covers(uint8_t program) {
    for (const Sf1Zone& z : factory_bank().zones)
        if (z.program == program) return true;
    return false;
}

size_t count_factory_notes(const MidsSong& song, const Sf1Bank& melodic, const Sf1Bank& drums) {
    // Notes whose (program,key) no disc zone covers: these need the factory
    // table (or stay silent with a nullptr factory). Factory coverage is
    // deliberately NOT excluded here so the audit reports the true
    // factory-dependent count (e.g. SBKGRNG1 program 18, KLOWN programs).
    uint8_t program[16] = {};
    size_t missing = 0;
    for (const MidsEvent& e : song.events) {
        const uint8_t op = e.status & 0xf0, ch = e.status & 0x0f;
        if (op == 0xC0) {
            program[ch] = e.data0 & 127;
        } else if (op == 0x90 && e.data1 != 0u) {
            const bool drum = ch == 9;
            if (!bank_covers(drum ? &drums : &melodic, drum, program[ch], e.data0, e.data1)) ++missing;
        }
    }
    return missing;
}

// One sounding zone. Envelope stages: 0 delay, 1 attack, 2 hold, 3 decay,
// 4 sustain, 5 release.
struct Sf1Synth::Voice {
    const Sf1Zone* zone = nullptr;
    const std::vector<int16_t>* samples = nullptr;
    uint8_t channel = 0, key = 0, velocity = 0, pitch_key = 0;
    uint64_t serial = 0;
    bool released = false, done = false;
    double pos = 0.0, step = 0.0; // source samples from zone start
    double base_cents = 0.0, vel_cb = 0.0;
    double vol_hold = 0, vol_decay = 0, mod_hold = 0, mod_decay = 0, vol_release = 0, mod_release = 0;
    int vol_stage = 0, mod_stage = 0;
    double vol_t = 0, mod_t = 0, vol_cb = 1000.0, mod_level = 0.0;
    double time = 0.0, mod_phase = 0.0, vib_phase = 0.0;
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    float gl = 0, gr = 0, dgl = 0, dgr = 0;
    float reverb = 0, chorus = 0; // send fractions
    size_t control_left = 0;

    double vol_amp() const {
        if (vol_stage == 0) return 0.0;
        if (vol_stage == 1) return std::min(1.0, vol_t / double(zone->vol_env.attack));
        return cb_amp(vol_cb);
    }
    void release(double seconds_override = 0.0) {
        if (released && seconds_override <= 0.0) return;
        if (vol_stage <= 1) {
            const double a = vol_amp();
            vol_cb = a > 0.0 ? -200.0 * log10d(a) : 1000.0;
        }
        if (vol_stage == 0) done = true;
        vol_stage = 5;
        if (mod_stage == 1) mod_level = std::min(1.0, mod_t / double(zone->mod_env.attack));
        if (mod_stage == 0) mod_level = 0.0;
        mod_stage = 5;
        if (seconds_override > 0.0) vol_release = mod_release = seconds_override;
        released = true;
    }
    void advance(double dt) {
        const Sf1Envelope& ve = zone->vol_env;
        vol_t += dt;
        switch (vol_stage) {
        case 0:
            if (vol_t >= ve.delay) vol_stage = 1, vol_t -= ve.delay;
            break;
        case 1:
            if (vol_t >= ve.attack) vol_stage = 2, vol_t -= ve.attack, vol_cb = 0.0;
            break;
        case 2:
            if (vol_t >= vol_hold) vol_stage = 3, vol_t -= vol_hold;
            break;
        case 3:
            vol_cb += 1000.0 * dt / vol_decay;
            if (vol_cb >= ve.sustain) vol_cb = ve.sustain, vol_stage = 4;
            break;
        case 5:
            vol_cb += 1000.0 * dt / vol_release;
            break;
        default:
            break;
        }
        if (vol_stage >= 2 && vol_cb >= 960.0) done = true;
        const Sf1Envelope& me = zone->mod_env;
        mod_t += dt;
        switch (mod_stage) {
        case 0:
            if (mod_t >= me.delay) mod_stage = 1, mod_t -= me.delay;
            break;
        case 1:
            mod_level = std::min(1.0, mod_t / double(me.attack));
            if (mod_t >= me.attack) mod_stage = 2, mod_t -= me.attack, mod_level = 1.0;
            break;
        case 2:
            if (mod_t >= mod_hold) mod_stage = 3, mod_t -= mod_hold;
            break;
        case 3:
            mod_level -= dt / mod_decay;
            if (mod_level <= me.sustain) mod_level = me.sustain, mod_stage = 4;
            break;
        case 5:
            mod_level = std::max(0.0, mod_level - dt / mod_release);
            break;
        default:
            break;
        }
        time += dt;
        if (time >= zone->mod_lfo_delay) mod_phase += double(zone->mod_lfo_hz) * dt;
        if (time >= zone->vib_lfo_delay) vib_phase += double(zone->vib_lfo_hz) * dt;
    }
    double at(uint32_t k, bool looping) const {
        if (looping && k >= zone->loop_end)
            k = zone->loop_start + (k - zone->loop_start) % (zone->loop_end - zone->loop_start);
        // A loop may end one sample past `end` (TiMidity: data length len + 1).
        if (k >= std::max(zone->sample_end, zone->loop_end) || k >= samples->size()) return 0.0;
        return double((*samples)[k]) / 32768.0;
    }
};

// Shared effects buses. The game sends AWEMAN32 no effect-mode message
// (0x00434500/0x00433cf0 only open, load, free and close), so the driver's
// default modes apply: reverb "Hall 2" and chorus "Chorus 3" (the AWE driver
// defaults; reading reference only). The EMU8000 runs these as undocumented
// DSP microcode, so the algorithms here are plain stand-ins sized to those
// mode names: a Schroeder/Moorer hall (8 damped combs + 4 allpasses per side,
// ~2.3 s RT60) and a single-tap chorus (23 ms delay, +-3 ms triangle sweep at
// 0.4 Hz, 10% feedback). Unmeasured; see REVERSE_ENGINEERING.md.
struct Sf1Synth::Effects {
    struct Comb {
        std::vector<float> buf;
        size_t i = 0;
        float store = 0.0f;
        float run(float in, float feedback, float damp) {
            const float out = buf[i];
            store = out * (1.0f - damp) + store * damp;
            buf[i] = in + store * feedback;
            if (++i == buf.size()) i = 0;
            return out;
        }
    };
    struct Allpass {
        std::vector<float> buf;
        size_t i = 0;
        float run(float in) {
            const float b = buf[i];
            buf[i] = in + b * 0.5f;
            if (++i == buf.size()) i = 0;
            return b - in;
        }
    };
    struct Chorus {
        std::vector<float> buf;
        size_t w = 0;
    };
    static constexpr float kReverbFeedback = 0.91f, kReverbDamp = 0.35f, kReverbInput = 0.015f;
    static constexpr double kChorusDelay = 0.023, kChorusDepth = 0.003, kChorusHz = 0.4;
    static constexpr float kChorusFeedback = 0.1f;
    Comb comb[2][8];
    Allpass allpass[2][4];
    Chorus chorus[2];
    double rate, chorus_phase = 0.0;
    size_t quiet_frames = 0; // frames since the last nonzero send input
    float level = 0.0f;      // peak wet output of the last render call

    explicit Effects(double r) : rate(r) {
        // Freeverb's public-domain 44.1 kHz tunings, scaled to the output rate.
        constexpr int kComb[8] = {1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617};
        constexpr int kAllpass[4] = {556, 441, 341, 225};
        constexpr int kSpread = 23;
        const double scale = rate / 44100.0;
        for (int side = 0; side < 2; ++side) {
            for (int k = 0; k < 8; ++k)
                comb[side][k].buf.assign(size_t(std::max(1L, lrint((kComb[k] + side * kSpread) * scale))),
                                         0.0f);
            for (int k = 0; k < 4; ++k)
                allpass[side][k].buf.assign(
                    size_t(std::max(1L, lrint((kAllpass[k] + side * kSpread) * scale))), 0.0f);
            chorus[side].buf.assign(size_t((kChorusDelay + kChorusDepth) * rate) + 4, 0.0f);
        }
        quiet_frames = size_t(rate * 60);
    }
    void clear() {
        for (auto& side : comb)
            for (Comb& c : side) std::fill(c.buf.begin(), c.buf.end(), 0.0f), c.store = 0.0f;
        for (auto& side : allpass)
            for (Allpass& a : side) std::fill(a.buf.begin(), a.buf.end(), 0.0f);
        for (Chorus& c : chorus) std::fill(c.buf.begin(), c.buf.end(), 0.0f);
        chorus_phase = 0.0;
        quiet_frames = size_t(rate * 60);
        level = 0.0f;
    }
    // A tail is live while sends arrived within the last 0.2 s (longer than
    // any comb or chorus delay) or the wet output is above -96 dB.
    bool busy() const { return quiet_frames < size_t(rate * 0.2) || level > 1.5e-5f; }
    void process(const std::vector<float>* send, float* left, float* right, size_t frames) {
        float peak = 0.0f;
        for (size_t i = 0; i < frames; ++i) {
            const float rl = send[0][i], rr = send[1][i], cl = send[2][i], cr = send[3][i];
            quiet_frames = (rl != 0.0f || rr != 0.0f || cl != 0.0f || cr != 0.0f) ? 0 : quiet_frames + 1;
            float out[2] = {0.0f, 0.0f};
            const float rin = (rl + rr) * kReverbInput;
            for (int side = 0; side < 2; ++side) {
                float acc = 0.0f;
                for (Comb& c : comb[side]) acc += c.run(rin, kReverbFeedback, kReverbDamp);
                for (Allpass& a : allpass[side]) acc = a.run(acc);
                out[side] = acc;
            }
            // Chorus: the two sides sweep in opposite LFO phase.
            const float cin[2] = {cl, cr};
            for (int side = 0; side < 2; ++side) {
                Chorus& c = chorus[side];
                const double delay =
                    (kChorusDelay + kChorusDepth * triangle(chorus_phase + 0.5 * side)) * rate;
                double rpos = double(c.w) - delay;
                const double n = double(c.buf.size());
                while (rpos < 0.0) rpos += n;
                const size_t r0 = size_t(rpos) % c.buf.size(), r1 = (r0 + 1) % c.buf.size();
                const float frac = float(rpos - std::floor(rpos));
                const float tap = c.buf[r0] * (1.0f - frac) + c.buf[r1] * frac;
                c.buf[c.w] = cin[side] + tap * kChorusFeedback;
                if (++c.w == c.buf.size()) c.w = 0;
                out[side] += tap;
            }
            chorus_phase += kChorusHz / rate;
            if (chorus_phase >= 1.0) chorus_phase -= 1.0;
            left[i] += out[0];
            right[i] += out[1];
            peak = std::max({peak, out[0], -out[0], out[1], -out[1]});
        }
        level = peak;
    }
};

Sf1Synth::Sf1Synth(const Sf1Bank* melodic, const Sf1Bank* drums, const Sf1Bank* factory, double rate)
    : melodic_(melodic), drums_(drums), factory_(factory), rate_(rate), fx_(std::make_unique<Effects>(rate)) {
    voices_.reserve(kMaxVoices);
}

Sf1Synth::~Sf1Synth() = default;

void Sf1Synth::reset() {
    voices_.clear();
    for (Channel& c : channels_) c = Channel{};
    fx_->clear();
}

size_t Sf1Synth::active_voices() const {
    return voices_.size();
}

bool Sf1Synth::busy() const {
    return !voices_.empty() || fx_->busy();
}

void Sf1Synth::event(uint8_t status, uint8_t data0, uint8_t data1) {
    const uint8_t op = status & 0xf0, ch = status & 0x0f;
    Channel& c = channels_[ch];
    if (op == 0x90 && data1 != 0u) {
        note_on(ch, data0 & 127, data1 & 127);
    } else if (op == 0x80 || op == 0x90) {
        for (Voice& v : voices_)
            if (v.channel == ch && v.key == data0 && !v.released) v.release();
    } else if (op == 0xB0) {
        controller(ch, data0 & 127, data1 & 127);
    } else if (op == 0xC0) {
        c.program = data0 & 127;
    } else if (op == 0xE0) {
        c.bend = uint16_t((data0 & 127) | ((data1 & 127) << 7));
    }
}

void Sf1Synth::controller(uint8_t ch, uint8_t cc, uint8_t value) {
    Channel& c = channels_[ch];
    const bool rpn0 = c.rpn_msb == 0 && c.rpn_lsb == 0;
    switch (cc) {
    case 1:
        c.mod = value;
        break;
    case 6: // data entry MSB: RPN 0 bend range semitones
        if (rpn0) c.bend_range = value * 100 + c.bend_range % 100;
        break;
    case 38: // data entry LSB: RPN 0 bend range cents
        if (rpn0) c.bend_range = (c.bend_range / 100) * 100 + std::min<int>(value, 99);
        break;
    case 7:
        c.volume = value;
        break;
    case 10:
        c.pan = value;
        break;
    case 11:
        c.expression = value;
        break;
    case 91:
        c.reverb = value;
        break;
    case 93:
        c.chorus = value;
        break;
    case 98:
    case 99: // NRPN select deselects the RPN
        c.rpn_msb = c.rpn_lsb = 127;
        break;
    case 100:
        c.rpn_lsb = value;
        break;
    case 101:
        c.rpn_msb = value;
        break;
    case 120: // all sound off
        voices_.erase(
            std::remove_if(voices_.begin(), voices_.end(), [&](const Voice& v) { return v.channel == ch; }),
            voices_.end());
        break;
    case 121: // reset all controllers (RP-015: volume and pan kept)
        c.expression = 127;
        c.mod = 0;
        c.bend = 8192;
        c.rpn_msb = c.rpn_lsb = 127;
        break;
    case 123: // all notes off
        for (Voice& v : voices_)
            if (v.channel == ch) v.release();
        break;
    default: // bank select: channel 10 is the kit
        break;
    }
}

void Sf1Synth::note_on(uint8_t ch, uint8_t key, uint8_t vel) {
    // A repeated key releases its previous voices (single note-off per key).
    for (Voice& v : voices_)
        if (v.channel == ch && v.key == key && !v.released) v.release();
    const bool drum = ch == 9;
    const uint8_t program = drum ? 0 : channels_[ch].program;
    const Sf1Bank* bank = drum ? drums_ : melodic_;
    const uint64_t first_serial = serial_ + 1;
    auto start_all = [&](const Sf1Bank* b) {
        bool any = false;
        if (!b) return false;
        for (const Sf1Zone& z : b->zones) {
            if (!zone_matches(z, drum, program, key, vel)) continue;
            // Exclusive class: the new note cuts this channel's earlier voices
            // of the same class (RRDRUMS hi-hats 42/44/46) as fast as possible.
            if (z.exclusive_class)
                for (Voice& v : voices_)
                    if (v.channel == ch && v.serial < first_serial &&
                        v.zone->exclusive_class == z.exclusive_class)
                        v.release(0.001);
            start_voice(z, b->samples, ch, key, vel);
            any = true;
        }
        return any;
    };
    if (!start_all(bank) && !drum) start_all(factory_);
}

void Sf1Synth::start_voice(const Sf1Zone& z, const std::vector<int16_t>& samples, uint8_t ch, uint8_t key,
                           uint8_t vel) {
    if (z.sample_end <= z.sample_start || z.sample_end > samples.size()) return;
    Voice* slot = nullptr;
    if (voices_.size() < kMaxVoices) {
        voices_.emplace_back();
        slot = &voices_.back();
    } else {
        // Steal: the quietest released voice, else the oldest voice.
        Voice* best = nullptr;
        for (Voice& v : voices_)
            if (v.released && (!best || v.vol_amp() < best->vol_amp())) best = &v;
        if (!best)
            for (Voice& v : voices_)
                if (!best || v.serial < best->serial) best = &v;
        *best = Voice{};
        slot = best;
    }
    Voice& v = *slot;
    v.zone = &z;
    v.samples = &samples;
    v.channel = ch;
    v.key = key;
    v.velocity = z.fixed_velocity >= 0 ? uint8_t(z.fixed_velocity) : vel;
    v.pitch_key = z.fixed_key >= 0 ? uint8_t(z.fixed_key) : key;
    v.serial = ++serial_;
    v.base_cents = double(z.scale_tuning) * (int(v.pitch_key) - int(z.root_key)) + z.pitch_correction +
                   100.0 * z.coarse_tune + z.fine_tune;
    v.vel_cb = concave_cb(v.velocity / 127.0);
    v.vol_hold = key_scaled(z.vol_env.hold, z.vol_env.key_to_hold, v.pitch_key);
    v.vol_decay = key_scaled(z.vol_env.decay, z.vol_env.key_to_decay, v.pitch_key);
    v.mod_hold = key_scaled(z.mod_env.hold, z.mod_env.key_to_hold, v.pitch_key);
    v.mod_decay = key_scaled(z.mod_env.decay, z.mod_env.key_to_decay, v.pitch_key);
    v.vol_release = z.vol_env.release;
    v.mod_release = z.mod_env.release;
    started_any_ = true;
}

void Sf1Synth::render(float* left, float* right, size_t frames) {
    const double dt = double(kControlFrames) / rate_;
    for (std::vector<float>& b : send_) b.assign(frames, 0.0f);
    for (Voice& v : voices_) {
        const Sf1Zone& z = *v.zone;
        size_t i = 0;
        while (i < frames && !v.done) {
            if (v.control_left == 0) {
                // Control point: current modulators, then advance one period.
                const Channel& c = channels_[v.channel];
                const double mod_lfo = v.time >= z.mod_lfo_delay ? triangle(v.mod_phase) : 0.0;
                const double vib_lfo = v.time >= z.vib_lfo_delay ? triangle(v.vib_phase) : 0.0;
                const double bend = (double(c.bend) - 8192.0) / 8192.0 * c.bend_range;
                const double vib_depth = z.vib_lfo_to_pitch + 50.0 * c.mod / 127.0; // CC1 -> vibrato
                const double cents = v.base_cents + bend + v.mod_level * z.mod_env_to_pitch +
                                     mod_lfo * z.mod_lfo_to_pitch + vib_lfo * vib_depth;
                v.step = double(z.sample_rate) / rate_ * pow2(cents / 1200.0);
                const double cb =
                    std::clamp(double(z.attenuation_cb) + v.vel_cb + concave_cb(c.volume / 127.0) +
                                   concave_cb(c.expression / 127.0) + mod_lfo * z.mod_lfo_to_volume,
                               0.0, 1440.0);
                const double amp = kMasterGain * cb_amp(cb) * v.vol_amp();
                const double pan =
                    std::clamp(double(z.pan) + (double(c.pan) - 64.0) / 64.0 * 500.0, -500.0, 500.0);
                const double theta = (pan + 500.0) / 1000.0 * kPi / 2.0;
                v.dgl = (float(amp * cos_series(theta)) - v.gl) / float(kControlFrames);
                v.dgr = (float(amp * sin_series(theta)) - v.gr) / float(kControlFrames);
                // SF2 default modulators CC91/CC93: +200 (20%) at full scale,
                // added to the zone's reverb/chorus send.
                v.reverb = float(std::clamp(z.reverb_send + 200.0 * c.reverb / 127.0, 0.0, 1000.0) / 1000.0);
                v.chorus = float(std::clamp(z.chorus_send + 200.0 * c.chorus / 127.0, 0.0, 1000.0) / 1000.0);
                if (z.filter_on) {
                    const double fc_cents =
                        z.filter_fc + v.mod_level * z.mod_env_to_fc + mod_lfo * z.mod_lfo_to_fc;
                    const double hz = std::clamp(8.176 * pow2(fc_cents / 1200.0), 5.0, 0.45 * rate_);
                    const double q = std::max(det_pow10(z.filter_q_cb / 200.0), 0.7071);
                    const double w = 2.0 * kPi * hz / rate_, cw = cos_series(w),
                                 alpha = sin_series(w) / (2.0 * q);
                    const double a0 = 1.0 + alpha;
                    v.b0 = (1.0 - cw) / 2.0 / a0;
                    v.b1 = (1.0 - cw) / a0;
                    v.b2 = v.b0;
                    v.a1 = -2.0 * cw / a0;
                    v.a2 = (1.0 - alpha) / a0;
                }
                v.advance(dt);
                v.control_left = kControlFrames;
            }
            const size_t n = std::min(frames - i, v.control_left);
            const bool looping = z.loops() && (z.sample_modes == 1 || !v.released);
            const double loop_len = double(z.loop_end - z.loop_start);
            const double loop_end = double(z.loop_end - z.sample_start);
            const double length = double(z.sample_end - z.sample_start);
            for (size_t k = 0; k < n; ++k) {
                if (looping)
                    while (v.pos >= loop_end) v.pos -= loop_len;
                if (!looping && v.pos >= length) {
                    v.done = true;
                    break;
                }
                const uint32_t idx = z.sample_start + uint32_t(v.pos);
                const double frac = v.pos - std::floor(v.pos);
                double s = v.at(idx, looping) * (1.0 - frac) + v.at(idx + 1, looping) * frac;
                if (z.filter_on) {
                    const double y = v.b0 * s + v.z1;
                    v.z1 = v.b1 * s - v.a1 * y + v.z2;
                    v.z2 = v.b2 * s - v.a2 * y;
                    s = y;
                }
                v.gl += v.dgl;
                v.gr += v.dgr;
                const float l = float(s) * v.gl, r = float(s) * v.gr;
                left[i + k] += l;
                right[i + k] += r;
                send_[0][i + k] += l * v.reverb;
                send_[1][i + k] += r * v.reverb;
                send_[2][i + k] += l * v.chorus;
                send_[3][i + k] += r * v.chorus;
                v.pos += v.step;
            }
            i += n;
            v.control_left -= n;
        }
    }
    voices_.erase(std::remove_if(voices_.begin(), voices_.end(), [](const Voice& v) { return v.done; }),
                  voices_.end());
    fx_->process(send_, left, right, frames);
}

namespace {
// Offline song renderer used by the test hook: sequences the song's events
// into an Sf1Synth at their exact frames and lets the last voices release.
void render_song(const MidsSong& song, const Sf1Bank& melodic, const Sf1Bank& drums,
                 std::vector<int16_t>& mono, const Sf1Bank* factory) {
    constexpr double kRate = 22050.0;
    constexpr size_t kMaxFrames = size_t(kRate * 1200); // cap 20 minutes
    constexpr size_t kMaxTail = size_t(kRate * 20);     // stuck-note guard
    mono.clear();
    std::vector<std::pair<uint32_t, uint32_t>> tempo = song.tempo;
    if (tempo.empty() || tempo[0].first != 0) tempo.insert(tempo.begin(), {0, 500000});
    auto tick_to_frames = [&](uint32_t tick) {
        double frames = 0.0;
        uint32_t last_tick = 0, usec = tempo[0].second;
        for (size_t i = 1; i <= tempo.size(); ++i) {
            uint32_t edge = (i < tempo.size()) ? tempo[i].first : tick;
            if (edge > tick) edge = tick;
            frames += double(edge - last_tick) * usec / 1e6 / double(song.division) * kRate;
            if (edge == tick) break;
            last_tick = edge;
            usec = tempo[i].second;
        }
        return size_t(frames);
    };
    Sf1Synth synth(&melodic, &drums, factory, kRate);
    std::vector<float> left, right;
    auto run = [&](size_t frames) {
        const size_t o = left.size();
        left.resize(o + frames, 0.0f);
        right.resize(o + frames, 0.0f);
        synth.render(left.data() + o, right.data() + o, frames);
    };
    for (const MidsEvent& e : song.events) {
        const size_t at = std::min(tick_to_frames(e.tick), kMaxFrames);
        if (at > left.size()) run(at - left.size());
        synth.event(e.status, e.data0, e.data1);
    }
    for (size_t tail = 0; synth.busy() && tail < kMaxTail && left.size() < kMaxFrames; tail += 1024)
        run(1024);
    if (!synth.started_any()) return; // nothing sounded (unmapped notes only)
    mono.resize(left.size());
    for (size_t i = 0; i < left.size(); ++i) {
        const double m = (double(left[i]) + double(right[i])) * 0.7071067811865476;
        mono[i] = int16_t(lrint(std::clamp(m, -1.0, 1.0) * 32767.0));
    }
}
} // namespace

// Offline render entry point shared with the test (no audio hardware).
bool render_race_song(const MidsSong& song, const Sf1Bank& melodic, const Sf1Bank& drums,
                      std::vector<int16_t>& mono, const Sf1Bank* factory) {
    render_song(song, melodic, drums, mono, factory);
    return !mono.empty();
}

RaceMusicPlayer::RaceMusicPlayer(const Assets& assets, Mixer& mixer) : assets_(assets), mixer_(mixer) {}

RaceMusicPlayer::~RaceMusicPlayer() {
    stop();
}

void RaceMusicPlayer::play_race(int original_course_index, const std::string& alias) {
    stop();
    // 0x00433bb0: the AWE `SbkGrng` mix is used when an AWE32 is present
    // (0x00434500); there is no GM fallback path worth emulating on modern
    // hardware, so the AWE mix always plays. Missing/unreadable assets are a
    // silent no-op and the race continues.
    std::string song;
    try {
        song = race_song_name(original_course_index, alias);
    } catch (const std::runtime_error&) {
        return;
    }
    try {
        song_data_ = read_mids(assets_.read("AUDIO/MUSIC/" + song + ".MID"));
        if (song == "KLOWN") {
            // Klown is a song, not a bank (no KLOWN.SBK on disc): it was
            // written against the AWE factory ROM presets. Merge every course
            // bank for best effort; notes outside those programs stay silent.
            auto merged = std::make_shared<Sf1Bank>();
            for (int n = 1; n <= 5; ++n) {
                auto b = read_sf1(assets_.read("AUDIO/MUSIC/GRUNGE" + std::to_string(n) + ".SBK"), true);
                merged->samples.insert(merged->samples.end(), b->samples.begin(), b->samples.end());
                const uint32_t off = uint32_t(merged->samples.size() - b->samples.size());
                for (const Sf1Zone& z : b->zones) {
                    Sf1Zone shifted = z;
                    shifted.sample_start += off;
                    shifted.sample_end += off;
                    shifted.loop_start += off;
                    shifted.loop_end += off;
                    merged->zones.push_back(shifted);
                }
            }
            melodic_ = std::move(merged);
        } else {
            melodic_ = read_sf1(
                assets_.read("AUDIO/MUSIC/GRUNGE" + std::to_string(original_course_index) + ".SBK"), true);
        }
        drums_ = read_sf1(assets_.read("AUDIO/MUSIC/RRDRUMS.SBK"), false);
    } catch (const std::runtime_error&) {
        stop();
        return;
    }
    song_ = song;
    loaded_ = true;
    playing_ = true;
    synth_ = std::make_unique<Sf1Synth>(melodic_.get(), drums_.get(), &factory_bank());
    // Live path: sequence into the mixer ring (update() renders blocks).
    // Deleting with `delete` (not reset): MusicStream is mixer-visible but
    // player-owned; the mixer only borrows the pointer until stop().
    stream_ = new MusicStream(1 << 15);
    stream_voice_ = mixer_.play_stream(stream_, 127 * music_level_ / 80);
}
void RaceMusicPlayer::stop() {
    if (stream_voice_ >= 0) mixer_.stop(stream_voice_);
    if (stream_) mixer_.retire_stream(stream_);
    if (stream_) stream_->stop(); // generation handshake: no stale frames
    delete stream_;
    stream_ = nullptr;
    stream_voice_ = -1;
    playing_ = false;
    paused_ = false;
    loaded_ = false;
    song_.clear();
    song_data_ = MidsSong{};
    melodic_.reset();
    drums_.reset();
    synth_.reset();
    position_frames_ = 0;
    event_cursor_ = 0;
    tick_ = 0;
    tick_frac_ = 0.0;
}

void RaceMusicPlayer::pause(bool value) {
    paused_ = value;
}

void RaceMusicPlayer::test_play(const MidsSong& song, std::shared_ptr<const Sf1Bank> melodic,
                                std::shared_ptr<const Sf1Bank> drums) {
    stop();
    song_data_ = song;
    melodic_ = std::move(melodic);
    drums_ = std::move(drums);
    song_ = "TEST";
    loaded_ = true;
    playing_ = true;
    synth_ = std::make_unique<Sf1Synth>(melodic_.get(), drums_.get(), &factory_bank());
    stream_ = new MusicStream(1 << 14);
    stream_voice_ = mixer_.play_stream(stream_, 127);
}

void RaceMusicPlayer::update() {
    // Game-thread sequencer: render whole blocks into the mixer ring, firing
    // each event at its exact frame inside the block. No allocation here
    // (the synth reserves its 32 voices); the audio thread consumes
    // lock-free and hears silence on underrun.
    if (!playing_ || paused_ || !loaded_ || !melodic_ || !drums_ || !stream_ || !synth_) return;
    constexpr double kRate = 22050.0;
    constexpr size_t kBlock = 512;
    const auto& events = song_data_.events;
    const double division = double(song_data_.division ? song_data_.division : 480);
    // Backpressure: never advance the sequencer past PCM refused by the ring.
    // Keep a short buffer and fill it on each game-frame pump.
    for (int refill = 0; refill < 8 && stream_->available() < 2048; ++refill) {
        float left[kBlock] = {}, right[kBlock] = {};
        size_t filled = 0;
        while (filled < kBlock) {
            // Loop the song at its end (0x00433e30 enables looping on the MIDI
            // stream): release every note, reset controllers, restart.
            if (event_cursor_ >= events.size() && !events.empty() && tick_ >= song_data_.total_ticks) {
                for (uint8_t ch = 0; ch < 16; ++ch) {
                    synth_->event(uint8_t(0xB0 | ch), 123, 0);
                    synth_->event(uint8_t(0xB0 | ch), 121, 0);
                }
                tick_ = 0;
                tick_frac_ = 0.0;
                event_cursor_ = 0;
            }
            const double now = double(tick_) + tick_frac_;
            while (event_cursor_ < events.size() && double(events[event_cursor_].tick) <= now) {
                const MidsEvent& e = events[event_cursor_++];
                synth_->event(e.status, e.data0, e.data1);
            }
            // Tempo (microseconds per quarter note) at the playhead, and the
            // next tick where it or an event changes the synth state.
            uint32_t usec = 500000;
            double next = double(song_data_.total_ticks);
            for (const auto& t : song_data_.tempo) {
                if (double(t.first) > now) {
                    next = std::min(next, double(t.first));
                    break;
                }
                if (t.second) usec = t.second;
            }
            if (event_cursor_ < events.size()) next = std::min(next, double(events[event_cursor_].tick));
            const double frames_per_tick = std::max(double(usec) / 1e6 / division * kRate, 1e-6);
            size_t n = kBlock - filled;
            if (next > now) n = std::min(n, size_t(std::ceil((next - now) * frames_per_tick)));
            n = std::max<size_t>(n, 1);
            synth_->render(left + filled, right + filled, n);
            filled += n;
            tick_frac_ += double(n) / frames_per_tick;
            const double whole = std::floor(tick_frac_);
            tick_ += uint32_t(whole);
            tick_frac_ -= whole;
        }
        // Centre-panned voices land at half scale per channel, as before.
        const float level = float(music_level_) / 80.0f * 0.70710678f;
        int16_t block[2 * kBlock];
        for (size_t i = 0; i < kBlock; ++i) {
            block[2 * i] = int16_t(lrint(std::clamp(left[i] * level, -1.0f, 1.0f) * 32767.0f));
            block[2 * i + 1] = int16_t(lrint(std::clamp(right[i] * level, -1.0f, 1.0f) * 32767.0f));
        }
        stream_->push(block, kBlock);
        position_frames_ += kBlock;
    }
    if (stream_voice_ >= 0) mixer_.set(stream_voice_, 127 * music_level_ / 80, 0, 256);
}

// Live-registry for the sound.cpp hook region: exactly one RaceMusicPlayer
// is pumped per tick (the game registers its race player after creating the
// SoundSystem). No game_main.cpp changes needed.
namespace {
RaceMusicPlayer* g_race_player = nullptr;
int g_race_music_level = 80;
} // namespace
void pump_race_music() {
    if (g_race_player) {
        g_race_player->set_music_level(g_race_music_level);
        g_race_player->update();
    }
}
void set_race_music_level(int level_0_100) {
    g_race_music_level = std::clamp(level_0_100, 0, 100);
    if (g_race_player) g_race_player->set_music_level(g_race_music_level);
}
void register_race_player(RaceMusicPlayer* player) {
    g_race_player = player;
}

// Jukebox music videos: EXE table 0x00473f98 (census `jukebox_videos`).
std::string jukebox_video_path(int slot) {
    if (slot == 0) return "VIDEO/JESSIE.AVI";
    if (slot == 1) return "VIDEO/DUEL.AVI";
    return "";
}
int jukebox_video_count() {
    return 2;
}

} // namespace rr
