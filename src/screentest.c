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

/*
 * Turning a display into a screen.
 *
 * Built for the host rather than for the emulated machine, and the reason is
 * the same one that made screen_from_display a function of its own: what a
 * compositor says is not something a test can arrange. A machine with two
 * displays answers differently from a machine with one and a build server
 * answers not at all, so the asking is checked by using it and the arithmetic
 * is checked here, where the numbers can simply be handed over.
 *
 * They are real numbers rather than round ones. 3456x2160 at a scale of two
 * and 3440x1440 at a scale of one are two displays on one desk, and between
 * them they cover the things that are easy to get wrong: a scale the
 * compositor applies as well as the one the emulator does, and a width that
 * does not divide into sixteen.
 *
 * The same desk answers the other half of this, which is how much of a
 * display a panel or a dock keeps - see insets below. That is worked out
 * backwards from how large a maximised window came out, so what a test has to
 * arrange is a size, and a size is the one thing it can.
 */

#include "screen.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int n;
static int fails;

static void check(long got, long want, const char *name)
{
    n++;
    if (got == want)
        printf("ok %d - %s\n", n, name);
    else
    {
        fails++;
        printf("not ok %d - %s (got %ld, want %ld)\n", n, name, got, want);
    }
}

/* A screen from a display, at a given TOSEMU_SCALE */
static void screen_at(int scale, int32_t pw, int32_t ph, int32_t out_scale,
                      int16_t *w, int16_t *h)
{
    char said[16];

    snprintf(said, sizeof said, "%d", scale);
    setenv("TOSEMU_SCALE", said, 1);

    screen_from_display(pw, ph, out_scale, w, h);
}

static void both(int scale, int32_t pw, int32_t ph, int32_t out_scale,
                 int want_w, int want_h, const char *what)
{
    char name[160];
    int16_t w, h;

    screen_at(scale, pw, ph, out_scale, &w, &h);

    snprintf(name, sizeof name, "%s is %d across", what, want_w);
    check(w, want_w, name);
    snprintf(name, sizeof name, "%s is %d down", what, want_h);
    check(h, want_h, name);
}

/* The two displays above, described the way the compositor describes them */
static const struct screen_display desk[] = {
    { 3456, 2160, 2 },      /* 1728x1080 to lay a window out in */
    { 3440, 1440, 1 },
};

/*
 * How much of a display the desktop kept, worked out from how large a
 * maximised window came out.
 *
 * Which display the window went to is not said, so the answer has to be found
 * by fitting: it is the one the maximised size fits inside with the least
 * left over. That is exact when there is one display and a guess when there
 * are two the same, and a guess is what the whole business is - a panel is on
 * every display of a desk that has one.
 */
static void insets(void)
{
    int32_t iw, ih;

    /* The one in front of you, with a panel forty tall along the bottom */
    screen_inset(desk, 2, 1728, 1040, &iw, &ih);
    check(iw, 0, "a panel along the bottom takes nothing across");
    check(ih, 40, "and forty down");

    /* And a dock down the side of the other one as well */
    screen_inset(desk, 2, 3372, 1400, &iw, &ih);
    check(iw, 68, "a dock down the side takes its width across");
    check(ih, 40, "and the panel is still there");

    /* A desktop that keeps nothing, which is a window as large as the display
     * and an inset of nothing */
    screen_inset(desk, 2, 1728, 1080, &iw, &ih);
    check(iw, 0, "a bare desktop keeps nothing across");
    check(ih, 0, "and nothing down");

    /* One display, which is the ordinary case and the one that is not a
     * guess: there is nowhere else the window can have gone */
    screen_inset(desk, 1, 1700, 1000, &iw, &ih);
    check(iw, 28, "with one display the answer is arithmetic");
    check(ih, 80, "in both directions");

    /*
     * A maximised size that fits inside neither, which is what a compositor
     * scaling by something other than a whole number reports: wl_output says
     * twice the pixels for a display shown at one and a half, so the display
     * looks smaller than the window it just offered. Nothing is taken off,
     * and the screen is the display as it was before any of this.
     */
    screen_inset(desk, 1, 2304, 1400, &iw, &ih);
    check(iw, 0, "a window larger than every display leaves the display alone");
    check(ih, 0, "in both directions");

    /* And the answers that are not answers */
    screen_inset(desk, 2, 0, 0, &iw, &ih);
    check(iw, 0, "a maximised size of nothing is not an answer");
    check(ih, 0, "in both directions");

    screen_inset(desk, 0, 1728, 1040, &iw, &ih);
    check(iw, 0, "and neither is a maximised size with no display to fit it");
    check(ih, 0, "in both directions");
}

int main(void)
{
    int16_t w, h;

    /*
     * The display that scales itself. It reports twice the pixels it shows a
     * window in, so a screen worked out from the pixels alone would come out
     * twice the size it should and the window would not fit on the glass.
     */
    both(1, 3456, 2160, 2, 1728, 1080, "a 3456x2160 display at scale two, 1:1");
    both(2, 3456, 2160, 2,  864,  540, "the same at two");
    both(3, 3456, 2160, 2,  576,  360, "the same at three");

    /*
     * The one that does not, and whose width is the interesting case: 3440
     * divided by three is 1146, and a row of a surface is a whole number of
     * words, so what comes back is the multiple of sixteen below it.
     */
    both(1, 3440, 1440, 1, 3440, 1440, "a 3440x1440 display, 1:1");
    both(2, 3440, 1440, 1, 1712,  720, "the same at two");
    both(3, 3440, 1440, 1, 1136,  480, "the same at three, rounded down");

    /* Which is the whole of the rounding: 1146 would have the VDI and the
     * surface disagree about where a row ends */
    screen_at(3, 3440, 1440, 1, &w, &h);
    check(w % 16, 0, "and a screen is always a whole number of words across");

    /* A scale that was never asked for is three, which is what a window
     * magnifies by unless it is told otherwise */
    unsetenv("TOSEMU_SCALE");
    screen_from_display(3440, 1440, 1, &w, &h);
    check(w, 1136, "with nothing said, the scale is three");

    /* And one that makes no sense is not obeyed */
    both(0, 3440, 1440, 1, 1136, 480, "a scale of nought");

    /*
     * A display too small to hold a GEM screen, which is a thing that happens
     * to anyone who asks for a large scale. The AES has dialogs of its own and
     * they have to go somewhere, so this stops rather than following the
     * arithmetic down.
     */
    both(16, 640, 400, 1, SCREEN_MIN_W, SCREEN_MIN_H, "a display divided away");
    both(3, 320, 200, 1, SCREEN_MIN_W, SCREEN_MIN_H, "a tiny display");

    /* A compositor that says nothing useful rather than nothing at all */
    both(1, 0, 0, 1, SCREEN_MIN_W, SCREEN_MIN_H, "a display of no size");

    /*
     * And one larger than a GEM coordinate. Everything the AES measures is a
     * signed word, so a screen it cannot describe the far corner of is worse
     * than a smaller one.
     */
    screen_at(1, 40000, 40000, 1, &w, &h);
    check(w <= 32767 && w > 0, 1, "an enormous display is cut to a word");
    check(h <= 32767 && h > 0, 1, "in both directions");
    check(w % 16, 0, "and is still a whole number of words across");

    insets();

    printf("1..%d\n", n);

    return fails ? 1 : 0;
}
