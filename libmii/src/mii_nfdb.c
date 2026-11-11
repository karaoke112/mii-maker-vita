/* NFDB: the Switch Mii database file (nn::mii NintendoFigurineDatabase).
 *
 *   0x0000 "NFDB"
 *   0x0004 StoreData[100]
 *   0x1A94 u8 version (1)
 *   0x1A95 u8 count
 *   0x1A96 u16 CRC16 of 0x0000-0x1A95, big-endian
 */
#include "mii_internal.h"

#define NFDB_ENTRIES_OFS 0x0004
#define NFDB_VERSION_OFS 0x1A94
#define NFDB_COUNT_OFS   0x1A95
#define NFDB_CRC_OFS     0x1A96
#define NFDB_VERSION     1

void mii_nfdb_init(mii_nfdb_t *db)
{
    memset(db, 0, sizeof(*db));
    db->version = NFDB_VERSION;
}

mii_result_t mii_nfdb_decode(mii_nfdb_t *db, const uint8_t *data, size_t size)
{
    if (!db || !data)
        return MII_ERR_ARGUMENT;
    if (size != MII_NFDB_SIZE)
        return MII_ERR_SIZE;
    if (memcmp(data, "NFDB", 4) != 0 || data[NFDB_VERSION_OFS] != NFDB_VERSION)
        return MII_ERR_MAGIC;
    if (mii_crc16(data, MII_NFDB_SIZE, 0) != 0)
        return MII_ERR_CRC;
    if (data[NFDB_COUNT_OFS] > MII_NFDB_MAX_MIIS)
        return MII_ERR_INVALID;
    mii_nfdb_init(db);
    db->count = data[NFDB_COUNT_OFS];
    memcpy(db->entries, data + NFDB_ENTRIES_OFS, (size_t)MII_NFDB_MAX_MIIS * MII_STOREDATA_SIZE);
    return MII_OK;
}

mii_result_t mii_nfdb_encode(const mii_nfdb_t *db, uint8_t out[MII_NFDB_SIZE])
{
    if (!db || !out)
        return MII_ERR_ARGUMENT;
    if (db->count > MII_NFDB_MAX_MIIS)
        return MII_ERR_INVALID;
    memset(out, 0, MII_NFDB_SIZE);
    memcpy(out, "NFDB", 4);
    /* Only the used slots are written; the rest stay zero like nn::mii. */
    memcpy(out + NFDB_ENTRIES_OFS, db->entries, (size_t)db->count * MII_STOREDATA_SIZE);
    out[NFDB_VERSION_OFS] = NFDB_VERSION;
    out[NFDB_COUNT_OFS] = db->count;
    mii_wr16be(out + NFDB_CRC_OFS, mii_crc16(out, NFDB_CRC_OFS, 0));
    return MII_OK;
}

mii_result_t mii_nfdb_get(const mii_nfdb_t *db, int index, mii_charinfo_t *ci)
{
    if (!db || !ci)
        return MII_ERR_ARGUMENT;
    if (index < 0 || index >= db->count)
        return MII_ERR_NOT_FOUND;
    /* Device CRC is not checked: a database copied from another console
     * has a different author id. */
    return mii_storedata_decode(ci, db->entries[index], MII_STOREDATA_SIZE, NULL);
}

int mii_nfdb_find(const mii_nfdb_t *db, const mii_create_id_t *id)
{
    if (!db || !id)
        return MII_ERR_ARGUMENT;
    for (int i = 0; i < db->count; i++)
        if (memcmp(db->entries[i] + 0x30, id->bytes, 16) == 0)
            return i;
    return MII_ERR_NOT_FOUND;
}

int mii_nfdb_put(mii_nfdb_t *db, const mii_charinfo_t *ci, const uint8_t author_id[16])
{
    uint8_t sd[MII_STOREDATA_SIZE];
    mii_result_t r;
    int index;

    if (!db || !ci)
        return MII_ERR_ARGUMENT;
    if (!mii_create_id_is_valid(&ci->create_id))
        return MII_ERR_INVALID;
    r = mii_storedata_encode(ci, author_id, sd);
    if (r != MII_OK)
        return r;
    index = mii_nfdb_find(db, &ci->create_id);
    if (index < 0) {
        if (db->count >= MII_NFDB_MAX_MIIS)
            return MII_ERR_FULL;
        index = db->count++;
    }
    memcpy(db->entries[index], sd, MII_STOREDATA_SIZE);
    return index;
}

mii_result_t mii_nfdb_remove(mii_nfdb_t *db, int index)
{
    if (!db)
        return MII_ERR_ARGUMENT;
    if (index < 0 || index >= db->count)
        return MII_ERR_NOT_FOUND;
    memmove(db->entries[index], db->entries[index + 1],
            (size_t)(db->count - index - 1) * MII_STOREDATA_SIZE);
    db->count--;
    memset(db->entries[db->count], 0, MII_STOREDATA_SIZE);
    return MII_OK;
}

mii_result_t mii_nfdb_move(mii_nfdb_t *db, int from, int to)
{
    uint8_t tmp[MII_STOREDATA_SIZE];

    if (!db)
        return MII_ERR_ARGUMENT;
    if (from < 0 || from >= db->count || to < 0 || to >= db->count)
        return MII_ERR_NOT_FOUND;
    if (from == to)
        return MII_OK;
    memcpy(tmp, db->entries[from], MII_STOREDATA_SIZE);
    if (from < to)
        memmove(db->entries[from], db->entries[from + 1], (size_t)(to - from) * MII_STOREDATA_SIZE);
    else
        memmove(db->entries[to + 1], db->entries[to], (size_t)(from - to) * MII_STOREDATA_SIZE);
    memcpy(db->entries[to], tmp, MII_STOREDATA_SIZE);
    return MII_OK;
}
