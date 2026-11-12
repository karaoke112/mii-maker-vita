#include "head_renderer.hpp"

#include <borealis.hpp>
#include <mii3d/mii3d.h>
#include "gpu_renderer.hpp"
#include <zlib.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

static constexpr size_t kCacheBytes = 40u << 20; // icons of a few tabs + thumbnails
static constexpr int kWorkers = 2;
// bump when icon rendering changes, so old files on the card are not reused
static constexpr uint64_t kIconCacheVersion = 3;

// Render passes. A preview is drawn quickly first, then sharper. The Vita
// gets a half-size first pass and a plain full-size one; PCs can afford a
// 2x-supersampled final pass.
enum Pass { PASS_HALF, PASS_FULL, PASS_SMOOTH };
#ifdef __PSV__
static constexpr int kFirstPreviewPass = PASS_HALF, kLastPreviewPass = PASS_FULL;
#else
static constexpr int kFirstPreviewPass = PASS_FULL, kLastPreviewPass = PASS_SMOOTH;
#endif

static bool isIcon(const HeadRenderer::Spec& s) { return s.icon >= 0 || s.lowPriority; }
static int passSize(const HeadRenderer::Spec& s, int pass) { return pass == PASS_HALF ? std::max(32, s.size / 2) : s.size; }

static uint64_t hashKey(const HeadRenderer::Spec& spec, int pass)
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
    // bump when renders change, so icons cached on the memory card are redone
    const int version = 2;
    mix(&version, sizeof(version));
    uint8_t bytes[MII_CHARINFO_SIZE];
    if (mii_charinfo_encode(&spec.mii, bytes) == MII_OK)
        mix(bytes, sizeof(bytes));
    else
        mix(&spec.mii, sizeof(spec.mii));
    const int v[8] = { spec.size, (int)(spec.yaw * 10.0f), (int)(spec.pitch * 10.0f), (int)(spec.zoom * 100.0f), (int)(spec.centerY * 10.0f),
                       spec.icon, pass, spec.fullBody ? 1 : 0 };
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
    for (std::thread& t : threads)
        if (t.joinable())
            t.join();
    for (mii3d_renderer* r : renderers)
        mii3d_renderer_destroy(r);
    mii3d_res_close(res);
    mii3d_res_close(bodyRes);
}

void HeadRenderer::init(const std::string& dataDir, const std::string& iconCacheDir, const std::string& gpuLog,
    bool useGpu)
{
    if (res)
        return;
    cacheDir = iconCacheDir;
    statusText = "To see your Miis, copy AFLResHigh_2_3.dat (Miitomo) or FFLResHigh.dat (Wii U), and FFLBodyRes.dat, to " +
        dataDir + "/";
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
        for (int i = 0; i < kWorkers; i++) {
            mii3d_renderer* r = nullptr;
            if (mii3d_renderer_create(&r, opened) == MII3D_OK)
                renderers.push_back(r);
        }
        if (renderers.empty()) {
            mii3d_res_close(opened);
            file.clear();
            break;
        }
        res = opened;
        statusText = std::string("Face resource: ") + name;
        brls::Logger::info("Loaded face resource {}", path);
        loadBody(dataDir);
        resourceSalt = kIconCacheVersion * 1000003u + file.size() * 31u + bodyFile.size() + (uint64_t)name[0];
        if (useGpu) {
            GpuRenderer& gpu = GpuRenderer::instance();
            gpuOn = gpu.init(res, bodyRes, gpuLog);
            statusText += gpuOn ? ". Renderer: GPU" : ". Renderer: software (" + gpu.reason() + ")";
        }
        for (mii3d_renderer* r : renderers)
            threads.emplace_back(&HeadRenderer::worker, this, r);
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
    for (mii3d_renderer* r : renderers)
        mii3d_renderer_set_body(r, bodyRes);
    statusText += ", body resource: FFLBodyRes.dat";
    brls::Logger::info("Loaded body resource {}", path);
}

