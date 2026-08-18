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
 * A dialog, and something to click in it.
 *
 * This is a GEM application in the ordinary way: it introduces itself with
 * appl_init, asks the AES for the workstation it draws through, puts a tree of
 * objects on the screen and waits for one of them to be pressed. Nothing in it
 * knows it is not running on an ST.
 *
 * The tree is built here rather than loaded from a resource file, because
 * rsrc_load needs the file to be found the way GEMDOS finds one, which is not
 * written yet. A resource file is how a real application would carry it.
 */

#include <gem.h>
#include <stdio.h>

/* Object types and flags, which gemlib has but does not always declare */
#define OB_BOX      (20)
#define OB_STRING   (28)
#define OB_BUTTON   (26)

#define FL_NONE     (0x0000)
#define FL_SELECTABLE (0x0001)
#define FL_DEFAULT  (0x0002)
#define FL_EXIT     (0x0004)
#define FL_LASTOB   (0x0020)

#define ST_NORMAL   (0x0000)
#define ST_SELECTED (0x0001)

/*
 * Which object is which, in the tree below. ROOT is not among them: gemlib
 * already says that the root of a tree is object zero, which is the same thing
 * this would have said.
 */
#define MESSAGE (1)
#define OK      (2)
#define CANCEL  (3)

static OBJECT dialog[4];

static void build_dialog(void)
{
    /*
     * The root, which is the box everything else sits in. Its ob_spec is not
     * a pointer but a packed description: two pixels of border, and a colour
     * word saying black on white.
     */
    dialog[ROOT].ob_next = -1;
    dialog[ROOT].ob_head = MESSAGE;
    dialog[ROOT].ob_tail = CANCEL;
    dialog[ROOT].ob_type = OB_BOX;
    dialog[ROOT].ob_flags = FL_NONE;
    dialog[ROOT].ob_state = ST_NORMAL;
    dialog[ROOT].ob_spec.index = 0x00021100L;
    dialog[ROOT].ob_x = 60;
    dialog[ROOT].ob_y = 50;
    dialog[ROOT].ob_width = 200;
    dialog[ROOT].ob_height = 90;

    /* Everything below is placed relative to the box it is in */
    dialog[MESSAGE].ob_next = OK;
    dialog[MESSAGE].ob_head = -1;
    dialog[MESSAGE].ob_tail = -1;
    dialog[MESSAGE].ob_type = OB_STRING;
    dialog[MESSAGE].ob_flags = FL_NONE;
    dialog[MESSAGE].ob_state = ST_NORMAL;
    dialog[MESSAGE].ob_spec.free_string = "Really do the thing?";
    dialog[MESSAGE].ob_x = 20;
    dialog[MESSAGE].ob_y = 20;
    dialog[MESSAGE].ob_width = 160;
    dialog[MESSAGE].ob_height = 8;

    /*
     * EXIT is what makes a button end the dialog, and DEFAULT is what makes
     * Return pick this one. Without EXIT a button can be pressed all day and
     * form_do goes on waiting.
     */
    dialog[OK].ob_next = CANCEL;
    dialog[OK].ob_head = -1;
    dialog[OK].ob_tail = -1;
    dialog[OK].ob_type = OB_BUTTON;
    dialog[OK].ob_flags = FL_SELECTABLE|FL_DEFAULT|FL_EXIT;
    dialog[OK].ob_state = ST_NORMAL;
    dialog[OK].ob_spec.free_string = "OK";
    dialog[OK].ob_x = 24;
    dialog[OK].ob_y = 56;
    dialog[OK].ob_width = 56;
    dialog[OK].ob_height = 16;

    dialog[CANCEL].ob_next = ROOT;      /* the last child points at its parent */
    dialog[CANCEL].ob_head = -1;
    dialog[CANCEL].ob_tail = -1;
    dialog[CANCEL].ob_type = OB_BUTTON;
    dialog[CANCEL].ob_flags = FL_SELECTABLE|FL_EXIT|FL_LASTOB;
    dialog[CANCEL].ob_state = ST_NORMAL;
    dialog[CANCEL].ob_spec.free_string = "Cancel";
    dialog[CANCEL].ob_x = 112;
    dialog[CANCEL].ob_y = 56;
    dialog[CANCEL].ob_width = 64;
    dialog[CANCEL].ob_height = 16;
}

int main(int argc, char **argv)
{
    short wchar, hchar, wbox, hbox;
    short handle, work_in[11], work_out[57];
    short pressed;
    short i;

    if (appl_init() < 0)
    {
        printf("No AES to talk to.\n");
        return 1;
    }

    /*
     * The workstation the AES already has open, and one of our own against it.
     * An application does not open a physical workstation: the AES owns the
     * screen and hands out the handle.
     */
    handle = graf_handle(&wchar, &hchar, &wbox, &hbox);

    for (i = 0; i < 10; i++)
        work_in[i] = 1;
    work_in[10] = 2;            /* coordinates in pixels */

    v_opnvwk(work_in, &handle, work_out);

    build_dialog();

    /*
     * form_dial reserves the screen the dialog sits on. On an ST that is so
     * the AES can put back what was underneath; here it is what gives the
     * dialog a window of its own, so the desktop treats it as a dialog - above
     * its parent, modal, and movable even while this program is blocked inside
     * form_do waiting for a button.
     */
    form_dial(FMD_START, 0, 0, 0, 0,
              dialog[ROOT].ob_x, dialog[ROOT].ob_y,
              dialog[ROOT].ob_width, dialog[ROOT].ob_height);

    objc_draw(dialog, ROOT, 8, dialog[ROOT].ob_x, dialog[ROOT].ob_y,
              dialog[ROOT].ob_width, dialog[ROOT].ob_height);

    pressed = form_do(dialog, 0);

    form_dial(FMD_FINISH, 0, 0, 0, 0,
              dialog[ROOT].ob_x, dialog[ROOT].ob_y,
              dialog[ROOT].ob_width, dialog[ROOT].ob_height);

    /* A button that ended a dialog is left selected, and an application is
     * expected to put that back before it draws the tree again */
    dialog[pressed].ob_state &= ~ST_SELECTED;

    printf("%s\n", (pressed == OK) ? "OK" : "Cancel");

    v_clsvwk(handle);
    appl_exit();

    return 0;
}
