#include "cfl3ds_store.hpp"

#include <borealis/core/logger.hpp>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <ctime>
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


// A file's size and time of change: reading the list again is skipped
// while they stay the same (switching to its tab read and checked the
// whole file every time)
static std::pair<long long, long long> stampOf(const std::string& path)
{
    struct stat st;
    if (stat(path.c_str(), &st) != 0)
        return { -1, -1 };
    return { (long long)st.st_size, (long long)st.st_mtime };
}

Cfl3dsStore& Cfl3dsStore::instance()
{
    static Cfl3dsStore s;
    return s;
}

// The database is in the shared extdata F000000B (3dbrew)
bool Cfl3dsStore::load()
{
    const std::string want = std::string(kAzahar) + "/F000000B/user/CFL_DB.dat";
    if (ok && want == file && stampOf(file) == stamp)
        return true; // (as read last time: Azahar has not changed it)
    stamp = { -1, -1 };
    ok = false;
    why.clear();
    used = 0;
    for (int& s : slotAt)
        s = -1;
    file = std::string(kAzahar) + "/F000000B/user/CFL_DB.dat";
    if (!exists(file)) {
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
    mapPlaces();
    ok = true;
    stamp = stampOf(file);
    brls::Logger::info("3DS Mii database {}: {} Miis", file, count());
    return true;
}

// Mii Maker's place for each Mii: its own page and slot (the order in the
// file means nothing to it). The position byte: the page in the low half,
// the place on it in the high, ten to a page.
static int placeOfByte(uint8_t b)
{
    const int page = b & 0xF, slot = b >> 4;
    return page < 10 && slot < 10 ? page * 10 + slot : -1;
}

static uint8_t byteOfPlace(int place) { return (uint8_t)((place / 10) | ((place % 10) << 4)); }

void Cfl3dsStore::mapPlaces()
{
    used = 0;
    for (int& s : slotAt)
        s = -1;
    std::vector<int> homeless; // (a place out of range, or one taken twice: the first free one)
    for (int s = 0; s < MII_CFLDB_MAX_MIIS; s++) {
        if (!mii_cfldb_slot_used(data.data(), s))
            continue;
        used++;
        const int p = placeOfByte(mii_cfldb_slot(data.data(), s)[2]);
        if (p >= 0 && slotAt[p] < 0)
            slotAt[p] = s;
        else
            homeless.push_back(s);
    }
    for (int s : homeless) {
        const int p = firstFree();
        if (p >= 0)
            slotAt[p] = s;
    }
    reid();
}

int Cfl3dsStore::firstFree() const
{
    for (int p = 0; p < MII_CFLDB_MAX_MIIS; p++)
        if (slotAt[p] < 0)
            return p;
    return -1;
}

int Cfl3dsStore::placeOfSlot(int slot) const
{
    for (int p = 0; p < MII_CFLDB_MAX_MIIS; p++)
        if (slotAt[p] == slot)
            return p;
    return -1;
}

void Cfl3dsStore::snap(mii_charinfo_t* ci) const
{
    mii_charinfo_t back;
    uint8_t v3[MII_VER3_STOREDATA_SIZE];
    if (mii_ver3_encode(ci, nullptr, v3) == MII_OK && mii_ver3_decode(&back, nullptr, v3, sizeof(v3), &ci->create_id) == MII_OK)
        *ci = back;
}

static uint32_t rd32be(const uint8_t* p) { return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3]; }

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

void Cfl3dsStore::reid()
{
    for (int s = 0; s < MII_CFLDB_MAX_MIIS; s++)
        if (mii_cfldb_slot_used(data.data(), s))
            idFor(mii_cfldb_slot(data.data(), s), &ids[s]);
}

int Cfl3dsStore::slotOf(const mii_create_id_t& id) const
{
    for (int s = 0; s < MII_CFLDB_MAX_MIIS; s++)
        if (mii_cfldb_slot_used(const_cast<uint8_t*>(data.data()), s) && mii_create_id_equal(&ids[s], &id))
            return s;
    return -1;
}

