#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#include "assets.hpp"
#include "mixer.hpp"
#include "race_music.hpp"
namespace {
int failures = 0;
void check(bool v, const char* what) {
    if (!v) {
        ++failures;
        std::cerr << "FAIL " << what << "\n";
    }
}
void expect_throw(const std::vector<uint8_t>& b, bool mids, const char* what) {
    bool threw = false;
    try {
        if (mids)
            (void)rr::read_mids(b);
        else
            (void)rr::read_sf1(b, true);
    } catch (const std::runtime_error&) {
        threw = true;
    }
    check(threw, what);
}

// Minimal valid MIDS: one block, one note-on + note-off on channel 1.
std::vector<uint8_t> minimal_mids() {
    std::vector<uint8_t> b;
    auto put = [&](const void* p, size_t n) {
        const uint8_t* q = static_cast<const uint8_t*>(p);
        b.insert(b.end(), q, q + n);
    };
    auto u32 = [&](uint32_t v) {
        uint8_t q[4] = {uint8_t(v), uint8_t(v >> 8), uint8_t(v >> 16), uint8_t(v >> 24)};
        put(q, 4);
    };
    // events: note-on (delta 0) + note-off (delta 480), stride 12
    // (delta, middle word, event word).
    const uint32_t ev_bytes = 24;
    const uint32_t data_bytes = 4 + 8 + ev_bytes;
    const uint32_t riff = 4 + 8 + 12 + 8 + data_bytes;
    put("RIFF", 4);
    u32(riff);
    put("MIDS", 4);
    put("fmt ", 4);
    u32(12);
    u32(480);
    u32(4096);
    u32(0); // flags: 12-byte stride
    put("data", 4);
    u32(data_bytes);
    u32(1); // one block
    u32(0);
    u32(ev_bytes); // start tick, size
    u32(0);
    u32(0);
    u32(0x00402190u); // delta 0, kind 0, vel 0x40 key 0x21 status 0x90
    u32(480);
    u32(0);
    u32(0x00002180u); // delta 480, kind 0, vel 0 key 0x21 status 0x80
    return b;
}

// Factory-preset probe: a synthetic bank with exactly one zone (program 30),
// so note events on program 18 are "unmapped" exactly like the research notes
// describe for SbkGrng1 ch5 (factory program 18 absent from GRUNGE1.SBK).
// The factory table must render those notes audibly instead of dropping them.
struct FactoryProbe {
    rr::MidsSong song;
    std::string why;
};
static FactoryProbe factory_probe_song() {
    rr::MidsSong s;
    s.division = 480;
    s.tempo.emplace_back(0, 500000);
    s.events = {{0, 0xC5, 18, 0}, {0, 0x95, 72, 100}, {480, 0x85, 72, 0}};
    return {s, "SbkGrng1 ch5 program 18"};
}

// Minimal valid SF1: one melodic preset (program 30, bank 0), one zone, one
// 16-sample sine-ish loop.
std::vector<uint8_t> minimal_sf1(int bank_id = 0);
std::vector<uint8_t> minimal_sf1(int bank_id) {
    std::vector<uint8_t> b;
    auto put = [&](const void* p, size_t n) {
        const uint8_t* q = static_cast<const uint8_t*>(p);
        b.insert(b.end(), q, q + n);
    };
    auto u16 = [&](uint16_t v) {
        uint8_t q[2] = {uint8_t(v), uint8_t(v >> 8)};
        put(q, 2);
    };
    auto u32 = [&](uint32_t v) {
        uint8_t q[4] = {uint8_t(v), uint8_t(v >> 8), uint8_t(v >> 16), uint8_t(v >> 24)};
        put(q, 4);
    };
    auto chunk = [&](const char* tag, const std::vector<uint8_t>& payload) {
        put(tag, 4);
        u32(uint32_t(payload.size()));
        put(payload.data(), payload.size());
        if (payload.size() & 1) {
            uint8_t z = 0;
            put(&z, 1);
        }
    };
    auto list = [&](const char* kind, const std::vector<uint8_t>& inner) {
        std::vector<uint8_t> payload;
        payload.insert(payload.end(), kind, kind + 4);
        payload.insert(payload.end(), inner.begin(), inner.end());
        put("LIST", 4);
        u32(uint32_t(payload.size()));
        put(payload.data(), payload.size());
        if (payload.size() & 1) {
            uint8_t z = 0;
            put(&z, 1);
        }
    };
    std::vector<uint8_t> info, sdta, pdta;
    auto info_chunk = [&](const char* tag, const std::vector<uint8_t>& p) {
        size_t o = info.size();
        info.resize(o + 8 + p.size() + (p.size() & 1));
        memcpy(info.data() + o, tag, 4);
        info[o + 4] = uint8_t(p.size());
        info[o + 5] = uint8_t(p.size() >> 8);
        info[o + 6] = uint8_t(p.size() >> 16);
        info[o + 7] = uint8_t(p.size() >> 24);
        memcpy(info.data() + o + 8, p.data(), p.size());
    };
    info_chunk("ifil", {1, 0, 0, 0});
    std::vector<int16_t> sine(16);
    for (int i = 0; i < 16; ++i) sine[i] = int16_t(lrint(20000.0 * sin(i * 6.2831853 / 16)));
    std::vector<uint8_t> smpl(32);
    memcpy(smpl.data(), sine.data(), 32);
    size_t o = sdta.size();
    sdta.resize(o + 8 + 32);
    memcpy(sdta.data() + o, "smpl", 4);
    sdta[o + 4] = 32;
    memcpy(sdta.data() + o + 8, smpl.data(), 32);
    auto pdta_chunk = [&](const char* tag, const std::vector<uint8_t>& p) {
        size_t q = pdta.size();
        pdta.resize(q + 8 + p.size() + (p.size() & 1));
        memcpy(pdta.data() + q, tag, 4);
        pdta[q + 4] = uint8_t(p.size());
        pdta[q + 5] = uint8_t(p.size() >> 8);
        pdta[q + 6] = uint8_t(p.size() >> 16);
        pdta[q + 7] = uint8_t(p.size() >> 24);
        memcpy(pdta.data() + q + 8, p.data(), p.size());
    };
    // phdr: preset + EOP terminal.
    std::vector<uint8_t> phdr(76, 0);
    memcpy(phdr.data(), "TestPreset", 10);
    phdr[20] = 30;
    phdr[22] = uint8_t(bank_id);
    phdr[38 + 24] = 1;
    pdta_chunk("phdr", phdr);
    // pbag: zone 0 (gens 0..1) + terminal.
    pdta_chunk("pbag", {0, 0, 0, 0, 1, 0, 0, 0});
    // pgen: instrument 0.
    pdta_chunk("pgen", {41, 0, 0, 0});
    // inst: instrument + EOI terminal.
    std::vector<uint8_t> inst(44, 0);
    inst[20] = 0;
    inst[22 + 20] = 1;
    pdta_chunk("inst", inst);
    // ibag: zone 0 (gens 0..3) + terminal.
    pdta_chunk("ibag", {0, 0, 0, 0, 3, 0, 0, 0});
    // igen: keyRange low=0 high=127 (high byte first), rootKey 60, sampleID 0.
    pdta_chunk("igen", {43, 0, 0, 127, 58, 0, 60, 0, 53, 0, 0, 0});
    // shdr (compact 16 bytes): start 0, end 16, loop 0..16.
    pdta_chunk("shdr", {0, 0, 0, 0, 16, 0, 0, 0, 0, 0, 0, 0, 16, 0, 0, 0});

    std::vector<uint8_t> body;
    {
        std::vector<uint8_t> inner;
        inner.insert(inner.end(), info.begin(), info.end());
        size_t q = body.size();
        body.resize(q + 12 + inner.size());
        memcpy(body.data() + q, "LIST", 4);
        uint32_t sz = uint32_t(4 + inner.size());
        memcpy(body.data() + q + 4, &sz, 4);
        memcpy(body.data() + q + 8, "INFO", 4);
        memcpy(body.data() + q + 12, inner.data(), inner.size());
    }
    {
        size_t q = body.size();
        body.resize(q + 12 + sdta.size());
        memcpy(body.data() + q, "LIST", 4);
        uint32_t sz = uint32_t(4 + sdta.size());
        memcpy(body.data() + q + 4, &sz, 4);
        memcpy(body.data() + q + 8, "sdta", 4);
        memcpy(body.data() + q + 12, sdta.data(), sdta.size());
    }
    {
        size_t q = body.size();
        body.resize(q + 12 + pdta.size());
        memcpy(body.data() + q, "LIST", 4);
        uint32_t sz = uint32_t(4 + pdta.size());
        memcpy(body.data() + q + 4, &sz, 4);
        memcpy(body.data() + q + 8, "pdta", 4);
        memcpy(body.data() + q + 12, pdta.data(), pdta.size());
    }
    (void)u16;
    (void)chunk;
    (void)list;
    std::vector<uint8_t> out;
    out.insert(out.end(), {'R', 'I', 'F', 'F'});
    uint32_t riff = uint32_t(4 + body.size());
    out.push_back(uint8_t(riff));
    out.push_back(uint8_t(riff >> 8));
    out.push_back(uint8_t(riff >> 16));
    out.push_back(uint8_t(riff >> 24));
    out.insert(out.end(), {'s', 'f', 'b', 'k'});
    out.insert(out.end(), body.begin(), body.end());
    return out;
}
// General synthetic SF1 builder for the generator-semantics tests: presets
// and instruments as generator lists (an instrument zone without a sampleID
// is its global zone), compact 16-byte sample headers, and caller PCM.
using GenList = std::vector<std::pair<uint16_t, int16_t>>;
struct FxPreset {
    uint16_t program = 0, bank = 0;
    std::vector<GenList> zones;
};
struct FxSample {
    uint32_t start, end, loop_start, loop_end;
};
std::vector<uint8_t> build_sf1(const std::vector<FxPreset>& presets,
                               const std::vector<std::vector<GenList>>& insts,
                               const std::vector<FxSample>& samples, const std::vector<int16_t>& pcm) {
    auto u16 = [](std::vector<uint8_t>& b, uint32_t v) {
        b.push_back(uint8_t(v));
        b.push_back(uint8_t(v >> 8));
    };
    auto u32 = [&](std::vector<uint8_t>& b, uint32_t v) {
        u16(b, v & 0xffff);
        u16(b, v >> 16);
    };
    auto chunk = [&](std::vector<uint8_t>& b, const char* tag, const std::vector<uint8_t>& p) {
        b.insert(b.end(), tag, tag + 4);
        u32(b, uint32_t(p.size()));
        b.insert(b.end(), p.begin(), p.end());
        if (p.size() & 1) b.push_back(0);
    };
    auto list = [&](std::vector<uint8_t>& b, const char* kind, const std::vector<uint8_t>& inner) {
        std::vector<uint8_t> p(kind, kind + 4);
        p.insert(p.end(), inner.begin(), inner.end());
        chunk(b, "LIST", p);
    };
    auto gens = [&](std::vector<uint8_t>& bag, std::vector<uint8_t>& gen, uint32_t& count, const GenList& z) {
        u16(bag, count);
        u16(bag, 0);
        for (const auto& [op, amount] : z) {
            u16(gen, op);
            u16(gen, uint16_t(amount));
            ++count;
        }
    };
    std::vector<uint8_t> phdr, pbag, pgen, inst, ibag, igen, shdr;
    uint32_t pg = 0, ig = 0, pb = 0, ib = 0;
    for (const FxPreset& p : presets) {
        phdr.resize(phdr.size() + 20, 0);
        u16(phdr, p.program);
        u16(phdr, p.bank);
        u16(phdr, pb);
        phdr.resize(phdr.size() + 12, 0);
        for (const GenList& z : p.zones) {
            gens(pbag, pgen, pg, z);
            ++pb;
        }
    }
    phdr.resize(phdr.size() + 24, 0);
    u16(phdr, pb);
    phdr.resize(phdr.size() + 12, 0);
    u16(pbag, pg);
    u16(pbag, 0);
    for (const auto& zones : insts) {
        inst.resize(inst.size() + 20, 0);
        u16(inst, ib);
        for (const GenList& z : zones) {
            gens(ibag, igen, ig, z);
            ++ib;
        }
    }
    inst.resize(inst.size() + 20, 0);
    u16(inst, ib);
    u16(ibag, ig);
    u16(ibag, 0);
    for (const FxSample& s : samples) {
        u32(shdr, s.start);
        u32(shdr, s.end);
        u32(shdr, s.loop_start);
        u32(shdr, s.loop_end);
    }
    std::vector<uint8_t> info, sdta, pdta, smpl;
    chunk(info, "ifil", {1, 0, 0, 0});
    for (int16_t v : pcm) u16(smpl, uint16_t(v));
    chunk(sdta, "smpl", smpl);
    chunk(pdta, "phdr", phdr);
    chunk(pdta, "pbag", pbag);
    chunk(pdta, "pgen", pgen);
    chunk(pdta, "inst", inst);
    chunk(pdta, "ibag", ibag);
    chunk(pdta, "igen", igen);
    chunk(pdta, "shdr", shdr);
    std::vector<uint8_t> body;
    list(body, "INFO", info);
    list(body, "sdta", sdta);
    list(body, "pdta", pdta);
    std::vector<uint8_t> out = {'R', 'I', 'F', 'F'};
    u32(out, uint32_t(4 + body.size()));
    out.insert(out.end(), {'s', 'f', 'b', 'k'});
    out.insert(out.end(), body.begin(), body.end());
    return out;
}

// Sine PCM with `period` samples per cycle (441 Hz at 44100 for period 100).
std::vector<int16_t> sine_pcm(size_t n, double period) {
    std::vector<int16_t> v(n);
    for (size_t i = 0; i < n; ++i)
        v[i] = int16_t(lrint(16000.0 * sin(6.283185307179586 * double(i) / period)));
    return v;
}

// One melodic preset (program 0) over one instrument with `zones`, sharing
// one 8000-sample sine (period `period`) whose header loop 99..7998 becomes
// 100..8000 after the SF1 correction (79 whole periods).
std::shared_ptr<const rr::Sf1Bank> one_zone_bank(const GenList& zone, double period = 100.0,
                                                 const GenList& preset_extra = {}) {
    GenList pz = preset_extra;
    pz.push_back({41, 0});
    GenList z = zone;
    z.push_back({53, 0});
    return rr::read_sf1(build_sf1({{0, 0, {pz}}}, {{z}}, {{0, 8000, 99, 7998}}, sine_pcm(8008, period)),
                        true);
}

struct Stereo {
    std::vector<float> l, r;
};
// Note-on at 0 (velocity 127), note-off after `on` frames, total `frames`.
// `dry` first zeroes CC91/CC93 so the voice rules are checked without the
// effects buses (the channel's power-on reverb depth is 40).
Stereo play(const rr::Sf1Bank& bank, uint8_t key, size_t on, size_t frames, bool drum = false,
            const std::vector<std::array<uint8_t, 3>>& setup = {}, bool dry = true) {
    rr::Sf1Synth synth(drum ? nullptr : &bank, drum ? &bank : nullptr, nullptr);
    const uint8_t ch = drum ? 9 : 0;
    if (dry) {
        synth.event(uint8_t(0xB0 | ch), 91, 0);
        synth.event(uint8_t(0xB0 | ch), 93, 0);
    }
    for (const auto& e : setup) synth.event(uint8_t(e[0] | ch), e[1], e[2]);
    synth.event(uint8_t(0x90 | ch), key, 127);
    Stereo s;
    s.l.assign(frames, 0.0f);
    s.r.assign(frames, 0.0f);
    synth.render(s.l.data(), s.r.data(), std::min(on, frames));
    synth.event(uint8_t(0x80 | ch), key, 0);
    if (frames > on) synth.render(s.l.data() + on, s.r.data() + on, frames - on);
    return s;
}
double rms(const std::vector<float>& v, size_t a, size_t b) {
    double e = 0;
    for (size_t i = a; i < b && i < v.size(); ++i) e += double(v[i]) * v[i];
    return b > a ? std::sqrt(e / double(b - a)) : 0.0;
}
// Rising zero crossings per second over [a, b) at 22050 Hz.
double freq(const std::vector<float>& v, size_t a, size_t b) {
    size_t n = 0;
    for (size_t i = a + 1; i < b && i < v.size(); ++i)
        if (v[i - 1] <= 0.0f && v[i] > 0.0f) ++n;
    return double(n) * 22050.0 / double(b - a);
}
bool near(double v, double want, double tol) {
    return std::fabs(v - want) <= tol;
}
} // namespace

