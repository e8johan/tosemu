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
 * The two strips along the top of a window: the title bar and the information
 * line.
 *
 * Both are the AES's to draw and both take a strip off the work area whether
 * or not anything is drawn in them, so a strip that is accounted for and left
 * blank is a window with a band of nothing across it - which is what an
 * information line was until it was drawn, and what a title bar becomes when
 * the desktop is asked to draw one of its own instead.
 *
 * So what is checked here is ink: whether there is any in each strip, and
 * where. The title bar is there when GEM is drawing the frame and not there
 * when the desktop is, which is the decorations setting and is what the
 * argument to this program says to expect. The information line is there
 * either way, because nothing on a desktop does that job.
 *
 * The last check is the one that matters most in use. The line is a box with
 * words in it, and a box that is not repainted before the words go in shows
 * the old ones underneath - so a long line replaced by a short one has to
 * leave nothing of the long one behind.
 *
 * A test that stops the emulator prints nothing further, so the count at the
 * end is what says the whole file ran.
 */

#include <stdio.h>
#include <string.h>
#include <gem.h>

/* What a window is made of, http://toshyp.atari.org/en/008009.html */
#define W_HAS_NAME    (0x0001)
#define W_HAS_CLOSER  (0x0002)
#define W_HAS_FULLER  (0x0004)
#define W_HAS_MOVER   (0x0008)
#define W_HAS_INFO    (0x0010)
#define W_HAS_SIZER   (0x0020)

/* What wind_get and wind_set are asked about. gemlib names these already, so
 * only the numbers are wanted here, and it names them the same way. */

static short control[5], global[15], intin[16], intout[7];
static long addrin[3], addrout[1];
static AESPB pb = { control, global, intin, intout, addrin, addrout };

static short call_aes(short op, short ni, short no, short ai, short ao)
{
    control[0] = op; control[1] = ni; control[2] = no;
    control[3] = ai; control[4] = ao;
    aes(&pb);
    return intout[0];
}

/* Setting one of the two strings, which is two words of an address rather than
 * an entry in addrin: wind_set takes everything in intin */
static void set_words(short window, short what, const char *text)
{
    intin[0] = window;
    intin[1] = what;
    intin[2] = (short)(((long)text) >> 16);
    intin[3] = (short)(((long)text) & 0xffff);
    intin[4] = 0;
    intin[5] = 0;
    call_aes(105, 6, 1, 0, 0);                       /* wind_set */
}

static int n;

static void check(long got, long want, const char *name)
{
    n++;
    if (got == want)
        printf("ok %d - %s\n", n, name);
    else
        printf("not ok %d - %s (got %ld, want %ld)\n", n, name, got, want);
}

/* Whether anything was drawn in a band of the screen, which is the only
 * question worth asking about a strip whose contents are words */
static int ink_in(short handle, short x, short y, short wide, short high)
{
    short value, index;
    short i, j;

    for (j = 0; j < high; j++)
        for (i = 0; i < wide; i++)
        {
            v_get_pixel(handle, (short)(x + i), (short)(y + j),
                        &value, &index);
            if (index != 0)
                return 1;
        }

    return 0;
}

int main(int argc, char **argv)
{
    short wchar, hchar, wbox, hbox;
    short work_in[11], work_out[57];
    short handle;
    short i;
    short window;
    short wx, wy;
    short wide = 400, high = 200;
    short inside_x, inside_w, inside_y, inside_h;
    short far_x;
    int atari_frame = (argc < 2) || strcmp(argv[1], "desktop") != 0;

    if (appl_init() < 0)
    {
        printf("Bail out! - no AES to talk to\n");
        return 1;
    }

    handle = graf_handle(&wchar, &hchar, &wbox, &hbox);
    for (i = 0; i < 10; i++)
        work_in[i] = 1;
    work_in[10] = 2;
    v_opnvwk(work_in, &handle, work_out);

    /* Below where a menu bar would go, so that nothing else has drawn where
     * these checks look */
    wx = 0;
    wy = hbox;

    intin[0] = W_HAS_NAME|W_HAS_CLOSER|W_HAS_FULLER|W_HAS_MOVER
             |W_HAS_INFO|W_HAS_SIZER;
    intin[1] = wx; intin[2] = wy; intin[3] = wide; intin[4] = high;
    window = call_aes(100, 5, 1, 0, 0);              /* wind_create */

    check(window > 0, 1, "a window with both strips was created");

    intin[0] = window;
    intin[1] = wx; intin[2] = wy; intin[3] = wide; intin[4] = high;
    call_aes(101, 5, 1, 0, 0);                       /* wind_open */

    /*
     * Where the work area starts, which is what says the two strips were
     * accounted for. It is a box below the title bar, which is itself a box
     * below the top of the window.
     */
    intin[0] = window;
    intin[1] = WF_WORKXYWH;
    call_aes(104, 6, 5, 0, 0);                       /* wind_get */

    check(intout[2], (short)(wy + 2 * hbox),
          "the work area starts below both strips");

    set_words(window, WF_NAME, "Title");
    set_words(window, WF_INFO, "Information");

    /*
     * Where to look in a strip: inside it, and away from both ends.
     *
     * A box has a border and so does the window round it, and a border is ink
     * whether or not anything was drawn between the two of them; the right
     * hand end of every strip has the scroll bar coming down past it. Either
     * would answer the question before it was asked.
     */
    inside_x = (short)(wx + 2);
    inside_w = (short)(22 * wchar);
    inside_h = (short)(hbox - 4);

    check(inside_x + inside_w < wide - wbox, 1,
          "there is room to look between the border and the scroll bar");

    /*
     * The title bar. GEM's own is drawn into the screen; the desktop's is not
     * ours to draw and the strip is not part of what is shown, so nothing at
     * all goes there.
     */
    check(ink_in(handle, inside_x, (short)(wy + 2), inside_w, inside_h),
          atari_frame ? 1 : 0,
          atari_frame ? "the title bar is drawn"
                      : "the title bar is left to the desktop");

    /* The information line, which is drawn either way: nothing a desktop puts
     * round a window says what the application wanted said */
    inside_y = (short)(wy + hbox + 2);

    check(ink_in(handle, inside_x, inside_y, inside_w, inside_h), 1,
          "the information line is drawn");

    /*
     * And a long line replaced by a short one. The far end is chosen past the
     * end of the short line and inside the long one, so it has ink before and
     * has to have none after: an information line that is not repainted first
     * shows the tail of whatever it said last.
     */
    far_x = (short)(inside_x + 20 * wchar);

    set_words(window, WF_INFO, "XXXXXXXXXXXXXXXXXXXXXXXXXXXXXX");

    check(ink_in(handle, far_x, inside_y, wchar, inside_h), 1,
          "a long information line reaches the far end of the strip");

    set_words(window, WF_INFO, "ok");

    check(ink_in(handle, far_x, inside_y, wchar, inside_h), 0,
          "and a short one afterwards leaves none of it behind");

    printf("1..%d\n", n);

    intin[0] = window;
    call_aes(102, 1, 1, 0, 0);                       /* wind_close */
    intin[0] = window;
    call_aes(103, 1, 1, 0, 0);                       /* wind_delete */

    v_clsvwk(handle);
    appl_exit();

    return 0;
}
