#pragma once

#include <mii/mii.h>

#include <string>
#include <vector>

struct MiiDetails;

// A list of Miis the Mii list can show and change: the app's own
// (MiiLibrary, the Switch format) or the 3DS Mii database of Azahar
// (Cfl3dsStore). Indices are in the order the list shows them; with
// fixedPlaces() they are places (as many as capacity()), some empty.
class MiiStore
{
  public:
    virtual ~MiiStore() = default;

    virtual int count() const = 0;
    virtual int capacity() const = 0;
    virtual bool get(int index, mii_charinfo_t* out) const = 0;
    // Inserts or replaces (matched by create id; a new Mii is given its id
    // here). Saved at once. The index, or -1 (full, or not saved).
    virtual int put(mii_charinfo_t* ci) = 0;
    virtual bool remove(int index) = 0;
    // A new Mii put at `index` (the ones from there on move along one; with
    // fixed places, only into an empty one). The index, or -1.
    virtual int putAt(mii_charinfo_t* ci, int index)
    {
        const int i = put(ci);
        if (i >= 0 && index >= 0 && index < i && move(i, index))
            return index;
        return i;
    }
    // Places that stay where they are, empty ones too (the 3DS's 100)
    virtual bool fixedPlaces() const { return false; }
    // The number of cards the list shows
    int places() const { return fixedPlaces() ? capacity() : count(); }
    // Whether Miis can be added and changed here at all (the 3DS's list
    // needs its personal Mii first)
    virtual bool usable() const { return true; }
    // Whether a Mii may be deleted (the 3DS's personal Mii may not)
    virtual bool canRemove(int index) const { (void)index; return true; }
    // Moving Miis around (Triangle in the list)
    virtual bool canMove() const { return false; }
    virtual bool move(int from, int to) { (void)from; (void)to; return false; }
    virtual bool swap(int a, int b) { (void)a; (void)b; return false; }

    virtual MiiDetails details(const mii_create_id_t& id) const = 0;
    virtual void setDetails(const mii_create_id_t& id, const MiiDetails& d) = 0;
    // The pants a whole Mii wears (MII3D_PANTS_*): gray, red for a favorite;
    // on a 3DS also blue (from another console) and gold (special)
    virtual int pants(const mii_charinfo_t& mii) const = 0;

    // The 3DS's list (its personal Mii, places...)
    virtual bool threeDs() const { return false; }
    // The personal Mii (the 3DS's own), shown with a crown
    virtual bool isPersonal(int index) const { (void)index; return false; }

    // Only what this list's format holds (3DS, Wii): snap() makes a Mii
    // that; the editor greys out the rest, look-alikes use only it
    virtual bool limited() const { return false; }
    virtual void snap(mii_charinfo_t* ci) const { (void)ci; }
    // Whether a part or color survives in this list (on the default Mii)
    bool holds(mii_field_t field, uint8_t value) const
    {
        if (!limited() || (unsigned)field >= MII_FIELD_COUNT)
            return true;
        if (holdCache.empty())
            holdCache.assign(MII_FIELD_COUNT * 256, -1);
        int8_t& k = holdCache[field * 256 + value];
        if (k < 0) {
            mii_charinfo_t ci;
            mii_charinfo_init_default(&ci, 0);
            mii_field_set(&ci, field, value);
            k = 0;
            if (mii_field_get(&ci, field) == value) {
                snap(&ci);
                k = mii_field_get(&ci, field) == value ? 1 : 0;
            }
        }
        return k == 1;
    }

  private:
    mutable std::vector<int8_t> holdCache;
};
