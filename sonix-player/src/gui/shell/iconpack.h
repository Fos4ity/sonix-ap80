#ifndef ICONPACK_H
#define ICONPACK_H

#include <stdbool.h>

#include "lvgl/lvgl.h"

// Icon packs (Settings -> Appearance -> Icons).
//
// A pack is a folder SONIX_RESOURCE_DIR "/icons/<name>/" of PNG files named
// after the built-in icons' source files (chevron-left.png, play.png, ... --
// the list is assets/icons in the source tree). Whatever the pack holds
// replaces the built-in picture; everything else stays as it is. A PNG may be
// any size: it is scaled to the size the interface draws that icon at.
//
// Glyph icons (the SVG ones) are tinted by the theme, so only the PNG's alpha
// channel is used for those. The coloured artwork -- the big menu tiles -- is
// taken from the pack's "tiles" subfolder (<name>/tiles/music.png, ...) and
// keeps the PNG's colours.
//
// Stored as [ui] icon_pack; empty is the built-in set.

typedef struct {
	lv_image_dsc_t *dsc;
	const char *file; // name without extension
	bool glyph;		  // tinted white shape; otherwise coloured artwork
	// Filled in by iconpack.c:
	const uint8_t *builtin_data;
	uint8_t *pack_data;
} iconpack_entry_t;

extern iconpack_entry_t iconpack_table[];
extern const int iconpack_table_count;

#define ICONPACK_MAX 16

// Applies the saved pack. Call once, before the interface is built.
void iconpack_init(void);

int iconpack_count(void);			 // rescans the folder
const char *iconpack_name(int index); // valid until the next rescan
const char *iconpack_current(void);	 // "" for the built-in set
// Switches the running interface to `name` ("" = built-in) and saves it.
// Returns how many icons the pack replaced, or -1 when it has none.
int iconpack_set(const char *name);

// For the pack picker: one icon as `pack` draws it ("" = built-in), size x
// size. `tile` picks the coloured menu artwork instead of a glyph; a pack
// without tiles gives the built-in tile. Free with iconpack_preview_free.
lv_image_dsc_t *iconpack_preview(const char *pack, const char *file, bool tile, int size);
void iconpack_preview_free(lv_image_dsc_t *dsc);

#endif
