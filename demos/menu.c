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
 * A menu bar, and the shape a menu tree has to have.
 *
 * A menu is an object tree, but unlike a dialog it is not a tree of any shape
 * the application likes. The AES walks it by position: the first three objects
 * are the screen, the bar and the row of titles, in that order, and the last
 * child of the root is the box holding the drop-down menus. The first menu in
 * that box is the Desk menu, and the AES rebuilds its contents every time the
 * bar goes up - an About entry, a separator, and one entry for each accessory
 * that has registered - so those entries have to be there waiting, whether or
 * not any accessory ever fills them in.
 *
 * A resource editor produces that shape without being asked, which is why it
 * is so rarely written down. Building one by hand means knowing it, so this
 * demo builds one by hand.
 *
 * The bar is a window of the desktop's, like every other GEM window here, so
 * it follows this program about rather than sitting at the top of a picture of
 * an ST.
 */

#include <gem.h>
#include <stdio.h>

/*
 * Where everything sits. The first six are fixed by the AES; the rest follow
 * from how many titles and entries this particular menu has.
 */
enum {
    THESCREEN,              /* the screen */
    THEBAR,                 /* the strip across the top of it */
    THEACTIVE,              /* the row of titles inside the strip */

    T_DESK, T_FILE,         /* the titles themselves */

    THEMENUS,               /* the box the drop-down menus live in */

    M_DESK,                 /* the Desk menu, which the AES rebuilds */
    I_ABOUT,                /*   what it puts back: an About entry, */
    I_SEP,                  /*   a separator, */
    I_ACC1, I_ACC2, I_ACC3, /*   and one slot per accessory. Six, because */
    I_ACC4, I_ACC5, I_ACC6, /*   six is as many as GEM will load. */

    M_FILE,                 /* and a menu of our own */
    I_OPEN,
    I_QUIT,

    NUM_OBJECTS
};

static OBJECT menu[NUM_OBJECTS];

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

/* Fills in one object. Everything here is either a box holding others or a
 * piece of text, so the spec is a string or a colour word and nothing else. */
static void object(short which, short next, short head, short tail,
                   short type, short flags, long spec,
                   short x, short y, short w, short h)
{
    OBJECT *o = &menu[which];

    o->ob_next = next; o->ob_head = head; o->ob_tail = tail;
    o->ob_type = type; o->ob_flags = flags; o->ob_state = 0;
    o->ob_spec.index = spec;
    o->ob_x = x; o->ob_y = y; o->ob_width = w; o->ob_height = h;
}

/*
 * The colour word of a box: a black border, black text, drawn over whatever is
 * beneath it rather than through it, filled solid white. The byte above it is
 * the border thickness, and -1 means one pixel drawn inside the edge.
 */
#define WHITE_BOX  (0x00ff11f0L)

