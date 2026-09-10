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

#ifndef SCREEN_H
#define SCREEN_H

#include <stdint.h>

/*
 * Which screen the machine has.
 *
 * Both the emulator and the daemon ask, and they have to be told the same
 * thing: the screen has to be one screen for everything sharing it, so this is
 * the one place that reads the environment and works out what it means. The
 * daemon is what decides between them when there is one - see aesproto.h - and
 * this is what it decides with.
 */
void screen_mode(int16_t *width, int16_t *height, int16_t *planes);

/*
 * How much larger than an ST pixel one on the desktop is, from TOSEMU_SCALE.
 *
 * It is here rather than in gfx.c because it is now two things at once: what a
 * window magnifies its screen by, and what the size of the display is divided
 * by to arrive at a screen that fills it. Those have to be the same number or
 * the window does not come out the size of the display.
 */
int screen_scale(void);

/*
 * The arithmetic that turns a display into a screen, without the asking.
 *
 * Apart from the asking so that it can be checked: what a compositor says is
 * not something a test can arrange, and this is the part with the divisions in
 * it. Sizes in physical pixels, scale as the compositor reports it.
 */
void screen_from_display(int32_t pixels_w, int32_t pixels_h, int32_t out_scale,
                         int16_t *width, int16_t *height);

/* One display as the compositor described it: the mode it is in, in its own
 * pixels, and how many of those it shows a window's pixel in */
struct screen_display {
    int32_t width, height;
    int32_t scale;
};

/*
 * How much of a display the desktop keeps for itself, worked out backwards
 * from how large a maximised window came out.
 *
 * There is no asking for this. Wayland tells a client how large the displays
 * are and never what is on them, so a panel, a dock and a frame the desktop
 * draws are all invisible until something is maximised and comes back smaller
 * than the display it is on - and which display that was is not said either,
 * so it is the one whose size the answer fits inside with the least left over.
 *
 * The maximised size is in the pixels a window is measured in; so is what
 * comes back, and both are nought when nothing fits.
 */
void screen_inset(const struct screen_display *displays, int count,
                  int32_t maximized_w, int32_t maximized_h,
                  int32_t *inset_w, int32_t *inset_h);

/* The smallest screen worth handing to GEM, which is the smallest one an
 * Atari had. Below this the AES's own dialogs have nowhere to go. */
#define SCREEN_MIN_W (320)
#define SCREEN_MIN_H (200)

#endif
