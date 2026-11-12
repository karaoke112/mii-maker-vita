#pragma once

#include <mii/mii.h>

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <list>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

struct mii3d_res;
struct mii3d_renderer;

// Renders Miis in 3D on worker threads, using resource files the user
// provides (see docs/3D_RENDERING.md), in ux0:data/mii-maker-vita/dat/; the
// first face resource found of:
//
//   AFLResHigh_2_3.dat   Miitomo: all 20 Switch glasses styles
//   FFLResHigh.dat       Wii U
//   FFLResMiddle.dat     Wii U, lower resolution
//
// plus, optionally, FFLBodyRes.dat for whole-Mii views.
//
// Without a face resource, available() is false and previews show where to
// put it.
class HeadRenderer
{
  public:
    using Image = std::shared_ptr<const std::vector<uint8_t>>; // size*size*4 RGBA
    // `size` is the image's side, which can be smaller than asked for in a
    // preview's first, quick pass.
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
        // the whole Mii (when FFLBodyRes.dat is installed; the head otherwise)
        bool fullBody = false;
        // icons: queued behind previews, one pass, kept on the memory card
        bool lowPriority = false;
    };

    static HeadRenderer& instance();

    // Looks for the resource files in `datDir` and starts the workers;
    // rendered icons are kept in `cacheDir`.
    void init(const std::string& datDir, const std::string& cacheDir);
    bool available() const { return res != nullptr; }
    bool hasBody() const { return bodyRes != nullptr; }
    const std::string& status() const { return statusText; }

    // Asks for a render; the callback runs later on the UI thread. Previews
    // come first as a quick image and then again sharper. A newer request
    // from the same owner replaces older work: an older image is never
    // delivered after a newer one.
    void request(const void* owner, const Spec& spec, Callback cb);
    // Drops queued work for `owner` and guarantees its callbacks never run.
    void forget(const void* owner);

  private:
    HeadRenderer() = default;
    ~HeadRenderer();
    void worker(mii3d_renderer* renderer);
    void loadBody(const std::string& datDir);
    bool loadIcon(uint64_t key, int size, std::vector<uint8_t>* out);
    void saveIcon(uint64_t key, const std::vector<uint8_t>& pixels);

    struct Job {
        const void* owner;
        uint64_t generation;
        Spec spec;
        int pass;
        Callback cb;
    };

    std::vector<uint8_t> file;
    mii3d_res* res = nullptr;
    std::vector<uint8_t> bodyFile;
    mii3d_res* bodyRes = nullptr;
    std::vector<mii3d_renderer*> renderers; // one per worker
    std::string statusText;
    std::string cacheDir;
    uint64_t resourceSalt = 0; // which resource files the disk cache belongs to

    std::mutex mutex;
    std::condition_variable wake;
    std::deque<Job> high, low; // previews, then grid icons
    std::vector<std::thread> threads;
    bool stopping = false;

    // Finished images, most recent first (LRU, so revisiting a screen does
    // not render everything again).
    std::list<std::pair<uint64_t, Image>> cache;
    size_t cacheBytes = 0;
    // Owners that are alive, with their newest request; only touched on
    // the UI thread.
    std::map<const void*, uint64_t> live;
    uint64_t nextGeneration = 1;
};
