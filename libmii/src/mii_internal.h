#ifndef MII_INTERNAL_H
#define MII_INTERNAL_H

#include "mii/mii.h"

#include <string.h>

static inline uint16_t mii_rd16le(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static inline uint16_t mii_rd16be(const uint8_t *p) { return (uint16_t)((p[0] << 8) | p[1]); }
static inline uint32_t mii_rd32le(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static inline uint32_t mii_rd32be(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}
static inline void mii_wr16le(uint8_t *p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static inline void mii_wr16be(uint8_t *p, uint16_t v) { p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v; }
static inline void mii_wr32le(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}
static inline void mii_wr32be(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v;
}

/* Extract / insert `width` bits at `shift` of a word. */
static inline uint32_t mii_bits(uint32_t word, unsigned shift, unsigned width)
{
    return (word >> shift) & ((1u << width) - 1u);
}
static inline uint32_t mii_put_bits(uint32_t word, unsigned shift, unsigned width, uint32_t value)
{
    uint32_t mask = ((1u << width) - 1u) << shift;
    return (word & ~mask) | ((value << shift) & mask);
}

/* Returns 1 if all 49 fields are within range (name / id not checked). */
int mii_fields_in_range(const mii_charinfo_t *ci);

/* Reads a 10-unit UTF-16LE name. Units after the first NUL are dropped:
 * real files often keep stale characters there. */
static inline void mii_read_name(uint16_t name[MII_NAME_LENGTH + 1], const uint8_t *src)
{
    int ended = 0;
    for (int i = 0; i < MII_NAME_LENGTH; i++) {
        uint16_t c = mii_rd16le(src + 2 * i);
        if (c == 0)
            ended = 1;
        name[i] = ended ? 0 : c;
    }
    name[MII_NAME_LENGTH] = 0;
}

#endif
