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
 * Objects the application draws itself.
 *
 * A G_USERDEF object is drawn by a routine belonging to the application, so
 * drawing one means the AES calling back into the program that asked. This
 * checks that the call arrives, that everything in the block it is handed says
 * what it should, that what the routine returns comes back, and that what it
 * drew landed on the screen.
 *
 * The routine writes down what it was told rather than checking it, because it
 * runs inside objc_draw and a failing check there would print in the middle of
 * the AES rather than in the list below.
 *
 * A test that stops the emulator prints nothing further, so the count at the
 * end is what says the whole file ran.
 */

#include <stdio.h>
#include <string.h>
#include <gem.h>

#define OB_BOX      (20)
#define OB_USERDEF  (24)

#define FL_NONE     (0x0000)
#define FL_LASTOB   (0x0020)

#define ST_NORMAL   (0x0000)
#define ST_SELECTED (0x0001)

#define WHITE       (0)
#define BLACK       (1)

#define BOX     (1)

/* Where the object is, inside the root box */
#define OBJ_X   (16)
#define OBJ_Y   (16)
#define OBJ_W   (32)
#define OBJ_H   (16)

/* What the application puts in the block for the routine to be handed back.
 * Any long will do; one that is nothing else makes a wrong answer obvious. */
#define THE_PARM (0x5AC0FFEEL)

/* What the routine answers with, which the AES goes on to draw the object in.
 * Nought means it has all been dealt with. */
#define THE_ANSWER (0)

static int n;

static void check(long got, long want, const char *name)
{
    n++;
    if (got == want)
        printf("ok %d - %s\n", n, name);
    else
        printf("not ok %d - %s (got %ld, want %ld)\n", n, name, got, want);
}

static OBJECT tree[2];
static USERBLK block;

static short handle;

/* What the routine was told, kept for the checks below to look at */
static struct {
    int calls;
    OBJECT *tree;
    short obj;
    short prevstate, currstate;
    short x, y, w, h;
    short xc, yc, wc, hc;
    long parm;
    short flags_from_tree;
    short sr;
    short masked;
} told;

/* The supervisor bit of the status register, and the interrupt mask beside it.
 * Reading the register is allowed either way on a 68000; changing it is not. */
#define SR_SUPERVISOR (0x2000)
#define SR_MASK_ALL   (0x0700)

/*
 * The routine.
 *
 * It draws one pixel, in the middle of the object, which is enough to say
 * afterwards that the VDI calls it makes reach the same screen the AES is
 * drawing on. Everything else it does is remember what it was given.
 */
static short draw_it(PARMBLK *pb)
{
    short pxy[4];

    told.calls++;

    /*
     * Which mode the machine is in, before anything else can change it.
     *
     * An application reaches the AES through a trap, so everything the AES
     * does for it happens in supervisor mode, and a routine of this kind is
     * called from inside that. Routines were written knowing it: masking the
     * interrupts out of the way while something is drawn is an ordinary thing
     * for one to do, and touching the status register at all is privileged.
     * Called in user mode it would be a privilege violation, into a vector
     * table with nothing in it.
     *
     * So the mask is put on and taken off again here rather than only asked
     * about, because the instruction that does it is the thing that has to
     * work. It is only tried when the mode says it can be: taking a privilege
     * violation instead would stop the machine, and a check that says which
     * mode it was in is worth more than one that says nothing at all.
     */
    __asm__ volatile ("move.w %%sr,%0" : "=d" (told.sr));

    if (told.sr & SR_SUPERVISOR)
    {
        short back = told.sr;

        __asm__ volatile ("ori.w %0,%%sr\n\tmove.w %1,%%sr"
                          :
                          : "i" (SR_MASK_ALL), "d" (back)
                          : "cc");
        told.masked = 1;
    }

    told.tree = pb->pb_tree;
    told.obj = pb->pb_obj;
    told.prevstate = pb->pb_prevstate;
    told.currstate = pb->pb_currstate;
    told.x = pb->pb_x;
    told.y = pb->pb_y;
    told.w = pb->pb_w;
    told.h = pb->pb_h;
    told.xc = pb->pb_xc;
    told.yc = pb->pb_yc;
    told.wc = pb->pb_wc;
    told.hc = pb->pb_hc;
    told.parm = pb->pb_parm;

    /* Read out of the tree the routine was handed, which is the application's
     * own rather than any copy of it: a copy would have the flags too, so what
     * this really checks is that the address is one this program can read */
    told.flags_from_tree = pb->pb_tree[pb->pb_obj].ob_flags;

    pxy[0] = pb->pb_xc;
    pxy[1] = pb->pb_yc;
    pxy[2] = pb->pb_xc + pb->pb_wc - 1;
    pxy[3] = pb->pb_yc + pb->pb_hc - 1;
    vs_clip(handle, 1, pxy);

    vsf_interior(handle, FIS_SOLID);
    vsf_color(handle, BLACK);
    vsf_perimeter(handle, 0);

    pxy[0] = pxy[2] = pb->pb_x + pb->pb_w/2;
    pxy[1] = pxy[3] = pb->pb_y + pb->pb_h/2;
    v_bar(handle, pxy);

    return THE_ANSWER;
}

