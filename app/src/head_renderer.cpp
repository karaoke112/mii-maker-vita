#include "head_renderer.hpp"

#include <dirent.h>
#include <sys/stat.h>

#include <borealis.hpp>
#include <mii3d/mii3d.h>
#include "gpu_renderer.hpp"
#include <zlib.h>

#include <algorithm>
#include <climits>
#include <cstdint>
#include <cstdio>
#include <cstring>

static constexpr size_t kCacheBytes = 40u << 20; // icons of a few tabs + thumbnails
static constexpr int kWorkers = 2;
// bump when icon rendering changes, so old files on the card are not reused
static constexpr uint64_t kIconCacheVersion = 7;

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
    const int v[10] = { spec.size, (int)(spec.yaw * 10.0f), (int)(spec.pitch * 10.0f), (int)(spec.zoom * 100.0f), (int)(spec.centerY * 10.0f),
                        spec.icon, pass, spec.fullBody ? 1 : 0, spec.smooth ? 1 : 0, spec.expression };
    mix(v, sizeof(v));
    return h;
}

int HeadRenderer::expressionCount() const { return res ? mii3d_expression_count(res) : 1; }

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

void HeadRenderer::init(const std::string& dataDir, const std::string& iconCacheFile, const std::string& gpuLog,
    bool useGpu)
{
    if (res)
        return;
    cacheDir = iconCacheFile;
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
        // before the workers start: they own the renderers afterwards
        for (int type = 0; type <= mii_field_max(MII_FIELD_MOUTH_TYPE); type++)
            mouthColor.push_back(mii3d_part_shows_color(renderers[0], MII3D_ICON_MOUTH, type) != 0);
        for (int type = 0; type <= mii_field_max(MII_FIELD_HAIR_TYPE); type++)
            hatHair.push_back(mii3d_hair_has_hat(renderers[0], type) != 0);
        for (mii3d_renderer* r : renderers)
            threads.emplace_back(&HeadRenderer::worker, this, r);
        // icons already on the memory card come from their own thread, so
        // they never wait behind renders
        threads.emplace_back(&HeadRenderer::diskWorker, this);
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
        dropOwner(owner);
        std::deque<Job>& q = icon ? (!cacheDir.empty() ? disk : drawsOnGpu(spec) ? gpuIcons : low) : high;
        if (!cached)
            q.push_back(Job { owner, generation, spec, first, cb });
        else if (cachedPass < last)
            q.push_back(Job { owner, generation, spec, cachedPass + 1, cb });
    }
    if (cached)
        cb(cached, passSize(spec, cachedPass), -1);
    if (!cached || cachedPass < last)
        wake.notify_all();
}

void HeadRenderer::setPriority(const void* owner, int p)
{
    lastMove = brls::getCPUTimeUsec(); // the cursor moved (see pumpGpuIcons)
    std::lock_guard<std::mutex> lock(mutex);
    priority[owner] = p;
}

HeadRenderer::Job HeadRenderer::takeBest(std::deque<Job>& q)
{
    size_t best = 0;
    int bestP = INT32_MAX;
    for (size_t i = 0; i < q.size(); i++) {
        auto it = priority.find(q[i].owner);
        const int p = it == priority.end() ? INT32_MAX - 1 : it->second;
        if (p < bestP) {
            bestP = p;
            best = i;
        }
    }
    Job job = std::move(q[best]);
    q.erase(q.begin() + (long)best);
    return job;
}

void HeadRenderer::dropOwner(const void* owner)
{
    auto mine = [owner](const Job& j) { return j.owner == owner; };
    for (std::deque<Job>* q : { &high, &low, &disk, &gpuIcons })
        q->erase(std::remove_if(q->begin(), q->end(), mine), q->end());
}

void HeadRenderer::invalidate(const void* owner)
{
    live[owner] = nextGeneration++;
    gpuPending.erase(owner);
    std::lock_guard<std::mutex> lock(mutex);
    dropOwner(owner);
}

void HeadRenderer::forget(const void* owner)
{
    live.erase(owner);
    gpuPending.erase(owner);
    GpuRenderer::instance().release(owner);
    std::lock_guard<std::mutex> lock(mutex);
    dropOwner(owner);
    priority.erase(owner);
}

void HeadRenderer::clearCache()
{
    std::lock_guard<std::mutex> lock(mutex);
    cache.clear();
    cacheBytes = 0;
    saves.clear();
    // the disk thread empties the file (it is the only one using it)
    clearPending = true;
    wake.notify_all();
}

