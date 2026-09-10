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
 * The menu bar while a dialog is up, which is a menu bar nobody may touch.
 *
 * A modal dialog is the application saying it will do nothing else until this
 * is answered, and the AES held it to that: form_do waits for the keyboard and
 * the mouse and for nothing else, so a menu chosen over the top of one would
 * be a message the application cannot come and read. On an ST the menu was run
 * by a process of the AES's own, which had its own stack and its own copy of
 * everything, and the message waited in the queue.
 *
 * Here there is one of everything. The bar and the dialog are both trees
 * brought across from the machine's memory into the AES's, one at a time, so
 * running the menu over a dialog takes the dialog's own copy away from the
 * form_do that is standing in it - and what it does next is read a tree that
 * has been handed back to the allocator. That is a crash rather than a wrong
 * answer, which is why this test's own count line is half of what it checks.
 *
 * So the pointer is walked into the bar and a menu chosen from while form_do
 * has the screen, and the dialog has to come out of it unharmed: the button
 * that was pressed answered form_do, the press came back in the application's
 * own tree, and nothing was chosen from a menu that should not have opened.
 *
 * The pointer is moved and pressed by TOSEMU_CLICKS, there being nobody here
 * to do it, and the coordinates are the high resolution screen's - see the
 * check line in the Makefile.
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

    NUM_MENU_OBJECTS
};

/* And the dialog, which is a box with a line of words and a button in it */
enum {
    D_BOX,
    D_WORDS,
    D_OK,

    NUM_DIALOG_OBJECTS
};

static OBJECT menu[NUM_MENU_OBJECTS];
static OBJECT dialog[NUM_DIALOG_OBJECTS];

/* A black border, black text, drawn over whatever is beneath it, filled solid
 * white, with a one pixel border drawn inside the edge */
#define WHITE_BOX  (0x00ff11f0L)

/* The same, with the two pixels of border a dialog is drawn with */
#define DIALOG_BOX (0x00021100L)

static int n;

static void check(long got, long want, const char *name)
{
    n++;
    if (got == want)
        printf("ok %d - %s\n", n, name);
    else
        printf("not ok %d - %s (got %ld, want %ld)\n", n, name, got, want);
}

static void object(OBJECT *tree, short which, short next, short head,
                   short tail, short type, short flags, long spec,
                   short x, short y, short w, short h)
{
    OBJECT *o = &tree[which];

    o->ob_next = next; o->ob_head = head; o->ob_tail = tail;
    o->ob_type = type; o->ob_flags = flags; o->ob_state = 0;
    o->ob_spec.index = spec;
    o->ob_x = x; o->ob_y = y; o->ob_width = w; o->ob_height = h;
}

static void build_menu(short wchar, short hchar, short hbox, short width)
{
    short title_w = 8 * wchar;      /* "  Desk  " and "  File  " */
    short item_w = 14 * wchar;
    short i;

    object(menu, THESCREEN, NIL, THEBAR, THEMENUS, G_IBOX, 0, 0L,
           0, 0, width, 200);

    object(menu, THEBAR, THEMENUS, THEACTIVE, THEACTIVE, G_BOX, 0, 0x00001100L,
           0, 0, width, hbox);
    object(menu, THEACTIVE, THEBAR, T_DESK, T_FILE, G_IBOX, 0, 0L,
           0, 0, 2 * title_w, hbox);

    object(menu, T_DESK, T_FILE, NIL, NIL, G_TITLE, 0, (long)"  Desk  ",
           0, 0, title_w, hbox);
    object(menu, T_FILE, THEACTIVE, NIL, NIL, G_TITLE, 0, (long)"  File  ",
           title_w, 0, title_w, hbox);

    object(menu, THEMENUS, THESCREEN, M_DESK, M_FILE, G_IBOX, 0, 0L,
           0, 0, width, 200);

    object(menu, M_DESK, M_FILE, I_ABOUT, I_ACC6, G_BOX, 0, WHITE_BOX,
           0, hbox, item_w, 8 * hchar);

    object(menu, I_ABOUT, I_SEP, NIL, NIL, G_STRING, 0, (long)"  About...    ",
           0, 0, item_w, hchar);
    object(menu, I_SEP, I_ACC1, NIL, NIL, G_STRING, 0, (long)"--------------",
           0, hchar, item_w, hchar);

    for (i = 0; i < 6; i++)
        object(menu, I_ACC1 + i, (i == 5) ? M_DESK : I_ACC1 + i + 1, NIL, NIL,
               G_STRING, 0, (long)"              ",
               0, (i + 2) * hchar, item_w, hchar);

    object(menu, M_FILE, THEMENUS, I_OPEN, I_QUIT, G_BOX, 0, WHITE_BOX,
           title_w, hbox, item_w, 3 * hchar);
    object(menu, I_OPEN, I_SAVE, NIL, NIL, G_STRING, 0, (long)"  Open...     ",
           0, 0, item_w, hchar);
    object(menu, I_SAVE, I_QUIT, NIL, NIL, G_STRING, 0, (long)"  Save...     ",
           0, hchar, item_w, hchar);
    object(menu, I_QUIT, M_FILE, NIL, NIL, G_STRING, OF_LASTOB,
           (long)"  Quit        ", 0, 2 * hchar, item_w, hchar);

    menu[I_SEP].ob_state = OS_DISABLED;
}

