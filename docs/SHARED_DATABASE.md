# The shared Mii database on PS Vita

Any Vita app or game port can use the Miis players make in Mii Maker Vita.
This page is the contract.

## Location

```
ux0:data/mii/
├── database.nfdb       the Miis (Switch NFDB format, 6808 bytes)
├── database.nfdb.bak   previous version, written before each save
├── author_id.bin       16 random bytes identifying this Vita
└── export/             files written by "Export" (.charinfo/.nfsd/.ffsd)
```

## Format

`database.nfdb` is byte-for-byte the Switch's own Mii database format:

| Offset | Size | Content |
| --- | --- | --- |
| 0x0000 | 4 | `"NFDB"` |
| 0x0004 | 100 × 0x44 | StoreData slots; only the first `count` are used, the rest are zero |
| 0x1A94 | 1 | version, always 1 |
| 0x1A95 | 1 | count (0–100) |
| 0x1A96 | 2 | CRC-16/XMODEM of bytes 0x0000–0x1A95, big-endian |

Each StoreData is a CoreData (0x30) + create ID (0x10) + data CRC + device CRC
(details in `libmii/include/mii/mii.h`). The device CRC is computed with
`author_id.bin`; readers should only check the data CRC.

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

## Rules

- **Read-only for other apps.** Only Mii Maker Vita writes the file. A game
  that wants to "save" a Mii should export a file the user can import.
- **Identify Miis by create ID**, not by slot index: users reorder and delete.
- **Tolerate a missing or invalid file**: fall back to `.bak`, then to your
  own default Mii.
- The Vita runs one app at a time, so no locking is needed.