static void build_tree(void)
{
    tree[ROOT].ob_next = -1;
    tree[ROOT].ob_head = BOX;
    tree[ROOT].ob_tail = BOX;
    tree[ROOT].ob_type = OB_BOX;
    tree[ROOT].ob_flags = FL_NONE;
    tree[ROOT].ob_state = ST_NORMAL;
    tree[ROOT].ob_spec.index = 0x00021100L;
    tree[ROOT].ob_x = 40;
    tree[ROOT].ob_y = 40;
    tree[ROOT].ob_width = 64;
    tree[ROOT].ob_height = 48;

    block.ub_code = draw_it;
    block.ub_parm = THE_PARM;

    tree[BOX].ob_next = ROOT;
    tree[BOX].ob_head = -1;
    tree[BOX].ob_tail = -1;
    tree[BOX].ob_type = OB_USERDEF;
    tree[BOX].ob_flags = FL_LASTOB;
    tree[BOX].ob_state = ST_NORMAL;
    tree[BOX].ob_spec.userblk = &block;
    tree[BOX].ob_x = OBJ_X;
    tree[BOX].ob_y = OBJ_Y;
    tree[BOX].ob_width = OBJ_W;
    tree[BOX].ob_height = OBJ_H;
}

int main(int argc, char **argv)
{
    short wchar, hchar, wbox, hbox;
    short pel, index;
    short x = 40 + OBJ_X, y = 40 + OBJ_Y;
    short sr_before, sr_after;

    if (appl_init() < 0)
    {
        printf("Bail out! - no AES to talk to\n");
        return 1;
    }

    handle = graf_handle(&wchar, &hchar, &wbox, &hbox);

    build_tree();

    memset(&told, 0, sizeof told);

    __asm__ volatile ("move.w %%sr,%0" : "=d" (sr_before));

    objc_draw(tree, ROOT, 8, tree[ROOT].ob_x, tree[ROOT].ob_y,
              tree[ROOT].ob_width, tree[ROOT].ob_height);

    __asm__ volatile ("move.w %%sr,%0" : "=d" (sr_after));

    check(told.calls, 1, "the routine is called once for one object");
    check((long)told.tree, (long)tree, "it is handed the application's tree");
    check(told.obj, BOX, "and which object in it");
    check(told.parm, THE_PARM, "the long from the block comes back unchanged");
    check(told.flags_from_tree, FL_LASTOB,
          "the tree it is handed can be read");

    /* The mode it runs in, which is the AES's rather than the application's */
    check(told.sr & SR_SUPERVISOR, SR_SUPERVISOR,
          "the routine runs in supervisor mode, the way GEM called one");
    check(told.masked, 1,
          "so it may mask the interrupts while it draws");
    check(sr_after & SR_SUPERVISOR, sr_before & SR_SUPERVISOR,
          "and the application is left in the mode it was in");

    /* The object's own place, in screen coordinates rather than relative to
     * the box it sits in - the AES has already added the parent's corner on */
    check(told.x, x, "the object's x");
    check(told.y, y, "the object's y");
    check(told.w, OBJ_W, "the object's width");
    check(told.h, OBJ_H, "the object's height");

    /* Where the drawing may go, which is what objc_draw was asked to redraw */
    check(told.xc, tree[ROOT].ob_x, "the clipping x");
    check(told.yc, tree[ROOT].ob_y, "the clipping y");
    check(told.wc, tree[ROOT].ob_width, "the clipping width");
    check(told.hc, tree[ROOT].ob_height, "the clipping height");

    check(told.prevstate, ST_NORMAL, "the state it was in");
    check(told.currstate, ST_NORMAL, "and the state it is drawn in");

    /* What the routine drew, on the screen the AES was drawing on. The colour
     * index is the second of the two answers v_get_pixel gives; the first is
     * the value in the planes. */
    v_get_pixel(handle, x + OBJ_W/2, y + OBJ_H/2, &pel, &index);
    check(index, BLACK, "what the routine drew reached the screen");

    /*
     * And again for a change of state rather than a draw. An object that draws
     * itself is asked again when its state changes, and told both states, so
     * that it can rub out what it drew before rather than draw over it.
     */
    memset(&told, 0, sizeof told);

    objc_change(tree, BOX, 0, tree[ROOT].ob_x, tree[ROOT].ob_y,
                tree[ROOT].ob_width, tree[ROOT].ob_height, ST_SELECTED, 1);

    check(told.calls, 1, "a change of state calls the routine too");
    check(told.prevstate, ST_NORMAL, "which is told what the state was");
    check(told.currstate, ST_SELECTED, "and what it is becoming");
    check(tree[BOX].ob_state, ST_SELECTED, "and the tree has the new state");

    printf("1..%d\n", n);

    appl_exit();

    return 0;
}
