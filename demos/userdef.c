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
 * An object the application draws itself.
 *
 * Everything in a GEM dialog is one of a dozen kinds the AES knows how to
 * draw - a box, a string, a button - except this one. A G_USERDEF object says
 * "call me and I will draw it", and what it points at is a routine in the
 * application. A resource editor calls the same thing a G_PROGDEF; the two
 * names are for the same twenty fourth object type.
 *
 * It is how every GEM program that did not look like a GEM program was
 * written: rounded buttons, dials, colour swatches, the little sliders in a
 * control panel. The AES lays the object out and works out when it was
 * clicked, and the application decides what it looks like.
 *
 * Here the AES and the application are not the same kind of code. The AES is
 * this emulator, running on the host; the routine is 68000 code in the
 * emulated machine. So drawing one of these is the one place where a call goes
 * the other way: the emulator is halfway through drawing a dialog when it
 * reaches an object it has to ask the application about, and it starts the CPU
 * again and runs it to the end of the routine before carrying on.
 *
 * Two buttons here, drawn round rather than square, which is what the WERCS
 * example that came with Lattice C did with the same feature.
 */

#include <gem.h>
#include <stdio.h>

/* Object types and flags, which gemlib has but does not always declare */
#define OB_BOX      (20)
#define OB_STRING   (28)
#define OB_USERDEF  (24)

#define FL_NONE     (0x0000)
#define FL_SELECTABLE (0x0001)
#define FL_DEFAULT  (0x0002)
#define FL_EXIT     (0x0004)
#define FL_LASTOB   (0x0020)

#define ST_NORMAL   (0x0000)
#define ST_SELECTED (0x0001)

/* The two colours every GEM screen has, whatever the palette makes of them */
#define WHITE       (0)
#define BLACK       (1)

/* Which object is which. ROOT is gemlib's name for object zero. */
#define MESSAGE (1)
#define OK      (2)
#define CANCEL  (3)

static OBJECT dialog[4];

/* One of these for each object that draws itself. It holds the routine and a
 * long the routine is handed back, and the object's ob_spec points at it
 * instead of at whatever it would otherwise have held. */
static USERBLK blocks[2];

/*
 * The workstation the routine draws through.
 *
 * It is the AES's own, which is what graf_handle answers with, rather than one
 * of the application's: the routine is called from inside the AES while the
 * AES is drawing, so it draws where the AES is drawing.
 */
static short handle;

/*
 * The routine itself.
 *
 * Everything it is told arrives in the block: which tree and which object in
 * it, where the object is, what it is clipped to, what state it is in and what
 * state it was in before, and the long from the USERBLK - which here is the
 * text on the button, because a G_USERDEF object's ob_spec is the block and
 * has nowhere left to keep a string.
 *
 * The one rule is that it may only call the VDI. The AES is in the middle of
 * drawing a tree and calling back into it would ask it to start again on
 * something it has not finished.
 */
static short round_button(PARMBLK *pb)
{
    OBJECT *tree = pb->pb_tree;
    short selected = pb->pb_currstate & ST_SELECTED;
    short thick = (tree[pb->pb_obj].ob_flags & FL_DEFAULT) ? 3 : 1;
    short pxy[4];
    short junk;

    /*
     * Only where the AES said. The clipping rectangle is the part of the
     * screen being redrawn, and drawing outside it would put the object back
     * over something that was meant to cover it.
     */
    pxy[0] = pb->pb_xc;
    pxy[1] = pb->pb_yc;
    pxy[2] = pb->pb_xc + pb->pb_wc - 1;
    pxy[3] = pb->pb_yc + pb->pb_hc - 1;
    vs_clip(handle, 1, pxy);

    pxy[0] = pb->pb_x;
    pxy[1] = pb->pb_y;
    pxy[2] = pb->pb_x + pb->pb_w - 1;
    pxy[3] = pb->pb_y + pb->pb_h - 1;

    /* Filled first, black when the button is held down. GEM colour 0 is white
     * and 1 is black, whatever the palette says they look like. */
    vsf_interior(handle, FIS_SOLID);
    vsf_color(handle, selected ? BLACK : WHITE);
    vsf_perimeter(handle, 0);
    v_rfbox(handle, pxy);

    /* Then the outline, heavier for the default button - which is the one
     * thing here that has to be read out of the tree rather than out of the
     * block, and the reason the tree the routine is given has to be the
     * application's own rather than a copy of it */
    vsl_color(handle, BLACK);
    vsl_width(handle, thick);
    v_rbox(handle, pxy);
    vsl_width(handle, 1);

    /*
     * And the text, centred, in whichever colour the fill is not.
     *
     * Transparently, which is the part that is easy to leave out: the VDI
     * normally paints the whole character cell and puts the letter in it, so a
     * white letter on a cell it has just painted white is a white rectangle.
     * MD_TRANS draws the letter and leaves everything round it alone.
     */
    vswr_mode(handle, MD_TRANS);
    vst_alignment(handle, 1, 5, &junk, &junk);
    vst_color(handle, selected ? WHITE : BLACK);
    v_gtext(handle, pb->pb_x + pb->pb_w/2, pb->pb_y + pb->pb_h/2 - 4,
            (char *)pb->pb_parm);
    vst_alignment(handle, 0, 0, &junk, &junk);
    vst_color(handle, BLACK);
    vswr_mode(handle, MD_REPLACE);

    /*
     * Nothing left for the AES to do. What a routine answers with is the state
     * to go on and draw the object in - the selecting, the outlining, the
     * crossing out - and nought means it has all been dealt with here.
     */
    return 0;
}

