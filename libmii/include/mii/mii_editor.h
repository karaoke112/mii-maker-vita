/*
 * Editor-support data: the Switch Mii palette and the order in which the
 * Switch's Mii editor lays out each grid of parts and colors.
 *
 * An editor shows grid cell (row, col) as value
 *   grid->items[row * grid->columns + col]
 * so the cursor position a player remembers from the Switch maps to the
 * same Mii on the Vita.
 */
#ifndef MII_MII_EDITOR_H
#define MII_MII_EDITOR_H

#include "mii/mii.h"

#ifdef __cplusplus
extern "C" {
#endif

/* sRGB colors as 0xRRGGBB. */
uint32_t mii_common_color_rgb(uint8_t common_color);   /* 0-99: hair, eyes, brows, mouth, beard, glasses */
uint32_t mii_faceline_color_rgb(uint8_t faceline_color); /* 0-9: skin */
uint32_t mii_favorite_color_rgb(uint8_t favorite_color); /* 0-11: shirt / favorite */
uint32_t mii_upper_lip_color_rgb(uint8_t mouth_color);  /* 0-99: darker lip shade for a mouth color */

typedef enum mii_grid_kind {
    MII_GRID_COMMON_COLOR, /* 10 x 10 */
    MII_GRID_FACELINE_COLOR,
    MII_GRID_HAIR,
    MII_GRID_EYE,
    MII_GRID_EYEBROW,
    MII_GRID_NOSE,
    MII_GRID_MOUTH,
    MII_GRID_GLASS,
    MII_GRID_COUNT
} mii_grid_kind_t;

typedef struct mii_grid {
    const uint8_t *items; /* field values in display order, row-major */
    uint8_t count;
    uint8_t columns;
} mii_grid_t;

const mii_grid_t *mii_editor_grid(mii_grid_kind_t kind);

/* Position of `value` within a grid, or -1. */
int mii_grid_index_of(const mii_grid_t *grid, uint8_t value);

/* A random, natural-looking Mii (for "look-alike" pickers): mostly
 * everyday hair, eye and skin colors, occasional facial hair, glasses or a
 * mole, default part positions, average height and build. `gender` 0 male,
 * 1 female, -1 random. `state` is the random generator's state (any value
 * but 0; it is advanced). The name is left as the default. */
void mii_charinfo_init_random(mii_charinfo_t *ci, int gender, uint32_t *state);

#ifdef __cplusplus
}
#endif

#endif /* MII_MII_EDITOR_H */
