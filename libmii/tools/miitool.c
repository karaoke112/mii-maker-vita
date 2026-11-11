/* miitool - convert and inspect Mii files on a PC.
 *
 *   miitool info    <file>
 *   miitool convert <in> <out>
 *
 * Formats are picked by extension: .charinfo .nfcd/.coredata .nfsd/.storedata
 * .ffsd/.cfsd/.ver3 (0x60) .ffcd/.cfcd (0x5C, no CRC).
 */
#include "mii/mii.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum fmt { F_UNKNOWN, F_CHARINFO, F_CORE, F_STORE, F_VER3, F_VER3_CORE };

static enum fmt fmt_from_path(const char *path)
{
    const char *dot = strrchr(path, '.');
    if (!dot)
        return F_UNKNOWN;
    dot++;
    if (!strcmp(dot, "charinfo")) return F_CHARINFO;
    if (!strcmp(dot, "nfcd") || !strcmp(dot, "coredata")) return F_CORE;
    if (!strcmp(dot, "nfsd") || !strcmp(dot, "storedata")) return F_STORE;
    if (!strcmp(dot, "ffsd") || !strcmp(dot, "cfsd") || !strcmp(dot, "ver3")) return F_VER3;
    if (!strcmp(dot, "ffcd") || !strcmp(dot, "cfcd")) return F_VER3_CORE;
    return F_UNKNOWN;
}

static long read_file(const char *path, uint8_t *buf, size_t cap)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        perror(path);
        return -1;
    }
    size_t n = fread(buf, 1, cap, f);
    fclose(f);
    return (long)n;
}

static int write_file(const char *path, const uint8_t *buf, size_t size)
{
    FILE *f = fopen(path, "wb");
    if (!f) {
        perror(path);
        return -1;
    }
    size_t n = fwrite(buf, 1, size, f);
    fclose(f);
    return n == size ? 0 : -1;
}

static mii_result_t load(const char *path, mii_charinfo_t *ci, mii_ver3_info_t *v3)
{
    uint8_t buf[0x100];
    long n = read_file(path, buf, sizeof(buf));
    if (n < 0)
        return MII_ERR_ARGUMENT;
    mii_ver3_info_init_default(v3);
    switch (fmt_from_path(path)) {
    case F_CHARINFO:  return mii_charinfo_decode(ci, buf, (size_t)n);
    case F_CORE:      return mii_coredata_decode(ci, buf, (size_t)n);
    case F_STORE:     return mii_storedata_decode(ci, buf, (size_t)n, NULL);
    case F_VER3:
    case F_VER3_CORE: return mii_ver3_decode(ci, v3, buf, (size_t)n, NULL);
    default:
        fprintf(stderr, "%s: unknown extension\n", path);
        return MII_ERR_ARGUMENT;
    }
}

static int cmd_info(const char *path)
{
    mii_charinfo_t ci;
    mii_ver3_info_t v3;
    char name[64];
    mii_result_t r = load(path, &ci, &v3);
    if (r != MII_OK && r != MII_ERR_INVALID) {
        fprintf(stderr, "%s: %s\n", path, mii_result_string(r));
        return 1;
    }
    mii_name_get_utf8(&ci, name, sizeof(name));
    printf("{\n  \"name\": \"%s\",\n  \"create_id\": \"", name);
    for (int i = 0; i < 16; i++)
        printf("%02x", ci.create_id.bytes[i]);
    printf("\"");
    for (int f = 0; f < MII_FIELD_COUNT; f++)
        printf(",\n  \"%s\": %u", mii_field_name((mii_field_t)f), mii_field_get(&ci, (mii_field_t)f));
    printf("\n}\n");
    if (r == MII_ERR_INVALID)
        fprintf(stderr, "warning: field %s out of range\n", mii_field_name((mii_field_t)mii_verify(&ci, 0)));
    return 0;
}

static int cmd_convert(const char *in, const char *out)
{
    mii_charinfo_t ci;
    mii_ver3_info_t v3;
    uint8_t buf[0x100];
    size_t size;
    mii_result_t r = load(in, &ci, &v3);
    if (r != MII_OK) {
        fprintf(stderr, "%s: %s\n", in, mii_result_string(r));
        return 1;
    }
    switch (fmt_from_path(out)) {
    case F_CHARINFO: r = mii_charinfo_encode(&ci, buf); size = MII_CHARINFO_SIZE; break;
    case F_CORE:     r = mii_coredata_encode(&ci, buf); size = MII_COREDATA_SIZE; break;
    case F_STORE:    r = mii_storedata_encode(&ci, NULL, buf); size = MII_STOREDATA_SIZE; break;
    case F_VER3:     r = mii_ver3_encode(&ci, &v3, buf); size = MII_VER3_STOREDATA_SIZE; break;
    case F_VER3_CORE: r = mii_ver3_encode(&ci, &v3, buf); size = 0x5C; break;
    default:
        fprintf(stderr, "%s: unknown extension\n", out);
        return 1;
    }
    if (r != MII_OK) {
        fprintf(stderr, "encode: %s\n", mii_result_string(r));
        return 1;
    }
    return write_file(out, buf, size) ? 1 : 0;
}

/* Lists a Switch-format Mii database (the shared database.nfdb). */
static int cmd_db(const char *path)
{
    static uint8_t buf[MII_NFDB_SIZE + 1];
    static mii_nfdb_t db;
    long n = read_file(path, buf, sizeof(buf));
    if (n < 0)
        return 1;
    mii_result_t r = mii_nfdb_decode(&db, buf, (size_t)n);
    if (r != MII_OK) {
        fprintf(stderr, "%s: %s\n", path, mii_result_string(r));
        return 1;
    }
    printf("%d Mii(s)\n", db.count);
    for (int i = 0; i < db.count; i++) {
        mii_charinfo_t ci;
        char name[64];
        r = mii_nfdb_get(&db, i, &ci);
        mii_name_get_utf8(&ci, name, sizeof(name));
        printf("%3d  ", i);
        for (int b = 0; b < 16; b++)
            printf("%02x", ci.create_id.bytes[b]);
        printf("  %-10s  %s%s\n", name, ci.gender ? "F" : "M", r == MII_OK ? "" : "  (invalid)");
    }
    return 0;
}

int main(int argc, char **argv)
{
    if (argc == 3 && !strcmp(argv[1], "info"))
        return cmd_info(argv[2]);
    if (argc == 4 && !strcmp(argv[1], "convert"))
        return cmd_convert(argv[2], argv[3]);
    if (argc == 3 && !strcmp(argv[1], "db"))
        return cmd_db(argv[2]);
    fprintf(stderr, "usage:\n  %s info <file>\n  %s convert <in> <out>\n  %s db <database.nfdb>\n",
            argv[0], argv[0], argv[0]);
    return 2;
}