static void build_dialog(void)
{
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

    dialog[MESSAGE].ob_next = OK;
    dialog[MESSAGE].ob_head = -1;
    dialog[MESSAGE].ob_tail = -1;
    dialog[MESSAGE].ob_type = OB_STRING;
    dialog[MESSAGE].ob_flags = FL_NONE;
    dialog[MESSAGE].ob_state = ST_NORMAL;
    dialog[MESSAGE].ob_spec.free_string = "Drawn by the program:";
    dialog[MESSAGE].ob_x = 20;
    dialog[MESSAGE].ob_y = 20;
    dialog[MESSAGE].ob_width = 168;
    dialog[MESSAGE].ob_height = 8;

    /*
     * The two buttons. Everything about them is ordinary except the type and
     * what ob_spec points at: they are selectable and they exit the dialog,
     * the AES finds them when they are clicked and inverts nothing, and
     * form_do answers with their number the way it would for a G_BUTTON.
     *
     * ob_spec is where the text of a button would have been, so the text moves
     * into the block instead. That is what ub_parm is for - the AES neither
     * reads it nor changes it, it hands it back.
     */
    blocks[0].ub_code = round_button;
    blocks[0].ub_parm = (long)"OK";

    blocks[1].ub_code = round_button;
    blocks[1].ub_parm = (long)"Cancel";

    dialog[OK].ob_next = CANCEL;
    dialog[OK].ob_head = -1;
    dialog[OK].ob_tail = -1;
    dialog[OK].ob_type = OB_USERDEF;
    dialog[OK].ob_flags = FL_SELECTABLE|FL_DEFAULT|FL_EXIT;
    dialog[OK].ob_state = ST_NORMAL;
    dialog[OK].ob_spec.userblk = &blocks[0];
    dialog[OK].ob_x = 24;
    dialog[OK].ob_y = 56;
    dialog[OK].ob_width = 56;
    dialog[OK].ob_height = 16;

    dialog[CANCEL].ob_next = ROOT;
    dialog[CANCEL].ob_head = -1;
    dialog[CANCEL].ob_tail = -1;
    dialog[CANCEL].ob_type = OB_USERDEF;
    dialog[CANCEL].ob_flags = FL_SELECTABLE|FL_EXIT|FL_LASTOB;
    dialog[CANCEL].ob_state = ST_NORMAL;
    dialog[CANCEL].ob_spec.userblk = &blocks[1];
    dialog[CANCEL].ob_x = 112;
    dialog[CANCEL].ob_y = 56;
    dialog[CANCEL].ob_width = 64;
    dialog[CANCEL].ob_height = 16;
}

int main(int argc, char **argv)
{
    short wchar, hchar, wbox, hbox;
    short pressed;

    if (appl_init() < 0)
    {
        printf("No AES to talk to.\n");
        return 1;
    }

    handle = graf_handle(&wchar, &hchar, &wbox, &hbox);

    build_dialog();

    form_dial(FMD_START, 0, 0, 0, 0,
              dialog[ROOT].ob_x, dialog[ROOT].ob_y,
              dialog[ROOT].ob_width, dialog[ROOT].ob_height);

    objc_draw(dialog, ROOT, 8, dialog[ROOT].ob_x, dialog[ROOT].ob_y,
              dialog[ROOT].ob_width, dialog[ROOT].ob_height);

    pressed = form_do(dialog, 0);

    form_dial(FMD_FINISH, 0, 0, 0, 0,
              dialog[ROOT].ob_x, dialog[ROOT].ob_y,
              dialog[ROOT].ob_width, dialog[ROOT].ob_height);

    dialog[pressed].ob_state &= ~ST_SELECTED;

    printf("%s\n", (pressed == OK) ? "OK" : "Cancel");

    appl_exit();

    return 0;
}