// Icons (heads, flat parts, thumbnails) go to the GPU when it is on.
bool HeadRenderer::drawsOnGpu(const Spec& s) const
{
    return gpuOn && isIcon(s) && (s.background >> 24) == 0;
}

// The name of an icon's file on the memory card: GPU and CPU drawings
// differ slightly, so they are kept apart.
uint64_t HeadRenderer::diskKey(const Spec& s, int pass) const
{
    return hashKey(s, pass) ^ (resourceSalt * 0x9E3779B97F4A7C15ull) ^ (drawsOnGpu(s) ? 0x6A09E667F3BCC909ull : 0);
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
    // previews first: they are what the user is looking at
    if (!gpuPending.empty())
        pumpGpuPreviews();
    pumpGpuIcons();
}

void HeadRenderer::pumpGpuPreviews()
{
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
            sp.background & 0xFFFFFFu, sp.expression);
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
        wake.notify_all();
    }
}

// Icons for the GPU: as many as fit in a few milliseconds per frame (at
// least one), nearest the cursor first. While the cursor moves, only every
// third frame draws one, leaving the frames time to scroll smoothly.
void HeadRenderer::pumpGpuIcons()
{
    const brls::Time start = brls::getCPUTimeUsec();
    if (start - lastMove < 200000 && (++frameCount % 3) != 0)
        return;
    const brls::Time budget = start - lastMove < 200000 ? 0 : 4000;
    GpuRenderer& gpu = GpuRenderer::instance();
    for (;;) {
        Job job;
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (gpuIcons.empty())
                return;
            if (!gpuOn) {
                // the GPU gave up: the CPU workers take the rest
                for (Job& j : gpuIcons)
                    low.push_back(std::move(j));
                gpuIcons.clear();
                wake.notify_all();
                return;
            }
            job = takeBest(gpuIcons);
        }
        auto it = live.find(job.owner);
        if (it == live.end() || it->second != job.generation)
            continue;
        const Spec& sp = job.spec;
        const int size = passSize(sp, job.pass);
        auto pixels = std::make_shared<std::vector<uint8_t>>((size_t)size * size * 4);
        const brls::Time began = brls::getCPUTimeUsec();
        const bool drawn = sp.icon >= 0
            ? gpu.renderPartIcon(sp.mii, sp.icon, size, pixels->data())
            : gpu.renderIcon(sp.mii, size, sp.yaw, sp.pitch, sp.zoom, sp.centerY, sp.fullBody && bodyRes, pixels->data());
        if (!drawn) {
            if (!gpu.ready()) {
                gpuOn = false;
                statusText += " (GPU stopped: " + gpu.reason() + ")";
            }
            // this one on the CPU
            std::lock_guard<std::mutex> lock(mutex);
            low.push_back(std::move(job));
            wake.notify_all();
            continue;
        }
        lastUs[2] = (int)(brls::getCPUTimeUsec() - began);
        Image image = pixels;
        {
            std::lock_guard<std::mutex> lock(mutex);
            cache.emplace_front(hashKey(sp, job.pass), image);
            cacheBytes += image->size();
            while (cacheBytes > kCacheBytes && cache.size() > 1) {
                cacheBytes -= cache.back().second->size();
                cache.pop_back();
            }
            if (!cacheDir.empty()) {
                saves.emplace_back(diskKey(sp, job.pass), image);
                wake.notify_all();
            }
        }
        job.cb(image, size, -1);
        if (brls::getCPUTimeUsec() - start >= budget)
            return;
    }
}

// The cache file: a header (magic, format, the renderer/resource salt),
// then records of { magic, length, key } and zlib-compressed RGBA. Records
// are only appended; the newest one of a key counts. A record cut short
// (the app was closed mid-write) ends the file.
static constexpr uint32_t kPackMagic = 0x4356494Du;   // "MIVC"
static constexpr uint32_t kRecordMagic = 0x4E434349u; // "ICCN"
static constexpr uint32_t kPackFormat = 1;
static constexpr uint64_t kPackLimit = 24u << 20; // rewritten when bigger
static constexpr size_t kHeaderBytes = 16, kRecordHeaderBytes = 16;

static void put32(uint8_t* p, uint32_t v) { for (int i = 0; i < 4; i++) p[i] = (uint8_t)(v >> (8 * i)); }
static void put64(uint8_t* p, uint64_t v) { for (int i = 0; i < 8; i++) p[i] = (uint8_t)(v >> (8 * i)); }
static uint32_t get32(const uint8_t* p) { uint32_t v = 0; for (int i = 3; i >= 0; i--) v = v << 8 | p[i]; return v; }
static uint64_t get64(const uint8_t* p) { uint64_t v = 0; for (int i = 7; i >= 0; i--) v = v << 8 | p[i]; return v; }