int main(int argc, char** argv) {
    // Selection rule (0x00433bb0).
    check(rr::race_song_name(1, "") == "SBKGRNG1", "course 1 -> SBKGRNG1");
    check(rr::race_song_name(4, "") == "SBKGRNG4", "course 4 (NAPA cli) name maps");
    check(rr::race_song_name(3, "krusty") == "KLOWN", "krusty -> KLOWN");
    check(rr::race_song_name(2, "Bozo the clown") == "KLOWN", "bozo prefix -> KLOWN");
    check(rr::race_song_name(5, "KRUSTY") == "KLOWN", "alias match is case-insensitive");
    check(rr::race_song_name(5, "krust") == "SBKGRNG5", "short prefix does not match");
    bool threw = false;
    try {
        (void)rr::race_song_name(0, "");
    } catch (const std::runtime_error&) {
        threw = true;
    }
    check(threw, "course 0 rejected");

    // Synthetic MIDS.
    const auto mids = minimal_mids();
    const rr::MidsSong song = rr::read_mids(mids);
    check(song.division == 480, "synthetic division");
    check(song.events.size() == 2, "synthetic event count");
    check(song.events[0].tick == 0 && song.events[0].status == 0x90 && song.events[0].data0 == 0x21 &&
              song.events[0].data1 == 0x40,
          "synthetic note-on");
    check(song.events[1].tick == 480 && song.events[1].status == 0x80, "synthetic note-off");
    for (size_t i = 0; i < mids.size(); ++i) {
        std::vector<uint8_t> cut(mids.begin(), mids.begin() + i);
        expect_throw(cut, true, "every MIDS truncation throws");
        if (failures) break;
    }
    check(!failures, "MIDS truncation sweep");
    expect_throw({0, 1, 2, 3}, true, "garbage MIDS throws");
    std::vector<uint8_t> bad_tag = mids;
    memcpy(bad_tag.data(), "RIFX", 4);
    expect_throw(bad_tag, true, "wrong RIFF tag throws");
    std::vector<uint8_t> bad_kind = mids;
    memcpy(bad_kind.data() + 8, "XXXX", 4);
    expect_throw(bad_kind, true, "non-MIDS form throws");

    // Synthetic SF1.
    const auto sf1 = minimal_sf1();
    {
        auto bank = rr::read_sf1(sf1, true);
        check(bank->zones.size() == 1, "synthetic zone count");
        check(bank->zones[0].program == 30, "synthetic program");
        check(bank->samples.size() == 16, "synthetic sample count");
        // Offline render: note 33 on channel 1 with program 30 -> audible.
        rr::MidsSong s;
        s.division = 480;
        s.tempo.emplace_back(0, 500000);
        s.events = {{0, 0xC1, 30, 0}, {0, 0x91, 33, 100}, {480, 0x81, 33, 0}};
        auto drums = rr::read_sf1(sf1, false);
        check(drums->zones.empty(), "drum view of melodic bank is empty");
        // Drum-kit bank built directly (preset bank byte 128).
        auto kit = rr::read_sf1(minimal_sf1(128), false);
        std::vector<int16_t> mono;
        check(rr::render_race_song(s, *bank, *kit, mono), "synthetic song renders");
        double energy = 0;
        for (int16_t v : mono) energy += double(v) * v;
        check(energy > 0, "synthetic render is audible");
    }
    for (size_t i = 0; i < sf1.size(); ++i) {
        std::vector<uint8_t> cut(sf1.begin(), sf1.begin() + i);
        bool ok = false;
        try {
            (void)rr::read_sf1(cut, true);
        } catch (const std::runtime_error&) {
            ok = true;
        }
        if (!ok) {
            check(false, "every SF1 truncation throws");
            break;
        }
    }
    expect_throw({1, 2, 3, 4, 5}, false, "garbage SF1 throws");

    // SBK generator semantics on synthetic SF1 fixtures (docs/
    // REVERSE_ENGINEERING.md "SBK generator semantics"). These pin the
    // spec-derived rules; none is an EMU8000 timbre measurement.
    {
        constexpr size_t kSec = 22050;
        // initialAttenuation (48) is a 0..127 level: 127 full, 0 silent.
        check(one_zone_bank({{48, 127}})->zones[0].attenuation_cb == 0, "atten 127 is full level");
        check(one_zone_bank({{48, 101}})->zones[0].attenuation_cb == 198, "atten 101 is 19.8 dB down");
        check(one_zone_bank({{48, 64}})->zones[0].attenuation_cb == 595, "atten 64 is 59.5 dB down");
        check(one_zone_bank({{48, 0}})->zones[0].attenuation_cb == 1000, "atten 0 is silent");
        check(one_zone_bank({})->zones[0].attenuation_cb == 0, "unset atten is full level");
        {
            const auto full = one_zone_bank({{48, 127}, {37, 127}, {54, 1}});
            const auto low = one_zone_bank({{48, 101}, {37, 127}, {54, 1}});
            const double a = rms(play(*full, 60, kSec, kSec).l, 2205, 11025);
            const double b = rms(play(*low, 60, kSec, kSec).l, 2205, 11025);
            check(a > 0 && near(b / a, std::pow(10.0, -198.0 / 200.0), 0.004),
                  "atten 101 renders 19.8 dB down");
        }
        // Root key: samplePitch (55) = root*100 - tune, rounded to the nearer
        // key; overridingRootKey (58) replaces the key; samples are 44100 Hz.
        {
            const rr::Sf1Zone z = one_zone_bank({{55, 5365}})->zones[0];
            check(z.root_key == 54 && z.pitch_correction == 35, "samplePitch 5365 -> key 54 + 35 cents");
            const rr::Sf1Zone y = one_zone_bank({{55, 7200}, {58, 59}})->zones[0];
            check(y.root_key == 59 && y.pitch_correction == 0, "rootKey 58 overrides samplePitch");
            const rr::Sf1Zone x = one_zone_bank({{55, 4281}, {58, 40}})->zones[0];
            // 4281: key 42 - 81 cents rounds to key 43 + 19; 58 replaces the key.
            check(x.root_key == 40 && x.pitch_correction == 19, "override keeps the samplePitch cents");
            const rr::Sf1Zone d = one_zone_bank({})->zones[0];
            check(d.root_key == 60 && d.pitch_correction == 0 && d.sample_rate == 44100,
                  "SBK defaults: key 60, 44100 Hz");
            // A 441 Hz sample (period 100 at 44100) with root 69 plays 882 Hz
            // at key 81; a 22050 Hz assumption would play 441 Hz.
            const auto bank = one_zone_bank({{55, 6900}, {37, 127}, {54, 1}});
            const double f = freq(play(*bank, 81, kSec, kSec).l, 2205, 22050);
            check(near(f, 882.0, 6.0), "samplePitch root at 44100 Hz plays the right pitch");
            const auto tuned = one_zone_bank({{55, 6900}, {37, 127}, {54, 1}, {51, 12}});
            check(near(freq(play(*tuned, 69, kSec, kSec).l, 2205, 22050), 882.0, 6.0),
                  "coarse tune adds semitones");
        }
        // Loops: header + gens 2/3, SF1 +1/+2 correction, gated by sampleModes.
        {
            auto bank =
                rr::read_sf1(build_sf1({{0, 0, {{{41, 0}}}}}, {{{{2, 10}, {3, -20}, {54, 1}, {53, 0}}}},
                                       {{0, 4000, 100, 2100}}, sine_pcm(4008, 100.0)),
                             true);
            const rr::Sf1Zone& z = bank->zones[0];
            check(z.loop_start == 111 && z.loop_end == 2082 && z.loops(), "loop = header + offsets + 1/+2");
            auto one_shot = rr::read_sf1(build_sf1({{0, 0, {{{41, 0}}}}}, {{{{53, 0}}}},
                                                   {{0, 4000, 100, 2100}}, sine_pcm(4008, 100.0)),
                                         true);
            check(!one_shot->zones[0].loops(), "sampleModes unset: no loop");
            auto tail = rr::read_sf1(build_sf1({{0, 0, {{{41, 0}}}}}, {{{{54, 1}, {53, 0}}}},
                                               {{0, 4000, 0, 4000}}, sine_pcm(4008, 100.0)),
                                     true);
            check(tail->zones[0].loop_end == 4001, "loop end clamps to end + 1");
            // Rendered: the one-shot (4000 samples = 2000 frames at the root)
            // is silent at 0.5 s while held; the looped zone still sounds.
            auto held = [&](const rr::Sf1Bank& b) { return rms(play(b, 60, kSec, kSec).l, 11025, 13230); };
            const auto looped = one_zone_bank({{37, 127}, {54, 1}});
            const auto unlooped = one_zone_bank({{37, 127}});
            check(held(*looped) > 0.01 && held(*unlooped) == 0.0, "sampleModes gates the loop");
        }
        // Instrument global zone = defaults; preset fills only unset
        // instrument generators; key ranges copy from the preset.
        {
            auto bank =
                rr::read_sf1(build_sf1({{0, 0, {{{48, 101}, {51, 2}, {43, 60 | (72 << 8)}, {41, 0}}}}},
                                       {{{{48, 101}, {37, 64}, {17, 0}}, {{48, 127}, {53, 0}}}},
                                       {{0, 4000, 0, 4000}}, sine_pcm(4008, 100.0)),
                             true);
            check(bank->zones.size() == 1, "global zone is not a playable zone");
            const rr::Sf1Zone& z = bank->zones[0];
            check(z.attenuation_cb == 0, "instrument zone overrides global and preset");
            check(z.vol_env.sustain == float((2000 - 21 * 64) / 2), "global zone provides the sustain");
            check(z.pan == -500, "global zone provides the pan");
            check(z.coarse_tune == 2, "preset fills an unset instrument generator");
            check(z.low_key == 60 && z.high_key == 72, "preset key range copies");
        }
        // Unit conversions (ms times, 0..127 sustain, Fc, Q, LFO, depths).
        {
            const rr::Sf1Zone z = one_zone_bank({{34, 100},
                                                 {36, 0},
                                                 {29, 47},
                                                 {8, 10},
                                                 {9, 36},
                                                 {11, 63},
                                                 {10, 64},
                                                 {22, 65},
                                                 {6, 64},
                                                 {13, 64},
                                                 {17, 127},
                                                 {57, 1},
                                                 {38, 500}})
                                      ->zones[0];
            check(near(z.vol_env.attack, 0.1, 0.0005), "attack 100 ms");
            check(near(z.vol_env.decay, 0.001, 0.0001), "decay 0 ms -> 1 ms floor");
            check(near(z.vol_env.release, 0.5, 0.002), "release 500 ms");
            check(z.vol_env.sustain == 1000.0f, "unset volume sustain is silent (SBK default)");
            check(near(z.mod_env.sustain, 0.49, 1e-6), "mod sustain 47 -> 0.49");
            check(z.filter_on && z.filter_fc == 4956 && z.filter_q_cb == 54,
                  "Fc 10 -> 4956 cents, Q 36 -> 54 cB");
            check(z.mod_env_to_fc == 7087 && z.mod_lfo_to_fc == 3600, "env/LFO to Fc depths");
            check(near(z.mod_lfo_hz, 5.45, 0.02), "mod LFO 65 -> 5.45 Hz");
            check(z.vib_lfo_to_pitch == 600 && z.mod_lfo_to_volume == 120,
                  "vib depth 600 cents, tremolo 12 dB");
            check(z.pan == 500 && z.exclusive_class == 1, "pan 127 right, exclusive class");
            check(!one_zone_bank({{8, 127}})->zones[0].filter_on, "Fc 127 leaves the filter open");
            check(near(one_zone_bank({})->zones[0].mod_lfo_hz, 5.39, 0.02),
                  "unset mod LFO rate (SBK default)");
            check(one_zone_bank({})->zones[0].mod_env.sustain == 0.0f,
                  "unset mod sustain is zero (SBK default)");
        }
        // Volume envelope: attack ramp, decay to an unset (silent) sustain,
        // and release time after note-off.
        {
            const Stereo att = play(*one_zone_bank({{34, 200}, {37, 127}, {54, 1}}), 60, kSec, kSec);
            check(rms(att.l, 0, 441) < 0.2 * rms(att.l, 6615, 8820), "200 ms attack ramps up");
            auto bank = one_zone_bank({{36, 100}, {54, 1}});
            rr::Sf1Synth held(bank.get(), nullptr, nullptr);
            std::vector<float> l(kSec, 0.0f), r(kSec, 0.0f);
            held.event(0x90, 60, 127);
            held.render(l.data(), r.data(), kSec / 2);
            check(rms(l, 0, 441) > 0.01 && held.active_voices() == 0,
                  "decay to unset sustain ends the voice");
            const Stereo slow = play(*one_zone_bank({{37, 127}, {38, 500}, {54, 1}}), 60, kSec / 2, kSec);
            const Stereo fast = play(*one_zone_bank({{37, 127}, {54, 1}}), 60, kSec / 2, kSec);
            const double sustained = rms(slow.l, 6615, 8820);
            // Release is linear in dB: 1000 cB per release time, so 100 ms
            // into a 500 ms release is 20 dB down and the voice ends at -96 dB.
            const double after = rms(slow.l, 13230, 13450) / sustained;
            check(after > 0.07 && after < 0.14, "500 ms release is 20 dB down after 100 ms");
            check(rms(slow.l, 21800, 22050) < 1e-4, "500 ms release reaches silence");
            check(rms(fast.l, 11466, 12000) < 1e-4, "unset release ends within a few ms");
        }
        // Low-pass filter and modulation envelope to Fc: a 4410 Hz sine
        // (period 10) through Fc 143 Hz is cut; the env sweep opens it.
        {
            const double open =
                rms(play(*one_zone_bank({{37, 127}, {54, 1}}, 10.0), 60, kSec, kSec).l, 2205, 4410);
            const double shut =
                rms(play(*one_zone_bank({{37, 127}, {54, 1}, {8, 10}}, 10.0), 60, kSec, kSec).l, 2205, 4410);
            const double swept = rms(
                play(*one_zone_bank({{37, 127}, {54, 1}, {8, 10}, {11, 63}, {29, 127}}, 10.0), 60, kSec, kSec)
                    .l,
                2205, 4410);
            check(open > 0 && shut < 0.01 * open, "Fc 10 cuts a 4410 Hz tone");
            check(swept > 0.7 * open, "mod env to Fc opens the filter");
            const double peak = rms(
                play(*one_zone_bank({{37, 127}, {54, 1}, {8, 100}}, 100.0), 60, kSec, kSec).l, 2205, 4410);
            const double res =
                rms(play(*one_zone_bank({{37, 127}, {54, 1}, {8, 100}, {9, 100}}, 100.0), 60, kSec, kSec).l,
                    2205, 4410);
            check(res >= peak * 0.99, "Q does not lower the pass band");
        }
        // LFOs: vibrato varies the pitch, tremolo the level.
        {
            auto spread = [&](const Stereo& s, bool use_freq) {
                double lo = 1e9, hi = 0;
                for (size_t a = 2205; a + 882 <= 22050; a += 882) {
                    const double v = use_freq ? freq(s.l, a, a + 882) : rms(s.l, a, a + 882);
                    lo = std::min(lo, v);
                    hi = std::max(hi, v);
                }
                return lo > 0 ? hi / lo : 1e9;
            };
            const Stereo flat = play(*one_zone_bank({{37, 127}, {54, 1}}), 60, kSec, kSec);
            const Stereo vib = play(*one_zone_bank({{37, 127}, {54, 1}, {6, 64}, {24, 64}}), 60, kSec, kSec);
            const Stereo trem =
                play(*one_zone_bank({{37, 127}, {54, 1}, {13, 64}, {22, 65}}), 60, kSec, kSec);
            check(spread(flat, true) < 1.1 && spread(vib, true) > 1.5, "vib LFO modulates pitch");
            check(spread(flat, false) < 1.05 && spread(trem, false) > 2.0, "mod LFO modulates volume");
        }
        // Pan: 0 hard left, 127 hard right (constant power).
        {
            const Stereo left = play(*one_zone_bank({{37, 127}, {54, 1}, {17, 0}}), 60, kSec, kSec);
            const Stereo right = play(*one_zone_bank({{37, 127}, {54, 1}, {17, 127}}), 60, kSec, kSec);
            check(rms(left.l, 2205, 4410) > 0.01 && rms(left.r, 2205, 4410) < 1e-6, "pan 0 is hard left");
            check(rms(right.r, 2205, 4410) > 0.01 && rms(right.l, 2205, 4410) < 1e-6,
                  "pan 127 is hard right");
        }
        // Exclusive class (RRDRUMS hi-hats 42/44/46): a new note of the same
        // class cuts the sounding one on that channel.
        {
            auto kit = [&](int16_t cls) {
                GenList a = {{43, 42 | (42 << 8)}, {37, 127}, {54, 1}, {53, 0}};
                GenList b = {{43, 46 | (46 << 8)}, {37, 127}, {54, 1}, {53, 0}};
                if (cls) {
                    a.push_back({57, cls});
                    b.push_back({57, cls});
                }
                return rr::read_sf1(build_sf1({{0, 128, {{{41, 0}}}}}, {{a, b}}, {{0, 8000, 99, 7998}},
                                              sine_pcm(8008, 100.0)),
                                    false);
            };
            for (int16_t cls : {0, 1}) {
                const auto bank = kit(cls);
                rr::Sf1Synth synth(nullptr, bank.get(), nullptr);
                std::vector<float> l(2205, 0.0f), r(2205, 0.0f);
                synth.event(0x99, 42, 100);
                synth.render(l.data(), r.data(), 441);
                synth.event(0x99, 46, 100);
                synth.render(l.data(), r.data(), 441);
                check(synth.active_voices() == (cls ? 1u : 2u),
                      cls ? "exclusive class cuts" : "no class: both ring");
            }
        }
        // SF2 default modulators the songs use: CC7 (concave), pitch bend
        // with an RPN 0 range of 12 semitones.
        {
            const auto bank = one_zone_bank({{37, 127}, {54, 1}});
            const double full = rms(play(*bank, 60, kSec, kSec, false, {{0xB0, 7, 127}}).l, 2205, 4410);
            const double half = rms(play(*bank, 60, kSec, kSec, false, {{0xB0, 7, 64}}).l, 2205, 4410);
            check(near(half / full, std::pow(64.0 / 127.0, 2.0), 0.005), "CC7 follows the concave curve");
            const auto bend = play(*bank, 60, kSec, kSec, false,
                                   {{0xB0, 101, 0}, {0xB0, 100, 0}, {0xB0, 6, 12}, {0xE0, 0x7f, 0x7f}});
            const double base = freq(play(*bank, 60, kSec, kSec).l, 2205, 22050);
            check(near(freq(bend.l, 2205, 22050) / base, 2.0, 0.02), "pitch bend uses the RPN 0 range");
        }
        // Effects buses: CC91/CC93 (+20% at 127) plus the zone sends 15/16
        // feed a shared reverb and chorus. Algorithms are stand-ins; these
        // checks pin the send rules and tail behaviour, not a timbre.
        {
            const size_t on = kSec / 2, total = 3 * kSec;
            const auto bank = one_zone_bank({{37, 127}, {54, 1}});
            const Stereo dry = play(*bank, 60, on, total);
            check(rms(dry.l, on + 2205, total) == 0.0 && rms(dry.r, on + 2205, total) == 0.0,
                  "no sends: the tail is exact silence");
            auto tail = [&](const Stereo& s, size_t a, size_t b) { return rms(s.l, a, b) + rms(s.r, a, b); };
            const Stereo wet = play(*bank, 60, on, total, false, {{0xB0, 91, 127}});
            const double early = tail(wet, on + 4410, on + 8820), late = tail(wet, on + 33075, on + 37485);
            check(early > 1e-4 && late > 0.0 && early / late > 10.0, "CC91 reverb tail rings and decays");
            const Stereo half = play(*bank, 60, on, total, false, {{0xB0, 91, 64}});
            check(near(tail(half, on + 4410, on + 8820) / early, 64.0 / 127.0, 0.01),
                  "reverb send is linear in CC91");
            const auto zone_send = one_zone_bank({{37, 127}, {54, 1}, {16, 51}}); // 51/256 -> 199 per mille
            check(near(tail(play(*zone_send, 60, on, total), on + 4410, on + 8820) / early, 199.0 / 200.0,
                       0.01),
                  "zone reverb send 16 adds like CC91");
            const Stereo deflt = play(*bank, 60, on, total, false, {}, false);
            const Stereo forty = play(*bank, 60, on, total, false, {{0xB0, 91, 40}, {0xB0, 93, 0}}, false);
            check(tail(deflt, on + 4410, on + 8820) > 0.0 && deflt.l == forty.l && deflt.r == forty.r,
                  "power-on reverb depth is 40");
            const Stereo cho = play(*bank, 60, on, total, false, {{0xB0, 93, 127}});
            double diff = 0;
            for (size_t i = 2205; i < on; ++i) diff += std::fabs(cho.l[i] - dry.l[i]);
            const double held = rms(cho.l, 2205, on);
            check(diff > 0.0 && tail(cho, on + 4410, total) < 1e-6 * held,
                  "chorus alters the held note and leaves no long tail");
            rr::Sf1Synth synth(bank.get(), nullptr, nullptr);
            std::vector<float> l(total, 0.0f), r(total, 0.0f);
            synth.event(0xB0, 91, 127);
            synth.event(0x90, 60, 127);
            synth.render(l.data(), r.data(), on);
            synth.event(0x80, 60, 0);
            synth.render(l.data(), r.data(), kSec);
            check(synth.active_voices() == 0 && synth.busy(), "reverb tail keeps the synth busy");
            synth.reset();
            std::fill(l.begin(), l.end(), 0.0f);
            synth.render(l.data(), r.data(), kSec);
            check(!synth.busy() && rms(l, 0, kSec) == 0.0, "reset clears the effect tails");
        }
    }
    // Factory presets (research: music.md "The disc banks are not a complete
    // GM set"). SbkGrng1 ch5 sounds program 18, absent from GRUNGE1.SBK;
    // Klown sounds programs 35/56/12/71/11/21 against GRUNGE5.SBK. Notes on
    // those programs must render through the factory table, not go silent.
    {
        auto bank = rr::read_sf1(sf1, true);
        auto kit = rr::read_sf1(minimal_sf1(128), false);
        const FactoryProbe probe = factory_probe_song();
        std::vector<int16_t> silent;
        // Fully-unmapped probe with nullptr factory: no notes, empty render.
        check(!rr::render_race_song(probe.song, *bank, *kit, silent, nullptr),
              "probe song silent without factory presets");
        check(silent.empty(), "unmapped probe renders nothing");
        std::vector<int16_t> mono;
        check(rr::render_race_song(probe.song, *bank, *kit, mono, &rr::factory_bank()),
              "probe song renders with factory presets");
        double energy = 0;
        for (int16_t v : mono) energy += double(v) * v;
        check(energy > 0, "program-18 note audible through factory presets");
        for (uint8_t p : {18, 35, 56, 12, 71, 11, 21}) check(rr::factory_covers(p), "factory covers program");
        check(rr::factory_covers(33), "factory covers GRUNGE1 program 33");
    }

    // Streaming PCM queue (research: music.md "Mixer integration needs one
    // extra interface"). The race sequencer renders on the game thread into
    // a bounded ring; the audio callback consumes frames and emits silence
    // on underrun. Stop uses a generation handshake: stale frames from the
    // previous song must never leak into the next one.
    {
        rr::Mixer mixer(4);
        rr::MusicStream stream(1 << 14);
        check(stream.capacity() >= 4096, "stream ring capacity");
        check(stream.available() == 0, "stream starts empty");
        std::vector<int16_t> frames(2048, 1000);
        check(stream.push(frames.data(), 1024), "stream push accepts frames");
        check(stream.available() == 1024, "stream reports pushed frames");
        std::vector<int16_t> out(2048, 0);
        stream.pull(out.data(), 512);
        check(out[0] == 1000 && out[1022] == 1000, "stream pull returns pushed PCM");
        check(out[1024] == 0 && out[2046] == 0, "stream underrun emits silence");
        const uint64_t gen = stream.generation();
        stream.stop();
        check(stream.available() == 0, "stop drains queued frames");
        check(stream.generation() != gen, "stop bumps the generation");
        size_t pushed = 0;
        std::vector<int16_t> fill(2 * 1024, 7); // 1024 stereo frames
        while (stream.push(fill.data(), 1024)) {
            if (++pushed > 64) break;
        }
        check(stream.available() <= stream.capacity(), "ring never overflows");
        std::vector<int16_t> keep(2048, 0);
        stream.pull(keep.data(), 1024);
        check(keep[0] == 7, "ring preserves oldest frames under pressure");
        stream.stop();
        int voice = mixer.play_stream(&stream, 127);
        check(voice >= 0, "mixer accepts a stream voice");
        stream.push(frames.data(), 1024);
        std::vector<int16_t> mixed(4);
        mixer.render(mixed.data(), 2);
        check(mixed[0] != 0 || mixed[1] != 0, "stream voice is audible");
        mixer.stop(voice);
        mixer.render(mixed.data(), 2);
    }

    // Live sequencer (open question 3 in analysis/music-notes.md): play_race
    // must sequence into the mixer, not only parse. After update() calls the
    // mixer renders audible frames; pause holds position; stop silences.
    {
        rr::Mixer mixer(8);
        rr::Assets empty("/nonexistent-rr-disc-root");
        rr::RaceMusicPlayer silent(empty, mixer);
        silent.play_race(4, "");
        check(!silent.active(), "player silent without assets");
        rr::Mixer live(8);
        rr::RaceMusicPlayer player(empty, live);
        rr::MidsSong s;
        s.division = 480;
        s.tempo.emplace_back(0, 500000);
        s.events = {{0, 0xC1, 30, 0}, {0, 0x91, 33, 100}, {480, 0x81, 33, 0}};
        auto bank = rr::read_sf1(sf1, true);
        auto kit = rr::read_sf1(minimal_sf1(128), false);
        player.test_play(s, bank, kit);
        check(player.active() && player.loaded(), "test sequencer starts");
        for (int i = 0; i < 8; ++i) player.update();
        std::vector<int16_t> out(2 * 2048, 0);
        live.render(out.data(), 2048);
        double energy = 0;
        for (int16_t v : out) energy += double(v) * v;
        check(energy > 0, "sequenced music audible through mixer");
        player.pause(true);
        const uint64_t held = player.position_frames();
        player.update();
        check(player.position_frames() == held, "pause holds song position");
        player.pause(false);
        player.set_music_level(64);
        player.update();
        player.stop();
        // Drain any frames already queued before stop (ring holds up to one
        // block per update); after the drain the voice is silent.
        for (int i = 0; i < 40; ++i) live.render(out.data(), 2048);
        live.render(out.data(), 64);
        bool quiet = std::all_of(out.begin(), out.end(), [](int16_t v) { return v == 0; });
        check(quiet, "stop silences after drain");
    }

    // Jukebox video triggers (research: options-and-hud.md 59-video audit):
    // JESSIE/DUEL are jukebox music videos (table 0x00473f98), not
    // duel-race outcomes. The trigger helper resolves the exact disc paths.
    {
        check(rr::jukebox_video_path(0) == "VIDEO/JESSIE.AVI", "jukebox slot 0 is JESSIE");
        check(rr::jukebox_video_path(1) == "VIDEO/DUEL.AVI", "jukebox slot 1 is DUEL");
        check(rr::jukebox_video_path(2).empty(), "jukebox slot out of range empty");
        check(rr::jukebox_video_count() == 2, "jukebox video count matches 0x00473f98");
    }

    // Player with no assets: silent no-op, never throws.
    {
        rr::Mixer mixer(8);
        // Nonexistent root: every asset read fails.
        rr::Assets empty("/nonexistent-rr-disc-root");
        rr::RaceMusicPlayer player(empty, mixer);
        player.play_race(4, "");
        check(!player.active() && !player.loaded(), "missing assets: silent no-op");
        player.update();
        player.pause(true);
        player.stop();
    }

    // Corpus (skip 77 without the disc).
    rr::Assets assets(argc > 1 ? argv[1] : RR_SOURCE_DIR "/../cdrom/ROADRASH");
    if (!assets.exists("ROADRASH.EXE")) {
        std::cout << "skip: no disc\n";
        return failures ? 1 : 77;
    }
    // All eleven disc songs (research music.md: corpus acceptance is eleven
    // files, not five): five generic GRUNGE mixes + five AWE SBKGRNG mixes +
    // KLOWN. Generic mixes parse with the same reader but never play.
    const char* songs[] = {"GRUNGE1",  "GRUNGE2",  "GRUNGE3",  "GRUNGE4",  "GRUNGE5", "SBKGRNG1",
                           "SBKGRNG2", "SBKGRNG3", "SBKGRNG4", "SBKGRNG5", "KLOWN"};
    for (const char* name : songs) {
        const std::string path = std::string("AUDIO/MUSIC/") + name + ".MID";
        if (!assets.exists(path)) {
            check(false, "corpus song present");
            continue;
        }
        rr::MidsSong s = rr::read_mids(assets.read(path));
        check(!s.events.empty(), "corpus song has events");
        std::cout << name << ": " << s.events.size() << " events, division " << s.division << ", tempos "
                  << s.tempo.size() << "\n";
    }
    // Research spot values: Klown division 120 with 112 tempos; Grunge
    // divisions 480 with a single tempo each.
    {
        rr::MidsSong klown = rr::read_mids(assets.read("AUDIO/MUSIC/KLOWN.MID"));
        check(klown.division == 120, "KLOWN division 120");
        check(klown.tempo.size() == 112, "KLOWN tempo count 112");
        rr::MidsSong g2 = rr::read_mids(assets.read("AUDIO/MUSIC/SBKGRNG2.MID"));
        check(g2.division == 480 && g2.tempo.size() == 1, "SBKGRNG2 single tempo");
    }
    for (int n = 1; n <= 5; ++n) {
        auto b = rr::read_sf1(assets.read("AUDIO/MUSIC/GRUNGE" + std::to_string(n) + ".SBK"), true);
        check(!b->zones.empty() && !b->samples.empty(), "corpus melodic bank loads");
        std::cout << "GRUNGE" << n << ": " << b->zones.size() << " zones, " << b->samples.size()
                  << " samples\n";
    }
    {
        auto kit = rr::read_sf1(assets.read("AUDIO/MUSIC/RRDRUMS.SBK"), false);
        check(kit->zones.size() >= 12, "drum kit zone count");
        // Factory-note audit per AWE song: sounding (program,key) notes with
        // no disc zone. SbkGrng1 must show program 18 (research census).
        const char* awe[] = {"SBKGRNG1", "SBKGRNG2", "SBKGRNG3", "SBKGRNG4", "SBKGRNG5"};
        for (int n = 1; n <= 5; ++n) {
            rr::MidsSong s = rr::read_mids(assets.read(std::string("AUDIO/MUSIC/") + awe[n - 1] + ".MID"));
            auto mel = rr::read_sf1(assets.read("AUDIO/MUSIC/GRUNGE" + std::to_string(n) + ".SBK"), true);
            const size_t missing = rr::count_factory_notes(s, *mel, *kit);
            std::vector<int16_t> mono;
            check(rr::render_race_song(s, *mel, *kit, mono), "AWE song renders");
            double energy = 0;
            for (size_t i = 0; i < mono.size(); i += 7) energy += double(mono[i]) * mono[i];
            std::cout << awe[n - 1] << ": factory notes " << missing << ", frames " << mono.size() << "\n";
            check(energy > 0, "AWE render audible with factory presets");
            if (n == 1) check(missing > 0, "SBKGRNG1 has factory notes (program 18)");
        }
        // Klown against the merged five-bank best effort: factory programs
        // from the census must still need the factory table.
        rr::MidsSong klown = rr::read_mids(assets.read("AUDIO/MUSIC/KLOWN.MID"));
        auto merged = std::make_shared<rr::Sf1Bank>();
        for (int n = 1; n <= 5; ++n) {
            auto b = rr::read_sf1(assets.read("AUDIO/MUSIC/GRUNGE" + std::to_string(n) + ".SBK"), true);
            merged->samples.insert(merged->samples.end(), b->samples.begin(), b->samples.end());
            const uint32_t off = uint32_t(merged->samples.size() - b->samples.size());
            for (const rr::Sf1Zone& z : b->zones) {
                rr::Sf1Zone shifted = z;
                shifted.sample_start += off;
                shifted.sample_end += off;
                shifted.loop_start += off;
                shifted.loop_end += off;
                merged->zones.push_back(shifted);
            }
        }
        const size_t klown_missing = rr::count_factory_notes(klown, *merged, *kit);
        std::cout << "KLOWN: factory notes " << klown_missing << "\n";
        check(klown_missing > 0, "KLOWN has factory notes (35/56/12/71/11/21)");
        // Full corpus render of the Napa song: must be audible.
        rr::MidsSong s = rr::read_mids(assets.read(std::string("AUDIO/MUSIC/") + "SBKGRNG4.MID"));
        auto mel = rr::read_sf1(assets.read("AUDIO/MUSIC/GRUNGE4.SBK"), true);
        std::vector<int16_t> mono;
        check(rr::render_race_song(s, *mel, *kit, mono), "corpus Napa song renders");
        double energy = 0;
        for (size_t i = 0; i < mono.size(); i += 7) energy += double(mono[i]) * mono[i];
        std::cout << "SBKGRNG4 render: " << mono.size() << " frames\n";
        check(energy > 0, "corpus render is audible");
        // Player loads the race song behind the same interface, and the live
        // sequencer now streams it into the mixer (not parse-only).
        rr::Mixer mixer(8);
        rr::RaceMusicPlayer player(assets, mixer);
        player.play_race(4, "");
        check(player.active() && player.loaded() && player.song() == "SBKGRNG4",
              "play_race(4) loads SBKGRNG4");
        check(player.event_count() == s.events.size(), "player event count matches parser");
        for (int i = 0; i < 4; ++i) player.update();
        std::vector<int16_t> out(2 * 2048, 0);
        mixer.render(out.data(), 2048);
        double live_energy = 0;
        for (int16_t v : out) live_energy += double(v) * v;
        check(live_energy > 0, "play_race(4) streams audible frames");
        // Repeated pumps with no consumer must not skip the song timeline.
        const auto buffered_position = player.position_frames();
        for (int i = 0; i < 120; ++i) player.update();
        check(player.position_frames() <= buffered_position + 2048, "music producer respects buffered PCM");
        int sounding_blocks = 0;
        for (int i = 0; i < 1800; ++i) {
            player.update();
            mixer.render(out.data(), 368);
            if (std::any_of(out.begin(), out.begin() + 736, [](int16_t v) { return v != 0; }))
                ++sounding_blocks;
        }
        check(sounding_blocks > 900, "live Napa music continues for thirty seconds");
        player.stop();
        mixer.render(out.data(), 2048);
        check(std::all_of(out.begin(), out.end(), [](int16_t v) { return v == 0; }),
              "stop detaches stream before freeing it");
        check(!player.active(), "stop silences");
        player.play_race(1, "krusty");
        check(player.active() && player.song() == "KLOWN", "krusty loads KLOWN");
        player.stop();
    }

    if (failures) std::cerr << failures << " failures\n";
    return failures ? 1 : 0;
}
