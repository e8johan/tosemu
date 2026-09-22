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
 * This is the slow way round on purpose. It is for tests and for anything else
 * that wants one pixel and is not in a hurry, and it reads the memory rather
 * than asking the VDI what it thinks it drew.
 */
uint16_t surface_pixel(const struct surface *s, uint16_t x, uint16_t y);

/*
 * And the colour indices of a run of pixels along one row, into a byte each.
 *
 * The same answer as surface_pixel for each of them, and the reason to have it
 * is that it is not the same work. Finding one pixel means working out which
 * row, which group of plane words, and which bit, and then reading a word of
 * every plane - and the pixel beside it is in the same words, so the next one
 * works all of that out again and reads them again. Sixteen consecutive pixels
 * are one group of words: taken together the addressing is paid once for the
 * sixteen, the words are read once, and what is left per pixel is a shift.
 *
 * Which is what showing a screen does, every pixel of it, as often as the
 * display refreshes. On a screen the size of a desktop that is five million of
 * them a frame, and a pixel at a time it was seven milliseconds of work - see
 * window_magnify, which is what this is for.
 *
 * Anything outside the surface reads as nought, which is what surface_pixel
 * answers for it too.
 */
void surface_row(const struct surface *s, uint16_t x, uint16_t y,
                 uint16_t count, uint8_t *into);

/*
 * Copies one surface over another of the same shape. A dialog starts as what
 * was on the screen behind it, so that the parts of its window either side of
 * the dialog itself show what they would have shown.
 */
void surface_copy(struct surface *dst, const struct surface *src);

/*
 * And one rectangle of it, to the pixel, into the same place on another
 * surface of the same shape.
 *
 * What a window drew is kept on a surface of the window's own, and what an
 * Atari would have shown is those surfaces laid on the screen one over another,
 * back to front. This is the laying - see gem_composite. Pixels either side of
 * the rectangle in the same plane words are left as they were, which is what
 * makes it a rectangle and not a rectangle rounded out to sixteen pixels.
 */
void surface_copy_rect(struct surface *dst, const struct surface *src,
                       int x, int y, int w, int h);

/*
 * What has been drawn in since anyone last looked, and taking it away.
 *
 * Showing a surface means converting it, and converting all of one every time
 * anybody might be looking is most of what an emulator with a window costs.
 * Nearly always almost none of it changed - an application draws in one
 * rectangle and leaves the rest alone, and an application that is only asking
 * what has happened draws in none of it at all.
 *
 * So whatever draws says where. It is one rectangle round everything since the
 * last take rather than a list of them, which is the cheapest thing to carry
 * and is what GEM's own way of drawing suits: the AES sets a clipping
 * rectangle and draws inside it, and that rectangle is the damage.
 *
 * It is always allowed to say more than happened, and never less. A caller
 * that cannot say where it drew says the whole surface - that is what a
 * rectangle larger than one is clamped to - and the cost of that is a picture
 * converted that need not have been.
 *
 * surface_damage_take is how the other end reads it: it answers 0 when
 * nothing was drawn, and otherwise gives the rectangle and forgets it. Whoever
 * takes it owes the drawing to whoever is looking, so a surface shown in two
 * windows wants taking once and giving to both.
 */
void surface_damage(struct surface *s, int x, int y, int w, int h);
int surface_damage_take(struct surface *s, int16_t *x, int16_t *y,
                        int16_t *w, int16_t *h);

/*
 * Fills a surface from a picture as it sits in the machine's memory - an
 * Atari screen, which is this same shape with the bytes of every word the
 * 68000's way round - and damages the rows that changed. Answers whether any
 * did.
 *
 * The bytes are the whole surface's worth, a row of every plane after
 * another. This is how a picture a program drew for itself, without the VDI,
 * is brought across to be shown.
 */
int surface_load_atari(struct surface *s, const uint8_t *bytes);

/*
 * Writes the surface out as a portable pixmap, for looking at what was drawn
 * without a compositor in the way. The colours come from the palette, so what
 * lands in the file is what would land on a screen.
 */
int surface_write_ppm(const struct surface *s, const char *path);

#endif /* SURFACE_H */
