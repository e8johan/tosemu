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
 * Icons and images, which are the objects that are pictures.
 *
 * Everything else in a tree is a word or a string, and these are neither: the
 * object points at a block, the block points at a form, and the form is a
 * bitmap in the machine's memory. So this checks the whole chain by reading
 * back the pixels it ends in.
 *
 * The two forms have their first and last bit set and nothing in between,
 * which is the shape that says whether the halves of every word came over the
 * right way round. A form is words, the machine keeps them the other way
 * round, and a copy that forgot to turn them over would put those two bits
 * eight pixels in from where they belong - which is the sort of wrong that
 * looks right until it is measured.
 *
 * An icon is drawn twice over: the mask in the background colour and then the
 * image in the foreground colour, so the three colours the checks look for say
 * which of the two reached the screen where. Three is more than the screen a
 * GEM application usually runs on has, so the suite asks for the ST's low
 * resolution one through TOSEMU_SCREEN: on a monochrome screen the mask and
 * the image are both black and there is nothing to tell apart.
 *
 * A test that stops the emulator prints nothing further, so the count at the
 * end is what says the whole file ran.
 */

#include <stdio.h>
#include <string.h>
#include <gem.h>

#define OB_BOX      (20)
#define OB_IMAGE    (23)
#define OB_ICON     (31)

#define FL_NONE     (0x0000)
#define FL_LASTOB   (0x0020)

#define ST_NORMAL   (0x0000)

/* Colours as the VDI numbers them, which is what v_get_pixel answers with */
#define WHITE       (0)
#define BLACK       (1)
#define RED         (2)

/* ROOT is the first object of any tree and gem.h already says so */
#define IMAGE   (1)
#define ICON    (2)

/* Where the dialog is, and where the two objects are inside it */
#define BOX_X   (40)
#define BOX_Y   (40)
#define BOX_W   (128)
#define BOX_H   (96)

#define IMG_X   (8)
#define IMG_Y   (8)

#define ICO_X   (8)
#define ICO_Y   (24)

/* And where they land, which is what the pixels are read at */
#define IMG_SX  (BOX_X + IMG_X)
#define IMG_SY  (BOX_Y + IMG_Y)
#define ICO_SX  (BOX_X + ICO_X)
#define ICO_SY  (BOX_Y + ICO_Y)

/* The icon's label sits below the icon itself, and its own rectangle is drawn
 * in the background colour before the words go into it */
#define TXT_W   (32)
#define TXT_H   (8)
#define TXT_SX  (ICO_SX)
#define TXT_SY  (ICO_SY + 8)

/*
 * The icon's colours and character, packed the way an ICONBLK packs them:
 * foreground in the top four bits, background in the next four, and the
 * character in the bottom eight. No character, so that nothing is drawn over
 * the two forms except the label.
 */
#define ICO_COLOURS (0x1200)

static int n;

static void check(long got, long want, const char *name)
{
    n++;
    if (got == want)
        printf("ok %d - %s\n", n, name);
    else
        printf("not ok %d - %s (got %ld, want %ld)\n", n, name, got, want);
}

static OBJECT tree[3];
static BITBLK image;
static ICONBLK icon;

/* The first and last bit of the word, and nothing else */
static short image_data[4] = { 0x8001, 0x0000, 0x0000, 0x0000 };

/* The icon is the same again, over a mask that covers all of it */
static short icon_mask[8] = { 0xffff, 0xffff, 0xffff, 0xffff,
                              0xffff, 0xffff, 0xffff, 0xffff };
static short icon_data[8] = { 0x8001, 0x0000, 0x0000, 0x0000,
                              0x0000, 0x0000, 0x0000, 0x0000 };

static char icon_label[] = "Icon";

static short handle;

/* The colour of one pixel, which is the second of the two answers v_get_pixel
 * gives - the first is the value in the planes */
static short at(short x, short y)
{
    short pel, index;

    v_get_pixel(handle, x, y, &pel, &index);

    return index;
}

/* Whether anything was drawn in the given colour anywhere in a rectangle,
 * which is how the label is looked for: where its letters land is the font's
 * business and not this test's */
static int any(short x, short y, short w, short h, short colour)
{
    short i, j;

    for (j = y; j < y + h; j++)
        for (i = x; i < x + w; i++)
            if (at(i, j) == colour)
                return 1;

    return 0;
}