void HeadRenderer::request(const void* owner, const Spec& spec, Callback cb)
{
    if (!res)
        return;
    const uint64_t generation = nextGeneration++;
    live[owner] = generation;
    const bool icon = isIcon(spec);
    if (gpuOn && !icon && (spec.background >> 24) == 0xFFu) {
        gpuPending[owner] = Job { owner, generation, spec, PASS_FULL, cb };
        return;
    }
    const int first = icon ? PASS_FULL : kFirstPreviewPass, last = icon ? PASS_FULL : kLastPreviewPass;
    Image cached;
    int cachedPass = -1;
    {
        std::lock_guard<std::mutex> lock(mutex);
        for (int pass = last; pass >= first && !cached; pass--) {
            const uint64_t key = hashKey(spec, pass);
            for (auto it = cache.begin(); it != cache.end(); ++it) {
                if (it->first == key) {
                    cached = it->second;
                    cachedPass = pass;
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
        if (!cached)
            q.push_back(Job { owner, generation, spec, first, cb });
        else if (cachedPass < last)
            q.push_back(Job { owner, generation, spec, cachedPass + 1, cb });
    }
    if (cached)
        cb(cached, passSize(spec, cachedPass), -1);
    if (!cached || cachedPass < last)
        wake.notify_one();
}

void HeadRenderer::forget(const void* owner)
{
    live.erase(owner);
    gpuPending.erase(owner);
    GpuRenderer::instance().release(owner);
    std::lock_guard<std::mutex> lock(mutex);
    auto mine = [owner](const Job& j) { return j.owner == owner; };
    high.erase(std::remove_if(high.begin(), high.end(), mine), high.end());
    low.erase(std::remove_if(low.begin(), low.end(), mine), low.end());
}

std::string HeadRenderer::timings() const
{
    char buf[96];
    std::snprintf(buf, sizeof(buf), "quick %d ms   full %d ms   icon %d ms", lastUs[0].load() / 1000,
        lastUs[1].load() / 1000, lastUs[2].load() / 1000);
    return buf;
}

void HeadRenderer::pumpGpu()
{
    if (gpuPending.empty())
        return;
    std::map<const void*, Job> work;
    work.swap(gpuPending);
    GpuRenderer& gpu = GpuRenderer::instance();
    for (auto& kv : work) {
        Job& job = kv.second;
        auto it = live.find(job.owner);
        if (it == live.end() || it->second != job.generation)
            continue;
        const Spec& sp = job.spec;
        const brls::Time started = brls::getCPUTimeUsec();
        const int image = gpu.render(job.owner, sp.mii, sp.size, sp.yaw, sp.pitch, sp.zoom, sp.centerY, sp.fullBody && bodyRes,
            sp.background & 0xFFFFFFu);
        lastUs[1] = (int)(brls::getCPUTimeUsec() - started);
        if (image > 0) {
            job.cb(nullptr, (sp.size + 7) & ~7, image);
            continue;
        }
        // GPU trouble: draw this one (and, if the GPU gave up, all later
        // ones) on the CPU
        if (!gpu.ready()) {
            gpuOn = false;
            statusText += " (GPU stopped: " + gpu.reason() + ")";
        }
        {
            std::lock_guard<std::mutex> lock(mutex);
            high.push_back(Job { job.owner, job.generation, sp, kFirstPreviewPass, job.cb });
        }
        wake.notify_one();
    }
}

// Icons on the memory card: zlib-compressed RGBA, named by their key.
bool HeadRenderer::loadIcon(uint64_t key, int size, std::vector<uint8_t>* out)
{
    if (cacheDir.empty())
        return false;
    char name[32];
    std::snprintf(name, sizeof(name), "/%016llx.z", (unsigned long long)key);
    std::vector<uint8_t> packed;
    if (!readFile(cacheDir + name, &packed))
        return false;
    out->resize((size_t)size * size * 4);
    uLongf n = (uLongf)out->size();
    return uncompress(out->data(), &n, packed.data(), (uLong)packed.size()) == Z_OK && n == out->size();
}

void HeadRenderer::saveIcon(uint64_t key, const std::vector<uint8_t>& pixels)
{
    if (cacheDir.empty())
        return;
    std::vector<uint8_t> packed(compressBound((uLong)pixels.size()));
    uLongf n = (uLongf)packed.size();
    if (compress2(packed.data(), &n, pixels.data(), (uLong)pixels.size(), 1) != Z_OK)
        return;
    char name[32];
    std::snprintf(name, sizeof(name), "/%016llx.z", (unsigned long long)key);
    const std::string path = cacheDir + name, tmp = path + ".tmp";
    FILE* f = std::fopen(tmp.c_str(), "wb");
    if (!f)
        return;
    const bool ok = std::fwrite(packed.data(), 1, n, f) == n;
    if (std::fclose(f) == 0 && ok)
        std::rename(tmp.c_str(), path.c_str());
    else
        std::remove(tmp.c_str());
}

void HeadRenderer::worker(mii3d_renderer* renderer)
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
        const bool icon = isIcon(sp);
        const int size = passSize(sp, job.pass);
        auto pixels = std::make_shared<std::vector<uint8_t>>((size_t)size * size * 4);
        const uint64_t diskKey = hashKey(sp, job.pass) ^ (resourceSalt * 0x9E3779B97F4A7C15ull);
        bool ok = icon && loadIcon(diskKey, size, pixels.get());
        const brls::Time started = brls::getCPUTimeUsec();
        if (!ok) {
            mii3d_result_t r;
            if (sp.icon >= 0) {
                r = mii3d_render_part_icon(renderer, &sp.mii, (mii3d_icon_part_t)sp.icon, pixels->data(), size);
            } else {
                mii3d_view_t view;
                mii3d_view_init_default(&view);
                view.yaw_deg = sp.yaw;
                view.pitch_deg = sp.pitch;
                view.zoom = sp.zoom;
                view.center_y = sp.centerY;
                view.full_body = sp.fullBody && bodyRes;
                view.supersample = job.pass == PASS_SMOOTH ? 2 : 1;
                // small images do not need a detailed face texture
                view.face_texture_size = size <= 200 ? 128 : 256;
                r = mii3d_render_head(renderer, &sp.mii, &view, pixels->data(), size, size);
            }
            ok = r == MII3D_OK;
            lastUs[icon ? 2 : (job.pass == PASS_HALF ? 0 : 1)] = (int)(brls::getCPUTimeUsec() - started);
            if (ok && icon)
                saveIcon(diskKey, *pixels);
        }
        if (!ok)
            continue;
        Image image = pixels;
        {
            std::lock_guard<std::mutex> lock(mutex);
            cache.emplace_front(hashKey(sp, job.pass), image);
            cacheBytes += image->size();
            while (cacheBytes > kCacheBytes && cache.size() > 1) {
                cacheBytes -= cache.back().second->size();
                cache.pop_back();
            }
            // a preview's pass is done: queue its next one behind the other
            // previews' current passes, unless a newer request replaced it
            if (!icon && job.pass < kLastPreviewPass) {
                const void* o = job.owner;
                const bool replaced = std::any_of(high.begin(), high.end(), [o](const Job& j) { return j.owner == o; });
                if (!replaced) {
                    high.push_back(Job { job.owner, job.generation, sp, job.pass + 1, job.cb });
                    wake.notify_one();
                }
            }
        }
        const void* owner = job.owner;
        const uint64_t generation = job.generation;
        Callback cb = std::move(job.cb);
        brls::sync([this, owner, generation, cb, image, size] {
            // the view may have gone away, or asked for something newer
            auto it = live.find(owner);
            if (it != live.end() && it->second == generation)
                cb(image, size, -1);
        });
    }
}
