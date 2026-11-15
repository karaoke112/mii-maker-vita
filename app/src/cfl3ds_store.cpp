#include "cfl3ds_store.hpp"

#include <borealis/core/logger.hpp>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <dirent.h>
#include <sys/stat.h>

#ifdef __PSV__
static const char* kAzahar = "ux0:data/azahar/nand/extdata/00048000";
#else
static const char* kAzahar = "data/azahar/nand/extdata/00048000";
#endif

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

Cfl3dsStore& Cfl3dsStore::instance()
{
    static Cfl3dsStore s;
    return s;
}

// The database is in the shared extdata F000000B (3dbrew); any folder there
// holding user/CFL_DB.dat is taken, that one first.
bool Cfl3dsStore::load()
{
    ok = false;
    why.clear();
    file.clear();
    std::vector<std::string> candidates = { std::string(kAzahar) + "/F000000B/user/CFL_DB.dat",
                                            std::string(kAzahar) + "/f000000b/user/CFL_DB.dat" };
    if (DIR* d = opendir(kAzahar)) {
        while (struct dirent* e = readdir(d))
            if (e->d_name[0] != '.')
                candidates.push_back(std::string(kAzahar) + "/" + e->d_name + "/user/CFL_DB.dat");
        closedir(d);
    }
    for (const std::string& c : candidates)
        if (exists(c)) {
            file = c;
            break;
        }
    if (file.empty()) {
        why = "missing";
        return false;
    }
    if (!readAll(file, &data)) {
        why = "unreadable";
        return false;
    }
    const mii_result_t r = mii_cfldb_check(data.data(), data.size());
    if (r != MII_OK) {
        brls::Logger::error("{}: {}", file, mii_result_string(r));
        why = "invalid";
        return false;
    }
    // every Mii in it must read (else a save could lose one)
    for (int s = 0; s < MII_CFLDB_MAX_MIIS; s++) {
        mii_charinfo_t ci;
        if (mii_cfldb_slot_used(data.data(), s) &&
            mii_ver3_decode(&ci, nullptr, mii_cfldb_slot(data.data(), s), MII_CFLDB_ENTRY_SIZE, nullptr) != MII_OK) {
            why = "invalid";
            return false;
        }
    }
    sortOrder();
    ok = true;
    brls::Logger::info("3DS Mii database {}: {} Miis", file, count());
    return true;
}

// Mii Maker's order: each Mii's own page and slot (the order in the file
// means nothing to it)
void Cfl3dsStore::sortOrder()
{
    order.clear();
    for (int s = 0; s < MII_CFLDB_MAX_MIIS; s++)
        if (mii_cfldb_slot_used(data.data(), s))
            order.push_back(s);
    auto pos = [this](int s) {
        const uint8_t b = mii_cfldb_slot(data.data(), s)[2];
        return (b & 0xF) * 16 + (b >> 4); /* page, then the slot on it */
    };
    std::stable_sort(order.begin(), order.end(), [&](int a, int b) { return pos(a) < pos(b); });
}

static uint32_t rd32be(const uint8_t* p) { return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3]; }

// The first place free in Mii Maker's grid (ten to a page), as its
// position byte: the page in the low half, the place on it in the high
int Cfl3dsStore::freePlace() const
{
    for (int i = 0; i < MII_CFLDB_MAX_MIIS; i++) {
        const uint8_t want = (uint8_t)((i / 10) | ((i % 10) << 4));
        bool used = false;
        for (int s : order)
            if (mii_cfldb_slot(const_cast<uint8_t*>(data.data()), s)[2] == want)
                used = true;
        if (!used)
            return want;
    }
    return 0;
}

// A create id for a 3DS Mii (the 3DS format has none): made from its system
// id, Mii id and creator MAC, so it stays the same for the same Mii
void Cfl3dsStore::idFor(const uint8_t* entry, mii_create_id_t* out)
{
    uint8_t h[16];
    uint64_t a = 1469598103934665603ull, b = 1099511628211ull ^ 0x5bd1e995u;
    for (int i = 0x04; i < 0x16; i++) {
        a = (a ^ entry[i]) * 1099511628211ull;
        b = (b ^ entry[i]) * 0x100000001b3ull + 0x9e3779b97f4a7c15ull;
    }
    for (int i = 0; i < 8; i++) {
        h[i] = (uint8_t)(a >> (8 * i));
        h[8 + i] = (uint8_t)(b >> (8 * i));
    }
    mii_create_id_from_random(out, h);
}

