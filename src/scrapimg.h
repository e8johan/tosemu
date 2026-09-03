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

#ifndef SCRAPIMG_H
#define SCRAPIMG_H

#include <stddef.h>
#include <stdint.h>

/*
 * The picture half of the scrap.
 *
 * GEM called it SCRAP.IMG and meant a GEM Raster file: a short header saying
 * how large the picture is and how many planes it has, then the rows, run
 * encoded. A desktop means a PNG. Neither knows anything about the other, and
 * the two disagree about more than text does - a picture on an ST is an index
 * into the machine's palette and a picture on a desktop is three bytes of
 * colour, so coming in there is a decision to make about every pixel.
 *
 * The palette is passed in rather than read here so that this stays a pair of
 * functions on buffers, testable without a machine to have a palette. It is
 * one entry per colour index, 0x00RRGGBB.
 */

/*
 * A GEM picture as a PNG. Allocates; the caller frees.
 *
 * Answers 0 for anything it cannot read, which includes a file that is not an
 * IMG at all - what is in the scrap directory is whatever an application put
 * there, and being handed something unexpected is not an error worth stopping
 * for.
 */
int scrap_img_to_png(const void *img, size_t img_length,
                     const uint32_t *palette, int colours,
                     void **png, size_t *png_length);

/*
 * And a PNG as a GEM picture, in as many planes as the screen has.
 *
 * The colours have to go somewhere: a photograph has more of them than an ST
 * has altogether, so each pixel becomes the nearest the palette can manage and
 * what was lost is carried into its neighbours. Without that a photograph
 * arrives as a few flat blocks, which looks like a fault rather than like the
 * sixteen colours it really is.
 */
int scrap_img_from_png(const void *png, size_t png_length,
                       const uint32_t *palette, int colours, int planes,
                       void **img, size_t *img_length);

/*
 * Whether pictures can be converted at all.
 *
 * libpng is looked for rather than required, so a machine without it gets a
 * session with a working text clipboard rather than a build failure. When it
 * is missing both of the above answer 0 and the scrap goes on holding pictures
 * for GEM applications only.
 */
int scrap_img_available(void);

#endif /* SCRAPIMG_H */
