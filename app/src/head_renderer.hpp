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

// Renders Mii heads in 3D on a worker thread, using a face resource the
// user provides (see docs/3D_RENDERING.md), first found of:
//
//   <data dir>/AFLResHigh_2_3.dat   Miitomo: all 20 Switch glasses styles
//   <data dir>/FFLResHigh.dat       Wii U
//   <data dir>/FFLResMiddle.dat     Wii U, lower resolution
//
// Without one, available() is false and previews show where to put it.
class HeadRenderer
{
  public:
    using Image = std::shared_ptr<const std::vector<uint8_t>>; // size*size*4 RGBA
    using Callback = std::function<void(Image image, int size)>;

    // What to draw: a head (icon < 0) framed by zoom/centerY, or a part
    // icon (a mii3d_icon_part_t value).
    struct Spec {
        mii_charinfo_t mii;
        int size = 256;
        float yaw = 0.0f;
        float zoom = 1.0f;
        float centerY = 0.0f;
        int icon = -1;
        // icons: queued behind previews, single quick pass only
        bool lowPriority = false;
    };

    static HeadRenderer& instance();

    // Looks for the resource file and starts the worker. Safe to call again
    // (e.g. after the user copies the file).
    void init(const std::string& dataDir);
    bool available() const { return res != nullptr; }
    const std::string& status() const { return statusText; }

    // Asks for a render; the callback runs later on the UI thread. Heads come
    // first as a quick image and then again smoother (supersampled).
    // A newer request from the same owner replaces older queued work.
    void request(const void* owner, const Spec& spec, Callback cb);
    // Drops queued work for `owner` and guarantees its callbacks never run.
    void forget(const void* owner);

  private:
    HeadRenderer() = default;
    ~HeadRenderer();
    void worker();

    struct Job {
        const void* owner;
        Spec spec;
        int supersample;
        Callback cb;
    };

    std::vector<uint8_t> file;
    mii3d_res* res = nullptr;
    mii3d_renderer* renderer = nullptr;
    std::string statusText;

    std::mutex mutex;
    std::condition_variable wake;
    std::deque<Job> high, low; // previews, then grid icons
    std::thread thread;
    bool stopping = false;

    // Finished images, most recent first (LRU, so revisiting a screen does
    // not render everything again).
    std::list<std::pair<uint64_t, Image>> cache;
    size_t cacheBytes = 0;
    // Owners that are alive; only touched on the UI thread.
    std::vector<const void*> live;
};
