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
    // Writes .charinfo, .nfsd and .ffsd copies to <data>/export/, named
    // <nickname>_<first 4 bytes of the create id in hex>.
    bool exportMii(int index, std::string* outPath);

    struct ImportResult {
        int imported = 0; // added or updated
        int failed = 0;   // unreadable or invalid files
        bool full = false; // stopped: the database holds 100 Miis
    };
    // <nickname>_<first 4 bytes of the create id in hex>, safe as a file name
    static std::string fileName(const mii_charinfo_t& ci);
    // QR code pictures made by "Create QR Code"
    std::string qrDir() const { return dir + "/qr"; }
    // Adds every Mii file in <data>/import/ (.charinfo, .nfsd, .ffsd, and
    // pictures of Mii QR codes: .png, .jpg, .bmp) to the
    // database, then moves the file to <data>/import/imported/. A Mii that is
    // already in the database (same create id) is updated, not duplicated.
    ImportResult importFolder();
    std::string importDir() const { return dir + "/import"; }

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
