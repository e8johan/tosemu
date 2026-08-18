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

#ifndef EMUVDI_H
#define EMUVDI_H

#include <stdint.h>

/*
 * What the rest of tosemu is allowed to know about the ported VDI.
 *
 * Everything on the other side of this header is built with its own compiler
 * flags against EmuTOS's headers, where a WORD is a type of its own and the
 * VDI's state is a hundred globals. None of that belongs in tosemu proper, so
 * this is the whole of the surface between them.
 */

/* Readies the VDI: the system fonts and the tables an open workstation
 * reports. Call once, before anything else here. */
void emuvdi_init();

/*
 * Points the VDI at a surface, which is where everything drawn from now on
 * goes. The bitmap is Atari planar - one word of each plane in turn, the
 * highest bit of a word the leftmost pixel - in host byte order.
 */
void emuvdi_surface_select(void *base, uint16_t width, uint16_t height,
                           uint16_t planes);

/*
 * Serves one VDI call.
 *
 * The five arrays are the ones the caller passed, already copied out of the
 * emulated memory, and the answers are written back into them: how many
 * entries of intout and ptsout were filled in goes into control[4] and
 * control[2], and a workstation handle into control[6].
 */
void emuvdi_call(int16_t *control, int16_t *intin, int16_t *ptsin,
                 int16_t *intout, int16_t *ptsout);

/*
 * Opens the workstation the AES draws through, and works out the sizes it
 * reports: how wide and tall a character is, and how large a box has to be to
 * hold one. Call after emuvdi_init and after a surface is selected.
 */
void emuvdi_aes_init();

/*
 * What graf_handle answers with. The handle is the AES's own workstation, and
 * an application opens a virtual one against it rather than opening a physical
 * one of its own.
 */
void emuvdi_graf_handle(int16_t *handle, int16_t *wchar, int16_t *hchar,
                        int16_t *wbox, int16_t *hbox);

/*
 * Bitmaps a call names, for the raster operations.
 *
 * vro_cpyfm and its relatives are handed the address of a form definition
 * block - an MFDB - in two words of the control array, and the VDI reads it as
 * a pointer. An application's MFDB is in the emulated machine's memory and
 * describes a bitmap there, so it has to be brought across; these keep the
 * layout of the structure on this side, where the type is known.
 *
 * There are two slots because no VDI call names more than two bitmaps, a
 * source and a destination.
 */
#define EMUVDI_MFDB_SOURCE      (0)
#define EMUVDI_MFDB_DESTINATION (1)

void *emuvdi_mfdb(int slot);

/* A bitmap of null means the screen, which is the convention an application
 * uses to say "where I can see it" rather than a bitmap of its own */
void emuvdi_mfdb_set(void *mfdb, void *data, int16_t width, int16_t height,
                     int16_t wdwidth, int16_t standard, int16_t planes);

/* Where in the control array a call expects each bitmap's address */
void emuvdi_control_set_pointer(int16_t *control, int index, void *p);

/*
 * A colour register, as something that can be put on a screen.
 *
 * The VDI works in colour indices and the hardware in pen numbers, and neither
 * is a colour. This turns an index into the colour it stands for, which is
 * what a compositor wants.
 */
uint32_t emuvdi_palette_argb(int index);

/*
 * Memory below the four gigabyte line, which is where anything whose address
 * ends up in one of GEM's thirty two bit fields has to live - an object tree's
 * strings among them. See emuvdi/README.
 */
void *host_vdi_alloc(long size);
void host_vdi_free(void *block);

/* How large the screen the AES lays windows out on is */
int16_t emuvdi_screen_width();
int16_t emuvdi_screen_height();

/*
 * Draws part of an object tree, clipped to a rectangle. This is EmuTOS's own
 * object library doing the drawing - the boxes, the borders, the text and the
 * three dimensional edges of every GEM dialog ever written - which is the
 * reason for carrying it.
 *
 * The tree is one aes_tree_in built, so its pointers are addresses on this
 * side rather than in the machine.
 */
/*
 * Building an object tree where the AES can read it.
 *
 * An OBJECT is not the same shape on both sides. EmuTOS says a LONG is a
 * signed 32 bit word and writes it as long, which is four bytes on a 68000 and
 * eight here, so an OBJECT is 24 bytes in the machine and 32 in this program.
 * Nothing can be copied across as a block, and the layout is not something the
 * other side of this header should have to know: it says what the fields are
 * and this side puts them where they go.
 */
void *emuvdi_tree_alloc(int count);
void emuvdi_tree_free(void *tree);

void emuvdi_tree_set(void *tree, int index, int16_t next, int16_t head,
                     int16_t tail, uint16_t type, uint16_t flags,
                     uint16_t state, void *spec, int16_t x, int16_t y,
                     int16_t w, int16_t h);

/* The state is the one field the AES writes back: a pressed button comes back
 * selected, and a checked menu entry comes back checked */
uint16_t emuvdi_tree_state(void *tree, int index);

/* A TEDINFO, which the text kinds of object point at. The three strings are
 * the text, the template and the validation. */
void *emuvdi_tedinfo_alloc();
void emuvdi_tedinfo_set(void *ted, char *text, char *tmplt, char *valid,
                        const int16_t *words, int word_count);
char *emuvdi_tedinfo_text(void *ted);

void emuvdi_objc_draw(void *tree, int16_t start, int16_t depth,
                      int16_t x, int16_t y, int16_t w, int16_t h);

/*
 * Whether the VDI has a function for this opcode. Everything it does not is
 * either a call no driver ever implemented or one of the GDOS extensions,
 * which arrive on their own range well above the VDI proper.
 */
int emuvdi_implements(int16_t opcode);

#endif /* EMUVDI_H */