static void build(short wchar, short hchar, short hbox)
{
    short title_w = 8 * wchar;      /* "  Desk  " and "  File  " */
    short item_w = 14 * wchar;
    short i;

    /*
     * An entry in a menu is one character tall, not one bar tall. The bar is
     * taller than its text to leave room for the line under it, and using that
     * height for the entries would be wrong in a way that is easy to miss: the
     * AES sets the Desk menu's own height, as one character per entry, and
     * then draws entries that do not fit inside it - so the one menu the
     * application did not lay out is the one that comes out truncated.
     */

    object(THESCREEN, NIL, THEBAR, THEMENUS, G_IBOX, 0, 0L,
           0, 0, 320, 200);

    /* The bar is stretched to the width of the screen by the AES, so what is
     * put here only has to be somewhere sensible */
    object(THEBAR, THEMENUS, THEACTIVE, THEACTIVE, G_BOX, 0, 0x00001100L,
           0, 0, 320, hbox);
    object(THEACTIVE, THEBAR, T_DESK, T_FILE, G_IBOX, 0, 0L,
           0, 0, 2 * title_w, hbox);

    object(T_DESK, T_FILE, NIL, NIL, G_TITLE, 0, (long)"  Desk  ",
           0, 0, title_w, hbox);
    object(T_FILE, THEACTIVE, NIL, NIL, G_TITLE, 0, (long)"  File  ",
           title_w, 0, title_w, hbox);

    /*
     * The box the drop-down menus live in. It covers the screen and is
     * invisible: what it is for is to be the last child of the root, which is
     * where the AES looks for the menus. Putting it anywhere else moves every
     * menu inside it, because an object's place is relative to its parent.
     */
    object(THEMENUS, THESCREEN, M_DESK, M_FILE, G_IBOX, 0, 0L,
           0, 0, 320, 200);

    /* The Desk menu. Its height is set by the AES once it knows how many
     * entries it kept, so what is put here does not matter. */
    object(M_DESK, M_FILE, I_ABOUT, I_ACC6, G_BOX, 0, WHITE_BOX,
           0, hbox, item_w, 8 * hchar);

    object(I_ABOUT, I_SEP, NIL, NIL, G_STRING, 0, (long)"  About...    ",
           0, 0, item_w, hchar);
    object(I_SEP, I_ACC1, NIL, NIL, G_STRING, 0, (long)"--------------",
           0, hchar, item_w, hchar);

    /* The slots the accessories go in. They are blank because nothing has
     * registered: the AES writes the name of each accessory into one of these
     * as it arrives, which is how the Desk menu comes to list them. */
    for (i = 0; i < 6; i++)
        object(I_ACC1 + i, (i == 5) ? M_DESK : I_ACC1 + i + 1, NIL, NIL,
               G_STRING, 0, (long)"              ",
               0, (i + 2) * hchar, item_w, hchar);

    /* And a menu that is this program's own */
    object(M_FILE, THEMENUS, I_OPEN, I_QUIT, G_BOX, 0, WHITE_BOX,
           title_w, hbox, item_w, 2 * hchar);
    object(I_OPEN, I_QUIT, NIL, NIL, G_STRING, 0, (long)"  Open...     ",
           0, 0, item_w, hchar);
    object(I_QUIT, M_FILE, NIL, NIL, G_STRING, OF_LASTOB, (long)"  Quit        ",
           0, hchar, item_w, hchar);

    /* Nothing to open yet, so say so rather than pretend */
    menu[I_OPEN].ob_state = OS_DISABLED;
    menu[I_SEP].ob_state = OS_DISABLED;
}

int main(int argc, char **argv)
{
    short wchar, hchar, wbox, hbox;
    short work_in[11], work_out[57];
    short handle, message[8];
    short i, running;

    appl_init();

    handle = graf_handle(&wchar, &hchar, &wbox, &hbox);
    for (i = 0; i < 10; i++)
        work_in[i] = 1;
    work_in[10] = 2;
    v_opnvwk(work_in, &handle, work_out);

    build(wchar, hchar, hbox);

    addrin[0] = (long)menu;
    intin[0] = 1;                                    /* install it */
    if (call_aes(30, 1, 1, 1, 0) == 0)               /* menu_bar */
    {
        printf("the AES would not take the menu\n");
        appl_exit();
        return 1;
    }

    for (running = 1; running; )
    {
        for (i = 0; i < 16; i++)
            intin[i] = 0;
        intin[0] = 0x0010;                           /* MU_MESAG */
        addrin[0] = (long)message;
        call_aes(25, 16, 7, 1, 0);                   /* evnt_multi */

        if (message[0] == MN_SELECTED)
        {
            printf("chose item %d of title %d\n", message[4], message[3]);

            if (message[4] == I_QUIT)
                running = 0;

            /*
             * The AES leaves the title held open on purpose, so that it still
             * looks open while whatever was chosen is being done. Putting it
             * back is the application's job and has to happen even on the way
             * out, or the last thing on the screen is a menu that never
             * closed.
             */
            addrin[0] = (long)menu;
            intin[0] = message[3];
            intin[1] = 1;                            /* back to normal */
            call_aes(33, 2, 1, 1, 0);                /* menu_tnormal */
        }
    }

    addrin[0] = 0;
    intin[0] = 0;                                    /* and take it away */
    call_aes(30, 1, 1, 1, 0);

    v_clsvwk(handle);
    appl_exit();

    return 0;
}
