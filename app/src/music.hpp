#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

struct stb_vorbis;

// Background music: one looping track at a time, from Ogg Vorbis files the
// user provides in ux0:data/mii-maker-vita/music/ (not included):
//
//   mii_channel.ogg     Mii Channel (Wii)
//   mii_maker_3ds.ogg   Mii Maker (3DS)
//   mii_maker_wiiu.ogg  Mii Maker (Wii U)
//
// The loop is read from the file's LOOPSTART and LOOPLENGTH (or LOOPEND)
// comments, in samples, as tools that convert game music write them; a
// file without them loops whole. Played on the Vita's BGM audio port;
// desktop builds stay silent.
namespace music {

enum class Track { NONE = 0, WII, DS3, WIIU, COUNT };

const char* fileName(Track t);
std::string path(Track t);

// Decodes a track and loops it at its loop points (testable without
// sound: read() gives interleaved stereo samples for ever).
class LoopingStream
{
  public:
    ~LoopingStream();
    bool open(const std::string& path);
    int sampleRate() const { return rate; }
    // Fills `frames` stereo frames; returns how many were written (0 only
    // when the file cannot be read).
    int read(int16_t* out, int frames);
    uint64_t loopStart() const { return start; }
    uint64_t loopEnd() const { return end; }

  private:
    std::vector<unsigned char> data; // the whole file, decoded from memory
    ::stb_vorbis* v = nullptr;
    int rate = 0;
    uint64_t start = 0, end = 0, pos = 0;
};

class Player
{
  public:
    static Player& instance();
    ~Player();
    // Starts `t` (stopping what was playing). False: the file is missing or
    // unreadable (the reason in `error`).
    bool play(Track t, std::string* error = nullptr);
    void stop();
    Track current() const { return track; }

  private:
    void run(std::string file);
    static int entry(unsigned int size, void* arg); // SceKernelThreadEntry
    std::string playing;
    unsigned long worker = 0; // SceUID (Vita) or pthread_t
    bool started = false;
    std::atomic<bool> running { false };
    Track track = Track::NONE;
};

} // namespace music
