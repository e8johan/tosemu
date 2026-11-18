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
 * A dialog that ends without FMD_FINISH.
 *
 * EWeb's alerts save the screen under the box and put it back themselves, so
 * they never ask for the redraw FMD_FINISH is: BEG_MCTRL, FMD_START, a loop
 * of their own, the copy back, END_MCTRL. Here a dialog is a window of the
 * compositor's, and one nobody finished stayed in front of the program for
 * good. Giving the mouse back is what ends it now.
 *
 * Whether a dialog is up is asked the way c-frame asks it: while one is,
 * drawing lands on a surface of its own and reading a pixel reads that, and
 * once it is gone the same pixel is the screen's, where nothing was drawn.
 */

#include <stdio.h>
#include <gem.h>

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

static void update(short mode)
{
    intin[0] = mode;
    call_aes(107, 1, 1, 0, 0);                       /* wind_update */
}

static void dial(short what)
{
    intin[0] = what;
    intin[5] = 100; intin[6] = 100; intin[7] = 100; intin[8] = 50;
    call_aes(51, 9, 1, 0, 0);                        /* form_dial */
}

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

static short handle;

/* Fills the box the dialog is in, on whatever is being drawn into */
static void fill(void)
{
    short box[4] = { 110, 110, 180, 140 };

    vsf_interior(handle, 1);
    vsf_color(handle, 1);
    v_bar(handle, box);
}

static int ink(void)
{
    short value, index;

    v_get_pixel(handle, 150, 125, &value, &index);
    return index != 0;
}

int main(void)
{
    short work_in[11], work_out[57], wchar, hchar, wbox, hbox, i;

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

    /* EWeb's way */
    update(BEG_UPDATE);
    update(BEG_MCTRL);
    dial(FMD_START);
    fill();
    check(ink(), 1, "a dialog put up under the mouse draws on its own surface");
    update(END_MCTRL);
    check(ink(), 0, "and giving the mouse back takes it away without FMD_FINISH");
    update(END_UPDATE);

    /* The usual way, which must come out the same */
    update(BEG_MCTRL);
    dial(FMD_START);
    fill();
    dial(FMD_FINISH);
    check(ink(), 0, "FMD_FINISH ends one as it always did");
    update(END_MCTRL);

    /* A dialog put up before the mouse was taken is not the bracket's */
    dial(FMD_START);
    fill();
    update(BEG_MCTRL | 0x100);
    update(END_MCTRL);
    check(ink(), 1, "a mouse taken and given back inside a dialog leaves it up");
    dial(FMD_FINISH);
    check(ink(), 0, "until it is finished");

    v_clsvwk(handle);
    appl_exit();

    printf("1..%d\n", n);

    return fails ? 1 : 0;
}
