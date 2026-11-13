#include "music.hpp"

#include "paths.hpp"

#include <borealis/core/logger.hpp>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#if !defined(__PSV__)
#include <pthread.h>
#endif
#include <sys/stat.h>
#include <vector>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wunused-variable"
#pragma GCC diagnostic ignored "-Wunused-but-set-variable"
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#pragma GCC diagnostic ignored "-Wsign-compare"
#define STB_VORBIS_NO_PUSHDATA_API
#include <stb_vorbis.c>
#pragma GCC diagnostic pop

#if defined(__PSV__)
#include <psp2/audioout.h>
#include <psp2/kernel/threadmgr.h>
#endif

namespace music {

const char* fileName(Track t)
{
    switch (t) {
    case Track::WII:  return "mii_channel.ogg";
    case Track::DS3:  return "mii_maker_3ds.ogg";
    case Track::WIIU: return "mii_maker_wiiu.ogg";
    default:          return "";
    }
}

std::string path(Track t)
{
    return paths::appDir() + "/music/" + fileName(t);
}

// ---- decoding ------------------------------------------------------------

LoopingStream::~LoopingStream()
{
    if (v)
        stb_vorbis_close(v);
}

static bool comment(const stb_vorbis_comment& c, const char* key, uint64_t* out)
{
    const size_t n = std::strlen(key);
    for (int i = 0; i < c.comment_list_length; i++) {
        const char* s = c.comment_list[i];
        if (strncasecmp(s, key, n) == 0 && s[n] == '=') {
            *out = std::strtoull(s + n + 1, nullptr, 10);
            return true;
        }
    }
    return false;
}

bool LoopingStream::open(const std::string& file)
{
    // read it all up front: decoding then never waits on the memory card,
    // which the icon cache keeps busy
    FILE* f = std::fopen(file.c_str(), "rb");
    if (!f)
        return false;
    std::fseek(f, 0, SEEK_END);
    const long size = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    if (size <= 0) {
        std::fclose(f);
        return false;
    }
    data.resize((size_t)size);
    const size_t got = std::fread(data.data(), 1, data.size(), f);
    std::fclose(f);
    if (got != data.size())
        return false;
    int err = 0;
    v = stb_vorbis_open_memory(data.data(), (int)data.size(), &err, nullptr);
    if (!v)
        return false;
    const stb_vorbis_info info = stb_vorbis_get_info(v);
    rate = (int)info.sample_rate;
    const uint64_t total = (uint64_t)stb_vorbis_stream_length_in_samples64(v);
    const stb_vorbis_comment c = stb_vorbis_get_comment(v);
    uint64_t length = 0, loopEndTag = 0;
    start = 0;
    end = total;
    if (comment(c, "LOOPSTART", &start)) {
        if (comment(c, "LOOPLENGTH", &length) && length > 0)
            end = start + length;
        else if (comment(c, "LOOPEND", &loopEndTag) && loopEndTag > start)
            end = loopEndTag;
    }
    if (total > 0 && (end > total || end == 0))
        end = total;
    if (start >= end)
        start = 0;
    pos = 0;
    brls::Logger::info("music: {} at {} Hz, loop {}-{} of {}", file, rate, start, end, total);
    return true;
}

int LoopingStream::read(int16_t* out, int frames)
{
    int done = 0;
    int stalls = 0;
    while (done < frames) {
        int want = frames - done;
        if (end > pos && (uint64_t)want > end - pos)
            want = (int)(end - pos);
        const int got = want > 0 ? stb_vorbis_get_samples_short_interleaved(v, 2, out + 2 * done, want * 2) : 0;
        done += got;
        pos += (uint64_t)got;
        if (pos >= end || got == 0) {
            // the loop end (or the end of the file): back to the loop start
            if (got == 0 && ++stalls > 2)
                break; // nothing decodes: give up
            stb_vorbis_seek(v, (unsigned int)start);
            pos = start;
        }
    }
    return done;
}

// ---- playing -------------------------------------------------------------

Player& Player::instance()
{
    static Player p;
    return p;
}

Player::~Player()
{
    stop();
}

void Player::stop()
{
    running = false;
    if (started) {
#if defined(__PSV__)
        sceKernelWaitThreadEnd((SceUID)worker, nullptr, nullptr);
        sceKernelDeleteThread((SceUID)worker);
#else
        pthread_join((pthread_t)worker, nullptr);
#endif
        started = false;
    }
    track = Track::NONE;
}

bool Player::play(Track t, std::string* error)
{
    stop();
    if (t == Track::NONE || t >= Track::COUNT)
        return true;
    const std::string file = path(t);
    struct stat st;
    if (stat(file.c_str(), &st) != 0) {
        if (error)
            *error = file;
        return false;
    }
    track = t;
#if defined(__PSV__)
    // A native thread at the highest user priority, so icon rendering and
    // the disk cache never hold the next buffer back; the decoder wants a
    // bigger stack than the default.
    running = true;
    playing = file;
    const SceUID id = sceKernelCreateThread("music", &Player::entry, 0x40, 256 * 1024, 0, 0, nullptr);
    started = false;
    if (id >= 0) {
        Player* self = this;
        if (sceKernelStartThread(id, sizeof(self), &self) >= 0) {
            worker = (unsigned long)id;
            started = true;
        } else {
            sceKernelDeleteThread(id);
        }
    }
    if (!started)
        running = false;
#else
    brls::Logger::info("music: {} (no sound in desktop builds)", file);
#endif
    return true;
}

int Player::entry(unsigned int size, void* arg)
{
    if (size < sizeof(Player*))
        return 0;
    Player* p = *static_cast<Player**>(arg);
    p->run(p->playing);
    return 0;
}

void Player::run(std::string file)
{
#if defined(__PSV__)
    LoopingStream stream;
    if (!stream.open(file)) {
        brls::Logger::error("music: cannot read {}", file);
        return;
    }
    const int grain = 1024; // frames per output call
    const int port = sceAudioOutOpenPort(SCE_AUDIO_OUT_PORT_TYPE_BGM, grain, stream.sampleRate(), SCE_AUDIO_OUT_MODE_STEREO);
    if (port < 0) {
        brls::Logger::error("music: audio port for {} Hz: 0x{:08X}", stream.sampleRate(), (unsigned)port);
        return;
    }
    int volume[2] = { SCE_AUDIO_OUT_MAX_VOL * 7 / 10, SCE_AUDIO_OUT_MAX_VOL * 7 / 10 };
    sceAudioOutSetVolume(port, (SceAudioOutChannelFlag)(SCE_AUDIO_VOLUME_FLAG_L_CH | SCE_AUDIO_VOLUME_FLAG_R_CH), volume);
    // a ring of buffers: one plays while the next is decoded
    const int kBuffers = 3;
    std::vector<int16_t> buffers[kBuffers];
    for (auto& b : buffers)
        b.assign(grain * 2, 0);
    int which = 0;
    while (running) {
        std::vector<int16_t>& b = buffers[which];
        if (stream.read(b.data(), grain) < grain)
            std::memset(b.data(), 0, b.size() * sizeof(int16_t));
        sceAudioOutOutput(port, b.data()); // waits for the previous buffer
        which = (which + 1) % kBuffers;
    }
    sceAudioOutOutput(port, nullptr); // let the last buffer finish
    sceAudioOutReleasePort(port);
#else
    (void)file;
#endif
}

} // namespace music