void HeadRenderer::resetPack()
{
    if (pack)
        std::fclose(pack);
    packIndex.clear();
    pack = std::fopen(cacheDir.c_str(), "w+b");
    if (!pack) {
        brls::Logger::warning("icon cache: cannot create {}", cacheDir);
        packEnd = 0;
        return;
    }
    uint8_t h[kHeaderBytes];
    put32(h, kPackMagic);
    put32(h + 4, kPackFormat);
    put64(h + 8, resourceSalt);
    std::fwrite(h, 1, sizeof(h), pack);
    std::fflush(pack);
    packEnd = kHeaderBytes;
}

void HeadRenderer::openPack()
{
    if (cacheDir.empty())
        return;
    pack = std::fopen(cacheDir.c_str(), "r+b");
    uint8_t h[kHeaderBytes];
    if (!pack || std::fread(h, 1, sizeof(h), pack) != sizeof(h) || get32(h) != kPackMagic ||
        get32(h + 4) != kPackFormat || get64(h + 8) != resourceSalt) {
        // missing, or from another version or other resource files
        resetPack();
        return;
    }
    std::fseek(pack, 0, SEEK_END);
    const uint64_t size = (uint64_t)std::ftell(pack);
    uint64_t pos = kHeaderBytes;
    while (pos + kRecordHeaderBytes <= size) {
        uint8_t r[kRecordHeaderBytes];
        std::fseek(pack, (long)pos, SEEK_SET);
        if (std::fread(r, 1, sizeof(r), pack) != sizeof(r) || get32(r) != kRecordMagic)
            break;
        const uint32_t length = get32(r + 4);
        if (pos + kRecordHeaderBytes + length > size)
            break; // cut short
        packIndex[get64(r + 8)] = PackEntry { pos + kRecordHeaderBytes, length };
        pos += kRecordHeaderBytes + length;
    }
    packEnd = pos; // anything after a broken record is written over
    brls::Logger::info("icon cache: {} icons, {} KB", packIndex.size(), packEnd / 1024);
    if (packEnd > kPackLimit)
        compactPack();
}

// Rewrites the file with the newest records only, keeping the most recently
// drawn half of the size limit.
void HeadRenderer::compactPack()
{
    std::vector<std::pair<uint64_t, PackEntry>> live(packIndex.begin(), packIndex.end());
    std::sort(live.begin(), live.end(), [](const auto& a, const auto& b) { return a.second.offset > b.second.offset; });
    std::vector<std::pair<uint64_t, std::vector<uint8_t>>> keep;
    uint64_t total = 0;
    for (const auto& kv : live) {
        if (total + kv.second.length + kRecordHeaderBytes > kPackLimit / 2)
            break;
        std::vector<uint8_t> data(kv.second.length);
        std::fseek(pack, (long)kv.second.offset, SEEK_SET);
        if (std::fread(data.data(), 1, data.size(), pack) != data.size())
            continue;
        total += kv.second.length + kRecordHeaderBytes;
        keep.emplace_back(kv.first, std::move(data));
    }
    resetPack();
    if (!pack)
        return;
    // oldest first, so the order (and what goes next time) stays the same
    for (auto it = keep.rbegin(); it != keep.rend(); ++it) {
        uint8_t r[kRecordHeaderBytes];
        put32(r, kRecordMagic);
        put32(r + 4, (uint32_t)it->second.size());
        put64(r + 8, it->first);
        std::fwrite(r, 1, sizeof(r), pack);
        std::fwrite(it->second.data(), 1, it->second.size(), pack);
        packIndex[it->first] = PackEntry { packEnd + kRecordHeaderBytes, (uint32_t)it->second.size() };
        packEnd += kRecordHeaderBytes + it->second.size();
    }
    std::fflush(pack);
    brls::Logger::info("icon cache: compacted to {} icons, {} KB", packIndex.size(), packEnd / 1024);
}