int Cfl3dsStore::slotOf(const mii_create_id_t& id) const
{
    for (int s : order) {
        mii_create_id_t c;
        idFor(mii_cfldb_slot(const_cast<uint8_t*>(data.data()), s), &c);
        if (mii_create_id_equal(&c, &id))
            return s;
    }
    return -1;
}

bool Cfl3dsStore::get(int index, mii_charinfo_t* out) const
{
    if (!ok || index < 0 || index >= count())
        return false;
    const uint8_t* e = mii_cfldb_slot(const_cast<uint8_t*>(data.data()), order[index]);
    mii_create_id_t id;
    idFor(e, &id);
    return mii_ver3_decode(out, nullptr, e, MII_CFLDB_ENTRY_SIZE, &id) == MII_OK;
}

int Cfl3dsStore::put(mii_charinfo_t* ci)
{
    if (!ok)
        return -1;
    int slot = slotOf(ci->create_id);
    mii_ver3_info_t info;
    mii_charinfo_t old;
    const bool isNew = slot < 0;
    if (!isNew) {
        // keep everything the Mii carries besides its face
        mii_ver3_decode(&old, &info, mii_cfldb_slot(data.data(), slot), MII_CFLDB_ENTRY_SIZE, nullptr);
    } else {
        for (int s = 0; s < MII_CFLDB_MAX_MIIS && slot < 0; s++)
            if (!mii_cfldb_slot_used(data.data(), s))
                slot = s;
        if (slot < 0)
            return -1; /* 100 Miis */
        // a Mii of this console: the personal Mii's system id and MAC (so
        // it is not a "foreign" one), made now, on a 3DS
        mii_ver3_info_init_default(&info);
        info.birth_platform = 3;
        info.copyable = 1;
        if (mii_cfldb_slot_used(data.data(), 0)) {
            mii_ver3_info_t personal;
            mii_ver3_decode(&old, &personal, mii_cfldb_slot(data.data(), 0), MII_CFLDB_ENTRY_SIZE, nullptr);
            std::memcpy(info.author_id, personal.author_id, sizeof(info.author_id));
            std::memcpy(info.creator_mac, personal.creator_mac, sizeof(info.creator_mac));
        }
        const std::time_t now = std::time(nullptr);
        const uint32_t since2010 = now > 1262304000 ? (uint32_t)(now - 1262304000) : 0;
        info.mii_id = 0x80000000u | 0x10000000u | ((since2010 / 2) & 0x0FFFFFFFu);
        const int place = freePlace();
        info.page_index = (uint8_t)(place & 0xF);
        info.slot_index = (uint8_t)(place >> 4);
        // a fresh id if one with the same time exists
        for (int tries = 0; tries < 64; tries++) {
            bool clash = false;
            for (int s : order)
                if (rd32be(mii_cfldb_slot(data.data(), s) + 0x0C) == info.mii_id)
                    clash = true;
            if (!clash)
                break;
            info.mii_id = 0x90000000u | ((info.mii_id + 1) & 0x0FFFFFFFu);
        }
    }
    uint8_t v3[MII_VER3_STOREDATA_SIZE];
    if (mii_ver3_encode(ci, &info, v3) != MII_OK)
        return -1;
    const std::vector<uint8_t> before = data;
    std::memcpy(mii_cfldb_slot(data.data(), slot), v3, MII_CFLDB_ENTRY_SIZE);
    if (isNew)
        sortOrder();
    if (!save()) {
        data = before;
        sortOrder();
        return -1;
    }
    idFor(mii_cfldb_slot(data.data(), slot), &ci->create_id);
    return (int)(std::find(order.begin(), order.end(), slot) - order.begin());
}

bool Cfl3dsStore::canRemove(int index) const
{
    return ok && index >= 0 && index < count() && order[index] != 0;
}

// The Miis after it in the file move up one slot (the personal Mii stays
// first); where the others show in Mii Maker does not change.
bool Cfl3dsStore::remove(int index)
{
    if (!canRemove(index))
        return false;
    const std::vector<uint8_t> before = data;
    const int slot = order[index];
    for (int s = slot; s + 1 < MII_CFLDB_MAX_MIIS; s++)
        std::memcpy(mii_cfldb_slot(data.data(), s), mii_cfldb_slot(data.data(), s + 1), MII_CFLDB_ENTRY_SIZE);
    std::memset(mii_cfldb_slot(data.data(), MII_CFLDB_MAX_MIIS - 1), 0, MII_CFLDB_ENTRY_SIZE);
    sortOrder();
    if (!save()) {
        data = before;
        sortOrder();
        return false;
    }
    return true;
}

