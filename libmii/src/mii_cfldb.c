/* CFL_DB.dat: the 3DS's Mii database (shared extdata 0xF000000B, the file
 * an emulator such as Azahar keeps in its NAND folder).
 *
 *   0x0000 "CFOG", u32 0x00000100           Mii Maker's Miis
 *   0x0008 100 x 0x5C                       Ver3StoreData without its last 4
 *                                           bytes; an empty slot is all zero;
 *                                           the first is the personal Mii
 *   0x23F8 "CFHE", a linked list of 3000 0xE-byte nodes   (StreetPass)
 *   0xC81E u16 big-endian: CRC-16 (as Ver3StoreData's) of 0x0000-0xC81D
 *   0xC820 "CFRA" ... and its own CRC at 0xE4BE  (game invitations)
 *   0xE4C0 3000 x 0x54                      (more StreetPass data)
 *
 * Only Mii Maker's section and the CRC after it are ever changed here; the
 * rest of the file stays byte for byte as it was. (3dbrew, "Mii"; checked
 * against a CFL_DB.dat from Azahar: both CRCs as described.) */
#include "mii_internal.h"

#define CFL_MAGIC_OFS   0x0000
#define CFL_ENTRIES_OFS 0x0008
#define CFL_CFHE_OFS    0x23F8
#define CFL_CRC_OFS     0xC81E
#define CFL_MIN_SIZE    0xC820

mii_result_t mii_cfldb_check(const uint8_t *db, size_t size)
{
    if (!db)
        return MII_ERR_ARGUMENT;
    if (size < CFL_MIN_SIZE)
        return MII_ERR_SIZE;
    if (memcmp(db + CFL_MAGIC_OFS, "CFOG", 4) != 0 || mii_rd32be(db + 4) != 0x00010000u ||
        memcmp(db + CFL_CFHE_OFS, "CFHE", 4) != 0)
        return MII_ERR_MAGIC;
    if (mii_cfldb_checksum(db) != mii_rd16be(db + CFL_CRC_OFS))
        return MII_ERR_CRC;
    return MII_OK;
}

uint16_t mii_cfldb_checksum(const uint8_t *db)
{
    return mii_crc16(db, CFL_CRC_OFS, 0);
}

void mii_cfldb_fix_checksum(uint8_t *db)
{
    mii_wr16be(db + CFL_CRC_OFS, mii_cfldb_checksum(db));
}

uint8_t *mii_cfldb_slot(uint8_t *db, int slot)
{
    if (!db || slot < 0 || slot >= MII_CFLDB_MAX_MIIS)
        return NULL;
    return db + CFL_ENTRIES_OFS + (size_t)slot * MII_CFLDB_ENTRY_SIZE;
}

int mii_cfldb_slot_used(const uint8_t *db, int slot)
{
    if (!db || slot < 0 || slot >= MII_CFLDB_MAX_MIIS)
        return 0;
    const uint8_t *e = db + CFL_ENTRIES_OFS + (size_t)slot * MII_CFLDB_ENTRY_SIZE;
    for (int i = 0; i < MII_CFLDB_ENTRY_SIZE; i++)
        if (e[i])
            return 1;
    return 0;
}
