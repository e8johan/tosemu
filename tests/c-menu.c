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
 * Where a menu that has dropped down is drawn, which is not on the screen.
 *
 * On an ST a menu was drawn over whatever was underneath and the AES put the
 * pixels back afterwards, because there was one screen and nowhere else to put
 * it. Here every GEM window is a window of the desktop's showing a rectangle
 * of that screen, so a menu drawn into the screen appears inside every window
 * showing that part of it - a menu inside a document, which is the picture of
 * another computer that having real windows was meant to avoid.
 *
 * So a menu gets a surface of its own and the screen is left alone. The window
 * it is shown in is the compositor's business and not something a test can
 * arrange; what is checked here is the half that is memory. This program puts
 * a window under the bar, paints its work area black, works the menu, and then
 * reads back the pixels the menu was drawn over. They have to be the ones it
 * painted.
 *
 * The second thing checked is the title of a menu that was opened and left.
 * The AES draws a title highlighted while its menu is down and draws it back
 * to normal on the way out, and that second drawing belongs to the bar rather
 * than to the menu - so a title left inverted says the two were confused.
 *
 * The pointer is moved and the menu picked from by TOSEMU_CLICKS, there being
 * nobody here to do it. A test that stops the emulator prints nothing further,
 * so the count at the end is what says the whole file ran.
 */

#include <stdio.h>
#include <gem.h>

/* Where everything in a menu tree sits. The AES walks it by position - see
 * demos/menu.c, which says the whole shape out loud. */
enum {
    THESCREEN,
    THEBAR,
    THEACTIVE,

    T_DESK, T_FILE,

    THEMENUS,

    M_DESK,
    I_ABOUT,
    I_SEP,
    I_ACC1, I_ACC2, I_ACC3,
    I_ACC4, I_ACC5, I_ACC6,

    M_FILE,
    I_OPEN,
    I_SAVE,
    I_QUIT,

    NUM_OBJECTS
};

/* How many entries the File menu has, and which of them is picked. Picking the
 * first is deliberate: an entry that was chosen is left inverted, which is
 * black where the window is black, so the entries that say whether the menu
 * reached the screen have to be ones nobody touched. */
#define FILE_ITEMS  (3)

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

/* What a window is made of, http://toshyp.atari.org/en/008009.html */
#define W_HAS_NAME    (0x0001)
#define W_HAS_CLOSER  (0x0002)
#define W_HAS_MOVER   (0x0008)

/* A black border, black text, drawn over whatever is beneath it, filled solid
 * white, with a one pixel border drawn inside the edge */
#define WHITE_BOX  (0x00ff11f0L)

static int n;

static void check(long got, long want, const char *name)
{
    n++;
    if (got == want)
        printf("ok %d - %s\n", n, name);
    else
        printf("not ok %d - %s (got %ld, want %ld)\n", n, name, got, want);
}

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

static void build(short wchar, short hchar, short hbox, short width)
{
    short title_w = 8 * wchar;      /* "  Desk  " and "  File  " */
    short item_w = 14 * wchar;
    short i;

    object(THESCREEN, NIL, THEBAR, THEMENUS, G_IBOX, 0, 0L, 0, 0, width, 200);

    object(THEBAR, THEMENUS, THEACTIVE, THEACTIVE, G_BOX, 0, 0x00001100L,
           0, 0, width, hbox);
    object(THEACTIVE, THEBAR, T_DESK, T_FILE, G_IBOX, 0, 0L,
           0, 0, 2 * title_w, hbox);

    object(T_DESK, T_FILE, NIL, NIL, G_TITLE, 0, (long)"  Desk  ",
           0, 0, title_w, hbox);
    object(T_FILE, THEACTIVE, NIL, NIL, G_TITLE, 0, (long)"  File  ",
           title_w, 0, title_w, hbox);

    object(THEMENUS, THESCREEN, M_DESK, M_FILE, G_IBOX, 0, 0L,
           0, 0, width, 200);

    object(M_DESK, M_FILE, I_ABOUT, I_ACC6, G_BOX, 0, WHITE_BOX,
           0, hbox, item_w, 8 * hchar);

    object(I_ABOUT, I_SEP, NIL, NIL, G_STRING, 0, (long)"  About...    ",
           0, 0, item_w, hchar);
    object(I_SEP, I_ACC1, NIL, NIL, G_STRING, 0, (long)"--------------",
           0, hchar, item_w, hchar);

    for (i = 0; i < 6; i++)
        object(I_ACC1 + i, (i == 5) ? M_DESK : I_ACC1 + i + 1, NIL, NIL,
               G_STRING, 0, (long)"              ",
               0, (i + 2) * hchar, item_w, hchar);

    object(M_FILE, THEMENUS, I_OPEN, I_QUIT, G_BOX, 0, WHITE_BOX,
           title_w, hbox, item_w, FILE_ITEMS * hchar);
    object(I_OPEN, I_SAVE, NIL, NIL, G_STRING, 0, (long)"  Open...     ",
           0, 0, item_w, hchar);
    object(I_SAVE, I_QUIT, NIL, NIL, G_STRING, 0, (long)"  Save...     ",
           0, hchar, item_w, hchar);
    object(I_QUIT, M_FILE, NIL, NIL, G_STRING, OF_LASTOB, (long)"  Quit        ",
           0, 2 * hchar, item_w, hchar);

    menu[I_SEP].ob_state = OS_DISABLED;
}

/* The colour of one pixel of the screen, which is what says what was drawn
 * where and is the only way to ask from in here */
static short pixel_at(short handle, short x, short y)
{
    short value, index;

    v_get_pixel(handle, x, y, &value, &index);

    return index;
}

