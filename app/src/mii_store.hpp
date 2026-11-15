#pragma once

#include <mii/mii.h>

#include <string>

struct MiiDetails;

// A list of Miis the Mii list can show and change: the app's own
// (MiiLibrary, the Switch format) or the 3DS Mii database of Azahar
// (Cfl3dsStore). Indices are in the order the list shows them.
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

    // Only what the 3DS format holds (the editor greys out the rest)
    virtual bool threeDs() const { return false; }
};