MiiDetails Cfl3dsStore::details(const mii_create_id_t& id) const
{
    MiiDetails d;
    const int slot = slotOf(id);
    if (slot < 0)
        return d;
    mii_charinfo_t ci;
    mii_ver3_info_t info;
    if (mii_ver3_decode(&ci, &info, mii_cfldb_slot(const_cast<uint8_t*>(data.data()), slot), MII_CFLDB_ENTRY_SIZE,
            nullptr) != MII_OK)
        return d;
    d.favorite = info.favorite;
    d.birthMonth = info.birth_month;
    d.birthDay = info.birth_day;
    d.mingle = !info.local_only;
    mii_charinfo_t tmp;
    mii_charinfo_init_default(&tmp, 0);
    std::memcpy(tmp.name, info.creator_name, sizeof(tmp.name));
    char name[64];
    mii_name_get_utf8(&tmp, name, sizeof(name));
    d.creator = name;
    return d;
}

// Kept in the Mii itself (the 3DS format has room for them)
void Cfl3dsStore::setDetails(const mii_create_id_t& id, const MiiDetails& d)
{
    const int slot = slotOf(id);
    if (slot < 0)
        return;
    uint8_t* e = mii_cfldb_slot(data.data(), slot);
    mii_charinfo_t ci;
    mii_ver3_info_t info;
    if (mii_ver3_decode(&ci, &info, e, MII_CFLDB_ENTRY_SIZE, nullptr) != MII_OK)
        return;
    info.favorite = d.favorite;
    info.birth_month = d.birthMonth;
    info.birth_day = d.birthDay;
    info.local_only = !d.mingle;
    mii_charinfo_t tmp;
    mii_charinfo_init_default(&tmp, 0);
    mii_name_set_utf8(&tmp, d.creator.c_str());
    std::memcpy(info.creator_name, tmp.name, sizeof(info.creator_name));
    uint8_t v3[MII_VER3_STOREDATA_SIZE];
    if (mii_ver3_encode(&ci, &info, v3) != MII_OK || std::memcmp(v3, e, MII_CFLDB_ENTRY_SIZE) == 0)
        return; /* (unchanged: nothing to write) */
    const std::vector<uint8_t> before = data;
    std::memcpy(e, v3, MII_CFLDB_ENTRY_SIZE);
    if (!save())
        data = before;
}

// Gold for a special Mii, blue for one from another console, red for a
// favorite (as a 3DS shows them)
int Cfl3dsStore::pants(const mii_charinfo_t& mii) const
{
    const int slot = slotOf(mii.create_id);
    if (slot < 0)
        return 0;
    const uint8_t* e = mii_cfldb_slot(const_cast<uint8_t*>(data.data()), slot);
    if (!(e[0x0C] & 0x80))
        return 3; /* MII3D_PANTS_GOLD */
    const uint8_t* personal = mii_cfldb_slot(const_cast<uint8_t*>(data.data()), 0);
    if (mii_cfldb_slot_used(data.data(), 0) && std::memcmp(e + 0x04, personal + 0x04, 8) != 0)
        return 1; /* MII3D_PANTS_BLUE */
    return (e[0x19] & 0x40) ? 2 /* MII3D_PANTS_RED */ : 0;
}

// A new file, checked, then swapped in; the old one kept as .bak (and the
// very first as .orig). sceIoRename does not replace a file.
bool Cfl3dsStore::save()
{
    mii_cfldb_fix_checksum(data.data());
    if (mii_cfldb_check(data.data(), data.size()) != MII_OK)
        return false;
    const std::string tmp = file + ".tmp", bak = file + ".bak", orig = file + ".orig";
    if (!exists(orig)) {
        std::vector<uint8_t> first;
        if (!readAll(file, &first) || !writeAll(orig, first))
            return false;
    }
    std::vector<uint8_t> check;
    if (!writeAll(tmp, data) || !readAll(tmp, &check) || check != data) {
        std::remove(tmp.c_str());
        brls::Logger::error("Could not write {}", tmp);
        return false;
    }
    std::remove(bak.c_str());
    if (std::rename(file.c_str(), bak.c_str()) != 0 || std::rename(tmp.c_str(), file.c_str()) != 0) {
        // put the old one back where it was
        if (!exists(file))
            std::rename(bak.c_str(), file.c_str());
        brls::Logger::error("Could not replace {}", file);
        return false;
    }
    return true;
}
