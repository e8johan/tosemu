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
 * A box with one character in it, which is the smallest object GEM has.
 *
 * It is also the one whose ob_spec is packed hardest. Everything else that
 * shows words points at them; this keeps the character in the spec itself,
 * in the top byte, with the border width under it and the colours under that.
 * Reaching a byte out of the middle of a long is where a machine's idea of
 * which end comes first stops being somebody else's problem, and the AES draws
 * a great many of these: every drive letter in the file selector, the arrows
 * on every scroll bar, the close box on the list, the sizer in the corner of a
 * window. Reading the wrong byte is not a wrong character but no character at
 * all, because the byte at the other end of those specs is nought.
 *
 * So the checks come in pairs. Each says something was drawn, which catches
 * the byte that is nought, and then that two boxes differing only in that byte
 * were drawn differently, which catches any other way of arriving at one
 * character for all of them. Which character it is remains the font's business
 * and is not asked about here.
 *
 * A test that stops the emulator prints nothing further, so the count at the
 * end is what says the whole file ran.
 */

#include <stdio.h>
#include <gem.h>

#define OB_BOX      (20)
#define OB_BOXCHAR  (27)

#define FL_NONE     (0x0000)
#define FL_LASTOB   (0x0020)

#define ST_NORMAL   (0x0000)

/* Colours as the VDI numbers them, which is what v_get_pixel answers with */
#define WHITE       (0)
#define BLACK       (1)

/*
 * The four boxes, and the spec each one is given.
 *
 * These are the shapes the AES's own resource uses rather than invented ones.
 * A drive letter is 0x41ff1100 - the letter, a border of -1, and a colour word
 * saying a black border and black text on nothing - and the arrows a window
 * puts on its scroll bars are 0x01011101 and its like. Both patterns are here
 * because they fail differently: the first ends in a byte of nought and drew
 * nothing at all, the second ends in a byte of one and drew an up arrow four
 * times over.
 */
#define LETTER_A    (1)
#define LETTER_B    (2)
#define ARROW_UP    (3)
#define ARROW_DOWN  (4)
#define BLANK       (5)

#define SPEC_A      (0x41ff1100L)
#define SPEC_B      (0x42ff1100L)
#define SPEC_UP     (0x01011101L)
#define SPEC_DOWN   (0x02011101L)
#define SPEC_BLANK  (0x20ff1100L)

#define BOXES       (5)

/* Where the tree is put. Nothing is drawn under it, so what the characters
 * are read against is a white screen. */
#define TREE_X      (40)
#define TREE_Y      (40)

/*
 * A border is drawn on the edge of each box and is not what is being looked
 * at, so the pixels are read from inside it.
 */
#define MARGIN      (2)

static int n;

static void check(long got, long want, const char *name)
{
    n++;
    if (got == want)
        printf("ok %d - %s\n", n, name);
    else
        printf("not ok %d - %s (got %ld, want %ld)\n", n, name, got, want);
}

static OBJECT tree[BOXES + 1];

static short handle;

/* How wide and tall a box is, which is a font's worth of room and then some
 * so that the character has somewhere to be centred */
static short bw, bh;

/* The colour of one pixel, which is the second of the two answers v_get_pixel
 * gives - the first is the value in the planes */
static short at(short x, short y)
{
    short pel, index;

    v_get_pixel(handle, x, y, &pel, &index);

    return index;
}

/* Where a box ended up on the screen */
static void corner(short obj, short *x, short *y)
{
    *x = TREE_X + tree[obj].ob_x;
    *y = TREE_Y + tree[obj].ob_y;
}

/* How much ink is inside one box, which says whether a character was drawn
 * without saying which one */
static int ink(short obj)
{
    short x, y, i, j;
    int found = 0;

    corner(obj, &x, &y);

    for (j = MARGIN; j < bh - MARGIN; j++)
        for (i = MARGIN; i < bw - MARGIN; i++)
            if (at(x + i, y + j) == BLACK)
                found++;

    return found;
}

