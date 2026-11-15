#include "wii_store.hpp"

#include "paths.hpp"

#include <borealis/core/logger.hpp>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <sys/stat.h>

static bool readAll(const std::string& path, std::vector<uint8_t>* out)
{
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f)
        return false;
    out->clear();
    uint8_t buf[8192];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0)
        out->insert(out->end(), buf, buf + n);
    std::fclose(f);
    return true;
}

static bool writeAll(const std::string& path, const std::vector<uint8_t>& d)
{
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f)
        return false;
    bool good = std::fwrite(d.data(), 1, d.size(), f) == d.size();
    good = (std::fclose(f) == 0) && good;
    return good;
}

static bool exists(const std::string& path)
{
    struct stat st;
    return stat(path.c_str(), &st) == 0;
}

static uint32_t rd32be(const uint8_t* p) { return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3]; }

WiiStore& WiiStore::instance()
{
    static WiiStore s;
    return s;
}

bool WiiStore::load()
{
    ok = false;
    entries.clear();
    file = paths::wiiDir() + "/RFL_DB.dat";
    // Once: the copy of the app's Miis an older version kept here is moved
    // aside; from now on this file is the Wii list's own.
    const std::string marker = paths::appDir() + "/wii_list_own";
    if (!exists(marker)) {
        if (exists(file)) {
            const std::string aside = file + ".vita-copy.bak";
            std::remove(aside.c_str());
            if (std::rename(file.c_str(), aside.c_str()) != 0) {
                brls::Logger::error("Could not move {} aside", file);
                return false;
            }
            brls::Logger::info("The old Wii copy is now {}", aside);
        }
        if (FILE* f = std::fopen(marker.c_str(), "wb")) {
            std::fputs("RFL_DB.dat is the Wii list's own (its old copy of the app's Miis: RFL_DB.dat.vita-copy.bak)\n", f);
            std::fclose(f);
        }
    }
    if (!exists(file)) {
        ok = true; // an empty list
        return true;
    }
    std::vector<uint8_t> data;
    if (!readAll(file, &data) || !mii_wii_db_valid(data.data(), data.size())) {
        brls::Logger::error("{}: not a Wii Mii database this app can change", file);
        return false;
    }
    for (int i = 0; i < MII_WII_DB_MAX; i++) {
        Entry e;
        mii_charinfo_t ci;
        if (!mii_wii_db_get(data.data(), data.size(), i, e.data()))
            continue;
        if (mii_wii_decode(&ci, nullptr, e.data(), e.size()) != MII_OK) {
            brls::Logger::error("{}: Mii {} does not read", file, i);
            entries.clear();
            return false; // (a save would lose it)
        }
        entries.push_back(e);
    }
    ok = true;
    brls::Logger::info("Wii Mii database {}: {} Miis", file, count());
    return true;
}

// A create id for a Wii Mii (the Wii format has none): from its Mii id
// and console id, so it stays the same for the same Mii
void WiiStore::idFor(const uint8_t* entry, mii_create_id_t* out)
{
    uint8_t h[16];
    uint64_t a = 1469598103934665603ull, b = 1099511628211ull ^ 0x2545F491u;
    for (int i = 0x18; i < 0x20; i++) {
        a = (a ^ entry[i]) * 1099511628211ull;
        b = (b ^ entry[i]) * 0x100000001b3ull + 0x9e3779b97f4a7c15ull;
    }
    for (int i = 0; i < 8; i++) {
        h[i] = (uint8_t)(a >> (8 * i));
        h[8 + i] = (uint8_t)(b >> (8 * i));
    }
    mii_create_id_from_random(out, h);
}

int WiiStore::find(const mii_create_id_t& id) const
{
    for (size_t i = 0; i < entries.size(); i++) {
        mii_create_id_t c;
        idFor(entries[i].data(), &c);
        if (mii_create_id_equal(&c, &id))
            return (int)i;
    }
    return -1;
}

bool WiiStore::idTaken(uint32_t miiId) const
{
    for (const Entry& e : entries)
        if (rd32be(e.data() + 0x18) == miiId)
            return true;
    return false;
}

bool WiiStore::get(int index, mii_charinfo_t* out) const
{
    if (!ok || index < 0 || index >= count())
        return false;
    if (mii_wii_decode(out, nullptr, entries[index].data(), MII_WII_DATA_SIZE) != MII_OK)
        return false;
    idFor(entries[index].data(), &out->create_id);
    return true;
}

void WiiStore::snap(mii_charinfo_t* ci) const
{
    uint8_t w[MII_WII_DATA_SIZE];
    mii_charinfo_t back;
    mii_wii_info_t info;
    mii_wii_info_init_default(&info);
    if (mii_wii_encode(ci, &info, w) == MII_OK && mii_wii_decode(&back, nullptr, w, sizeof(w)) == MII_OK) {
        back.create_id = ci->create_id;
        *ci = back;
    }
}

