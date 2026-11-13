#pragma once

#include <mii/mii.h>

#include <atomic>
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
struct mii3d_poses;
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
// Drawn icons and thumbnails are kept in one file (icons.cache): records
// appended as they are drawn, indexed in memory, rewritten without the
// old ones when it grows too big.
//
// Without a face resource, available() is false and previews show where to
// put it.
class HeadRenderer
{
  public:
    using Image = std::shared_ptr<const std::vector<uint8_t>>; // size*size*4 RGBA
    // `size` is the image's side, which can be smaller than asked for in a
    // preview's first, quick pass. GPU renders come as a NanoVG image id
    // (`gpuImage` > 0, owned by the renderer; `image` is empty).
    using Callback = std::function<void(Image image, int size, int gpuImage)>;

    // What to draw: a head (icon < 0) framed by zoom/centerY, or a part
    // icon (a mii3d_icon_part_t value).
    struct Spec {
        mii_charinfo_t mii;
        int size = 256;
        float yaw = 0.0f;
        float pitch = 0.0f; // tilt (positive: looking down at the Mii)
        float zoom = 1.0f;
        float centerY = 0.0f;
        int icon = -1;
        // the whole Mii (when FFLBodyRes.dat is installed; the head otherwise)
        bool fullBody = false;
        // icons: queued behind previews, one pass, kept on the memory card
        bool lowPriority = false;
        // one-pass renders (icons, thumbnails): smooth edges (costs 4x)
        bool smooth = false;
        // 0xFFRRGGBB: an opaque backdrop, which lets the GPU draw it (stage
        // previews); 0: transparent, CPU only
        uint32_t background = 0;
        // FFL's expressions (0: the normal face); see expressionCount()
        int expression = 0;
        // whole-Mii views: 0 standing, n the pose of animation n - 1 (see poseCount)
        int pose = 0;
        // the pants (MII3D_PANTS_*): gray, or red for a favorite
        int pants = 0;
    };

    static HeadRenderer& instance();
    // Whether a mouth type shows the lip color (icons of the others are the
    // same in every color). True when unknown.
    // Icons load in order of priority (lower first; the grids use the
    // distance from the cursor), then in the order asked. UI thread.
    void setPriority(const void* owner, int priority);
    bool gpuActive() const { return gpuOn; }
    bool hairHasHat(int type) const { return type >= 0 && type < (int)hatHair.size() && hatHair[type]; }
    bool mouthShowsColor(int type) const { return type < 0 || type >= (int)mouthColor.size() || mouthColor[type]; }

    // Looks for the resource files in `datDir` and starts the workers;
    // drawn icons are kept in the file `cacheFile`.
    void init(const std::string& datDir, const std::string& cacheFile, const std::string& gpuLog, bool useGpu);
    // Main thread, between frames: draws pending GPU previews.
    void pumpGpu();
    bool available() const { return res != nullptr; }
    bool hasBody() const { return bodyRes != nullptr; }
    // How many expressions the installed face resource has: 70 with
    // Miitomo's AFLRes, 19 with an FFLRes, 1 without one.
    int expressionCount() const;
    // The poses from MiiBodyMiddle.bfres (Mario Kart 8's), when installed
    // with FFLBodyRes.dat: how many, and each one's animation name.
    int poseCount() const;
    std::string poseName(int index) const;
    // How high the neck is in a pose, a fraction of standing (1 for pose 0)
    float poseNeck(int gender, int pose) const;
    // Photo Studio: draws `spec` into a width x height picture (RGBA, on
    // `background`, 0xAARRGGBB; alpha 0 for a clear one), smoothed, with a
    // renderer of its own: any thread, takes a moment.
    bool renderPhoto(const Spec& spec, int width, int height, uint32_t background, std::vector<uint8_t>* rgba) const;
    const std::string& status() const { return statusText; }
    // How long the latest renders took, in ms (quick preview pass, full
    // preview pass, icon), for the SELECT timing readout.
    std::string timings() const;

    // Asks for a render; the callback runs later on the UI thread. Previews
    // come first as a quick image and then again sharper. A newer request
    // from the same owner replaces older work: an older image is never
    // delivered after a newer one.
    void request(const void* owner, const Spec& spec, Callback cb);
    // Drops queued work for `owner` and guarantees its callbacks never run.
    // Drops the owner's pending work and results but keeps its GPU image
    // (owners shared by several views: one of them went away).
    void invalidate(const void* owner);
    void forget(const void* owner);
    // Deletes every finished image, in memory and on the memory card.
    // UI thread.
    void clearCache();

  private:
    HeadRenderer() = default;
    ~HeadRenderer();
    void worker(mii3d_renderer* renderer);
    void diskWorker();
    // the cache file: only the disk thread touches it
    void openPack();
    void compactPack();
    void resetPack();
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
    std::vector<uint8_t> posesFile;
    mii3d_poses* poses = nullptr;
    mutable std::map<int, float> neckCache; // UI thread
    void loadPoses(const std::string& dataDir);
    std::vector<mii3d_renderer*> renderers; // one per worker
    std::string statusText;
    std::string cacheDir; // the cache file's path (empty: no cache file)
    struct PackEntry {
        uint64_t offset; // of the data, after the record header
        uint32_t length;
    };
    FILE* pack = nullptr;
    uint64_t packEnd = 0;
    std::map<uint64_t, PackEntry> packIndex;
    bool clearPending = false; // guarded by `mutex`
    uint64_t resourceSalt = 0; // which resource files the disk cache belongs to

    std::mutex mutex;
    std::condition_variable wake;
    std::deque<Job> high, low; // previews, then grid icons
    std::deque<Job> disk;      // icons to look for on the memory card first
    std::deque<Job> gpuIcons;  // icons the GPU draws, between frames
    // drawn icons to write to the memory card (on the disk thread)
    std::deque<std::pair<uint64_t, Image>> saves;
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
    std::atomic<int> lastUs[3] = { { 0 }, { 0 }, { 0 } };
    std::map<const void*, Job> gpuPending; // UI thread only
    std::atomic<bool> gpuOn { false }; // read by the disk thread too
    std::vector<bool> mouthColor, hatHair;
    std::map<const void*, int> priority; // guarded by `mutex`
    std::atomic<int64_t> lastMove { 0 }; // when the cursor last moved (us)
    unsigned frameCount = 0;
    Job takeBest(std::deque<Job>& q); // with `mutex` held
    bool drawsOnGpu(const Spec& s) const;
    uint64_t diskKey(const Spec& s, int pass) const;
    void dropOwner(const void* owner); // with `mutex` held
    void pumpGpuIcons();
    void pumpGpuPreviews();
};