/*
 * The dialog, well below the bar and the menus that drop out of it.
 *
 * Where it is matters, because the same clicks that work the menu have to miss
 * the dialog on the way: a press that landed on one of its objects would end
 * form_do early and the menu would never have been opened over anything.
 */
static void build_dialog(short wchar, short hchar)
{
    object(dialog, D_BOX, NIL, D_WORDS, D_OK, G_BOX, 0, DIALOG_BOX,
           10 * wchar, 6 * hchar, 24 * wchar, 5 * hchar);

    object(dialog, D_WORDS, D_OK, NIL, NIL, G_STRING, 0,
           (long)"Answer this first", 2 * wchar, hchar, 20 * wchar, hchar);

    /* EXIT is what makes a button end the dialog, and a button that ended one
     * is left selected - which is the state this test reads back */
    object(dialog, D_OK, D_BOX, NIL, NIL, G_BUTTON,
           OF_SELECTABLE|OF_DEFAULT|OF_EXIT|OF_LASTOB, (long)"OK",
           4 * wchar, 3 * hchar, 6 * wchar, hchar);
}

int main(int argc, char **argv)
{
    short wchar, hchar, wbox, hbox;
    short work_in[11], work_out[57];
    short handle, message[8];
    short wide, high;
    short mx, my, buttons, kstate, key, clicks;
    short happened, pressed;
    short i;

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

    build_menu(wchar, hchar, hbox, wide);
    build_dialog(wchar, hchar);

    /* Where the clicks have to land, said out loud: they are worked out here
     * and written down in the Makefile, and a screen with another character
     * size on it would want them somewhere else */
    printf("# the File menu drops down at %d,%d and OK is at %d,%d\n",
           8 * wchar, hbox,
           dialog[D_BOX].ob_x + dialog[D_OK].ob_x + wchar,
           dialog[D_BOX].ob_y + dialog[D_OK].ob_y + hchar / 2);

    if (!menu_bar(menu, 1))
    {
        printf("Bail out! - the AES would not take the menu\n");
        return 1;
    }

    /*
     * And the dialog, which reserves the screen it sits on the way every GEM
     * dialog does. That is what says a dialog is up: the AES is told before
     * anything is drawn and told again when it is finished with.
     */
    form_dial(FMD_START, 0, 0, 0, 0,
              dialog[D_BOX].ob_x, dialog[D_BOX].ob_y,
              dialog[D_BOX].ob_width, dialog[D_BOX].ob_height);

    objc_draw(dialog, D_BOX, 8, dialog[D_BOX].ob_x, dialog[D_BOX].ob_y,
              dialog[D_BOX].ob_width, dialog[D_BOX].ob_height);

    pressed = form_do(dialog, 0);

    check(pressed, D_OK, "the dialog ended on the button that was pressed");
    check(dialog[D_OK].ob_state & OS_SELECTED, OS_SELECTED,
          "and the press came back in the application's own tree");

    form_dial(FMD_FINISH, 0, 0, 0, 0,
              dialog[D_BOX].ob_x, dialog[D_BOX].ob_y,
              dialog[D_BOX].ob_width, dialog[D_BOX].ob_height);

    /*
     * And whether a menu was chosen from while all that was going on, which is
     * a message sitting in the queue - form_do does not ask for messages, so
     * one chosen over the dialog would be waiting here rather than lost.
     *
     * A timer to give up after, because the answer this wants is silence and a
     * wait for a message that is not coming would otherwise be the whole of
     * the test.
     */
    happened = evnt_multi(MU_MESAG|MU_TIMER, 0, 0, 0,
                          0, 0, 0, 0, 0,
                          0, 0, 0, 0, 0,
                          message, 250L,
                          &mx, &my, &buttons, &kstate, &key, &clicks);

    check(happened, MU_TIMER, "and no menu was opened over the top of it");

    printf("1..%d\n", n);

    menu_bar(0L, 0);

    v_clsvwk(handle);
    appl_exit();

    return 0;
}