/* And whether two boxes have the same ink in the same places, which is what
 * says two specs drew the same character */
static int same(short a, short b)
{
    short ax, ay, bx, by, i, j;

    corner(a, &ax, &ay);
    corner(b, &bx, &by);

    for (j = MARGIN; j < bh - MARGIN; j++)
        for (i = MARGIN; i < bw - MARGIN; i++)
            if (at(ax + i, ay + j) != at(bx + i, by + j))
                return 0;

    return 1;
}

static void one_box(short obj, long spec)
{
    tree[obj].ob_next = (obj == BOXES) ? ROOT : obj + 1;
    tree[obj].ob_head = -1;
    tree[obj].ob_tail = -1;
    tree[obj].ob_type = OB_BOXCHAR;
    tree[obj].ob_flags = (obj == BOXES) ? FL_LASTOB : FL_NONE;
    tree[obj].ob_state = ST_NORMAL;
    tree[obj].ob_spec.index = spec;

    /* In a row, with a gap between them so that one box's border cannot be
     * read as the next one's character */
    tree[obj].ob_x = (obj - 1) * (bw + 4);
    tree[obj].ob_y = 0;
    tree[obj].ob_width = bw;
    tree[obj].ob_height = bh;
}

static void build_tree(void)
{
    tree[ROOT].ob_next = -1;
    tree[ROOT].ob_head = LETTER_A;
    tree[ROOT].ob_tail = BLANK;
    tree[ROOT].ob_type = OB_BOX;
    tree[ROOT].ob_flags = FL_NONE;
    tree[ROOT].ob_state = ST_NORMAL;

    /* No border, and filled solid white, so that every black pixel inside a
     * box came from the character rather than from what was on the screen */
    tree[ROOT].ob_spec.index = 0x00001170L;
    tree[ROOT].ob_x = TREE_X;
    tree[ROOT].ob_y = TREE_Y;
    tree[ROOT].ob_width = BOXES * (bw + 4);
    tree[ROOT].ob_height = bh + 4;

    one_box(LETTER_A, SPEC_A);
    one_box(LETTER_B, SPEC_B);
    one_box(ARROW_UP, SPEC_UP);
    one_box(ARROW_DOWN, SPEC_DOWN);
    one_box(BLANK, SPEC_BLANK);
}

int main(int argc, char **argv)
{
    short wchar, hchar, wbox, hbox;

    if (appl_init() < 0)
    {
        printf("Bail out! - no AES to talk to\n");
        return 1;
    }

    handle = graf_handle(&wchar, &hchar, &wbox, &hbox);

    bw = 3 * wchar;
    bh = 2 * hchar;

    build_tree();

    objc_draw(tree, ROOT, 8, tree[ROOT].ob_x, tree[ROOT].ob_y,
              tree[ROOT].ob_width, tree[ROOT].ob_height);

    /*
     * A drive letter, whose spec ends in a byte of nought. Every one of the
     * twenty-six in the file selector is this shape.
     */
    check(ink(LETTER_A) > 0, 1, "a letter in a box is drawn");
    check(ink(LETTER_B) > 0, 1, "and so is the next letter");
    check(same(LETTER_A, LETTER_B), 0,
          "and they are different letters, so the top byte is what is read");

    /*
     * An arrow off a scroll bar, whose spec ends in a byte of one. This pair
     * is the same question asked where the wrong answer is a character rather
     * than nothing: read the wrong end and both of these are an up arrow.
     */
    check(ink(ARROW_UP) > 0, 1, "an arrow in a box is drawn");
    check(ink(ARROW_DOWN) > 0, 1, "and so is the one pointing the other way");
    check(same(ARROW_UP, ARROW_DOWN), 0, "and they point different ways");

    /*
     * And a space, which is a character that draws nothing - so an empty box
     * is something the object can ask for, and the ones above are not empty
     * for want of anything to draw.
     */
    check(ink(BLANK), 0, "a space in a box draws nothing");

    printf("1..%d\n", n);

    appl_exit();

    return 0;
}
