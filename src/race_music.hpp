#pragma once
// Race music: RIFF MIDS songs + EMU SoundFont 1 banks (AWE32 path).
//
// Corrected spec (post plan 13): the race songs are RIFF `MIDS` files, not
// Standard MIDI Files. `GRUNGE1..5.MID` are the generic (non-AWE) mixes and
// `SBKGRNG1..5.MID` are the AWE mixes; `KLOWN.MID` overrides by player alias.
// Melodic banks are `GRUNGE1..5.SBK` (SoundFont 1, `ifil` 1.0) with
// bank-specific (non-GM) programs; `RRDRUMS.SBK` holds the channel-10 kit
// (preset 0 bank 128). Some notes fall on unmapped (program,key) zones;
// those notes are silent, exactly as on hardware missing factory presets.
//   // 0x00433bb0 (song/bank selection), 0x00434500 (AWE detection).
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace rr {
class Assets;
class Mixer;

// A single channel voice event with an absolute tick.
struct MidsEvent {
    uint32_t tick = 0;  // accumulated delta ticks, division units
    uint8_t status = 0; // 0x80..0xEF channel voice status
    uint8_t data0 = 0;
    uint8_t data1 = 0;
};

struct MidsSong {
    uint32_t division = 480;       // ticks per quarter note (MIDS `fmt ` field 0)
    std::vector<MidsEvent> events; // tick order, all blocks concatenated
    // (tick, microseconds per quarter note); kind==1 tempo events.
    std::vector<std::pair<uint32_t, uint32_t>> tempo;
    uint32_t total_ticks = 0;
};

// Bounds-checked RIFF MIDS parser. Throws std::runtime_error on malformed,
// truncated or garbage input.
MidsSong read_mids(const std::vector<uint8_t>& bytes);

// SF2-style envelope in seconds (SBK milliseconds converted at parse time).
// Volume envelopes hold their sustain as attenuation in centibels (1000 =
// silent); modulation envelopes as a 0..1 level. Key scaling is timecents per
// key relative to key 60 (SF2 keynumToHold/Decay).
struct Sf1Envelope {
    float delay = 0.001f, attack = 0.001f, hold = 0.001f, decay = 0.001f, release = 0.001f;
    float sustain = 0.0f; // volume: cB attenuation; modulation: level 0..1
    int16_t key_to_hold = 0, key_to_decay = 0;
};

// One playable sample zone of a SoundFont 1 bank, with the preset and
// instrument generators merged and converted from SBK units to SF2 units
// (docs/REVERSE_ENGINEERING.md "SBK generator semantics"). Defaults are the
// neutral values used by the synthesized factory zones; read_sf1 applies the
// SBK defaults (unset sustain = silent, LFO rates) instead.
struct Sf1Zone {
    uint8_t program = 0; // melodic preset program (bank 0), or 0 for drums
    uint8_t bank = 0;    // 0 melodic, 128 drums
    uint8_t low_key = 0, high_key = 127;
    uint8_t low_vel = 0, high_vel = 127;
    uint8_t root_key = 60;                      // samplePitch (55) / overridingRootKey (58)
    int16_t pitch_correction = 0;               // cents, fractional part of samplePitch
    int8_t coarse_tune = 0;                     // semitones (51)
    int8_t fine_tune = 0;                       // cents (52)
    int16_t scale_tuning = 100;                 // cents per key (56)
    int8_t fixed_key = -1, fixed_velocity = -1; // keynum (46) / velocity (47)
    int16_t attenuation_cb = 0;                 // initialAttenuation (48) in centibels
    uint32_t sample_start = 0, sample_end = 0;  // sample indices into samples
    uint32_t loop_start = 0, loop_end = 0;      // sample indices, end exclusive
    uint8_t sample_modes = 0;                   // 54: 1 loop, 3 loop until release, else one-shot
    uint32_t sample_rate = 44100;               // SBK headers carry no rate (TiMidity: 44100)
    int16_t pan = 0;                            // -500 (left) .. 500 (right), 0.1% units
    int16_t chorus_send = 0, reverb_send = 0;   // effects sends (15/16), 0.1% units
    Sf1Envelope vol_env, mod_env;
    float mod_lfo_delay = 0.001f, mod_lfo_hz = 8.176f;
    float vib_lfo_delay = 0.001f, vib_lfo_hz = 8.176f;
    int16_t mod_lfo_to_pitch = 0, vib_lfo_to_pitch = 0, mod_env_to_pitch = 0; // cents
    int16_t filter_fc = 13500; // absolute cents; filter_on false = open
    bool filter_on = false;
    int16_t filter_q_cb = 0;                      // resonance above DC gain, centibels
    int16_t mod_lfo_to_fc = 0, mod_env_to_fc = 0; // cents
    int16_t mod_lfo_to_volume = 0;                // centibels at LFO peak
    uint8_t exclusive_class = 0;                  // 57
    bool loops() const { return (sample_modes & 1) && loop_end > loop_start; }
};

struct Sf1Bank {
    std::vector<int16_t> samples; // 16-bit mono PCM from `sdta/smpl`
    std::vector<Sf1Zone> zones;
};

// Bounds-checked SoundFont 1 (`sfbk`, `ifil` major 1) parser. Instrument
// global zones are defaults for the instrument's zones; preset generators
// apply only where the instrument zone leaves one unset (key/velocity ranges
// always copy, as in SF1). Values are converted from SBK units with the rules
// of TiMidity's sbkconv.c (reading reference; not vendored). Unknown
// generators are skipped. Throws std::runtime_error on malformed input.
// `melodic=true` keeps bank-0 presets, `melodic=false` keeps the bank-128 kit.
std::shared_ptr<const Sf1Bank> read_sf1(const std::vector<uint8_t>& bytes, bool melodic);

// Factory presets for notes the disc banks cannot play (research music.md:
// "The disc banks are not a complete GM set"). SbkGrng1 ch5 sounds program
// 18, absent from GRUNGE1.SBK; Klown sounds programs 35/56/12/71/11/21
// against GRUNGE5.SBK (drums: channel 9 via RRDRUMS); GRUNGE1 ch1 also
// sounds program 33, absent from its GRUNGE1.SBK. The original AWE32 fell
// back to its factory ROM for these; the native bank is a small synthesized
// GM-timbre set (no disc bytes), used only when no disc zone covers the
// (program,key). `factory_covers(p)` reports the covered melodic programs.
const Sf1Bank& factory_bank();
bool factory_covers(uint8_t program);
// Count note-on events in `song` whose (program,key) is covered by neither
// `melodic`/`drums` nor the factory table (`factory_prog_key` lists the
// census-observed soundings as (bank,program) for the report).
size_t count_factory_notes(const MidsSong& song, const Sf1Bank& melodic, const Sf1Bank& drums);

// Spec-derived EMU-style voice engine shared by the offline render and the
// live player: SF2 volume/modulation envelopes, mod/vib LFOs, resonant
// low-pass filter, pan, exclusive classes, sample loops, and the SF2 default
// modulators (velocity, CC7/CC11 volume, CC10 pan, pitch bend with RPN 0
// range, CC1 vibrato, CC91/CC93 effects sends). Two shared effects buses
// follow the AWE driver's default modes (reverb "Hall 2", chorus "Chorus 3")
// with spec-level stand-in algorithms. No EMU8000 timbre oracle exists (Wine
// has none), so none of this is a measured match.
class Sf1Synth {
  public:
    static constexpr size_t kMaxVoices = 32; // EMU8000 oscillator count
    // `melodic`/`drums` may be null (no notes on that side); unmapped melodic
    // notes fall back to `factory` when non-null.
    Sf1Synth(const Sf1Bank* melodic, const Sf1Bank* drums, const Sf1Bank* factory, double rate = 22050.0);
    ~Sf1Synth();
    void reset(); // all voices off, controllers to MIDI defaults
    void event(uint8_t status, uint8_t data0, uint8_t data1);
    // Adds `frames` of output into the two channel buffers (not cleared).
    void render(float* left, float* right, size_t frames);
    size_t active_voices() const;
    // Voices sounding or effect tails still above -96 dB.
    bool busy() const;
    bool started_any() const { return started_any_; }

  private:
    struct Voice;
    struct Effects;
    struct Channel {
        uint8_t program = 0, volume = 100, expression = 127, pan = 64, mod = 0;
        uint8_t reverb = 40, chorus = 0; // CC91/CC93 (GS power-on depths)
        uint16_t bend = 8192;
        int bend_range = 200; // cents (RPN 0)
        uint8_t rpn_msb = 127, rpn_lsb = 127;
    };
    void note_on(uint8_t ch, uint8_t key, uint8_t vel);
    void start_voice(const Sf1Zone& z, const std::vector<int16_t>& samples, uint8_t ch, uint8_t key,
                     uint8_t vel);
    void controller(uint8_t ch, uint8_t cc, uint8_t value);
    const Sf1Bank *melodic_, *drums_, *factory_;
    double rate_;
    Channel channels_[16];
    std::vector<Voice> voices_;
    std::unique_ptr<Effects> fx_;
    std::vector<float> send_[4]; // reverb L/R, chorus L/R inputs for one render call
    uint64_t serial_ = 0;
    bool started_any_ = false;
};

// Offline render of a parsed song against melodic + drum banks (no audio
// hardware). Returns true when at least one sample was rendered. Unmapped
// notes fall back to `factory` (default: the factory table, so SbkGrng/Klown
// corpus renders are audible like the original AWE path); pass nullptr
// explicitly for the exact hardware-silence audit.
bool render_race_song(const MidsSong&, const Sf1Bank&, const Sf1Bank&, std::vector<int16_t>& mono,
                      const Sf1Bank* factory = &factory_bank());
// Live-registry pump for the sound.cpp hook region: SoundSystem::update
// (already called once per race tick) forwards here so the sequencer runs
// without touching game_main.cpp, which the live session owns.
void pump_race_music();
void set_race_music_level(int level_0_100);
class RaceMusicPlayer;
void register_race_player(RaceMusicPlayer* player);

// Song/bank selection (0x00433bb0): Grunge1..5 by original course 1..5 using
// the SbkGrng AWE mix; the Klown song when the player alias is "krusty" or
// "bozo" (comparison is the original 6/4-byte prefix match).
std::string race_song_name(int original_course_index, const std::string& alias);
bool race_song_is_klown(const std::string& alias);

class RaceMusicPlayer {
  public:
    RaceMusicPlayer(const Assets&, Mixer&);
    ~RaceMusicPlayer();
    // 0x00433bb0: load song+bank for the course; silent no-op when assets are
    // absent or unreadable. Never throws.
    void play_race(int original_course_index, const std::string& alias);
    void stop();
    void pause(bool);
    void update(); // game thread: render the next block into the mixer ring
    bool active() const { return playing_; }
    std::string song() const { return song_; }
    // Test hook: parse + render without audio hardware.
    bool loaded() const { return loaded_; }
    size_t event_count() const { return song_data_.events.size(); }
    // Live-sequencer hooks: buffered frame position, music level (0..100,
    // 0x004362e0 curve channel, default 80), and a synthetic-song entry
    // point for tests (bypasses asset loading, same render path).
    uint64_t position_frames() const { return position_frames_; }
    void set_music_level(int level) { music_level_ = level < 0 ? 0 : level > 100 ? 100 : level; }
    void test_play(const MidsSong& song, std::shared_ptr<const Sf1Bank> melodic,
                   std::shared_ptr<const Sf1Bank> drums);

  private:
    const Assets& assets_;
    Mixer& mixer_;
    bool playing_ = false, paused_ = false, loaded_ = false;
    std::string song_;
    MidsSong song_data_;
    std::shared_ptr<const Sf1Bank> melodic_, drums_;
    class MusicStream* stream_ = nullptr; // owned; mixer only borrows during play
    int stream_voice_ = -1;
    uint64_t position_frames_ = 0;
    uint32_t event_cursor_ = 0;
    uint32_t tick_ = 0;
    double tick_frac_ = 0.0;
    int music_level_ = 80; // 0x004362e0 music channel default
    std::unique_ptr<Sf1Synth> synth_;
};
// Jukebox music videos (options-and-hud.md 59-video audit): table 0x00473f98
// holds JESSIE/DUEL, shown by the jukebox popup 0x00445730.
std::string jukebox_video_path(int slot);
int jukebox_video_count();
} // namespace rr
