#include "mii_library.hpp"

#include "mii_qr.hpp"
#include "trade.hpp"

#include <borealis/core/logger.hpp>

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <dirent.h>
#include <cstdio>
#include <cstring>
#include <random>
#include <sys/stat.h>
#include <thread>
#include <mutex>
#include <memory>
#include <vector>

#include "paths.hpp"

#ifdef __PSV__
#include <psp2/kernel/rng.h>
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
    : dir(paths::miiDir())
{
    std::memset(authorId, 0, sizeof(authorId));
}

bool MiiLibrary::load()
{
    // A random per-console author id, used for the StoreData device CRC.
    const std::string authorPath = paths::appDir() + "/author_id.bin";
    std::vector<uint8_t> data;
    if (readFile(authorPath, &data) && data.size() == sizeof(authorId)) {
        std::memcpy(authorId, data.data(), sizeof(authorId));
    } else {
        platformRandomBytes(authorId, sizeof(authorId));
        writeFile(authorPath, authorId, sizeof(authorId));
    }

    loadDetails();
    entries.clear();
    std::vector<std::array<uint8_t, MII_STOREDATA_SIZE>> second;
    const bool ok = loadPart(0, &entries);
    if (ok && loadPart(1, &second)) {
        for (auto& e : second) // the second hundred (a Mii in both: the first)
            if (find(*reinterpret_cast<const mii_create_id_t*>(e.data() + 0x30)) < 0 && count() < kMax)
                entries.push_back(e);
    }
    if (ok) {
        // the Wii copy, for game ports, if an older version made none
        struct stat st;
        if (stat((paths::wiiDir() + "/RFL_DB.dat").c_str(), &st) != 0)
            exportWii();
    }
    return ok;
}

bool MiiLibrary::loadPart(int part, std::vector<std::array<uint8_t, MII_STOREDATA_SIZE>>* out)
{
    static mii_nfdb_t db;
    std::vector<uint8_t> data;
    for (const std::string& path : { dbPath(part), dbPath(part) + ".bak" }) {
        if (!readFile(path, &data))
            continue;
        mii_result_t r = mii_nfdb_decode(&db, data.data(), data.size());
        if (r == MII_OK) {
            brls::Logger::info("Loaded {} Miis from {}", db.count, path);
            for (int i = 0; i < db.count; i++) {
                std::array<uint8_t, MII_STOREDATA_SIZE> e;
                std::memcpy(e.data(), db.entries[i], MII_STOREDATA_SIZE);
                out->push_back(e);
            }
            return true;
        }
        brls::Logger::error("{}: {}", path, mii_result_string(r));
    }
    return false;
}

bool MiiLibrary::save()
{
    // the first hundred where other apps read them; the second hundred
    // beside it (removed when there are none)
    bool ok = savePart(0);
    if (count() > MII_NFDB_MAX_MIIS) {
        ok = savePart(1) && ok;
    } else {
        std::remove(dbPath(1).c_str());
        std::remove((dbPath(1) + ".bak").c_str());
    }
    exportWii();
    return ok;
}

