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
 * The frame round a window.
 *
 * GEM draws a window's frame as an object tree and then draws the tree, which
 * is a better idea than it sounds: the arrows are characters one to seven of
 * the system font, the bars are boxes, and the elevator is a box inside
 * another box. Nothing about it needs a routine that knows what a slider is.
 *
 * So this builds the same tree and hands it to the same object renderer. The
 * alternative was to draw four gadgets against the VDI by hand, which is more
 * code and would not look like GEM; this way the pixels come out of the code
 * that drew them on an ST.
 *
 * The title bar is not in it. The desktop's own frame stands in for that one -
 * it drags the window, closes it and says what it is called - and a second
 * title bar inside the first is the picture of another computer that having
 * real windows was meant to avoid. What is here is the part nothing on the
 * desktop does: sliders scroll a document rather than a window, and an
 * information line says whatever the application wants it to say.
 *
 * EmuTOS builds this tree too, in gemwmlib.c, and it was not reused. Not
 * because of the drawing, which is what is being borrowed anyway, but because
 * it builds from its own WINDOW table and window list - the same state
 * aeswind.c keeps - and using it would mean keeping two of them in step for
 * ever.
 */

#include "aes_p.h"

#include <string.h>

#include "emuvdi/emuvdi.h"

/* What a window is made of, http://toshyp.atari.org/en/008002.html */
#define W_NAME     (0x0001)
#define W_CLOSE    (0x0002)
#define W_FULL     (0x0004)
#define W_MOVE     (0x0008)
#define W_INFO     (0x0010)
#define W_SIZE     (0x0020)
#define W_UPARROW  (0x0040)
#define W_DNARROW  (0x0080)
#define W_VSLIDE   (0x0100)
#define W_LFARROW  (0x0200)
#define W_RTARROW  (0x0400)
#define W_HSLIDE   (0x0800)

/* The object types, obdefs.h */
#define G_BOX      (20)
#define G_IBOX     (25)
#define G_BOXCHAR  (27)

#define NONE       (-1)

/*
 * What each gadget is drawn as.
 *
 * The high byte is the character, which is where the arrows come from: one is
 * an up arrow, two a down arrow, three a right arrow, four a left arrow and
 * six the diagonal lines in a size box. They are characters of the system font
 * rather than pictures, which is why a frame drawn at a different character
 * size still looks right.
 *
 * The byte below it is the border thickness and the word below that the
 * colours. These are EmuTOS's numbers, unchanged, so that a window looks like
 * a window rather than nearly like one.
 */
#define SPEC_BAR      (0x00011101L)
#define SPEC_SLIDE    (0x00011111L)
#define SPEC_ELEV     (0x00011101L)
#define SPEC_UPARROW  (0x01011101L)
#define SPEC_DNARROW  (0x02011101L)
#define SPEC_RTARROW  (0x03011101L)
#define SPEC_LFARROW  (0x04011101L)
#define SPEC_SIZER    (0x06011101L)

/* Where each one is in the tree being built */
enum {
    F_BOX,
    F_VBAR, F_UPARROW, F_DNARROW, F_VSLIDE, F_VELEV,
    F_HBAR, F_LFARROW, F_RTARROW, F_HSLIDE, F_HELEV,
    F_SIZER,
    F_COUNT
};

/* One object as it is being put together, before it is handed across */
struct piece {
    int16_t next, head, tail;
    int16_t type;
    int32_t spec;
    int16_t x, y, w, h;
    int used;
};

static struct piece piece[F_COUNT];

static void add(int16_t parent, int16_t child, int16_t type, int32_t spec,
                int16_t x, int16_t y, int16_t w, int16_t h)
{
    piece[child].used = 1;
    piece[child].type = type;
    piece[child].spec = spec;
    piece[child].x = x;
    piece[child].y = y;
    piece[child].w = w;
    piece[child].h = h;
    piece[child].head = piece[child].tail = NONE;

    /* On the end of the parent's children, which is a list that runs back to
     * the parent rather than to nothing */
    piece[child].next = parent;

    if (piece[parent].head == NONE)
        piece[parent].head = child;
    else
        piece[piece[parent].tail].next = child;

    piece[parent].tail = child;
}

/* How large a slider's elevator is and where it sits, both in thousandths of
 * the slider, which is how GEM says it */
static void elevator(int16_t along, int16_t least, int16_t size, int16_t where,
                     int16_t *out_size, int16_t *out_where)
{
    int16_t s;

    if (size < 0)
        s = least;
    else
    {
        s = (int16_t)(((long)along * size + 500) / 1000);
        if (s < least)
            s = least;
    }

    if (s > along)
        s = along;

    *out_size = s;
    *out_where = (int16_t)(((long)(along - s) * where + 500) / 1000);
}

/*
 * Draws the frame of a window that is being shown without its title bar.
 *
 * The rectangle is the one being shown - the window less the title bar - and
 * everything is worked out inside it, so the gadgets land where the
 * application was told its work area was not.
 */