bool Cfl3dsStore::get(int place, mii_charinfo_t* out) const
{
    if (!ok || place < 0 || place >= MII_CFLDB_MAX_MIIS || slotAt[place] < 0)
        return false;
    const uint8_t* e = mii_cfldb_slot(const_cast<uint8_t*>(data.data()), slotAt[place]);
    return mii_ver3_decode(out, nullptr, e, MII_CFLDB_ENTRY_SIZE, &ids[slotAt[place]]) == MII_OK;
}

bool Cfl3dsStore::info(const mii_create_id_t& id, mii_ver3_info_t* out) const
{
    const int slot = slotOf(id);
    mii_charinfo_t ci;
    return slot >= 0 &&
        mii_ver3_decode(&ci, out, mii_cfldb_slot(const_cast<uint8_t*>(data.data()), slot), MII_CFLDB_ENTRY_SIZE, nullptr) == MII_OK;
}

// A Mii already here is saved again in its place, with all it carries
// besides its face; a new one goes in the place asked for with
// setNextPlace (when free), else the first free.
int Cfl3dsStore::put(mii_charinfo_t* ci)
{
    if (!usable())
        return -1;
    const int slot = slotOf(ci->create_id);
    if (slot < 0) {
        int place = nextPlace >= 0 && nextPlace < MII_CFLDB_MAX_MIIS && slotAt[nextPlace] < 0 ? nextPlace : firstFree();
        nextPlace = -1;
        return place < 0 ? -1 : putNew(ci, place, nullptr);
    }
    mii_ver3_info_t info;
    mii_charinfo_t old;
    mii_ver3_decode(&old, &info, mii_cfldb_slot(data.data(), slot), MII_CFLDB_ENTRY_SIZE, nullptr);
    uint8_t v3[MII_VER3_STOREDATA_SIZE];
    if (mii_ver3_encode(ci, &info, v3) != MII_OK)
        return -1;
    const std::vector<uint8_t> before = data;
    std::memcpy(mii_cfldb_slot(data.data(), slot), v3, MII_CFLDB_ENTRY_SIZE);
    if (!save()) {
        data = before;
        return -1;
    }
    idFor(mii_cfldb_slot(data.data(), slot), &ci->create_id);
    return placeOfSlot(slot);
}

int Cfl3dsStore::putNew(mii_charinfo_t* ci, int place, const mii_ver3_info_t* from)
{
    if (!usable() || place < 0 || place >= MII_CFLDB_MAX_MIIS || slotAt[place] >= 0)
        return -1;
    int slot = -1;
    for (int s = 0; s < MII_CFLDB_MAX_MIIS && slot < 0; s++)
        if (!mii_cfldb_slot_used(data.data(), s))
            slot = s;
    if (slot < 0)
        return -1; /* 100 Miis */
    mii_ver3_info_t info;
    mii_charinfo_t old;
    const std::time_t now = std::time(nullptr);
    const uint32_t since2010 = now > 1262304000 ? (uint32_t)(now - 1262304000) : 0;
    if (from) {
        info = *from;
    } else {
        // a Mii of this console: the personal Mii's system id and MAC (so
        // it is not a "foreign" one), made now, on a 3DS
        mii_ver3_info_init_default(&info);
        info.birth_platform = 3;
        info.copyable = 1;
        mii_ver3_info_t personal;
        mii_ver3_decode(&old, &personal, mii_cfldb_slot(data.data(), 0), MII_CFLDB_ENTRY_SIZE, nullptr);
        std::memcpy(info.author_id, personal.author_id, sizeof(info.author_id));
        std::memcpy(info.creator_mac, personal.creator_mac, sizeof(info.creator_mac));
        info.mii_id = 0x80000000u | 0x10000000u | ((since2010 / 2) & 0x0FFFFFFFu);
    }
    info.page_index = (uint8_t)(place / 10);
    info.slot_index = (uint8_t)(place % 10);
    // a Mii id of its own (the same Mii twice would be one Mii to the 3DS);
    // a special Mii stays special
    for (int tries = 0; tries < 256; tries++) {
        bool clash = false;
        for (int s = 0; s < MII_CFLDB_MAX_MIIS; s++)
            if (mii_cfldb_slot_used(data.data(), s) && rd32be(mii_cfldb_slot(data.data(), s) + 0x0C) == info.mii_id)
                clash = true;
        if (!clash)
            break;
        info.mii_id = (info.mii_id & 0xF0000000u) | ((tries == 0 ? since2010 / 2 : info.mii_id + 1) & 0x0FFFFFFFu);
    }
    uint8_t v3[MII_VER3_STOREDATA_SIZE];
    if (mii_ver3_encode(ci, &info, v3) != MII_OK)
        return -1;
    const std::vector<uint8_t> before = data;
    std::memcpy(mii_cfldb_slot(data.data(), slot), v3, MII_CFLDB_ENTRY_SIZE);
    mii_cfldb_slot(data.data(), slot)[2] = byteOfPlace(place);
    if (!save()) {
        data = before;
        mapPlaces();
        return -1;
    }
    mapPlaces();
    idFor(mii_cfldb_slot(data.data(), slot), &ci->create_id);
    return placeOfSlot(slot);
}

