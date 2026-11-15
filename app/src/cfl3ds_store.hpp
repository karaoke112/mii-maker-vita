#pragma once

#include "mii_library.hpp"
#include "mii_store.hpp"

#include <string>
#include <vector>

// The 3DS's Mii database as Azahar keeps it on the memory card
// (ux0:data/azahar/nand/extdata/00048000/F000000B/user/CFL_DB.dat): the
// Miis of Mii Maker, at most 100.
//
// Written carefully: only Mii Maker's slots and the checksum after them
// change (the StreetPass and invitation sections stay byte for byte); the
// personal Mii (the first slot) stays first and is never deleted; a Mii
// saved unchanged comes back byte for byte; each save goes to a new file,
// is checked, then replaces the old one, which is kept as CFL_DB.dat.bak
// (and the file as it first was, as CFL_DB.dat.orig, never overwritten).
// A file that does not check out (unknown layout, wrong checksum) is not
// changed at all.
class Cfl3dsStore : public MiiStore
{
  public:
    static Cfl3dsStore& instance();

    // Finds and reads the file. False: none, or not one this app can
    // safely change (see error()).
    bool load();
    bool loaded() const { return ok; }
    const std::string& error() const { return why; }
    const std::string& path() const { return file; }

    int count() const override { return (int)order.size(); }
    int capacity() const override { return MII_CFLDB_MAX_MIIS; }
    bool get(int index, mii_charinfo_t* out) const override;
    int put(mii_charinfo_t* ci) override;
    bool remove(int index) override;
    bool canRemove(int index) const override;
    MiiDetails details(const mii_create_id_t& id) const override;
    void setDetails(const mii_create_id_t& id, const MiiDetails& d) override;
    int pants(const mii_charinfo_t& mii) const override;
    bool threeDs() const override { return true; }

  private:
    Cfl3dsStore() = default;
    void sortOrder();
    int freePlace() const;
    bool save();
    int slotOf(const mii_create_id_t& id) const;
    static void idFor(const uint8_t* entry, mii_create_id_t* out);

    std::string file, why;
    bool ok = false;
    std::vector<uint8_t> data;
    std::vector<int> order; // file slots in Mii Maker's order
};
