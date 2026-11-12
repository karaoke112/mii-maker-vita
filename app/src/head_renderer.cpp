#include "head_renderer.hpp"

#include <borealis.hpp>
#include <mii3d/mii3d.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

static constexpr size_t kCacheBytes = 40u << 20; // icons of a few tabs + thumbnails

static uint64_t hashKey(const HeadRenderer::Spec& spec, int supersample)
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
    if (mii_charinfo_encode(&spec.mii, bytes) == MII_OK)
        mix(bytes, sizeof(bytes));
    else
        mix(&spec.mii, sizeof(spec.mii));
    const int v[7] = { spec.size, (int)(spec.yaw * 10.0f), (int)(spec.zoom * 100.0f), (int)(spec.centerY * 10.0f),
                       spec.icon, supersample, spec.fullBody ? 1 : 0 };
    mix(v, sizeof(v));
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
    mii3d_res_close(bodyRes);
}

void HeadRenderer::init(const std::string& dataDir)
{
    if (res)
        return;
    statusText = "To see your Miis, copy FFLResHigh.dat (Wii U) or AFLResHigh_2_3.dat (Miitomo) to " + dataDir + "/";
    for (const char* name : { "AFLResHigh_2_3.dat", "FFLResHigh.dat", "FFLResMiddle.dat" }) {
        const std::string path = dataDir + "/" + name;
        if (!readFile(path, &file))
            continue;
        mii3d_res_t* opened = nullptr;
        mii3d_result_t r = mii3d_res_open(&opened, file.data(), file.size());
        if (r != MII3D_OK) {
            brls::Logger::error("{}: {}", path, mii3d_result_string(r));
            statusText = std::string(name) + " is not a face resource this app can read";
            file.clear();
            continue;
        }
        if (mii3d_renderer_create(&renderer, opened) != MII3D_OK) {
            mii3d_res_close(opened);
            file.clear();
            break;
        }
        res = opened;
        statusText = std::string("Face resource: ") + name;
        brls::Logger::info("Loaded face resource {}", path);
        loadBody(dataDir);
        thread = std::thread(&HeadRenderer::worker, this);
        return;
    }
}

// The body resource is optional: without it, whole-Mii views show the head.
void HeadRenderer::loadBody(const std::string& dataDir)
{
    const std::string path = dataDir + "/FFLBodyRes.dat";
    if (!readFile(path, &bodyFile))
        return;
    mii3d_res_t* opened = nullptr;
    mii3d_result_t r = mii3d_res_open_body(&opened, bodyFile.data(), bodyFile.size());
    if (r != MII3D_OK) {
        brls::Logger::error("{}: {}", path, mii3d_result_string(r));
        statusText += " (FFLBodyRes.dat is not a body resource this app can read)";
        bodyFile.clear();
        return;
    }
    bodyRes = opened;
    mii3d_renderer_set_body(renderer, bodyRes);
    statusText += ", body resource: FFLBodyRes.dat";
    brls::Logger::info("Loaded body resource {}", path);
}

void HeadRenderer::request(const void* owner, const Spec& spec, Callback cb)
{
    if (!res)
        return;
    if (std::find(live.begin(), live.end(), owner) == live.end())
        live.push_back(owner);
    const bool icon = spec.icon >= 0 || spec.lowPriority;
    Image cached;
    int cachedQuality = 0;
    {
        std::lock_guard<std::mutex> lock(mutex);
        for (int ss = icon ? 1 : 2; ss >= 1 && !cached; ss--) {
            const uint64_t key = hashKey(spec, ss);
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
        auto mine = [owner](const Job& j) { return j.owner == owner; };
        high.erase(std::remove_if(high.begin(), high.end(), mine), high.end());
        low.erase(std::remove_if(low.begin(), low.end(), mine), low.end());
        std::deque<Job>& q = icon ? low : high;
        if (cachedQuality == 1 && !icon)
            q.push_back(Job { owner, spec, 2, cb });
        else if (!cached)
            q.push_back(Job { owner, spec, 1, cb });
    }
    if (cached)
        cb(cached, spec.size);
    else
        wake.notify_one();
}

void HeadRenderer::forget(const void* owner)
{
    live.erase(std::remove(live.begin(), live.end(), owner), live.end());
    std::lock_guard<std::mutex> lock(mutex);
    auto mine = [owner](const Job& j) { return j.owner == owner; };
    high.erase(std::remove_if(high.begin(), high.end(), mine), high.end());
    low.erase(std::remove_if(low.begin(), low.end(), mine), low.end());
}

void HeadRenderer::worker()
{
    for (;;) {
        Job job;
        {
            std::unique_lock<std::mutex> lock(mutex);
            wake.wait(lock, [this] { return stopping || !high.empty() || !low.empty(); });
            if (stopping)
                return;
            std::deque<Job>& q = high.empty() ? low : high;
            job = std::move(q.front());
            q.pop_front();
        }
        const Spec& sp = job.spec;
        auto pixels = std::make_shared<std::vector<uint8_t>>((size_t)sp.size * sp.size * 4);
        mii3d_result_t r;
        if (sp.icon >= 0) {
            r = mii3d_render_part_icon(renderer, &sp.mii, (mii3d_icon_part_t)sp.icon, pixels->data(), sp.size);
        } else {
            mii3d_view_t view;
            mii3d_view_init_default(&view);
            view.yaw_deg = sp.yaw;
            view.zoom = sp.zoom;
            view.center_y = sp.centerY;
            view.full_body = sp.fullBody && bodyRes;
            view.supersample = job.supersample;
            r = mii3d_render_head(renderer, &sp.mii, &view, pixels->data(), sp.size, sp.size);
        }
        if (r != MII3D_OK)
            continue;
        Image image = pixels;
        {
            std::lock_guard<std::mutex> lock(mutex);
            cache.emplace_front(hashKey(sp, job.supersample), image);
            cacheBytes += image->size();
            while (cacheBytes > kCacheBytes && cache.size() > 1) {
                cacheBytes -= cache.back().second->size();
                cache.pop_back();
            }
            // a preview's quick pass is done: queue its smooth pass behind
            // the other previews' quick passes
            if (job.supersample == 1 && sp.icon < 0 && !sp.lowPriority) {
                const void* o = job.owner;
                const bool replaced = std::any_of(high.begin(), high.end(), [o](const Job& j) { return j.owner == o; });
                if (!replaced)
                    high.push_back(Job { job.owner, sp, 2, job.cb });
            }
        }
        const void* owner = job.owner;
        Callback cb = std::move(job.cb);
        const int size = sp.size;
        brls::sync([this, owner, cb, image, size] {
            // the view may have gone away while we rendered
            if (std::find(live.begin(), live.end(), owner) != live.end())
                cb(image, size);
        });
    }
}