bool MiiLibrary::savePart(int part)
{
    static mii_nfdb_t db;
    static uint8_t buf[MII_NFDB_SIZE];
    mii_nfdb_init(&db);
    const int from = part * MII_NFDB_MAX_MIIS, to = std::min(count(), from + MII_NFDB_MAX_MIIS);
    for (int i = from; i < to; i++)
        std::memcpy(db.entries[db.count++], entries[i].data(), MII_STOREDATA_SIZE);
    if (mii_nfdb_encode(&db, buf) != MII_OK)
        return false;

    // Write a new file, keep the previous one as .bak, then swap in.
    // (sceIoRename does not replace an existing file.)
    const std::string path = dbPath(part), tmp = path + ".tmp", bak = path + ".bak";
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

// ---- details ---------------------------------------------------------------
// details.txt: one Mii a line, tab-separated: the create id in hex,
// favorite (0/1), birth month, birth day, mingle (0/1), the creator's name
// (UTF-8). Miis with nothing set have no line.

std::string MiiLibrary::idKey(const mii_create_id_t& id)
{
    static const char* hex = "0123456789abcdef";
    std::string s;
    for (uint8_t b : id.bytes) {
        s += hex[b >> 4];
        s += hex[b & 15];
    }
    return s;
}

void MiiLibrary::loadDetails()
{
    detailMap.clear();
    std::vector<uint8_t> data;
    if (!readFile(dir + "/details.txt", &data))
        return;
    std::string text(data.begin(), data.end());
    size_t pos = 0;
    while (pos < text.size()) {
        size_t end = text.find('\n', pos);
        if (end == std::string::npos)
            end = text.size();
        const std::string line = text.substr(pos, end - pos);
        pos = end + 1;
        std::vector<std::string> f;
        size_t a = 0;
        for (;;) {
            const size_t t = line.find('\t', a);
            f.push_back(line.substr(a, t == std::string::npos ? std::string::npos : t - a));
            if (t == std::string::npos)
                break;
            a = t + 1;
        }
        if (f.size() < 5 || f[0].size() != 32)
            continue;
        MiiDetails d;
        d.favorite = f[1] == "1";
        d.birthMonth = (uint8_t)std::min(12, std::max(0, std::atoi(f[2].c_str())));
        d.birthDay = (uint8_t)std::min(31, std::max(0, std::atoi(f[3].c_str())));
        d.mingle = f[4] != "0";
        if (f.size() > 5)
            d.creator = f[5].substr(0, 40);
        detailMap[f[0]] = d;
    }
}

void MiiLibrary::saveDetails() const
{
    std::string text;
    for (const auto& kv : detailMap) {
        const MiiDetails& d = kv.second;
        text += kv.first + "\t" + (d.favorite ? "1" : "0") + "\t" + std::to_string(d.birthMonth) + "\t" +
            std::to_string(d.birthDay) + "\t" + (d.mingle ? "1" : "0") + "\t" + d.creator + "\n";
    }
    const std::string path = dir + "/details.txt", tmp = path + ".tmp";
    if (writeFile(tmp, (const uint8_t*)text.data(), text.size())) {
        std::remove(path.c_str());
        std::rename(tmp.c_str(), path.c_str());
    }
}

MiiDetails MiiLibrary::details(const mii_create_id_t& id) const
{
    auto it = detailMap.find(idKey(id));
    return it == detailMap.end() ? MiiDetails() : it->second;
}

void MiiLibrary::setDetails(const mii_create_id_t& id, const MiiDetails& d)
{
    MiiDetails clean = d;
    for (char& c : clean.creator)
        if (c == '\t' || c == '\n' || c == '\r')
            c = ' ';
    if (clean.isDefault())
        detailMap.erase(idKey(id));
    else
        detailMap[idKey(id)] = clean;
    saveDetails();
    exportWii(); // the Wii database carries them
}

// from a 3DS / Wii U file or QR code
void MiiLibrary::detailsFromVer3(const mii_create_id_t& id, const mii_ver3_info_t& info)
{
    MiiDetails d;
    d.favorite = info.favorite != 0;
    d.birthMonth = info.birth_month <= 12 ? info.birth_month : 0;
    d.birthDay = info.birth_day <= 31 ? info.birth_day : 0;
    d.mingle = info.local_only == 0;
    mii_charinfo_t tmp;
    mii_charinfo_init_default(&tmp, 0);
    std::memcpy(tmp.name, info.creator_name, sizeof(tmp.name));
    char name[64];
    mii_name_get_utf8(&tmp, name, sizeof(name));
    d.creator = name;
    if (d.isDefault())
        detailMap.erase(idKey(id));
    else
        detailMap[idKey(id)] = d;
}

int MiiLibrary::find(const mii_create_id_t& id) const
{
    for (int i = 0; i < count(); i++)
        if (std::memcmp(entries[i].data() + 0x30, id.bytes, 16) == 0)
            return i;
    return -1;
}

int MiiLibrary::putEntry(const mii_charinfo_t& ci)
{
    std::array<uint8_t, MII_STOREDATA_SIZE> sd;
    if (!mii_create_id_is_valid(&ci.create_id) || mii_storedata_encode(&ci, authorId, sd.data()) != MII_OK)
        return -1;
    int index = find(ci.create_id);
    if (index < 0) {
        if (count() >= kMax)
            return -1;
        index = count();
        entries.push_back(sd);
    } else {
        entries[index] = sd;
    }
    return index;
}

// The first 100 Miis as a Wii Mii database, where Wii game ports can read
// them (ux0:data/mii/wii/RFL_DB.dat, the Wii's /shared2/menu/FaceLib/).
// Parts the Wii lacks become its default ones there.
bool MiiLibrary::exportWii() const
{
    static uint8_t miis[MII_WII_DB_MAX][MII_WII_DATA_SIZE];
    mii_wii_info_t info;
    mii_wii_info_init_default(&info);
    std::memcpy(info.system_id, authorId, sizeof(info.system_id));
    int n = 0;
    for (int i = 0; i < count() && n < MII_WII_DB_MAX; i++) {
        mii_charinfo_t ci;
        if (!get(i, &ci))
            continue;
        // the details the Wii keeps: favorite, birthday, mingle, creator
        const MiiDetails d = details(ci.create_id);
        info.favorite = d.favorite;
        info.birth_month = d.birthMonth;
        info.birth_day = d.birthDay;
        info.local_only = !d.mingle;
        std::memset(info.creator_name, 0, sizeof(info.creator_name));
        if (!d.creator.empty()) {
            mii_charinfo_t tmp;
            mii_charinfo_init_default(&tmp, 0);
            mii_name_set_utf8(&tmp, d.creator.c_str());
            std::memcpy(info.creator_name, tmp.name, sizeof(info.creator_name));
        }
        if (mii_wii_encode(&ci, &info, miis[n]) == MII_OK)
            n++;
    }
    auto data = std::make_shared<std::vector<uint8_t>>(MII_WII_DB_SIZE);
    if (mii_wii_db_write(miis, n, data->data(), data->size()) != MII_OK)
        return false;
    // Written on its own thread: the file is 768 KB, and saving (moving a
    // Mii, say) should not wait for the memory card. One write at a time,
    // the newest last.
    static std::mutex writing;
    std::thread([data] {
        std::lock_guard<std::mutex> lock(writing);
        const std::string path = paths::wiiDir() + "/RFL_DB.dat", tmp = path + ".tmp";
        if (!writeFile(tmp, data->data(), data->size())) {
            brls::Logger::error("Could not write {}", tmp);
            return;
        }
        std::remove(path.c_str());
        std::rename(tmp.c_str(), path.c_str());
    }).detach();
    return true;
}

bool MiiLibrary::get(int index, mii_charinfo_t* out) const
{
    if (index < 0 || index >= count())
        return false;
    // the device CRC is not checked: a database copied from another
    // console has a different author id
    return mii_storedata_decode(out, entries[index].data(), MII_STOREDATA_SIZE, nullptr) == MII_OK;
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
    int index = putEntry(*ci);
    if (index < 0) {
        brls::Logger::error("Could not store Mii (invalid, or the library is full)");
        return -1;
    }
    return save() ? index : -1;
}

bool MiiLibrary::remove(int index)
{
    if (index < 0 || index >= count())
        return false;
    mii_charinfo_t ci;
    if (get(index, &ci) && detailMap.erase(idKey(ci.create_id)))
        saveDetails();
    entries.erase(entries.begin() + index);
    return save();
}

bool MiiLibrary::move(int from, int to)
{
    if (from < 0 || from >= count() || to < 0 || to >= count())
        return false;
    auto e = entries[from];
    entries.erase(entries.begin() + from);
    entries.insert(entries.begin() + to, e);
    return save();
}

bool MiiLibrary::swap(int a, int b)
{
    if (a == b)
        return true;
    if (a > b)
        std::swap(a, b);
    if (a < 0 || b >= count())
        return false;
    std::swap(entries[a], entries[b]);
    return save();
}

void MiiLibrary::ver3Info(const mii_charinfo_t& mii, mii_ver3_info_t* out) const
{
    mii_ver3_info_init_default(out);
    out->copyable = 1;
    out->birth_platform = 3;
    // creation time (2 s units since 2010), derived from the create id so
    // the same Mii keeps the same id: somewhere in 2020-2024
    uint32_t h = 2166136261u;
    for (uint8_t b : mii.create_id.bytes)
        h = (h ^ b) * 16777619u;
    out->mii_id = 0x80000000u /* normal */ | 0x10000000u /* valid */ | (0x09600000u + h % 0x04B00000u);
    std::memcpy(out->author_id, authorId, 8);
    std::memcpy(out->creator_mac, authorId + 8, 6);
    out->creator_mac[0] &= 0xFE; // an ordinary (unicast) address
    // the details: favorite, birthday, mingle, and the creator (else, as a
    // 3DS shows it, this console's user name)
    const MiiDetails d = details(mii.create_id);
    out->favorite = d.favorite;
    out->birth_month = d.birthMonth;
    out->birth_day = d.birthDay;
    out->local_only = !d.mingle;
    mii_charinfo_t tmp;
    mii_charinfo_init_default(&tmp, 0);
    mii_name_set_utf8(&tmp, d.creator.empty() ? trade::userName().c_str() : d.creator.c_str());
    std::memcpy(out->creator_name, tmp.name, sizeof(out->creator_name));
}

std::string MiiLibrary::fileName(const mii_charinfo_t& ci)
{
    char id[9];
    std::snprintf(id, sizeof(id), "%02x%02x%02x%02x", ci.create_id.bytes[0], ci.create_id.bytes[1],
        ci.create_id.bytes[2], ci.create_id.bytes[3]);
    // <name>_<id>: characters a memory card cannot hold become "_"
    char name[64];
    mii_name_get_utf8(&ci, name, sizeof(name));
    std::string safe;
    for (const char* p = name; *p; p++) {
        const unsigned char ch = (unsigned char)*p;
        safe += (ch < 0x20 || std::strchr("\\/:*?\"<>|", ch)) ? '_' : (char)ch;
    }
    while (!safe.empty() && (safe.back() == ' ' || safe.back() == '.'))
        safe.pop_back();
    return (safe.empty() ? std::string("Mii") : safe) + "_" + id;
}

bool MiiLibrary::exportMii(int index, std::string* outPath)
{
    mii_charinfo_t ci;
    if (!get(index, &ci))
        return false;

    const std::string base = dir + "/export/" + fileName(ci);

    uint8_t buf[MII_VER3_STOREDATA_SIZE];
    bool ok = true;
    ok &= mii_charinfo_encode(&ci, buf) == MII_OK && writeFile(base + ".charinfo", buf, MII_CHARINFO_SIZE);
    ok &= mii_storedata_encode(&ci, authorId, buf) == MII_OK && writeFile(base + ".nfsd", buf, MII_STOREDATA_SIZE);
    mii_ver3_info_t info;
    ver3Info(ci, &info);
    ok &= mii_ver3_encode(&ci, &info, buf) == MII_OK && writeFile(base + ".ffsd", buf, MII_VER3_STOREDATA_SIZE);
    if (outPath)
        *outPath = base;
    return ok;
}

static std::string lowerExtension(const std::string& name)
{
    const size_t dot = name.rfind('.');
    std::string ext = dot == std::string::npos ? "" : name.substr(dot + 1);
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return (char)std::tolower(c); });
    return ext;
}

