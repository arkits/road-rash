#pragma once
#include "wav.hpp"
#include <SDL3/SDL.h>
#include <array>
#include <atomic>
namespace rr {
// Bounded streaming PCM ring (research music.md "Mixer integration needs one
// extra interface"): the game-thread race sequencer renders stereo int16
// frames with push(); the audio callback (via Mixer::render) consumes them
// with pull(), emitting silence on underrun. Storage is reserved once at
// construction; no allocation or locks occur in pull(). stop() drains queued
// frames and bumps the generation so stale samples from a previous song can
// never leak into the next one. Single producer, single consumer.
class MusicStream {
  public:
    explicit MusicStream(size_t capacity_frames);
    size_t capacity() const { return capacity_; }
    size_t available() const;
    uint64_t generation() const;
    // Push up to `frames` stereo frames; returns false when the ring is full
    // (oldest frames are preserved, newest are refused — never overwritten).
    bool push(const int16_t* stereo, size_t frames);
    // Consume exactly `frames` stereo frames into `out` (silence on
    // underrun). Lock-free; safe from the audio thread.
    void pull(int16_t* out, size_t frames) noexcept;
    void stop() noexcept; // drain + generation handshake
  private:
    size_t capacity_ = 0;
    std::vector<int16_t> ring_;             // interleaved stereo, capacity_*2
    std::atomic<size_t> head_{0}, tail_{0}; // frame cursors (monotonic)
    std::atomic<uint64_t> generation_{0};
};
class Mixer {
  public:
    static constexpr int rate = 22050;
    explicit Mixer(int max_voices);
    ~Mixer();
    Mixer(const Mixer&) = delete;
    Mixer& operator=(const Mixer&) = delete;
    int play(const PcmSound*, int volume, int pan, int pitch_q8, bool loop);
    // Streaming voice: mixes `stream`'s ring (owned by the caller, must
    // outlive the voice) at `volume` 0..127, centre-panned. The stream
    // pointer is read lock-free in render(); stop(id) detaches it.
    int play_stream(MusicStream* stream, int volume);
    bool set(int voice, int volume, int pan, int pitch_q8);
    bool stop(int voice);
    // DirectSound Play(0,0,flags) on a playing buffer: switch looping on or off without restarting.
    bool set_loop(int voice, bool loop);
    bool cursor(int voice, uint64_t& source_frame) const;
    // True while a started voice is queued or still sounding (DirectSound GetStatus PLAYING).
    bool active(int voice) const;
    int queued_frames() const;
    // Game-thread lifetime boundary: drain commands under SDL's stream lock
    // before releasing a sound. The callback never takes a user mutex.
    void retire(const PcmSound*) noexcept;
    void retire_stream(MusicStream*) noexcept;
    void render(int16_t* out, int frames);
    void pause(bool value) { paused_ = value; }
    bool open_device();
    void close_device();
    bool handle(const SDL_Event&);
    bool device_open() const { return stream_ != nullptr; }
    SDL_AudioDeviceID device_id() const { return stream_ ? SDL_GetAudioStreamDevice(stream_) : 0; }

  private:
    struct Command {
        int kind = 0, id = 0, volume = 0, pan = 0, pitch = 256;
        const PcmSound* sound = nullptr;
        bool loop = false;
        MusicStream* stream = nullptr;
    };
    struct Voice {
        const PcmSound* sound = nullptr;
        uint64_t position = 0;
        int id = -1, volume = 0, pan = 0, pitch = 256;
        bool loop = false;
        MusicStream* stream = nullptr;
    };
    std::array<Command, 256> queue_{};
    std::atomic<unsigned> head_{0}, tail_{0};
    std::atomic<bool> paused_{false};
    std::vector<Voice> voices_;
    struct PlaybackCursor {
        std::atomic<int> id{-1};
        std::atomic<uint64_t> frame{0};
        std::atomic<bool> active{false};
    };
    std::atomic<int> consumed_id_{-1};
    std::array<PlaybackCursor, 256> cursors_{};
    int next_id_ = 0;
    SDL_AudioStream* stream_ = nullptr;
    bool push(const Command&);
    void consume();
    static void SDLCALL callback(void*, SDL_AudioStream*, int, int);
};
} // namespace rr
