#pragma once

#include <mii/mii.h>

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <list>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

struct mii3d_res;
struct mii3d_renderer;

// Renders Mii heads in 3D on a worker thread, using the Wii U face
// resource the user provides (see docs/3D_RENDERING.md):
//
//   <data dir>/FFLResHigh.dat   (preferred)  or  FFLResMiddle.dat
//
// Without it, available() is false and views fall back to the 2D preview.
class HeadRenderer
{
  public:
    using Image = std::shared_ptr<const std::vector<uint8_t>>; // size*size*4 RGBA
    using Callback = std::function<void(Image image, int size)>;

    static HeadRenderer& instance();

    // Looks for the resource file and starts the worker. Safe to call again
    // (e.g. after the user copies the file).
    void init(const std::string& dataDir);
    bool available() const { return res != nullptr; }
    const std::string& status() const { return statusText; }

    // Asks for a render; the callback runs later on the UI thread, first
    // with a quick image and then again with a smoother (supersampled) one.
    // A newer request from the same owner replaces older queued work.
    void request(const void* owner, const mii_charinfo_t& mii, int size, float yaw, Callback cb);
    // Drops queued work for `owner` and guarantees its callbacks never run.
    void forget(const void* owner);

  private:
    HeadRenderer() = default;
    ~HeadRenderer();
    void worker();

    struct Job {
        const void* owner;
        mii_charinfo_t mii;
        int size;
        float yaw;
        int supersample;
        Callback cb;
    };

    std::vector<uint8_t> file;
    mii3d_res* res = nullptr;
    mii3d_renderer* renderer = nullptr;
    std::string statusText;

    std::mutex mutex;
    std::condition_variable wake;
    std::deque<Job> jobs;
    std::thread thread;
    bool stopping = false;

    // Finished images, most recent first (small LRU so revisiting the
    // library does not render everything again).
    std::list<std::pair<uint64_t, Image>> cache;
    // Owners that are alive; only touched on the UI thread.
    std::vector<const void*> live;
};