MiiLibrary::ImportResult MiiLibrary::importFolder()
{
    ImportResult result;
    const std::string in = importDir(), done = in + "/imported";
    mkdir(done.c_str(), 0777);
    DIR* d = opendir(in.c_str());
    if (!d)
        return result;
    std::vector<std::string> names;
    while (struct dirent* e = readdir(d))
        names.push_back(e->d_name);
    closedir(d);
    std::sort(names.begin(), names.end());

    bool changed = false;
    for (const std::string& name : names) {
        const std::string ext = lowerExtension(name);
        const bool image = ext == "png" || ext == "jpg" || ext == "jpeg" || ext == "bmp";
        if (ext != "charinfo" && ext != "nfsd" && ext != "ffsd" && ext != "cfsd" && !image)
            continue;
        std::vector<uint8_t> data;
        mii_charinfo_t ci;
        mii_ver3_info_t info;
        bool hasInfo = false;
        mii_result_t r = MII_ERR_SIZE;
        if (image) {
            // a picture of a Mii QR code (3DS, Wii U, Switch 2, this app)
            if (qr::scanImageFile(in + "/" + name, &ci))
                r = MII_OK;
        } else if (readFile(in + "/" + name, &data)) {
            if (ext == "charinfo")
                r = mii_charinfo_decode(&ci, data.data(), data.size());
            else if (ext == "nfsd")
                r = mii_storedata_decode(&ci, data.data(), data.size(), nullptr);
            else {
                r = mii_ver3_decode(&ci, &info, data.data(), data.size(), nullptr);
                hasInfo = r == MII_OK;
            }
        }
        if (r != MII_OK || mii_verify(&ci, 0) != MII_FIELD_COUNT) {
            brls::Logger::warning("import {}: not a valid Mii file", name);
            result.failed++;
            continue;
        }
        if (!mii_create_id_is_valid(&ci.create_id))
            newCreateId(&ci.create_id); // Ver3 files carry no create id
        if (putEntry(ci) < 0) {
            result.full = true;
            break;
        }
        if (hasInfo) // its favorite, birthday, creator...
            detailsFromVer3(ci.create_id, info);
        changed = true;
        result.imported++;
        const std::string from = in + "/" + name, to = done + "/" + name;
        std::remove(to.c_str());
        std::rename(from.c_str(), to.c_str());
    }
    if (changed)
        saveDetails();
    if (changed && !save())
        brls::Logger::error("import: could not save the database");
    return result;
}