bool Cfl3dsStore::canRemove(int place) const
{
    return usable() && place >= 0 && place < MII_CFLDB_MAX_MIIS && slotAt[place] > 0;
}

// The Miis after it in the file move up one slot (the personal Mii stays
// first); where the others show in Mii Maker does not change.
bool Cfl3dsStore::remove(int place)
{
    if (!canRemove(place))
        return false;
    const std::vector<uint8_t> before = data;
    const int slot = slotAt[place];
    for (int s = slot; s + 1 < MII_CFLDB_MAX_MIIS; s++)
        std::memcpy(mii_cfldb_slot(data.data(), s), mii_cfldb_slot(data.data(), s + 1), MII_CFLDB_ENTRY_SIZE);
    std::memset(mii_cfldb_slot(data.data(), MII_CFLDB_MAX_MIIS - 1), 0, MII_CFLDB_ENTRY_SIZE);
    if (!save()) {
        data = before;
        return false;
    }
    // (the places shown stay: a Mii that was put in a free one when read
    // keeps it)
    for (int& s : slotAt)
        if (s == slot)
            s = -1;
        else if (s > slot)
            s--;
    used--;
    reid(); // (the slots after it moved up)
    return true;
}

// Only the position byte of the Miis changes (moving one changes nothing
// else in it)
bool Cfl3dsStore::move(int from, int to)
{
    if (!usable() || from < 0 || to < 0 || from >= MII_CFLDB_MAX_MIIS || to >= MII_CFLDB_MAX_MIIS || slotAt[from] < 0)
        return false;
    if (from == to)
        return true;
    const std::vector<uint8_t> before = data;
    const int a = slotAt[from], b = slotAt[to];
    mii_cfldb_slot(data.data(), a)[2] = byteOfPlace(to);
    if (b >= 0)
        mii_cfldb_slot(data.data(), b)[2] = byteOfPlace(from);
    if (!save()) {
        data = before;
        return false;
    }
    slotAt[to] = a;
    slotAt[from] = b;
    return true;
}

bool Cfl3dsStore::swap(int a, int b)
{
    if (a >= 0 && a < MII_CFLDB_MAX_MIIS && slotAt[a] < 0)
        return move(b, a);
    return move(a, b);
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
    return deferWrite() || writeNow();
}

// the end of a batch: the changes written at once (if that fails, the list
// as the file has it)
bool Cfl3dsStore::writeOut()
{
    const bool ok = writeNow();
    if (!ok) {
        stamp = { -1, -1 }; // (read it again for sure)
        load();
    }
    return ok;
}

bool Cfl3dsStore::writeNow()
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
    stamp = stampOf(file); // (our own change: no need to read it again)
    return true;
}