static void build_tree(void)
{
    tree[ROOT].ob_next = -1;
    tree[ROOT].ob_head = IMAGE;
    tree[ROOT].ob_tail = ICON;
    tree[ROOT].ob_type = OB_BOX;
    tree[ROOT].ob_flags = FL_NONE;
    tree[ROOT].ob_state = ST_NORMAL;
    tree[ROOT].ob_spec.index = 0x00021100L;
    tree[ROOT].ob_x = BOX_X;
    tree[ROOT].ob_y = BOX_Y;
    tree[ROOT].ob_width = BOX_W;
    tree[ROOT].ob_height = BOX_H;

    /* Sixteen pixels across - two bytes - and four lines down */
    image.bi_pdata = image_data;
    image.bi_wb = 2;
    image.bi_hl = 4;
    image.bi_x = 0;
    image.bi_y = 0;
    image.bi_color = BLACK;

    tree[IMAGE].ob_next = ICON;
    tree[IMAGE].ob_head = -1;
    tree[IMAGE].ob_tail = -1;
    tree[IMAGE].ob_type = OB_IMAGE;
    tree[IMAGE].ob_flags = FL_NONE;
    tree[IMAGE].ob_state = ST_NORMAL;
    tree[IMAGE].ob_spec.bitblk = &image;
    tree[IMAGE].ob_x = IMG_X;
    tree[IMAGE].ob_y = IMG_Y;
    tree[IMAGE].ob_width = 16;
    tree[IMAGE].ob_height = 4;

    icon.ib_pmask = icon_mask;
    icon.ib_pdata = icon_data;
    icon.ib_ptext = icon_label;
    icon.ib_char = ICO_COLOURS;
    icon.ib_xchar = 0;
    icon.ib_ychar = 0;

    /* Both of these are relative to where the object is, and the AES adds the
     * object's own corner on before it draws */
    icon.ib_xicon = 0;
    icon.ib_yicon = 0;
    icon.ib_wicon = 16;
    icon.ib_hicon = 8;
    icon.ib_xtext = 0;
    icon.ib_ytext = 8;
    icon.ib_wtext = TXT_W;
    icon.ib_htext = TXT_H;

    tree[ICON].ob_next = ROOT;
    tree[ICON].ob_head = -1;
    tree[ICON].ob_tail = -1;
    tree[ICON].ob_type = OB_ICON;
    tree[ICON].ob_flags = FL_LASTOB;
    tree[ICON].ob_state = ST_NORMAL;
    tree[ICON].ob_spec.iconblk = &icon;
    tree[ICON].ob_x = ICO_X;
    tree[ICON].ob_y = ICO_Y;
    tree[ICON].ob_width = TXT_W;
    tree[ICON].ob_height = 16;
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

    build_tree();

    objc_draw(tree, ROOT, 8, tree[ROOT].ob_x, tree[ROOT].ob_y,
              tree[ROOT].ob_width, tree[ROOT].ob_height);

    /*
     * The image, which is drawn in bi_color wherever a bit is set and leaves
     * everything else as it was.
     */
    check(at(IMG_SX, IMG_SY), BLACK, "the first bit of an image is drawn");
    check(at(IMG_SX + 15, IMG_SY), BLACK, "and the last bit of the word");
    check(at(IMG_SX + 8, IMG_SY), WHITE,
          "with nothing between them, so the word came over unswapped");
    check(at(IMG_SX, IMG_SY + 1), WHITE, "the line below it is empty");

    /*
     * The icon, whose mask is drawn first in the background colour and whose
     * image goes over it in the foreground colour. So a pixel the image sets
     * says the image arrived, and one only the mask covers says the mask did.
     */
    check(at(ICO_SX, ICO_SY), BLACK, "the first bit of an icon is drawn");
    check(at(ICO_SX + 15, ICO_SY), BLACK, "and the last bit of the word");
    check(at(ICO_SX + 8, ICO_SY), RED,
          "with the mask showing between them, unswapped as well");
    check(at(ICO_SX, ICO_SY + 1), RED, "and the mask below it");
    check(at(ICO_SX, ICO_SY + 7), RED, "down to the last line of the icon");

    /*
     * And the label, which is a string hanging off the block rather than off
     * the object. Its rectangle is filled with the background colour before
     * the letters are drawn into it, and neither happens for an icon with
     * nothing to say.
     */
    check(at(TXT_SX, TXT_SY), RED, "the label's rectangle is filled");
    check(any(TXT_SX, TXT_SY, TXT_W, TXT_H, BLACK), 1,
          "and the label itself is drawn in it");

    printf("1..%d\n", n);

    appl_exit();

    return 0;
}
