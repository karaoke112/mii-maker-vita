#pragma once

#include "mii_library.hpp"
#include "mii_store.hpp"

#include <array>
#include <string>
#include <utility>
#include <vector>

// The Wii Miis: a list of their own (up to 100), kept as the Wii's Mii
// database (ux0:data/mii/wii/RFL_DB.dat, the file the Wii keeps in
// /shared2/menu/FaceLib/), where ports of Wii games read it.
//
// Older versions of this app wrote a copy of the app's own Miis there; the
// first time this version runs, that copy is moved aside
// (RFL_DB.dat.vita-copy.bak) and the Wii list starts empty. Each save goes
// to a new file, is checked, then replaces the old one, kept as .bak. A file
// that does not check out is not changed.
class WiiStore : public MiiStore
{
  public:
    static WiiStore& instance();

    bool load();
    // (there may be no file yet: an empty list, written on the first save)
    bool usable() const override { return ok; }
    const std::string& path() const { return file; }

    int count() const override { return (int)entries.size(); }
    int capacity() const override { return MII_WII_DB_MAX; }
    bool get(int index, mii_charinfo_t* out) const override;
    int put(mii_charinfo_t* ci) override;
    bool remove(int index) override;
    bool canMove() const override { return true; }
    bool move(int from, int to) override;
    bool swap(int a, int b) override;
    MiiDetails details(const mii_create_id_t& id) const override;
    void setDetails(const mii_create_id_t& id, const MiiDetails& d) override;
    int pants(const mii_charinfo_t& mii) const override;
    bool limited() const override { return true; }
    void snap(mii_charinfo_t* ci) const override;

  private:
    WiiStore() = default;
    using Entry = std::array<uint8_t, MII_WII_DATA_SIZE>;
    bool save();    // (deferred during a batch)
    bool writeNow();
    bool writeOut() override;
    int find(const mii_create_id_t& id) const;
    static void idFor(const uint8_t* entry, mii_create_id_t* out);
    bool idTaken(uint32_t miiId) const;

    std::string file;
    bool ok = false;
    std::pair<long long, long long> stamp { -1, -1 }; // the file's size and time when read or written
    std::vector<Entry> entries;
    // each entry's create id (made from its bytes; made again after a change)
    mutable std::vector<mii_create_id_t> ids;
    mutable bool idsStale = true;
    const mii_create_id_t& idAt(int index) const;
};
