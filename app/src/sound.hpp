#pragma once

#include <borealis/core/audio.hpp>

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <vector>

// Sound effects: the cursor moving, clicks, going back, errors, slider
// steps (borealis asks for those), and the app's own (saving a Mii,
// picking one up, putting it down). Kenney's Interface Sounds (CC0), in
// app resources/sounds/ as 48 kHz mono WAV. Mixed on the Vita's main audio
// port, beside the music; desktop builds stay silent.
namespace sound {

enum Effect { FOCUS, CLICK, BACK, ERROR, TICK, SAVE, PICK_UP, PUT_DOWN, EFFECT_COUNT };

// Settings > Sound Effects: 0 off, 1 quiet, 2 medium, 3 loud
void setLevel(int level);
void play(Effect e);
// Loads the sounds and gives borealis this player (at start-up, after
// brls::Application::init).
void init();

// Mixes the playing sounds into `frames` stereo frames (testable without
// sound); false when nothing is playing.
class Mixer : public brls::AudioPlayer
{
  public:
    static Mixer& instance();
    bool load(enum brls::Sound sound) override;
    bool play(enum brls::Sound sound, float pitch = 1) override;
    void start(Effect e);
    bool mix(int16_t* stereo, int frames);
    bool loadFiles();
    std::atomic<int> level { 2 };
    static int entry(unsigned int size, void* arg); // the Vita's mixing thread

  private:
    Mixer() = default;
    void run();
    struct Voice {
        int effect = -1;
        size_t pos = 0;
    };
    std::vector<int16_t> samples[EFFECT_COUNT];
    Voice voices[4];
    std::mutex mutex;
    std::condition_variable wake;
    bool started = false;
};

} // namespace sound
