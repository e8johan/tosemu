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

#ifndef SURFACE_H
#define SURFACE_H

#include <stdint.h>

/*
 * Somewhere to draw.
 *
 * A surface is an Atari bitmap: the pixels of a row are spread across as many
 * planes as it has colours for, a word of each plane in turn, and the highest
 * bit of a word is the leftmost pixel. That is the shape the VDI draws in, and
 * keeping it means the VDI is EmuTOS's code rather than a rewrite of it.
 *
 * The words are in host byte order rather than the 68000's. Nothing in the
 * emulated machine can see a surface - an application reaches one only through
 * the VDI - so there is no reason to pay for the swap on every pixel, and the
 * VDI reads plane memory a word at a time, so it never notices.
 *
 * Three coordinate spaces meet around this and are worth keeping apart:
 *
 *   surface pixels    what the VDI and the application draw in
 *   buffer pixels     surface pixels times the scale a window is shown at
 *   the compositor's  what Wayland deals in
 *
 * Only the first exists yet.
 */

struct surface;

/* Makes a surface, cleared, or returns null if there is no room for it */
struct surface *surface_create(uint16_t width, uint16_t height,
                               uint16_t planes);
void surface_free(struct surface *s);

/*
 * Says where the VDI draws from now on. Everything the VDI does is to whatever
 * was selected last, which is how the one set of drawing code serves every
 * window: the caller picks the surface, then draws.
 */
void surface_select(struct surface *s);
struct surface *surface_selected();

uint16_t surface_width(const struct surface *s);
uint16_t surface_height(const struct surface *s);
uint16_t surface_planes(const struct surface *s);

/*
 * The colour index at a pixel, gathered from the planes.
 *
 * This is the slow way round on purpose. It is for tests and for whatever
 * turns a surface into something a compositor can show, neither of which is
 * in a hurry, and both of which want to be reading the memory rather than
 * asking the VDI what it thinks it drew.
 */
uint16_t surface_pixel(const struct surface *s, uint16_t x, uint16_t y);

/*
 * Copies one surface over another of the same shape. A dialog starts as what
 * was on the screen behind it, so that the parts of its window either side of
 * the dialog itself show what they would have shown.
 */
void surface_copy(struct surface *dst, const struct surface *src);

/*
 * Writes the surface out as a portable pixmap, for looking at what was drawn
 * without a compositor in the way. The colours come from the palette, so what
 * lands in the file is what would land on a screen.
 */
int surface_write_ppm(const struct surface *s, const char *path);

#endif /* SURFACE_H */
