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
 * What comes back is in the VDI's own byte order throughout, raster included,
 * and marked F_STDFORM to say so - whichever order the file was written in.
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

/*
 * Finds the ASSIGN.SYS this machine is to use and reads the list of fonts in
 * it for one device. Call once, when the screen is settled and before anything
 * asks whether there is a GDOS.
 *
 * The device is the number the section is written under, which is the same
 * number the AES opens the physical workstation with: the screen resolution
 * plus two. Nothing is loaded here - a program that never asks for fonts
 * should not pay for reading them - only the list is kept.
 */
void gdos_assign_init(WORD device);

/*
 * Whether there are fonts to be had, which is the whole of what vq_gdos
 * answers. A machine with no ASSIGN.SYS had no GDOS, and saying otherwise
 * would send an application down a road with nothing at the end of it.
 */
int gdos_installed(void);

/*
 * The fonts themselves, loaded the first time this is called and kept
 * thereafter. Answers the head of the chain, or null when there are none.
 */
Fonthead *gdos_loaded_chain(void);

/*
 * And the text scratch buffer they need, which is the reason vst_load_fonts
 * takes one at all: the VDI's own is sized at compile time for the 8x16 system
 * font, and a loaded font's cell is larger than that. The offset to the second
 * half of the buffer goes in *half, which is what the VDI calls scrpt2.
 */
WORD *gdos_loaded_scratch(WORD *half);

/*
 * Puts the loaded fonts where the VDI looks for them, and answers how many
 * faces that added. This is what vst_load_fonts comes to.
 *
 * EmuTOS has this call already, in vdi_vst_load_fonts, and it cannot be used.
 * It takes the chain and the scratch buffer as addresses in two words of the
 * control array each, and reads them back with ULONG_AT - which is a ULONG,
 * and a ULONG here is eight bytes rather than the four it is on a 68000. So
 * the read of the scratch buffer at control[7] swallows control[9], which is
 * the offset it is about to use, and the read of the chain at control[10]
 * swallows two words past the end of what a caller wrote. There is no pair of
 * values that can be put in those words to make both reads come out right.
 *
 * The same eight bytes made vdi_raster.c a file tosemu carries a copy of. It
 * is not worth carrying vdi_text.c as well for two lines, and it does not have
 * to be: what those two lines lead to is twenty lines of bookkeeping, which is
 * what this is. Everything else in vdi_text.c reads the fonts through the
 * chain rather than through the control array and goes on working untouched.
 */
WORD gdos_install(Vwk *vwk);

#endif /* EMUVDI_GDOS_H */