void HeadRenderer::diskWorker()
{
    openPack();
    for (;;) {
        Job job;
        std::pair<uint64_t, Image> save;
        bool clear = false;
        {
            std::unique_lock<std::mutex> lock(mutex);
            wake.wait(lock, [this] { return stopping || !disk.empty() || !saves.empty() || clearPending; });
            if (stopping) {
                if (pack)
                    std::fclose(pack);
                pack = nullptr;
                return;
            }
            clear = clearPending;
            clearPending = false;
        }
        if (clear) {
            resetPack();
            brls::Logger::info("icon cache: cleared");
            continue;
        }
        {
            std::unique_lock<std::mutex> lock(mutex);
            if (disk.empty() && saves.empty())
                continue;
            // loading what is on screen comes before saving
            if (disk.empty()) {
                save = std::move(saves.front());
                saves.pop_front();
            } else {
                job = takeBest(disk);
            }
        }
        if (save.second) {
            saveIcon(save.first, *save.second);
            continue;
        }
        const int size = passSize(job.spec, job.pass);
        auto pixels = std::make_shared<std::vector<uint8_t>>();
        const uint64_t key = hashKey(job.spec, job.pass);
        if (!loadIcon(diskKey(job.spec, job.pass), size, pixels.get())) {
            // not drawn yet: render it
            std::lock_guard<std::mutex> lock(mutex);
            (drawsOnGpu(job.spec) ? gpuIcons : low).push_back(std::move(job));
            wake.notify_all();
            continue;
        }
        Image image = pixels;
        {
            std::lock_guard<std::mutex> lock(mutex);
            cache.emplace_front(key, image);
            cacheBytes += image->size();
            while (cacheBytes > kCacheBytes && cache.size() > 1) {
                cacheBytes -= cache.back().second->size();
                cache.pop_back();
            }
        }
        const void* owner = job.owner;
        const uint64_t generation = job.generation;
        Callback cb = std::move(job.cb);
        brls::sync([this, owner, generation, cb, image, size] {
            auto it = live.find(owner);
            if (it != live.end() && it->second == generation)
                cb(image, size, -1);
        });
    }
}

bool HeadRenderer::loadIcon(uint64_t key, int size, std::vector<uint8_t>* out)
{
    if (!pack)
        return false;
    auto it = packIndex.find(key);
    if (it == packIndex.end())
        return false;
    std::vector<uint8_t> packed(it->second.length);
    std::fseek(pack, (long)it->second.offset, SEEK_SET);
    if (std::fread(packed.data(), 1, packed.size(), pack) != packed.size())
        return false;
    out->resize((size_t)size * size * 4);
    uLongf n = (uLongf)out->size();
    return uncompress(out->data(), &n, packed.data(), (uLong)packed.size()) == Z_OK && n == out->size();
}

void HeadRenderer::saveIcon(uint64_t key, const std::vector<uint8_t>& pixels)
{
    if (!pack)
        return;
    std::vector<uint8_t> packed(compressBound((uLong)pixels.size()));
    uLongf n = (uLongf)packed.size();
    if (compress2(packed.data(), &n, pixels.data(), (uLong)pixels.size(), 1) != Z_OK)
        return;
    if (packEnd + kRecordHeaderBytes + n > kPackLimit) {
        compactPack();
        if (!pack)
            return;
    }
    uint8_t r[kRecordHeaderBytes];
    put32(r, kRecordMagic);
    put32(r + 4, (uint32_t)n);
    put64(r + 8, key);
    std::fseek(pack, (long)packEnd, SEEK_SET);
    const bool ok = std::fwrite(r, 1, sizeof(r), pack) == sizeof(r) && std::fwrite(packed.data(), 1, n, pack) == n;
    std::fflush(pack);
    if (!ok)
        return; // the card is full: the record is not indexed (and is written over next time)
    packIndex[key] = PackEntry { packEnd + kRecordHeaderBytes, (uint32_t)n };
    packEnd += kRecordHeaderBytes + n;
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
            if (&q == &low) {
                job = takeBest(low);
            } else {
                job = std::move(q.front());
                q.pop_front();
            }
        }
        const Spec& sp = job.spec;
        const bool icon = isIcon(sp);
        const int size = passSize(sp, job.pass);
        auto pixels = std::make_shared<std::vector<uint8_t>>((size_t)size * size * 4);
        const uint64_t fileKey = diskKey(sp, job.pass);
        bool ok = false; // icons on the memory card were looked for already
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
                view.expression = sp.expression;
                // thumbnails are drawn once (then cached): smooth them
                view.supersample = job.pass == PASS_SMOOTH || sp.smooth ? 2 : 1;
                // small images do not need a detailed face texture
                view.face_texture_size = size <= 200 ? 128 : 256;
                r = mii3d_render_head(renderer, &sp.mii, &view, pixels->data(), size, size);
            }
            ok = r == MII3D_OK;
            lastUs[icon ? 2 : (job.pass == PASS_HALF ? 0 : 1)] = (int)(brls::getCPUTimeUsec() - started);
            if (ok && icon) {
                // written by the disk thread, the only one using the file
                std::lock_guard<std::mutex> lock(mutex);
                saves.emplace_back(fileKey, pixels);
                wake.notify_all();
            }
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
                    wake.notify_all();
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
