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

/* The GEM trap and its parameter blocks.
 *
 * GEMDOS, BIOS and XBIOS take their arguments on the stack. GEM does not, so
 * the way arguments arrive is worth a test of its own before anything is built
 * on top of it. The first half of this builds the parameter block by hand,
 * which is both the check and the documentation of what the trap expects. The
 * second half goes through the bindings, because that is how an application
 * will actually arrive.
 *
 * A test that stops the emulator prints nothing further, so the count at the
 * end is what says the whole file ran rather than only the part before an
 * unimplemented call.
 */

#include <stdio.h>
#include <gem.h>

/* What appl_init reports for an AES that has only just been started */
#define WANT_VERSION (0x0140)
#define WANT_APPS    (1)

static int n;

static void check(long got, long want, const char *name)
{
    n++;
    if (got == want)
        printf("ok %d - %s\n", n, name);
    else
        printf("not ok %d - %s (got %ld, want %ld)\n", n, name, got, want);
}

/* The six arrays of an AES parameter block, at the sizes the AES bindings
 * reserve for them */
static short control[5];
static short global[15];
static short intin[16];
static short intout[7];
static long addrin[3];
static long addrout[1];

static AESPB pb = { control, global, intin, intout, addrin, addrout };

/*
 * An AES call as the trap sees it: the function number and the length of each
 * argument array go in the control array, and the answer comes back in
 * intout[0].
 */
static short call_aes(short opcode, short n_intin, short n_intout,
                      short n_addrin, short n_addrout)
{
    control[0] = opcode;
    control[1] = n_intin;
    control[2] = n_intout;
    control[3] = n_addrin;
    control[4] = n_addrout;

    intout[0] = -1; /* So that an answer is told apart from no answer */

    aes(&pb);

    return intout[0];
}

