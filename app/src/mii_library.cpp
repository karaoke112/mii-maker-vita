#include "mii_library.hpp"

#include <borealis/core/logger.hpp>

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <random>
#include <sys/stat.h>
#include <vector>

#ifdef __PSV__
#include <psp2/kernel/rng.h>
static const char* kDataDir = "ux0:data/mii";
#else
static const char* kDataDir = "mii-data";
#endif

void platformRandomBytes(uint8_t* out, size_t size)
{
#ifdef __PSV__
    sceKernelGetRandomNumber(out, size);
#else
    std::random_device rd;
    for (size_t i = 0; i < size; i++)
        out[i] = (uint8_t)rd();
#endif
}

static void makeDir(const std::string& path)
{
    if (mkdir(path.c_str(), 0777) != 0 && errno != EEXIST)
        brls::Logger::warning("mkdir {} failed: {}", path, strerror(errno));
}

static bool readFile(const std::string& path, std::vector<uint8_t>* out)
{
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f)
        return false;
    uint8_t buf[4096];
    size_t n;
    out->clear();
    while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0)
        out->insert(out->end(), buf, buf + n);
    std::fclose(f);
    return true;
}

static bool writeFile(const std::string& path, const uint8_t* data, size_t size)
{
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f)
        return false;
    bool ok = std::fwrite(data, 1, size, f) == size;
    ok = (std::fclose(f) == 0) && ok;
    return ok;
}

MiiLibrary& MiiLibrary::instance()
{
    static MiiLibrary lib;
    return lib;
}

MiiLibrary::MiiLibrary()
    : dir(kDataDir)
{
    mii_nfdb_init(&db);
    std::memset(authorId, 0, sizeof(authorId));
}

bool MiiLibrary::load()
{
    makeDir(dir);
    makeDir(dir + "/export");

    // A random per-console author id, used for the StoreData device CRC.
    std::vector<uint8_t> data;
    if (readFile(dir + "/author_id.bin", &data) && data.size() == sizeof(authorId)) {
        std::memcpy(authorId, data.data(), sizeof(authorId));
    } else {
        platformRandomBytes(authorId, sizeof(authorId));
        writeFile(dir + "/author_id.bin", authorId, sizeof(authorId));
    }

    for (const std::string& path : { dbPath(), dbPath() + ".bak" }) {
        if (!readFile(path, &data))
            continue;
        mii_result_t r = mii_nfdb_decode(&db, data.data(), data.size());
        if (r == MII_OK) {
            brls::Logger::info("Loaded {} Miis from {}", db.count, path);
            return true;
        }
        brls::Logger::error("{}: {}", path, mii_result_string(r));
    }
    mii_nfdb_init(&db);
    return false;
}

bool MiiLibrary::save()
{
    static uint8_t buf[MII_NFDB_SIZE];
    if (mii_nfdb_encode(&db, buf) != MII_OK)
        return false;

    // Write a new file, keep the previous one as .bak, then swap in.
    // (sceIoRename does not replace an existing file.)
    const std::string path = dbPath(), tmp = path + ".tmp", bak = path + ".bak";
    if (!writeFile(tmp, buf, sizeof(buf))) {
        brls::Logger::error("Could not write {}", tmp);
        return false;
    }
    std::remove(bak.c_str());
    std::rename(path.c_str(), bak.c_str());
    if (std::rename(tmp.c_str(), path.c_str()) != 0) {
        brls::Logger::error("Could not replace {}", path);
        return false;
    }
    return true;
}

bool MiiLibrary::get(int index, mii_charinfo_t* out) const
{
    return mii_nfdb_get(&db, index, out) == MII_OK;
}

void MiiLibrary::newCreateId(mii_create_id_t* id) const
{
    uint8_t rnd[16];
    platformRandomBytes(rnd, sizeof(rnd));
    mii_create_id_from_random(id, rnd);
}

int MiiLibrary::put(mii_charinfo_t* ci)
{
    if (!mii_create_id_is_valid(&ci->create_id))
        newCreateId(&ci->create_id);
    int index = mii_nfdb_put(&db, ci, authorId);
    if (index < 0) {
        brls::Logger::error("Could not store Mii: {}", mii_result_string((mii_result_t)index));
        return -1;
    }
    return save() ? index : -1;
}

bool MiiLibrary::remove(int index)
{
    return mii_nfdb_remove(&db, index) == MII_OK && save();
}

bool MiiLibrary::exportMii(int index, std::string* outPath)
{
    mii_charinfo_t ci;
    if (!get(index, &ci))
        return false;

    char id[9];
    std::snprintf(id, sizeof(id), "%02x%02x%02x%02x", ci.create_id.bytes[0], ci.create_id.bytes[1],
        ci.create_id.bytes[2], ci.create_id.bytes[3]);
    const std::string base = dir + "/export/" + id;

    uint8_t buf[MII_VER3_STOREDATA_SIZE];
    bool ok = true;
    ok &= mii_charinfo_encode(&ci, buf) == MII_OK && writeFile(base + ".charinfo", buf, MII_CHARINFO_SIZE);
    ok &= mii_storedata_encode(&ci, authorId, buf) == MII_OK && writeFile(base + ".nfsd", buf, MII_STOREDATA_SIZE);
    ok &= mii_ver3_encode(&ci, nullptr, buf) == MII_OK && writeFile(base + ".ffsd", buf, MII_VER3_STOREDATA_SIZE);
    if (outPath)
        *outPath = base;
    return ok;
}
