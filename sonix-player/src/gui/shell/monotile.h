#ifndef MONOTILE_H
#define MONOTILE_H

#include <stdbool.h>

#include "lvgl/lvgl.h"

// Menu tiles in one colour ("Monochrome menu icons").
//
// Tinting a tile with the text colour only reads when the tile is a drawing on
// nothing: its shape is then the drawing. A tile that is a drawing on a plate
// (the Nothing packs' discs, Phosphor Fill's squares) has the plate for a
// shape, and tinting it leaves six identical blobs. So a tile is looked at
// first, and drawn in one colour the way its kind allows.
typedef enum {
	// Shaded, many-coloured artwork (the built-in glass tiles, brand logos):
	// nothing in it can be told apart by shape alone. It keeps its colours.
	MONOTILE_ARTWORK = 0,
	// One flat colour on nothing: tinted as it is.
	MONOTILE_GLYPH,
	// A drawing on a flat plate: drawn from monotile_mask(), the drawing
	// without its plate.
	MONOTILE_PLATE,
} monotile_kind_t;

monotile_kind_t monotile_kind(const lv_image_dsc_t *tile);

// For a MONOTILE_PLATE tile: the drawing alone, white on nothing, to be tinted
// like a glyph. NULL for the other kinds. Stays valid until monotile_reset().
const lv_image_dsc_t *monotile_mask(const lv_image_dsc_t *tile);

// Forgets everything worked out so far and frees the masks. For when the tiles'
// pixels change (another icon pack); nothing may still be showing a mask.
void monotile_reset(void);

// Whether the main menu's tiles, as they are now, can be drawn in one colour
// at all. False for the built-in set, where the switch would change nothing.
bool monotile_available(void);

#endif /* MONOTILE_H */
