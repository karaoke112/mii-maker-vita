#include "sound.hpp"

#include <borealis.hpp>

#include <cstdio>
#include <cstring>
#include <thread>

#if defined(__PSV__)
#include <psp2/audioout.h>
#include <psp2/kernel/threadmgr.h>
#endif

namespace sound {

static const char* const kFiles[EFFECT_COUNT] = { "focus", "click", "back", "error", "tick", "save", "pick_up", "put_down",
    "click_001", "confirmation_002", "select_005" };

// A 16-bit mono 48 kHz PCM WAV's samples (the files are made that way).
static bool readWav(const std::string& path, std::vector<int16_t>* out)
{
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f)
        return false;
    std::vector<uint8_t> d;
    uint8_t buf[4096];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0)
        d.insert(d.end(), buf, buf + n);
    std::fclose(f);
    auto u16 = [&](size_t at) { return (uint32_t)d[at] | ((uint32_t)d[at + 1] << 8); };
    auto u32 = [&](size_t at) { return u16(at) | (u16(at + 2) << 16); };
    if (d.size() < 12 || std::memcmp(d.data(), "RIFF", 4) != 0 || std::memcmp(d.data() + 8, "WAVE", 4) != 0)
        return false;
    bool fmtOk = false;
    for (size_t at = 12; at + 8 <= d.size();) {
        const uint32_t size = u32(at + 4);
        const size_t body = at + 8;
        if (body + size > d.size())
            return false;
        if (!std::memcmp(d.data() + at, "fmt ", 4) && size >= 16)
            fmtOk = u16(body) == 1 /* PCM */ && u16(body + 2) == 1 && u32(body + 4) == 48000 && u16(body + 14) == 16;
        else if (!std::memcmp(d.data() + at, "data", 4) && fmtOk) {
            out->resize(size / 2);
            for (size_t i = 0; i < out->size(); i++)
                (*out)[i] = (int16_t)u16(body + 2 * i);
            return true;
        }
        at = body + size + (size & 1);
    }
    return false;
}

Mixer& Mixer::instance()
{
    static Mixer m;
    return m;
}

bool Mixer::loadFiles()
{
    bool all = true;
    for (int e = 0; e < EFFECT_COUNT; e++) {
        const std::string path = std::string(BRLS_RESOURCES) + "sounds/" + kFiles[e] + ".wav";
        if (!readWav(path, &samples[e])) {
            brls::Logger::error("sound: cannot read {}", path);
            all = false;
        }
    }
    return all;
}

bool Mixer::load(enum brls::Sound) { return true; }

// borealis's sounds: only the cursor moving plays one (the others stay
// silent until a sound is chosen for them)
bool Mixer::play(enum brls::Sound s, float)
{
    if (s == brls::SOUND_FOCUS_CHANGE || s == brls::SOUND_FOCUS_SIDEBAR) {
        start(CLICK_001);
        return true;
    }
    return false;
}

// Starts a sound: again from its start if it is playing, else in a free
// voice (or the one furthest along).
void Mixer::start(Effect e)
{
    if (level <= 0 || samples[e].empty())
        return;
    {
        std::lock_guard<std::mutex> lock(mutex);
        Voice* pick = nullptr;
        for (Voice& v : voices)
            if (v.effect == e)
                pick = &v;
        for (Voice& v : voices)
            if (!pick && v.effect < 0)
                pick = &v;
        if (!pick) {
            pick = &voices[0];
            for (Voice& v : voices)
                if (v.pos > pick->pos)
                    pick = &v;
        }
        pick->effect = e;
        pick->pos = 0;
    }
    wake.notify_one();
}

bool Mixer::mix(int16_t* stereo, int frames)
{
    static const int kGain[4] = { 0, 90, 170, 256 }; // /256, by level
    const int gain = kGain[level < 0 ? 0 : level > 3 ? 3 : level.load()];
    bool any = false;
    std::vector<int32_t> acc((size_t)frames, 0);
    {
        std::lock_guard<std::mutex> lock(mutex);
        for (Voice& v : voices) {
            if (v.effect < 0)
                continue;
            const std::vector<int16_t>& s = samples[v.effect];
            const size_t n = std::min((size_t)frames, s.size() - v.pos);
            for (size_t i = 0; i < n; i++)
                acc[i] += s[v.pos + i];
            v.pos += n;
            if (v.pos >= s.size())
                v.effect = -1;
            any = true;
        }
    }
    for (int i = 0; i < frames; i++) {
        int32_t x = acc[i] * gain / 256;
        x = x > 32767 ? 32767 : x < -32768 ? -32768 : x;
        stereo[2 * i] = stereo[2 * i + 1] = (int16_t)x;
    }
    return any;
}

// The Vita's main port, 48 kHz: short buffers (5 ms), so a sound starts
// as the button is pressed; between sounds the thread waits.
void Mixer::run()
{
#if defined(__PSV__)
    const int grain = 256;
    const int port = sceAudioOutOpenPort(SCE_AUDIO_OUT_PORT_TYPE_MAIN, grain, 48000, SCE_AUDIO_OUT_MODE_STEREO);
    if (port < 0) {
        brls::Logger::error("sound: audio port: 0x{:08X}", (unsigned)port);
        return;
    }
    int volume[2] = { SCE_AUDIO_OUT_MAX_VOL, SCE_AUDIO_OUT_MAX_VOL };
    sceAudioOutSetVolume(port, (SceAudioOutChannelFlag)(SCE_AUDIO_VOLUME_FLAG_L_CH | SCE_AUDIO_VOLUME_FLAG_R_CH), volume);
    std::vector<int16_t> buffers[2] = { std::vector<int16_t>(grain * 2), std::vector<int16_t>(grain * 2) };
    int which = 0;
    for (;;) {
        {
            std::unique_lock<std::mutex> lock(mutex);
            wake.wait(lock, [this] {
                for (const Voice& v : voices)
                    if (v.effect >= 0)
                        return true;
                return false;
            });
        }
        // until the sounds end, then one buffer of silence to finish on
        bool more = true;
        while (more) {
            more = mix(buffers[which].data(), grain);
            sceAudioOutOutput(port, buffers[which].data()); // waits for the previous buffer
            which ^= 1;
        }
    }
#endif
}

void setLevel(int level)
{
    Mixer::instance().level = level;
}

void play(Effect e)
{
    Mixer::instance().start(e);
}

void init()
{
    Mixer& m = Mixer::instance();
    m.loadFiles();
    brls::Application::setAudioPlayer(&m);
#if defined(__PSV__)
    // A pthread (it waits on a std::mutex / condition_variable, which need
    // one: a thread made with sceKernelCreateThread crashed on its first
    // wait), raised to the music's priority: a sound late behind icon
    // rendering would feel like lag.
    std::thread([&m] {
        sceKernelChangeThreadPriority(0 /* this thread */, 0x40);
        m.run();
    }).detach();
#endif
}

} // namespace sound
