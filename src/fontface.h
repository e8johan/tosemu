/*
 * TOSEMU - an emulated environment for TOS applications
 * Copyright (C) 2026 Johan Toverland Thelin <e8johan@gmail.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 *
 */

#ifndef FONTFACE_H
#define FONTFACE_H

/*
 * Typefaces at any size, on the host's own font machinery.
 *
 * This is the SpeedoGDOS half of the fonts: an application asks for a face by
 * the name a document was written in - Swiss 721, Dutch 801 - and gets it at
 * whatever size it asks for, rather than at the sizes somebody put .FNT files
 * on the disk for.
 *
 * It is on this side of emuvdi rather than inside it because FreeType and
 * fontconfig cannot be brought in there: that half is built against EmuTOS's
 * headers, where WORD, LONG and BOOL are EmuTOS's and string.h is EmuTOS's
 * own. So this is written in plain types and included from over there the way
 * files.h and settings.h already are.
 *
 * Sizes are in 64ths of a point throughout, which is both what vst_setsize
 * takes and what FreeType works in, so nothing has to be scaled twice.
 */

/*
 * Readies the engine and settles which faces there are. Call once. Answers how
 * many faces were found, which is 0 on a build without FreeType and on a
 * machine where none of the substitutes could be resolved.
 *
 * The faces are not every font the host has. An application shows this list in
 * a menu and picks from it by number, and a hundred faces it has never heard
 * of is not what a GEM program is expecting - SpeedoGDOS shipped four. So the
 * list is the substitution table's: the names documents were written in, each
 * resolved to whatever the host has that is closest.
 */
int fontface_init(void);

/* How many faces there are, and what each of them is. The index runs from 0. */
int fontface_count(void);
int fontface_id(int index);
const char *fontface_name(int index);

/* Whether an id is one of these, which is how the VDI tells a face that is
 * drawn from outlines from one that came out of a .FNT file */
int fontface_known(int id);

/*
 * The size to draw at, in 64ths of a point, and how many dots to the inch the
 * screen has in each direction.
 *
 * The two resolutions differ on the screens whose pixels are not square: an ST
 * medium screen is 640 across and 200 down in the space a high resolution one
 * puts 400 in, so a character has to be half as tall in pixels to be the same
 * height on the glass. The bitmap fonts said this by shipping a second set;
 * here it is arithmetic.
 */
void fontface_resolution(int xdpi, int ydpi);

/*
 * What a face measures at a size. Every field is in pixels, and they are the
 * fields a font header carries so that vqt_fontheader and vqt_fontinfo have
 * something to answer with.
 */
struct fontface_metrics {
    int top;            /* baseline to the top of the tallest character */
    int ascent;         /* baseline to the top of a capital */
    int half;           /* baseline to the top of a lower case x */
    int descent;        /* baseline down to the bottom of a lower case p */
    int bottom;         /* baseline down to the lowest ink of any character */
    int max_width;      /* the widest character */
    int first, last;    /* the characters it has, as an eight bit range */
};

int fontface_metrics(int id, int size64, struct fontface_metrics *into);

/*
 * How wide a string is, and how tall, in pixels. The length is in characters
 * and the text is an eight bit Atari string rather than anything wider.
 *
 * This is the answer an application lays a page out from, so it has to be the
 * same answer the drawing will produce - both come from the same advances.
 */
int fontface_extent(int id, int size64, const char *text, int length,
                    int *width, int *height);

/* One character's advance, for vqt_advance */
int fontface_advance(int id, int size64, int character);

/*
 * A string, drawn.
 *
 * The bitmap is one bit to a pixel with the high bit of a byte leftmost, which
 * is the order every Atari raster is in, and its rows are a whole number of
 * words so that it can be handed to the VDI's raster calls as it stands.
 *
 * There is no antialiasing, and that is a decision rather than a gap: an Atari
 * screen is a few planes of indexed colour whose palette an application chose,
 * so there are no greys to draw the edges in. SpeedoGDOS rendered monochrome
 * on this hardware as well.
 *
 * origin_x and origin_y say where the baseline's left end is inside the
 * bitmap, since a character may lean left of where it starts.
 */
struct fontface_bitmap {
    unsigned short *bits;
    int width, height;      /* in pixels */
    int words;              /* per row */
    int origin_x, origin_y;
};

int fontface_render(int id, int size64, const char *text, int length,
                    struct fontface_bitmap *into);
void fontface_render_free(struct fontface_bitmap *bitmap);

/* Which host face a name was resolved to, for saying so in a diagnostic */
const char *fontface_resolved(int index);

#endif /* FONTFACE_H */
