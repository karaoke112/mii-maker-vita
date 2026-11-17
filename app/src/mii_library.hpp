#pragma once

#include <mii/mii.h>

#include "mii_store.hpp"

#include <array>
#include <map>
#include <string>
#include <vector>

// The user's Miis (up to 1000), persisted as official Switch Mii databases
// (NFDB, 100 each) in a location shared with other homebrew: the first 100
// in database.nfdb (what other apps read), the rest in database2.nfdb.
//
//   Vita:    ux0:data/mii/database.nfdb, database2.nfdb   (+ .bak, export/)
//   Desktop: ./data/mii/database.nfdb
// (the author id lives in the app's own folder; see paths.hpp)
//
// See docs/SHARED_DATABASE.md for the contract other apps and game ports
// can rely on.
// What a Mii carries besides its face that the Switch's format has no room
// for (the 3DS / Wii U and Wii formats do): kept beside the database, by
// create id, in <data>/details.txt, and written into .ffsd / .cfsd files,
// QR codes and the Wii database.
struct MiiDetails {
    bool favorite = false; // red pants
    uint8_t birthMonth = 0, birthDay = 0; // 0: not set
    std::string creator;   // "": not set (the console's user name is used)
    bool mingle = true;    // may travel to other consoles (Wii: "Mingle")
    bool isDefault() const { return !favorite && !birthMonth && !birthDay && creator.empty() && mingle; }
};

class MiiLibrary : public MiiStore
{
  public:
    static constexpr int kMax = 1000;
    static constexpr int kParts = kMax / MII_NFDB_MAX_MIIS; // a database file per hundred
    static MiiLibrary& instance();

    bool load();
    bool save();

    int count() const override { return (int)entries.size(); }
    int capacity() const override { return kMax; }
    bool get(int index, mii_charinfo_t* out) const override;
    // Inserts or replaces (matched by create id). Assigns a create id first
    // if the Mii has none. Saves to disk. Returns the index or -1.
    int put(mii_charinfo_t* ci) override;
    bool remove(int index) override;
    // Reordering (saved): `move` takes a Mii out and puts it back at `to`;
    // `swap` exchanges two.
    bool canMove() const override { return true; }
    bool move(int from, int to) override;
    bool swap(int a, int b) override;
    int pants(const mii_charinfo_t& mii) const override;
    // Writes .charinfo, .nfsd and .ffsd copies to <data>/export/, named
    // <nickname>_<first 4 bytes of the create id in hex>.
    bool exportMii(int index, std::string* outPath);
    // the same for any Mii; `info`: its 3DS data, written as it is
    bool exportMii(const mii_charinfo_t& ci, std::string* outPath, const mii_ver3_info_t* info = nullptr);
    // a Mii's favorite, birthday, creator... as a 3DS Mii carries them
    static MiiDetails detailsOf(const mii_ver3_info_t& info);

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
    // The import screen: what importFolder would add, without adding it
    struct Pending {
        std::string file; // name in importDir()
        mii_charinfo_t mii;
        bool hasInfo = false;
        mii_ver3_info_t info {};
    };
    std::vector<Pending> scanImports(int* unreadable);
    // Adds these (their files move to import/imported/), to this list or
    // `into` another (the 3DS's)
    ImportResult importPending(const std::vector<Pending>& chosen, MiiStore* into = nullptr);
    // Deletes a file from the import folder
    bool deleteImport(const std::string& file);
    std::string importDir() const { return dir + "/import"; }

    void newCreateId(mii_create_id_t* id) const;
    // this console's author id (16 bytes; a Wii Mii takes the first four as its console's)
    const uint8_t* author() const { return authorId; }
    MiiDetails details(const mii_create_id_t& id) const override;
    // Saved at once (details.txt is small)
    void setDetails(const mii_create_id_t& id, const MiiDetails& d) override;
    // The 3DS / Wii U header for a Mii leaving this console (QR codes,
    // .ffsd files): a normal, valid Mii made on a "3DS-compatible" device
    // (as the Switch 2 writes it; a 3DS refuses origin 4), with an id that
    // stays the same for the same Mii and this console's system id / MAC.
    void ver3Info(const mii_charinfo_t& mii, mii_ver3_info_t* out) const;
    // the same with these details (a Mii of another list)
    void ver3Info(const mii_charinfo_t& mii, const MiiDetails& d, mii_ver3_info_t* out) const;
    const std::string& dataDir() const { return dir; }

  private:
    MiiLibrary();
    // database.nfdb, database2.nfdb ... database10.nfdb
    std::string dbPath(int part) const { return dir + (part ? "/database" + std::to_string(part + 1) + ".nfdb" : "/database.nfdb"); }
    std::vector<uint8_t> written[kParts]; // each file as last written (unchanged ones are not written again)
    bool loadPart(int part, std::vector<std::array<uint8_t, MII_STOREDATA_SIZE>>* out);
    bool savePart(int part);
    int find(const mii_create_id_t& id) const;
    // Adds, or replaces the Mii with the same create id; the index or -1
    int putEntry(const mii_charinfo_t& ci);

    std::string dir;
    std::vector<std::array<uint8_t, MII_STOREDATA_SIZE>> entries; // StoreData, in order
    std::map<std::string, MiiDetails> detailMap; // by create id (hex)
    void loadDetails();
    void saveDetails() const;
    static std::string idKey(const mii_create_id_t& id);
    void detailsFromVer3(const mii_create_id_t& id, const mii_ver3_info_t& info);
    uint8_t authorId[16];
};

// Fills `out` with cryptographically random bytes (sceKernelGetRandomNumber
// on Vita, std::random_device elsewhere).
void platformRandomBytes(uint8_t* out, size_t size);
