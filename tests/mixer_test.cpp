#include "mixer.hpp"
#include <algorithm>
#include <iostream>
int main(int argc, char**) {
    if (argc > 1) {
#ifdef __EMSCRIPTEN__
        // Needs a real audio callback clock; Node has no WebAudio and the
        // SDL dummy driver does not advance playback. The dummy-driver path
        // is covered by the native mixer-device run (plan 23 Task 2).
        std::cout << "mixer-device needs audio hardware; skipping under Node\n";
        return 77;
#endif
        if (!SDL_Init(SDL_INIT_AUDIO)) {
            std::cerr << SDL_GetError() << '\n';
            return 1;
        }
        int result = 0;
        {
            rr::PcmSound sound{22050, 1, {1000, 1000}};
            rr::Mixer mixer(4);
            mixer.pause(true);
            const int voice = mixer.play(&sound, 127, 0, 256, true);
            if (!mixer.open_device()) {
                std::cerr << SDL_GetError() << '\n';
                result = 1;
            }
            // The audio thread publishes the voice's cursor on its first
            // callback; allow a loaded CI runner up to 5 s to get there.
            uint64_t position = 999;
            for (int i = 0; i < 5000 && !mixer.cursor(voice, position); ++i) SDL_Delay(1);
            if (!mixer.cursor(voice, position) || position != 0) {
                std::cerr << "FAIL paused voice cursor at frame 0 after the first callback\n";
                result = 1;
            }
            // Plan 33b Task 4: another device's removal (or the physical default
            // device's, which SDL migrates) leaves our stream untouched.
            const SDL_AudioDeviceID ours = mixer.device_id();
            SDL_Event removed{};
            removed.type = SDL_EVENT_AUDIO_DEVICE_REMOVED;
            removed.adevice.recording = false;
            removed.adevice.which = ours + 1000;
            if (!mixer.handle(removed) || mixer.device_id() != ours) {
                std::cerr << "FAIL unrelated device removal reopened the stream\n";
                result = 1;
            }
            removed.adevice.which = ours;
            mixer.pause(true);
            if (!mixer.handle(removed) || !mixer.device_open() || mixer.device_id() == ours) {
                std::cerr << "FAIL device reopened after removal: " << SDL_GetError() << '\n';
                result = 1;
            }
            if (!mixer.cursor(voice, position) || position != 0) {
                std::cerr << "FAIL cursor kept across the device change\n";
                result = 1;
            }
            mixer.retire(&sound);
        }
        SDL_Quit();
        return result;
    }
    int failures = 0;
    auto check = [&](bool yes, const char* what) {
        if (!yes) {
            ++failures;
            std::cerr << "FAIL " << what << '\n';
        }
    };
    rr::PcmSound constant{22050, 1, {1000, 1000, 1000, 1000}};
    rr::Mixer mixer(2);
    const int id = mixer.play(&constant, 127, 0, 256, true);
    int16_t out[200];
    mixer.render(out, 100);
    check(id >= 0 && std::all_of(std::begin(out), std::end(out), [](int16_t s) { return s == 1000; }),
          "mono loop at unity gain in both channels");
    mixer.set(id, 127, -64, 256);
    mixer.render(out, 1);
    check(out[0] == 1000 && out[1] == 0, "left pan endpoint");
    mixer.set(id, 127, 63, 256);
    mixer.render(out, 1);
    check(out[0] == 0 && out[1] == 1000, "right pan endpoint");
    mixer.stop(id);
    mixer.render(out, 1);
    check(out[0] == 0 && out[1] == 0, "stopped voice silent");
    rr::PcmSound ramp{22050, 1, {100, 200, 300, 400}};
    mixer.play(&ramp, 127, 0, 512, false);
    mixer.render(out, 3);
    check(out[0] == 100 && out[2] == 300 && out[4] == 0,
          "double pitch consumes every second sample then ends");
    rr::PcmSound low_rate{11025, 1, {0, 1000, 2000}};
    mixer.play(&low_rate, 127, 0, 256, false);
    mixer.render(out, 5);
    check(out[0] == 0 && out[2] == 500 && out[4] == 1000 && out[6] == 1500 && out[8] == 2000,
          "linear rate conversion");
    mixer.render(out, 10);
    const int paused_voice = mixer.play(&ramp, 127, 0, 256, false);
    mixer.pause(true);
    mixer.render(out, 20);
    check(std::all_of(out, out + 40, [](int16_t s) { return s == 0; }), "paused output silent");
    mixer.pause(false);
    mixer.render(out, 1);
    check(out[0] == 100, "resume preserves voice position");
    uint64_t cursor = 999;
    check(mixer.cursor(paused_voice, cursor) && cursor == 1, "published playback cursor");
    mixer.pause(true);
    mixer.render(out, 20);
    check(mixer.cursor(paused_voice, cursor) && cursor == 1, "paused cursor stays fixed");
    mixer.pause(false);
    mixer.retire(&ramp);
    mixer.render(out, 1);
    check(out[0] == 0 && out[1] == 0, "retired sound no longer read");
    mixer.stop(paused_voice);
    mixer.render(out, 0);
    rr::PcmSound loud{22050, 1, {32767}};
    mixer.play(&loud, 127, 0, 256, true);
    mixer.play(&loud, 127, 0, 256, true);
    mixer.render(out, 1);
    check(out[0] == 32767 && out[1] == 32767, "saturating mix prevents overflow");
    rr::Mixer steal(1);
    const int stale = steal.play(&loud, 127, 0, 256, true);
    const int active = steal.play(&constant, 127, 0, 256, true);
    steal.stop(stale);
    steal.render(out, 1);
    check(active != stale && out[0] == 1000, "oldest voice stolen; stale handle cannot stop replacement");
    rr::PcmSound stereo{22050, 2, {100, -200, 300, -400}};
    rr::Mixer channels(1);
    channels.play(&stereo, 127, 0, 256, false);
    channels.render(out, 2);
    check(out[0] == 100 && out[1] == -200 && out[2] == 300 && out[3] == -400, "stereo channels preserved");
    // Plan 18 Task 6 Review Focus 3: a voice volume change between two render blocks
    // scales the next block by the new gain; no sample from the old level follows.
    rr::Mixer levels(1);
    const int lv = levels.play(&constant, 100, 0, 256, true);
    int16_t block[4];
    levels.render(block, 2);
    check(block[0] == block[2] && block[0] > 0, "voice renders at the set level");
    levels.set(lv, 50, 0, 256);
    levels.render(block, 2);
    check(block[0] == block[2] && block[0] > 0 && block[0] < 1000, "next block uses the new level only");
    rr::Mixer queue(1);
    int accepted = 0;
    while (queue.play(&constant, 127, 0, 256, true) >= 0) ++accepted;
    check(accepted == 255, "full command queue reports failure");
    queue.render(out, 1);
    check(queue.play(&constant, 127, 0, 256, true) >= 0, "queue recovers after consuming");
    check(mixer.play(nullptr, 127, 0, 256, false) == -1, "invalid sound rejected");
    std::cout << failures << " failures\n";
    return failures ? 1 : 0;
}