int main(int argc, char **argv)
{
    short id;
    int i;

    /* An identifier that has not been handed out yet, so that the array being
     * written is told apart from it happening to hold the right thing */
    for (i = 0; i < 15; i++)
        global[i] = -2;

    /* 10 appl_init, by hand */
    id = call_aes(10, 0, 1, 0, 0);
    check(id > 0, 1, "appl_init answers in intout with an identifier");
    check(global[0], WANT_VERSION, "appl_init reports the AES version");
    check(global[1], WANT_APPS, "appl_init reports how many applications fit");
    check(global[2], id, "appl_init puts the identifier in the global array");
    check(global[5], 0, "appl_init clears the resource tree pointer");
    check(global[6], 0, "appl_init clears the resource tree pointer");

    /* An AES of this version does not own the last two words of the global
     * array, so an application built against an older binding does not have
     * them written over */
    check(global[13], -2, "appl_init leaves the AES 4 globals alone");
    check(global[14], -2, "appl_init leaves the AES 4 globals alone");

    /* 19 appl_exit, by hand */
    check(call_aes(19, 0, 1, 0, 0), 1, "appl_exit answers success");

    /* 17 appl_yield - stubbed, because an application yielding when it is the
     * only one has already had its turn */
    check(call_aes(10, 0, 1, 0, 0) > 0, 1, "appl_init again for appl_yield");
    check(call_aes(17, 0, 1, 0, 0), 1, "appl_yield answers success");
    check(call_aes(19, 0, 1, 0, 0), 1, "appl_exit after appl_yield");

    /* And now the same thing the way an application does it */
    id = appl_init();
    check(id > 0, 1, "appl_init through the binding gives an identifier");
    check(gl_ap_version, WANT_VERSION, "the binding sees the AES version");
    check(_AESapid, id, "the binding sees its own identifier");
    check(appl_exit(), 1, "appl_exit through the binding succeeds");

    /*
     * How an application is meant to reach the screen: appl_init, then
     * graf_handle for the workstation the AES already has open, then a virtual
     * workstation of its own against it. No physical workstation is opened
     * anywhere, which is the point.
     */
    {
        short wchar, hchar, wbox, hbox, phys, vwk;
        short work_in[11], work_out[57];
        short pxy[4], pel, index;
        short j;

        id = appl_init();
        check(id > 0, 1, "appl_init before asking for a workstation");

        phys = graf_handle(&wchar, &hchar, &wbox, &hbox);
        check(phys > 0, 1, "graf_handle gives the AES's workstation");
        check(wchar > 0 && hchar > 0, 1, "graf_handle reports a character size");
        check(wbox >= wchar && hbox >= hchar, 1,
              "a box is at least as large as the character in it");

        for (j = 0; j < 10; j++)
            work_in[j] = 1;
        work_in[10] = 2;

        vwk = phys;
        v_opnvwk(work_in, &vwk, work_out);
        check(vwk > 0, 1, "v_opnvwk opens a workstation against it");
        check(vwk != phys, 1, "and it is not the AES's own");

        /* Draw in it, and read the pixel back */
        vswr_mode(vwk, MD_REPLACE);
        vsf_interior(vwk, FIS_SOLID);
        vsf_color(vwk, 1);
        pxy[0] = 4; pxy[1] = 4; pxy[2] = 12; pxy[3] = 12;
        v_bar(vwk, pxy);

        v_get_pixel(vwk, 8, 8, &pel, &index);
        check(index, 1, "and drawing in it reaches the screen");

        v_clsvwk(vwk);
        check(appl_exit(), 1, "appl_exit after closing the workstation");
    }

    /*
     * Waiting. An application spends nearly all its life in evnt_multi, and
     * the two sources that exist so far are the timer and its own messages.
     *
     * These go through the parameter block rather than the bindings, because
     * evnt_multi takes twenty three arguments and the bindings disagree about
     * how many, which is a question about gemlib rather than about the AES.
     */
    {
        static short msg[8], got[8];
        short which;

        id = call_aes(10, 0, 1, 0, 0);      /* appl_init */

        /* A message to itself, which is how an application drives its own
         * redraws, and how the AES will reach it once there is more of one */
        for (i = 0; i < 8; i++)
            msg[i] = (short)(100 + i);

        intin[0] = id;                      /* to */
        intin[1] = 16;                      /* a message is eight words */
        addrin[0] = (long)msg;
        check(call_aes(12, 2, 1, 1, 0), 1, "appl_write takes a message");

        for (i = 0; i < 8; i++)
            got[i] = 0;
        addrin[0] = (long)got;
        check(call_aes(23, 0, 1, 1, 0), 1, "evnt_mesag gives one back");
        check(got[0], 100, "and it is the message that was sent");
        check(got[7], 107, "all eight words of it");

        /* evnt_multi with a message already waiting, so what comes back is
         * the message and not the timer */
        intin[0] = id;
        intin[1] = 16;
        addrin[0] = (long)msg;
        call_aes(12, 2, 1, 1, 0);

        for (i = 0; i < 16; i++)
            intin[i] = 0;
        intin[0] = MU_MESAG|MU_TIMER;
        intin[14] = 1000;                   /* a second, which it will not use */
        addrin[0] = (long)got;
        which = call_aes(25, 16, 7, 1, 0);
        check(which & MU_MESAG, MU_MESAG, "evnt_multi reports the message");
        check(which & MU_TIMER, 0, "and not the timer it did not wait for");

        /* And with nothing waiting, the timer is what happens */
        for (i = 0; i < 16; i++)
            intin[i] = 0;
        intin[0] = MU_MESAG|MU_TIMER;
        intin[14] = 50;                     /* short enough for a test */
        addrin[0] = (long)got;
        which = call_aes(25, 16, 7, 1, 0);
        check(which & MU_TIMER, MU_TIMER, "evnt_multi times out when nothing comes");
        check(which & MU_MESAG, 0, "with no message to report");

        call_aes(19, 0, 1, 0, 0);           /* appl_exit */
    }

    /*
     * Windows. The AES owns where one is and what is drawn around it, and the
     * application owns what is inside; wind_calc is how the two are converted
     * between, and getting that wrong is how a program draws over its own
     * title bar.
     */
    {
        short handle, wx, wy, ww, wh;

        id = call_aes(10, 0, 1, 0, 0);      /* appl_init */

        /* How large the desktop is, which is what an application asks before
         * deciding where to put anything */
        intin[0] = 0;                       /* the desktop */
        intin[1] = 4;                       /* WF_WORKXYWH */
        call_aes(104, 6, 5, 0, 0);
        check(intout[3] > 0 && intout[4] > 0, 1, "wind_get sizes the desktop");

        /* A window with a title, a closer and a mover */
        intin[0] = 0x0001|0x0002|0x0008;
        intin[1] = 0; intin[2] = 0; intin[3] = 320; intin[4] = 200;
        handle = call_aes(100, 5, 1, 0, 0); /* wind_create */
        check(handle > 0, 1, "wind_create gives a handle");

        intin[0] = handle;
        intin[1] = 20; intin[2] = 20; intin[3] = 200; intin[4] = 100;
        check(call_aes(101, 5, 1, 0, 0), 1, "wind_open opens it");

        /* The whole of it is what it was opened with */
        intin[0] = handle; intin[1] = 5;    /* WF_CURRXYWH */
        call_aes(104, 6, 5, 0, 0);
        check(intout[1], 20, "wind_get gives back where it was put");
        check(intout[3], 200, "and how wide it was made");

        /* The part to draw in is smaller, by the title bar */
        intin[0] = handle; intin[1] = 4;    /* WF_WORKXYWH */
        call_aes(104, 6, 5, 0, 0);
        wx = intout[1]; wy = intout[2]; ww = intout[3]; wh = intout[4];
        check(wy > 20, 1, "the work area starts below the title bar");
        check(wh < 100, 1, "and is shorter than the window by as much");
        check(wx, 20, "with nothing taken off the left");
        check(ww, 200, "or the right, there being no slider");

        /* wind_calc has to agree with wind_get, in both directions */
        intin[0] = 1;                       /* border to work */
        intin[1] = 0x0001|0x0002|0x0008;
        intin[2] = 20; intin[3] = 20; intin[4] = 200; intin[5] = 100;
        call_aes(108, 6, 5, 0, 0);
        check(intout[2], wy, "wind_calc agrees about where the work area is");
        check(intout[4], wh, "and about how tall");

        intin[0] = 0;                       /* work to border */
        intin[1] = 0x0001|0x0002|0x0008;
        intin[2] = wx; intin[3] = wy; intin[4] = ww; intin[5] = wh;
        call_aes(108, 6, 5, 0, 0);
        check(intout[2], 20, "and converts back to where the window is");
        check(intout[4], 100, "and how tall it is");

        /* Closing is not deleting: a closed window can be opened again */
        intin[0] = handle;
        check(call_aes(102, 1, 1, 0, 0), 1, "wind_close closes it");
        intin[0] = handle;
        check(call_aes(103, 1, 1, 0, 0), 1, "wind_delete lets it go");

        intin[0] = handle;
        check(call_aes(104, 6, 5, 0, 0), 0, "and then it is not a window");

        call_aes(19, 0, 1, 0, 0);           /* appl_exit */
    }

    /* The VDI has its own parameter block, a different shape from the AES one.
     * v_updwk is a call with nothing to do rather than one that is missing, so
     * it has to come back rather than stop the emulator. */
    v_updwk(1);
    check(1, 1, "v_updwk returns through the VDI trap");

    printf("1..%d\n", n);

    return 0;
}
