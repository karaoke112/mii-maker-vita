# The shared Mii database on PS Vita

Any Vita app or game port can use the Miis players make in Mii Maker Vita.
This page is the contract.

## Location

```
ux0:data/mii/
├── database.nfdb       the first 100 Miis (Switch NFDB format, 6808 bytes)
├── database.nfdb.bak   previous version, written before each save
├── database2.nfdb      Miis 101-200, in the same format (only while there are more than 100; + .bak)
├── details.txt         what the Switch format has no room for: favorite, birthday, mingle, creator (see below)
├── wii/
│   ├── RFL_DB.dat      the same Miis as the Wii's Mii database, for ports of Wii games
│   └── RFL_Res.dat     the Wii's Mii face resource, if the player provides it (not made here)
├── export/             files written by "Export": <name>_<id>.charinfo/.nfsd/.ffsd
└── import/             Mii files to add with "Import" (moved to import/imported/ once added)
```

Mii Maker Vita's own files are kept apart, in `ux0:data/mii-maker-vita/`
(`author_id.bin`, and the user's resource files in `dat/`). Other apps
should not need anything from there.

## Format

`database.nfdb` is byte-for-byte the Switch's own Mii database format
(`database2.nfdb` too; the list goes on from it, in order, so an app that
reads only `database.nfdb` sees the first 100):

| Offset | Size | Content |
| --- | --- | --- |
| 0x0000 | 4 | `"NFDB"` |
| 0x0004 | 100 × 0x44 | StoreData slots; only the first `count` are used, the rest are zero |
| 0x1A94 | 1 | version, always 1 |
| 0x1A95 | 1 | count (0–100) |
| 0x1A96 | 2 | CRC-16/XMODEM of bytes 0x0000–0x1A95, big-endian |

Each StoreData is a CoreData (0x30) + create ID (0x10) + data CRC + device CRC
(details in `libmii/include/mii/mii.h`). The device CRC is computed with
Mii Maker Vita's `author_id.bin`; readers should only check the data CRC.

## Reading it (C)

```c
#include <mii/mii.h>

static uint8_t buf[MII_NFDB_SIZE];
static mii_nfdb_t db;

FILE *f = fopen("ux0:data/mii/database.nfdb", "rb");
size_t n = f ? fread(buf, 1, sizeof(buf), f) : 0;
if (f) fclose(f);

if (mii_nfdb_decode(&db, buf, n) == MII_OK) {
    for (int i = 0; i < db.count; i++) {
        mii_charinfo_t mii;
        if (mii_nfdb_get(&db, i, &mii) == MII_OK) {
            /* Switch-style data: use as is, or convert:
             *   mii_ver3_encode(&mii, NULL, ver3);  -> 3DS/Wii U/amiibo format */
        }
    }
}
```

Add libmii to your build with `add_subdirectory(path/to/libmii)` and link
`mii`; it has no dependencies.

## For ports of Wii games: `wii/RFL_DB.dat`

Every save also writes the first 100 Miis to `wii/RFL_DB.dat`, byte for
byte the file the Wii keeps at `/shared2/menu/FaceLib/RFL_DB.dat` and that
its face library (RVLFaceLib, "RFL") reads. A port that compiles RFL (from
a decompilation) can map that NAND folder to `ux0:data/mii/wii/`:

| Offset | Size | Content |
| --- | --- | --- |
| 0x00000 | 4 | `"RNOD"` |
| 0x00004 | 100 × 0x4A | RFLCharData slots (big-endian bit-fields, most significant first); an empty slot is all zero |
| 0x01CEC | 4 | flags (isolation, bit 31) |
| 0x01D00 | 0x1D4E0 | the hidden database's header: `"RNHD"`, head and tail -1, 10000 empty entries |
| 0x1F1DE | 2 | RFL's CRC-16 over 0x00000–0x1F1DF with this field zero, big-endian (the whole header then checks to 0) |
| 0x1F1E0 | 10000 × 0x40 | room for hidden Miis (RFLiHiddenCharData, zero) |

RFL's structures are big-endian; a little-endian port converts them after
reading (and checks the CRC on the bytes as read). The Mii IDs are stable
per Mii (from its create ID), with bit 31 set (a normal Mii); the system
ID is the first four bytes of Mii Maker Vita's author ID.

The Wii has fewer parts and colors than later consoles: Miis using newer
ones get the Wii's default part, or the nearest color, in this copy only.
`libmii` reads and writes the format (`mii_wii_encode`, `mii_wii_decode`,
`mii_wii_db_write`).

RFL also needs the Wii's face resource, `RFL_Res.dat`. It is Nintendo's
file and is not included: players copy it from their own Wii's NAND
(`/shared2/menu/FaceLib/RFL_Res.dat`) into `ux0:data/mii/wii/`.

## Rules

- **Read-only for other apps.** Only Mii Maker Vita writes the file. A game
  that wants to "save" a Mii should export a file the user can import.
- **Identify Miis by create ID**, not by slot index: users reorder and delete.
- **Tolerate a missing or invalid file**: fall back to `.bak`, then to your
  own default Mii.
- The Vita runs one app at a time, so no locking is needed.

## details.txt

A Mii's favorite mark, birthday, mingle setting and creator name, which
the 3DS / Wii U and Wii formats have but the Switch's does not. One line
per Mii (Miis with nothing set have none), UTF-8, fields separated by tabs:

| Field | Content |
| --- | --- |
| 1 | the create id, 32 lowercase hex digits (StoreData bytes 0x30-0x3F) |
| 2 | favorite: `1` or `0` (a favorite wears red pants) |
| 3 | birth month, `0` (not set) to `12` |
| 4 | birth day, `0` (not set) to `31` |
| 5 | mingle: `1` (may travel to other consoles) or `0` |
| 6 | the creator's name (empty: not set) |

The Wii database and exported `.ffsd` files carry these too.

