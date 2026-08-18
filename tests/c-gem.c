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
#include <string.h>
#include <osbind.h>
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

    /*
     * An object tree, drawn by the AES.
     *
     * The tree is in this program's memory, where the AES cannot read it: an
     * OBJECT is 24 bytes here and a different size on the other side of the
     * trap, its words are the other way round, and ob_spec holds addresses
     * that mean nothing there. So it is copied across, and what is checked
     * here is that what comes out the far end is the dialog that went in.
     */
    {
        static OBJECT dlg[3];
        short pel, ink, wchar, hchar, wbox, hbox, phys, vwk, j;
        short work_in2[11], work_out2[57];

        id = call_aes(10, 0, 1, 0, 0);      /* appl_init */

        /* Somewhere to read the result back from */
        phys = graf_handle(&wchar, &hchar, &wbox, &hbox);
        for (j = 0; j < 10; j++)
            work_in2[j] = 1;
        work_in2[10] = 2;
        vwk = phys;
        v_opnvwk(work_in2, &vwk, work_out2);

        dlg[0].ob_next = -1; dlg[0].ob_head = 1; dlg[0].ob_tail = 2;
        dlg[0].ob_type = 20;                /* G_BOX */
        dlg[0].ob_flags = 0; dlg[0].ob_state = 0;
        dlg[0].ob_spec.index = 0x00021100L; /* two thick, black on white */
        dlg[0].ob_x = 40; dlg[0].ob_y = 40;
        dlg[0].ob_width = 120; dlg[0].ob_height = 60;

        dlg[1].ob_next = 2; dlg[1].ob_head = -1; dlg[1].ob_tail = -1;
        dlg[1].ob_type = 28;                /* G_STRING */
        dlg[1].ob_flags = 0; dlg[1].ob_state = 0;
        dlg[1].ob_spec.free_string = "Hi";
        dlg[1].ob_x = 8; dlg[1].ob_y = 8;
        dlg[1].ob_width = 16; dlg[1].ob_height = 8;

        dlg[2].ob_next = 0; dlg[2].ob_head = -1; dlg[2].ob_tail = -1;
        dlg[2].ob_type = 26;                /* G_BUTTON */
        dlg[2].ob_flags = 0x0021;           /* SELECTABLE and last */
        dlg[2].ob_state = 0;
        dlg[2].ob_spec.free_string = "OK";
        dlg[2].ob_x = 40; dlg[2].ob_y = 36;
        dlg[2].ob_width = 40; dlg[2].ob_height = 16;

        intin[0] = 0; intin[1] = 8;
        intin[2] = 0; intin[3] = 0; intin[4] = 320; intin[5] = 200;
        addrin[0] = (long)dlg;
        check(call_aes(42, 6, 1, 1, 0), 1, "objc_draw draws a tree");

        /*
         * The border is two pixels of black at the box's edge, and the inside
         * of it is clear. Both halves matter: a border drawn seventeen pixels
         * thick also puts black at the edge, and only the second check tells
         * the two apart.
         */
        v_get_pixel(vwk, 40, 40, &pel, &ink);
        check(ink, 1, "the border is drawn at the box's edge");
        v_get_pixel(vwk, 41, 41, &pel, &ink);
        check(ink, 1, "and is two pixels thick");
        v_get_pixel(vwk, 44, 44, &pel, &ink);
        check(ink, 0, "with nothing painted inside it");

        /*
         * The button, which is a child and so is placed relative to the box.
         * Its own border is drawn just inside its rectangle rather than on it,
         * so what is checked is that something of it is there rather than
         * exactly which pixel.
         */
        {
            short found = 0, bx, by;

            for (by = 76; by < 92 && !found; by++)
                for (bx = 80; bx < 120; bx++)
                {
                    v_get_pixel(vwk, bx, by, &pel, &ink);
                    if (ink)
                    {
                        found = 1;
                        break;
                    }
                }

            check(found, 1, "the button inside it is drawn too");
        }

        /*
         * form_do, which runs the dialog until something ends it. Nothing is
         * going to click a button in a test, so TOSEMU_KEYS hands it a Return,
         * which is what picks the default button.
         */
        dlg[2].ob_flags = 0x0027;           /* selectable, default, exit, last */
        addrin[0] = (long)dlg;
        intin[0] = 0;                       /* no editable field to start in */
        check(call_aes(50, 1, 1, 1, 0), 2, "form_do ends on the default button");
        check(dlg[2].ob_state & 1, 1, "and the tree comes back with it selected");

        /*
         * A click, rather than a keypress. One click on a button ends the
         * dialog: the AES sees the press, tracks the button until it comes
         * back up, and then asks whether the button is up - which it already
         * is. Answering that only when the buttons next change is what made a
         * click take two clicks.
         */
        dlg[2].ob_state = 0;
        addrin[0] = (long)dlg;
        intin[0] = 0;
        check(call_aes(50, 1, 1, 1, 0), 2, "one click on a button ends form_do");

        /*
         * An alert, which is a dialog the AES builds itself out of a string.
         * It comes from the AES's own resource rather than from the
         * application, so this is also what says that resource was set up.
         */
        {
            static char alert[] = "[1][Something went wrong][OK|Cancel]";

            intin[0] = 1;                   /* OK is the default */
            addrin[0] = (long)alert;
            check(call_aes(52, 1, 1, 1, 0), 1, "form_alert ends on OK");
        }

        /*
         * A menu bar.
         *
         * The tree has to have the shape the AES walks - the screen, the bar
         * and the row of titles first, the box holding the menus last, and the
         * Desk menu inside it with room after its first entry for the
         * accessories the AES splices in. Nothing here has any accessories,
         * but the room still has to be there: the AES rebuilds that menu
         * whenever a bar goes up, and it does not check.
         */
        {
            enum {
                THESCREEN, THEBAR, THEACTIVE,
                T_DESK,
                THEMENUS,
                M_DESK, I_ABOUT, I_SEP,
                I_ACC1, I_ACC2, I_ACC3, I_ACC4, I_ACC5, I_ACC6,
                NUM_OBJECTS
            };
            static OBJECT bar[NUM_OBJECTS];
            short hbox = 11, wide = 8 * 8;
            short j;

            for (j = 0; j < NUM_OBJECTS; j++)
            {
                bar[j].ob_next = bar[j].ob_head = bar[j].ob_tail = NIL;
                bar[j].ob_type = G_STRING;
                bar[j].ob_flags = bar[j].ob_state = 0;
                bar[j].ob_spec.free_string = "  entry       ";
                bar[j].ob_x = bar[j].ob_y = 0;
                bar[j].ob_width = wide;
                bar[j].ob_height = hbox;
            }

            bar[THESCREEN].ob_head = THEBAR;
            bar[THESCREEN].ob_tail = THEMENUS;
            bar[THESCREEN].ob_type = G_IBOX;
            bar[THESCREEN].ob_width = 320;
            bar[THESCREEN].ob_height = 200;

            bar[THEBAR].ob_next = THEMENUS;
            bar[THEBAR].ob_head = bar[THEBAR].ob_tail = THEACTIVE;
            bar[THEBAR].ob_type = G_BOX;
            bar[THEBAR].ob_spec.index = 0x00001100L;
            bar[THEBAR].ob_width = 320;

            bar[THEACTIVE].ob_next = THEBAR;
            bar[THEACTIVE].ob_head = bar[THEACTIVE].ob_tail = T_DESK;
            bar[THEACTIVE].ob_type = G_IBOX;

            bar[T_DESK].ob_next = THEACTIVE;
            bar[T_DESK].ob_type = G_TITLE;
            bar[T_DESK].ob_spec.free_string = "  Desk  ";

            bar[THEMENUS].ob_next = THESCREEN;
            bar[THEMENUS].ob_head = bar[THEMENUS].ob_tail = M_DESK;
            bar[THEMENUS].ob_type = G_IBOX;
            bar[THEMENUS].ob_width = 320;
            bar[THEMENUS].ob_height = 200;

            bar[M_DESK].ob_next = THEMENUS;
            bar[M_DESK].ob_head = I_ABOUT;
            bar[M_DESK].ob_tail = I_ACC6;
            bar[M_DESK].ob_type = G_BOX;
            bar[M_DESK].ob_spec.index = 0x00ff11f0L;
            bar[M_DESK].ob_y = hbox;
            bar[M_DESK].ob_width = 14 * 8;
            bar[M_DESK].ob_height = 8 * hbox;

            for (j = I_ABOUT; j <= I_ACC6; j++)
            {
                bar[j].ob_next = (j == I_ACC6) ? M_DESK : j + 1;
                bar[j].ob_y = (short)((j - I_ABOUT) * hbox);
                bar[j].ob_width = 14 * 8;
            }
            bar[I_ABOUT].ob_spec.free_string = "  About...    ";
            bar[I_ACC6].ob_flags = OF_LASTOB;

            addrin[0] = (long)bar;
            intin[0] = 1;
            check(call_aes(30, 1, 1, 1, 0), 1, "menu_bar puts a bar up");

            addrin[0] = (long)bar;
            intin[0] = 0;
            check(call_aes(30, 1, 1, 1, 0), 1, "menu_bar takes it away again");

            /* Twice, because the AES rebuilds the Desk menu every time and
             * the second rebuild is the one that would find the tree in
             * whatever state the first left it */
            addrin[0] = (long)bar;
            intin[0] = 1;
            check(call_aes(30, 1, 1, 1, 0), 1, "menu_bar puts it up again");

            addrin[0] = (long)bar;
            intin[0] = 0;
            call_aes(30, 1, 1, 1, 0);
        }

        /*
         * A resource file.
         *
         * Written here rather than checked in, because what is being tested is
         * the reading of the format and a file to hand would only say that
         * this agrees with whatever wrote it. Three objects, a box holding two
         * strings, and every pointer in it an offset from the front - which is
         * the whole of what rsrc_load has to put right.
         */
        {
            static unsigned char rsc[] = {
                /* header */
                0,0,        /* version */
                0,40,       /* object */
                0,112,      /* tedinfo, and none of them */
                0,112,      /* iconblk */
                0,112,      /* bitblk */
                0,112,      /* free strings */
                0,112,      /* string data */
                0,119,      /* image data */
                0,119,      /* free images */
                0,36,       /* tree index */
                0,3,        /* objects */
                0,1,        /* trees */
                0,0, 0,0, 0,0, 0,0, 0,0,    /* ted, ib, bb, string, image */
                0,119,      /* how large the whole thing is */

                /* the tree index: one tree, whose root is at 40 */
                0,0,0,40,

                /* object 0: a box holding the other two */
                255,255, 0,1, 0,2,          /* next, head, tail */
                0,20,                       /* G_BOX */
                0,0, 0,0,                   /* flags, state */
                0,2,17,0,                   /* ob_spec: colours, not a pointer */
                0,0, 0,0, 0,20, 0,3,        /* x, y, w, h in characters */

                /* object 1: a string, pointing at offset 112 */
                0,2, 255,255, 255,255,
                0,28,                       /* G_STRING */
                0,0, 0,0,
                0,0,0,112,
                0,1, 0,1, 0,10, 0,1,

                /* object 2: another, and the last object in the tree */
                0,0, 255,255, 255,255,
                0,28,
                0,32,                       /* LASTOB */
                0,0,
                0,0,0,115,
                0,1, 0,2, 0,10, 0,1,

                /* the strings they point at */
                'H','i',0,
                'B','y','e',0
            };
            long handle;
            short tree, wchar, hchar, wbox, hbox;
            short vh = graf_handle(&wchar, &hchar, &wbox, &hbox);

            (void)vh;

            handle = Fcreate("TEST.RSC", 0);
            check(handle >= 0, 1, "a resource file can be written");
            if (handle >= 0)
            {
                Fwrite((short)handle, (long)sizeof rsc, rsc);
                Fclose((short)handle);
            }

            addrin[0] = (long)"TEST.RSC";
            check(call_aes(110, 0, 1, 1, 0), 1, "rsrc_load reads a resource");

            /* Where the trees are, which the AES writes into the global array
             * rather than answering with */
            check((global[5] != 0) || (global[6] != 0), 1,
                  "rsrc_load says where the trees are");

            intin[0] = 0;                   /* R_TREE */
            intin[1] = 0;
            addrout[0] = 0;
            check(call_aes(112, 2, 1, 0, 1), 1, "rsrc_gaddr answers for a tree");
            check(addrout[0] != 0, 1, "rsrc_gaddr gives an address");

            if (addrout[0])
            {
                OBJECT *root = (OBJECT *)addrout[0];

                check(root->ob_head, 1, "the tree came back put together");
                check(root->ob_type, 20, "and with its types intact");

                /*
                 * The offsets became addresses. A string that still held its
                 * offset would point at the front of the resource, which is
                 * the header and not a string at all.
                 */
                check(strcmp(root[1].ob_spec.free_string, "Hi"), 0,
                      "an offset to a string became a pointer to one");
                check(strcmp(root[2].ob_spec.free_string, "Bye"), 0,
                      "and so did the next one");

                /* And the characters became pixels */
                check(root[1].ob_x, wchar, "a coordinate in characters "
                                           "became one in pixels");
                check(root[1].ob_width, 10 * wchar, "and so did a width");
                check(root->ob_spec.index, 0x00021100L,
                      "a box's colours were left alone");
            }

            /* An index past the end is asked for by applications that guess */
            intin[0] = 0;
            intin[1] = 99;
            check(call_aes(112, 2, 1, 0, 1), 0,
                  "rsrc_gaddr refuses a tree that is not there");

            tree = call_aes(111, 0, 1, 0, 0);       /* rsrc_free */
            check(tree, 1, "rsrc_free gives the memory back");
            check((global[5] == 0) && (global[6] == 0), 1,
                  "and forgets where the trees were");

            Fdelete("TEST.RSC");
        }

        v_clsvwk(vwk);
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
