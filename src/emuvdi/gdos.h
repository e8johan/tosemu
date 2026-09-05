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

#ifndef EMUVDI_GDOS_H
#define EMUVDI_GDOS_H

/*
 * The fonts GDOS would have loaded, between the files they come out of and the
 * VDI that draws with them.
 *
 * This is written in EmuTOS's types rather than in emuvdi.h's, because a font
 * here is a Fonthead - the structure the VDI walks, whose fields the drawing
 * reads directly - and there is nothing to be gained by describing it twice.
 * So this header is for the emuvdi side to say things to itself, the way
 * aeskernel.h is, and nothing in tosemu proper includes it.
 */

/*
 * Reads one .FNT file.
 *
 * Answers null, having said on stderr which file and what was wrong with it,
 * for anything that is not a font. A font list is something a person wrote and
 * a wrong line in one is not a reason to stop the machine.
 *
 * What comes back is in the VDI's own byte order except for the raster, which
 * is left as the file had it with F_STDFORM clear - vdi_vst_load_fonts runs
 * trnsfont over anything so marked and sets the flag itself, and that routine
 * is private to vdi_text.c, so leaving the work to it is the only way to have
 * it done once.
 */
Fonthead *gdos_font_read(const char *host_path);

/* And letting one go again, with the tables hanging off it */
void gdos_font_free(Fonthead *font);

/*
 * Puts a set of fonts into the order the VDI walks them in - by face, and
 * within a face by size, smallest first - and chains them together. Answers
 * the head of the chain, or null for no fonts at all.
 *
 * The order is not a tidiness: vqt_name counts the places where the id changes
 * as it walks, so a face whose sizes are not together is counted twice, and
 * vst_point and vst_height walk forward keeping the last size that still fits,
 * which finds the wrong one if the sizes do not ascend. The array is sorted in
 * place.
 */
Fonthead *gdos_font_chain(Fonthead **font, int count);

/*
 * How large a text scratch buffer a font needs, in bytes, counting only one of
 * its two halves.
 *
 * The VDI builds a character in this buffer before it goes to the screen, and
 * how large it has to be is decided by the largest cell the font has and by
 * what may be done to it - a character can be rotated, so its width and height
 * may swap; doubled, when a size is asked for that no font has; and outlined,
 * which adds a pixel all round. The arithmetic is vdi_text.c's, done there for
 * the 8x16 system font at compile time; this is the same arithmetic for a font
 * that is not known until it is read.
 *
 * Without it a loaded font draws through a buffer sized for the system font
 * and writes past the end of it.
 */
WORD gdos_font_scratch(const Fonthead *font);

#endif /* EMUVDI_GDOS_H */
