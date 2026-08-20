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
 * Watching a box, which is how an application that draws its own buttons finds
 * out that one of them was pressed rather than merely pointed at.
 *
 * What this is really checking is which words the arguments arrive in. The
 * graphics manager's calls share one layout, and the first word of it is the
 * parent object that graf_slidebox needs and graf_watchbox has no use for - so
 * watchbox's own arguments start at the second word, and a binding leaves the
 * first alone rather than closing the gap up.
 *
 * Reading them one word too low is not an error anybody is told about. It is
 * an off by one that watches whatever object number happened to be lying in
 * that first word and puts the real object number into the state, which draws
 * as a checkmark, a cross and a grey wash over a button that was only clicked.
 * So the checks below are on which object was touched and what it was left
 * holding, both of which say plainly which word was read.
 *
 * A test that stops the emulator prints nothing further, so the count at the
 * end is what says the whole file ran.
 */

#include <stdio.h>
#include <string.h>
#include <gem.h>

#define OB_BOX      (20)

#define FL_NONE     (0x0000)
#define FL_LASTOB   (0x0020)

#define ST_NORMAL   (0x0000)
#define ST_CHECKED  (0x0004)
#define ST_DISABLED (0x0008)

#define BOX     (1)

/* Where the dialog is, and the box inside it that is watched */
#define BOX_X   (40)
#define BOX_Y   (40)

#define OBJ_X   (16)
#define OBJ_Y   (16)
#define OBJ_W   (32)
#define OBJ_H   (16)

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

/*
 * A parameter block of our own, for the second half of this.
 *
 * The library's binding clears the word watchbox does not use, and an
 * application's need not: the AES ignores it, so whatever the binding last had
 * in that part of the stack stays there. ProCalc's leaves the object number,
 * which is what made reading one word too low look as though it worked.
 */
static short control[5], global[15], intin[16], intout[7];
static long addrin[3], addrout[1];
static AESPB pb = { control, global, intin, intout, addrin, addrout };

#define GRAF_WATCHBOX (75)

static short watchbox_with_a_dirty_word(OBJECT *t, short obj,
                                        short instate, short outstate)
{
    control[0] = GRAF_WATCHBOX;
    control[1] = 4;         /* how many words go in */
    control[2] = 1;         /* and how many come back */
    control[3] = 1;         /* the tree */
    control[4] = 0;

    intin[0] = obj;         /* the word the AES has no use for */
    intin[1] = obj;
    intin[2] = instate;
    intin[3] = outstate;

    addrin[0] = (long)t;

    intout[0] = -1;
    aes(&pb);

    return intout[0];
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
    tree[ROOT].ob_x = BOX_X;
    tree[ROOT].ob_y = BOX_Y;
    tree[ROOT].ob_width = 64;
    tree[ROOT].ob_height = 48;

    tree[BOX].ob_next = ROOT;
    tree[BOX].ob_head = -1;
    tree[BOX].ob_tail = -1;
    tree[BOX].ob_type = OB_BOX;
    tree[BOX].ob_flags = FL_LASTOB;
    tree[BOX].ob_state = ST_NORMAL;
    tree[BOX].ob_spec.index = 0x00011100L;
    tree[BOX].ob_x = OBJ_X;
    tree[BOX].ob_y = OBJ_Y;
    tree[BOX].ob_width = OBJ_W;
    tree[BOX].ob_height = OBJ_H;
}

int main(int argc, char **argv)
{
    short wchar, hchar, wbox, hbox;
    short state;

    if (appl_init() < 0)
    {
        printf("Bail out! - no AES to talk to\n");
        return 1;
    }

    graf_handle(&wchar, &hchar, &wbox, &hbox);

    build_tree();

    objc_draw(tree, ROOT, 8, tree[ROOT].ob_x, tree[ROOT].ob_y,
              tree[ROOT].ob_width, tree[ROOT].ob_height);

    /*
     * The two states are neither of them a plausible object number in a tree
     * this small, so what comes back says whether a state was written or an
     * object number was.
     */
    graf_watchbox(tree, BOX, ST_CHECKED, ST_DISABLED);

    state = tree[BOX].ob_state;

    check(state == ST_CHECKED || state == ST_DISABLED, 1,
          "the watched object is left in one of the two states it was given");
    check(tree[ROOT].ob_state, ST_NORMAL,
          "and the object before it in the tree is not the one that was watched");

    /*
     * And again with the unused word carrying the object number rather than
     * nothing, which is what an application's own binding leaves there. It
     * must make no difference at all: reading from it instead puts the object
     * number into the state, and a number is a handful of state bits - a
     * checkmark, a cross and a grey wash over a button that was only clicked.
     */
    tree[ROOT].ob_state = ST_NORMAL;
    tree[BOX].ob_state = ST_NORMAL;

    watchbox_with_a_dirty_word(tree, BOX, ST_CHECKED, ST_DISABLED);

    state = tree[BOX].ob_state;

    check(state == ST_CHECKED || state == ST_DISABLED, 1,
          "the word the AES has no use for is not read as one it has");
    check(state == BOX, 0,
          "so the object is not left holding its own number as a state");
    check(tree[ROOT].ob_state, ST_NORMAL,
          "and it is still the right object that was watched");

    printf("1..%d\n", n);

    appl_exit();

    return 0;
}
