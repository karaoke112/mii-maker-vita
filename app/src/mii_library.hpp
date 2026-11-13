#pragma once

#include <mii/mii.h>

#include <array>
#include <string>
#include <vector>

// The user's Miis (up to 200), persisted as official Switch Mii databases
// (NFDB, 100 each) in a location shared with other homebrew: the first 100
// in database.nfdb (what other apps read), the rest in database2.nfdb.
//
//   Vita:    ux0:data/mii/database.nfdb, database2.nfdb   (+ .bak, export/)
//   Desktop: ./data/mii/database.nfdb
// (the author id lives in the app's own folder; see paths.hpp)
//
// See docs/SHARED_DATABASE.md for the contract other apps and game ports
// can rely on.
class MiiLibrary
{
  public:
    static constexpr int kMax = 200;
    static MiiLibrary& instance();

    bool load();
    bool save();

    int count() const { return (int)entries.size(); }
    bool get(int index, mii_charinfo_t* out) const;
    // Writes the Miis as the Wii's Mii database for Wii game ports (done
    // by save(), and at start-up when there is none).
    bool exportWii() const;
    // Inserts or replaces (matched by create id). Assigns a create id first
    // if the Mii has none. Saves to disk. Returns the index or -1.
    int put(mii_charinfo_t* ci);
    bool remove(int index);
    // Reordering (saved): `move` takes a Mii out and puts it back at `to`;
    // `swap` exchanges two.
    bool move(int from, int to);
    bool swap(int a, int b);
    // Writes .charinfo, .nfsd and .ffsd copies to <data>/export/, named
    // <nickname>_<first 4 bytes of the create id in hex>.
    bool exportMii(int index, std::string* outPath);

    struct ImportResult {
        int imported = 0; // added or updated
        int failed = 0;   // unreadable or invalid files
        bool full = false; // stopped: the library holds kMax Miis
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
    // The 3DS / Wii U header for a Mii leaving this console (QR codes,
    // .ffsd files): a normal, valid Mii made on a "3DS-compatible" device
    // (as the Switch 2 writes it; a 3DS refuses origin 4), with an id that
    // stays the same for the same Mii and this console's system id / MAC.
    void ver3Info(const mii_charinfo_t& mii, mii_ver3_info_t* out) const;
    const std::string& dataDir() const { return dir; }

  private:
    MiiLibrary();
    std::string dbPath(int part) const { return dir + (part ? "/database2.nfdb" : "/database.nfdb"); }
    bool loadPart(int part, std::vector<std::array<uint8_t, MII_STOREDATA_SIZE>>* out);
    bool savePart(int part);
    int find(const mii_create_id_t& id) const;
    // Adds, or replaces the Mii with the same create id; the index or -1
    int putEntry(const mii_charinfo_t& ci);

    std::string dir;
    std::vector<std::array<uint8_t, MII_STOREDATA_SIZE>> entries; // StoreData, in order
    uint8_t authorId[16];
};

// Fills `out` with cryptographically random bytes (sceKernelGetRandomNumber
// on Vita, std::random_device elsewhere).
void platformRandomBytes(uint8_t* out, size_t size);
