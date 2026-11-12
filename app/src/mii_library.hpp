#pragma once

#include <mii/mii.h>

#include <string>

// The user's Miis, persisted as an official Switch Mii database (NFDB) in a
// location shared with other homebrew:
//
//   Vita:    ux0:data/mii/database.nfdb   (+ .bak, export/)
//   Desktop: ./data/mii/database.nfdb
// (the author id lives in the app's own folder; see paths.hpp)
//
// See docs/SHARED_DATABASE.md for the contract other apps and game ports
// can rely on.
class MiiLibrary
{
  public:
    static MiiLibrary& instance();

    bool load();
    bool save();

    int count() const { return db.count; }
    bool get(int index, mii_charinfo_t* out) const;
    // Inserts or replaces (matched by create id). Assigns a create id first
    // if the Mii has none. Saves to disk. Returns the index or -1.
    int put(mii_charinfo_t* ci);
    bool remove(int index);
    // Writes .charinfo, .nfsd and .ffsd copies to <data>/export/.
    bool exportMii(int index, std::string* outPath);

    void newCreateId(mii_create_id_t* id) const;
    const std::string& dataDir() const { return dir; }

  private:
    MiiLibrary();
    std::string dbPath() const { return dir + "/database.nfdb"; }

    std::string dir;
    mii_nfdb_t db;
    uint8_t authorId[16];
};

// Fills `out` with cryptographically random bytes (sceKernelGetRandomNumber
// on Vita, std::random_device elsewhere).
void platformRandomBytes(uint8_t* out, size_t size);