int WiiStore::put(mii_charinfo_t* ci)
{
    if (!ok)
        return -1;
    mii_charinfo_t m = *ci;
    // the Wii takes a Mii with no name for an empty slot
    if (!m.name[0])
        mii_name_set_utf8(&m, "Mii");
    int index = find(m.create_id);
    mii_wii_info_t info;
    mii_charinfo_t old;
    if (index >= 0) {
        // keep everything it carries besides its face
        mii_wii_decode(&old, &info, entries[index].data(), MII_WII_DATA_SIZE);
    } else {
        if (count() >= capacity())
            return -1;
        // a Mii of this console, with a Mii id of its own
        mii_wii_info_init_default(&info);
        std::memcpy(info.system_id, MiiLibrary::instance().author(), sizeof(info.system_id));
        info.mii_id = mii_wii_id_for(&m);
        for (int tries = 0; tries < 256 && idTaken(info.mii_id); tries++)
            info.mii_id = 0x80000000u | ((info.mii_id + 1) & 0x1FFFFFFFu);
    }
    Entry e;
    if (mii_wii_encode(&m, &info, e.data()) != MII_OK)
        return -1;
    const std::vector<Entry> before = entries;
    if (index >= 0) {
        entries[index] = e;
    } else {
        entries.push_back(e);
        index = count() - 1;
    }
    if (!save()) {
        entries = before;
        return -1;
    }
    idFor(entries[index].data(), &ci->create_id);
    return index;
}

bool WiiStore::remove(int index)
{
    if (!ok || index < 0 || index >= count())
        return false;
    const std::vector<Entry> before = entries;
    entries.erase(entries.begin() + index);
    if (!save()) {
        entries = before;
        return false;
    }
    return true;
}

bool WiiStore::move(int from, int to)
{
    if (!ok || from < 0 || to < 0 || from >= count() || to >= count())
        return false;
    if (from == to)
        return true;
    const std::vector<Entry> before = entries;
    Entry e = entries[from];
    entries.erase(entries.begin() + from);
    entries.insert(entries.begin() + to, e);
    if (!save()) {
        entries = before;
        return false;
    }
    return true;
}

bool WiiStore::swap(int a, int b)
{
    if (!ok || a < 0 || b < 0 || a >= count() || b >= count())
        return false;
    std::swap(entries[a], entries[b]);
    if (!save()) {
        std::swap(entries[a], entries[b]);
        return false;
    }
    return true;
}

MiiDetails WiiStore::details(const mii_create_id_t& id) const
{
    MiiDetails d;
    const int i = find(id);
    mii_charinfo_t ci;
    mii_wii_info_t info;
    if (i < 0 || mii_wii_decode(&ci, &info, entries[i].data(), MII_WII_DATA_SIZE) != MII_OK)
        return d;
    d.favorite = info.favorite;
    d.birthMonth = info.birth_month <= 12 ? info.birth_month : 0;
    d.birthDay = info.birth_day <= 31 ? info.birth_day : 0;
    d.mingle = !info.local_only;
    mii_charinfo_t tmp;
    mii_charinfo_init_default(&tmp, 0);
    std::memcpy(tmp.name, info.creator_name, sizeof(tmp.name));
    char name[64];
    mii_name_get_utf8(&tmp, name, sizeof(name));
    d.creator = name;
    return d;
}

// Kept in the Mii itself (the Wii format has room for them)
void WiiStore::setDetails(const mii_create_id_t& id, const MiiDetails& d)
{
    const int i = find(id);
    mii_charinfo_t ci;
    mii_wii_info_t info;
    if (i < 0 || mii_wii_decode(&ci, &info, entries[i].data(), MII_WII_DATA_SIZE) != MII_OK)
        return;
    info.favorite = d.favorite;
    info.birth_month = d.birthMonth;
    info.birth_day = d.birthDay;
    info.local_only = !d.mingle;
    mii_charinfo_t tmp;
    mii_charinfo_init_default(&tmp, 0);
    mii_name_set_utf8(&tmp, d.creator.c_str());
    std::memcpy(info.creator_name, tmp.name, sizeof(info.creator_name));
    Entry e;
    if (mii_wii_encode(&ci, &info, e.data()) != MII_OK || e == entries[i])
        return; /* (unchanged: nothing to write) */
    const Entry was = entries[i];
    entries[i] = e;
    if (!save())
        entries[i] = was;
}

int WiiStore::pants(const mii_charinfo_t& mii) const
{
    return details(mii.create_id).favorite ? 2 /* MII3D_PANTS_RED */ : 0;
}

// A new file, checked, then swapped in; the old one kept as .bak.
// sceIoRename does not replace a file.
bool WiiStore::save()
{
    static uint8_t miis[MII_WII_DB_MAX][MII_WII_DATA_SIZE];
    for (size_t i = 0; i < entries.size(); i++)
        std::memcpy(miis[i], entries[i].data(), MII_WII_DATA_SIZE);
    std::vector<uint8_t> data(MII_WII_DB_SIZE);
    if (mii_wii_db_write(miis, count(), data.data(), data.size()) != MII_OK || !mii_wii_db_valid(data.data(), data.size()))
        return false;
    const std::string tmp = file + ".tmp", bak = file + ".bak";
    std::vector<uint8_t> check;
    if (!writeAll(tmp, data) || !readAll(tmp, &check) || check != data) {
        std::remove(tmp.c_str());
        brls::Logger::error("Could not write {}", tmp);
        return false;
    }
    std::remove(bak.c_str());
    const bool had = exists(file);
    if ((had && std::rename(file.c_str(), bak.c_str()) != 0) || std::rename(tmp.c_str(), file.c_str()) != 0) {
        if (!exists(file) && had)
            std::rename(bak.c_str(), file.c_str());
        brls::Logger::error("Could not replace {}", file);
        return false;
    }
    return true;
}