void aes_frame_draw(int16_t kind, int16_t x, int16_t y, int16_t w, int16_t h,
                    int16_t hslide, int16_t hslsize,
                    int16_t vslide, int16_t vslsize)
{
    int16_t handle, wchar, hchar, wbox, hbox;
    int16_t work_w, work_h;
    int have_vbar, have_hbar;
    void *tree;
    int i;

    if (w <= 0 || h <= 0)
        return;

    have_vbar = (kind & (W_UPARROW|W_DNARROW|W_VSLIDE|W_SIZE)) != 0;
    have_hbar = (kind & (W_LFARROW|W_RTARROW|W_HSLIDE|W_SIZE)) != 0;

    /* Nothing to draw. A window with no gadgets is its work area and the
     * desktop's frame round the outside, which is the whole of it. */
    if (!have_vbar && !have_hbar)
        return;

    emuvdi_graf_handle(&handle, &wchar, &hchar, &wbox, &hbox);

    memset(piece, 0, sizeof piece);
    for (i = 0; i < F_COUNT; i++)
        piece[i].next = piece[i].head = piece[i].tail = NONE;

    /* The whole of what is shown, which is the parent of everything else */
    piece[F_BOX].used = 1;
    piece[F_BOX].type = G_IBOX;
    piece[F_BOX].spec = SPEC_BAR;
    piece[F_BOX].x = x;
    piece[F_BOX].y = y;
    piece[F_BOX].w = w;
    piece[F_BOX].h = h;

    /* The work area, which is not drawn but says how much room the bars have.
     * A pixel of border all round, and then whichever bars there are. */
    work_w = w - 2;
    work_h = h - 2;
    if (have_vbar)
        work_w -= wbox - 1;
    if (have_hbar)
        work_h -= hbox - 1;

    if (have_vbar)
    {
        int16_t top = 0, room = work_h + 2;

        add(F_BOX, F_VBAR, G_BOX, SPEC_BAR,
            (int16_t)(1 + work_w), 0, wbox, room);

        if (kind & W_UPARROW)
        {
            add(F_VBAR, F_UPARROW, G_BOXCHAR, SPEC_UPARROW, 0, top, wbox, hbox);
            top += hbox - 1;
            room -= hbox - 1;
        }

        if (kind & W_DNARROW)
        {
            room -= hbox - 1;
            add(F_VBAR, F_DNARROW, G_BOXCHAR, SPEC_DNARROW,
                0, (int16_t)(top + room - 1), wbox, hbox);
        }

        if (kind & W_VSLIDE)
        {
            int16_t size, where;

            add(F_VBAR, F_VSLIDE, G_BOX, SPEC_SLIDE, 0, top, wbox, room);

            elevator(room, hbox, vslsize, vslide, &size, &where);
            add(F_VSLIDE, F_VELEV, G_BOX, SPEC_ELEV, 0, where, wbox, size);
        }
    }

    if (have_hbar)
    {
        int16_t left = 0, room = work_w + 2;

        add(F_BOX, F_HBAR, G_BOX, SPEC_BAR,
            0, (int16_t)(1 + work_h), room, hbox);

        if (kind & W_LFARROW)
        {
            add(F_HBAR, F_LFARROW, G_BOXCHAR, SPEC_LFARROW, 0, 0, wbox, hbox);
            left += wbox - 1;
            room -= wbox - 1;
        }

        if (kind & W_RTARROW)
        {
            room -= wbox - 1;
            add(F_HBAR, F_RTARROW, G_BOXCHAR, SPEC_RTARROW,
                (int16_t)(left + room - 1), 0, wbox, hbox);
        }

        if (kind & W_HSLIDE)
        {
            int16_t size, where;

            add(F_HBAR, F_HSLIDE, G_BOX, SPEC_SLIDE, left, 0, room, hbox);

            elevator(room, wbox, hslsize, hslide, &size, &where);
            add(F_HSLIDE, F_HELEV, G_BOX, SPEC_ELEV, where, 0, size, hbox);
        }
    }

    /* The size box, in the corner where the two bars meet. It only has its
     * diagonal lines in when the window is the one in front, and every window
     * here is in front of itself. */
    if (have_vbar && have_hbar)
        add(F_BOX, F_SIZER, G_BOXCHAR,
            (kind & W_SIZE) ? SPEC_SIZER : SPEC_BAR,
            (int16_t)(w - wbox), (int16_t)(h - hbox), wbox, hbox);

    /* And across it goes, in the order it was built, which is the order the
     * indices are in */
    tree = emuvdi_tree_alloc(F_COUNT);
    if (!tree)
        return;

    for (i = 0; i < F_COUNT; i++)
    {
        if (!piece[i].used)
        {
            /* Not in this window. It still has to be an object, because a tree
             * is walked by index, so it is one with nothing in it. */
            emuvdi_tree_set(tree, i, NONE, NONE, NONE, G_IBOX, 0, 0,
                            (void *)(uintptr_t)0, 0, 0, 0, 0);
            continue;
        }

        emuvdi_tree_set(tree, i, piece[i].next, piece[i].head, piece[i].tail,
                        piece[i].type, (i == F_COUNT - 1) ? 0x20 : 0, 0,
                        (void *)(uintptr_t)piece[i].spec,
                        piece[i].x, piece[i].y, piece[i].w, piece[i].h);
    }

    emuvdi_objc_draw(tree, F_BOX, 8, x, y, w, h);

    emuvdi_tree_free(tree);
}
