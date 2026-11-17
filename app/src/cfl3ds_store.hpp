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

    // a personal Mii to make the others from (its console's ids)
    bool usable() const override { return ok && mii_cfldb_slot_used(const_cast<uint8_t*>(data.data()), 0); }

    // Indices are Mii Maker's 100 places (ten to a page), empty or not
    int count() const override { return used; }
    int capacity() const override { return MII_CFLDB_MAX_MIIS; }
    bool fixedPlaces() const override { return true; }
    bool get(int place, mii_charinfo_t* out) const override;
    int put(mii_charinfo_t* ci) override;
    // a new Mii in an empty place (-1: taken, or not saved); `info`: what a
    // 3DS Mii read from a file carries (its console, birthday...), kept
    int putAt(mii_charinfo_t* ci, int place) override { return putNew(ci, place, nullptr); }
    int putNew(mii_charinfo_t* ci, int place, const mii_ver3_info_t* info);
    bool remove(int place) override;
    bool canRemove(int place) const override;
    bool canMove() const override { return true; }
    // to an empty place, or trading places with the Mii there
    bool move(int from, int to) override;
    bool swap(int a, int b) override;
    // the first empty place, or -1
    int firstFree() const;
    // where a new Mii made with put() goes, when free (else the first free)
    void setNextPlace(int place) { nextPlace = place; }
    // its 3DS data besides the face (for exporting it as it is)
    bool info(const mii_create_id_t& id, mii_ver3_info_t* out) const;
    // what a Mii becomes stored as a 3DS Mii
    bool limited() const override { return true; }
    void snap(mii_charinfo_t* ci) const override;
    bool isPersonal(int place) const override { return ok && place >= 0 && place < MII_CFLDB_MAX_MIIS && slotAt[place] == 0; }
    MiiDetails details(const mii_create_id_t& id) const override;
    void setDetails(const mii_create_id_t& id, const MiiDetails& d) override;
    int pants(const mii_charinfo_t& mii) const override;
    bool threeDs() const override { return true; }

  private:
    Cfl3dsStore() = default;
    void mapPlaces();
    bool save();    // (deferred during a batch)
    bool writeNow();
    bool writeOut() override;
    int slotOf(const mii_create_id_t& id) const;
    int placeOfSlot(int slot) const;
    static void idFor(const uint8_t* entry, mii_create_id_t* out);

    std::string file, why;
    bool ok = false;
    std::vector<uint8_t> data;
    int slotAt[MII_CFLDB_MAX_MIIS]; // the file slot shown in each place, or -1
    int used = 0;
    int nextPlace = -1;
};