int main(int argc, char **argv)
{
    short wchar, hchar, wbox, hbox;
    short work_in[11], work_out[57];
    short handle, message[8];
    short i, running;
    short wide, high;
    short window;
    short wx, wy, ww, wh;
    short menu_x, menu_y, menu_w;
    short over_y;
    short pxy[4];

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

    wide = work_out[0] + 1;
    high = work_out[1] + 1;

    build(wchar, hchar, hbox, wide);

    /*
     * Where the File menu will drop down, which is directly under its title
     * and is worked out here rather than written down: the character size
     * decides all of it, and a number taken off one screen misses on another.
     */
    menu_x = 8 * wchar;
    menu_y = hbox;
    menu_w = 14 * wchar;

    addrin[0] = (long)menu;
    intin[0] = 1;                                    /* install it */
    if (call_aes(30, 1, 1, 1, 0) == 0)               /* menu_bar */
    {
        printf("Bail out! - the AES would not take the menu\n");
        return 1;
    }

    /*
     * A window under the bar, large enough that the menu drops down inside it.
     * That is the whole point of the exercise: on an ST there was nothing
     * underneath a menu but the screen, and here there is a window.
     */
    intin[0] = W_HAS_NAME|W_HAS_CLOSER|W_HAS_MOVER;
    intin[1] = 0; intin[2] = hbox; intin[3] = wide; intin[4] = high - hbox;
    window = call_aes(100, 5, 1, 0, 0);              /* wind_create */

    intin[0] = window;
    intin[1] = 0; intin[2] = hbox; intin[3] = wide; intin[4] = high - hbox;
    call_aes(101, 5, 1, 0, 0);                       /* wind_open */

    intin[0] = window;
    intin[1] = 4;                                    /* WF_WORKXYWH */
    call_aes(104, 6, 5, 0, 0);                       /* wind_get */
    wx = intout[1]; wy = intout[2]; ww = intout[3]; wh = intout[4];

    /* Painted solid, so that a pixel read back afterwards says plainly
     * whether it is still the application's or has become the menu's */
    vswr_mode(handle, MD_REPLACE);
    vsf_interior(handle, FIS_SOLID);
    vsf_color(handle, 1);
    pxy[0] = wx; pxy[1] = wy;
    pxy[2] = wx + ww - 1; pxy[3] = wy + wh - 1;
    v_bar(handle, pxy);

    /*
     * The row of the menu the question is about, worked out rather than
     * written down: a coordinate taken off one screen misses on another.
     *
     * The window keeps the strip its title bar would go in, so its work area
     * starts a bar's height below the menu, and a menu entry is a character
     * tall - which puts the top of the work area inside the second entry. That
     * is the row to ask about: the first entry is the one that gets chosen and
     * is left inverted, which is black where the window is black and would say
     * nothing either way.
     */
    over_y = wy + 1;

    check(over_y >= menu_y + hchar && over_y < menu_y + 2 * hchar, 1,
          "the second entry of the menu is where the window's work area starts");

    check(pixel_at(handle, menu_x + 2, over_y), 1,
          "the work area is painted where the menu will be");

    /*
     * And now the menu, worked by TOSEMU_CLICKS: into the Desk title, on to
     * the File title, and a press on the first entry. Going by way of Desk is
     * not decoration - it is what makes a title that was opened and left behind
     * be drawn back to normal, which is the second thing checked below.
     */
    for (running = 1; running; )
    {
        for (i = 0; i < 16; i++)
            intin[i] = 0;
        intin[0] = 0x0010;                           /* MU_MESAG */
        addrin[0] = (long)message;
        call_aes(25, 16, 7, 1, 0);                   /* evnt_multi */

        if (message[0] == MN_SELECTED)
            running = 0;
    }

    check(message[3], T_FILE, "the File menu is what was chosen from");
    check(message[4], I_OPEN, "and its first entry is what was chosen");

    /*
     * The pixels the menu was drawn over. An entry nobody touched is a white
     * box with black words in it, so both ends of it read as white if the menu
     * reached the screen - and the window painted them black, so black is the
     * answer that says it did not.
     */
    check(pixel_at(handle, menu_x + 2, over_y), 1,
          "the left edge of the drop-down menu is still the window's own");
    check(pixel_at(handle, menu_x + menu_w - 4, over_y), 1,
          "and so is the right, past the end of the entry's words");

    /*
     * The Desk title, which was opened on the way past and left. Its menu is
     * gone and the title has to have gone back to normal with it: a title is
     * drawn highlighted by inverting it, so a black background where the bar
     * is white says the drawing that put it back went somewhere else.
     */
    check(pixel_at(handle, 2, 2), 0,
          "the title of the menu that was passed through is not left inverted");

    /*
     * The title that was chosen from is held open on purpose, and putting it
     * back is the application's to do. It has to reach the screen as well,
     * being on the bar rather than on the menu.
     */
    intin[0] = message[3];
    intin[1] = 1;                                    /* back to normal */
    addrin[0] = (long)menu;
    call_aes(33, 2, 1, 1, 0);                        /* menu_tnormal */

    check(pixel_at(handle, menu_x + 2, 2), 0,
          "and neither is the one that was chosen from, once put back");

    printf("1..%d\n", n);

    intin[0] = window;
    call_aes(102, 1, 1, 0, 0);                       /* wind_close */
    intin[0] = window;
    call_aes(103, 1, 1, 0, 0);                       /* wind_delete */

    addrin[0] = 0;
    intin[0] = 0;
    call_aes(30, 1, 1, 1, 0);                        /* menu_bar, remove */

    v_clsvwk(handle);
    appl_exit();

    return 0;
}
