#include "head_renderer.hpp"

#include <borealis.hpp>
#include <mii3d/mii3d.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

static constexpr size_t kCacheEntries = 128;

static uint64_t hashKey(const mii_charinfo_t& mii, int size, float yaw, int supersample)
{
    // FNV-1a over the Mii and the view parameters
    uint64_t h = 1469598103934665603ull;
    auto mix = [&h](const void* p, size_t n) {
        const uint8_t* b = static_cast<const uint8_t*>(p);
        for (size_t i = 0; i < n; i++) {
            h ^= b[i];
            h *= 1099511628211ull;
        }
    };
    uint8_t bytes[MII_CHARINFO_SIZE];
    if (mii_charinfo_encode(&mii, bytes) == MII_OK)
        mix(bytes, sizeof(bytes));
    else
        mix(&mii, sizeof(mii));
    mix(&size, sizeof(size));
    int y = (int)(yaw * 10.0f);
    mix(&y, sizeof(y));
    mix(&supersample, sizeof(supersample));
    return h;
}

static bool readFile(const std::string& path, std::vector<uint8_t>* out)
{
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f)
        return false;
    std::fseek(f, 0, SEEK_END);
    long n = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    bool ok = n > 0;
    if (ok) {
        out->resize((size_t)n);
        ok = std::fread(out->data(), 1, (size_t)n, f) == (size_t)n;
    }
    std::fclose(f);
    return ok;
}

HeadRenderer& HeadRenderer::instance()
{
    static HeadRenderer r;
    return r;
}

HeadRenderer::~HeadRenderer()
{
    {
        std::lock_guard<std::mutex> lock(mutex);
        stopping = true;
    }
    wake.notify_all();
    if (thread.joinable())
        thread.join();
    mii3d_renderer_destroy(renderer);
    mii3d_res_close(res);
}

void HeadRenderer::init(const std::string& dataDir)
{
    if (res)
        return;
    statusText = "3D: put FFLResHigh.dat (or FFLResMiddle.dat) in " + dataDir + "/";
    for (const char* name : { "FFLResHigh.dat", "FFLResMiddle.dat" }) {
        const std::string path = dataDir + "/" + name;
        if (!readFile(path, &file))
            continue;
        mii3d_res_t* opened = nullptr;
        mii3d_result_t r = mii3d_res_open(&opened, file.data(), file.size());
        if (r != MII3D_OK) {
            brls::Logger::error("{}: {}", path, mii3d_result_string(r));
            statusText = std::string("3D: ") + name + " is not a valid Wii U face resource";
            file.clear();
            continue;
        }
        if (mii3d_renderer_create(&renderer, opened) != MII3D_OK) {
            mii3d_res_close(opened);
            file.clear();
            break;
        }
        res = opened;
        statusText = std::string("3D heads: ") + name;
        brls::Logger::info("Loaded face resource {}", path);
        thread = std::thread(&HeadRenderer::worker, this);
        return;
    }
}

void HeadRenderer::request(const void* owner, const mii_charinfo_t& mii, int size, float yaw, Callback cb)
{
    if (!res)
        return;
    if (std::find(live.begin(), live.end(), owner) == live.end())
        live.push_back(owner);
    Image cached;
    int cachedQuality = 0;
    {
        std::lock_guard<std::mutex> lock(mutex);
        for (int ss = 2; ss >= 1 && !cached; ss--) {
            const uint64_t key = hashKey(mii, size, yaw, ss);
            for (auto it = cache.begin(); it != cache.end(); ++it) {
                if (it->first == key) {
                    cached = it->second;
                    cachedQuality = ss;
                    cache.splice(cache.begin(), cache, it);
                    break;
                }
            }
        }
        // replace this owner's queued work, if any
        jobs.erase(std::remove_if(jobs.begin(), jobs.end(), [owner](const Job& j) { return j.owner == owner; }),
            jobs.end());
        if (cachedQuality == 1)
            jobs.push_back(Job { owner, mii, size, yaw, 2, cb });
        else if (!cached)
            jobs.push_back(Job { owner, mii, size, yaw, 1, cb });
    }
    if (cached)
        cb(cached, size);
    else
        wake.notify_one();
}

void HeadRenderer::forget(const void* owner)
{
    live.erase(std::remove(live.begin(), live.end(), owner), live.end());
    std::lock_guard<std::mutex> lock(mutex);
    jobs.erase(std::remove_if(jobs.begin(), jobs.end(), [owner](const Job& j) { return j.owner == owner; }),
        jobs.end());
}

void HeadRenderer::worker()
{
    for (;;) {
        Job job;
        {
            std::unique_lock<std::mutex> lock(mutex);
            wake.wait(lock, [this] { return stopping || !jobs.empty(); });
            if (stopping)
                return;
            job = std::move(jobs.front());
            jobs.pop_front();
        }
        auto pixels = std::make_shared<std::vector<uint8_t>>((size_t)job.size * job.size * 4);
        mii3d_view_t view;
        mii3d_view_init_default(&view);
        view.yaw_deg = job.yaw;
        view.supersample = job.supersample;
        if (mii3d_render_head(renderer, &job.mii, &view, pixels->data(), job.size, job.size) != MII3D_OK)
            continue;
        Image image = pixels;
        {
            std::lock_guard<std::mutex> lock(mutex);
            cache.emplace_front(hashKey(job.mii, job.size, job.yaw, job.supersample), image);
            if (cache.size() > kCacheEntries)
                cache.pop_back();
            // quick pass done: queue the smooth pass behind everyone's quick pass
            if (job.supersample == 1) {
                const void* o = job.owner;
                const bool replaced = std::any_of(jobs.begin(), jobs.end(), [o](const Job& j) { return j.owner == o; });
                if (!replaced)
                    jobs.push_back(Job { job.owner, job.mii, job.size, job.yaw, 2, job.cb });
            }
        }
        const void* owner = job.owner;
        Callback cb = std::move(job.cb);
        const int size = job.size;
        brls::sync([this, owner, cb, image, size] {
            // the view may have gone away while we rendered
            if (std::find(live.begin(), live.end(), owner) != live.end())
                cb(image, size);
        });
    }
}
