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
 * The title bar is in it, and whether it is drawn is not this file's to decide.
 * A window is shown in a window of the desktop's, and either the desktop puts
 * its own frame round the outside - in which case a second title bar inside the
 * first is the picture of another computer that having real windows was meant
 * to avoid - or it does not, and GEM's own is the only one there is. aeswind.c
 * knows which and hands over the rectangle that is actually being shown; this
 * draws a frame to fit it.
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

/* Anything that puts a strip along the top, which is the title bar */
#define W_STRIP (W_NAME|W_CLOSE|W_FULL|W_MOVE)

/* The object types, obdefs.h */
#define G_BOX      (20)
#define G_IBOX     (25)
#define G_BOXTEXT  (22)
#define G_BOXCHAR  (27)

/* What a TEDINFO says about the words in a box, obdefs.h. The font is the one
 * the system draws everything else in, and centred is where a window's name
 * goes. */
#define IBM        (3)
#define TE_LEFT    (0)
#define TE_CNTR    (2)

/*
 * The colours of a window's name, which is the one part of the frame that says
 * whether this is the window somebody is working in.
 *
 * Both are a black border and black text. The topped one is opaque and filled
 * with the second pattern, which is what makes the light hatching behind the
 * name of the window in front; the untopped one is drawn through, so the name
 * sits on the bar with nothing behind it. These are EmuTOS's two words
 * unchanged - see gemwmlib.c - so a title bar looks like a title bar.
 */
#define TOPPED_COLOR   (0x11a1)
#define UNTOPPED_COLOR (0x1100)

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
#define SPEC_TITLE    (0x00011101L)
#define SPEC_SLIDE    (0x00011111L)
#define SPEC_ELEV     (0x00011101L)
#define SPEC_CLOSER   (0x05011101L)
#define SPEC_FULLER   (0x07011101L)
#define SPEC_UPARROW  (0x01011101L)
#define SPEC_DNARROW  (0x02011101L)
#define SPEC_RTARROW  (0x03011101L)
#define SPEC_LFARROW  (0x04011101L)
#define SPEC_SIZER    (0x06011101L)

/* Where each one is in the tree being built */
enum {
    /* Not in the frame at all, which is most of the screen */
    FRAME_NOTHING = -1,

    F_BOX,
    F_TITLE, F_CLOSER, F_NAME, F_FULLER,
    F_VBAR, F_UPARROW, F_DNARROW, F_VSLIDE, F_VELEV,
    F_HBAR, F_LFARROW, F_RTARROW, F_HSLIDE, F_HELEV,
    F_SIZER,
    F_COUNT
};

/* One object as it is being put together, before it is handed across */
struct piece {
    int16_t next, head, tail;
    int16_t parent;
    int16_t type;
    void *spec;
    int16_t x, y, w, h;         /* Where it is inside its parent */
    int used;
};

static struct piece piece[F_COUNT];

/*
 * The window's name, as the thing a G_BOXTEXT points at.
 *
 * Every other gadget in the frame is a colour word packed into the object
 * itself; a box with words in it is the one kind that needs a structure of its
 * own, and it has to be one the AES can read, which means below the four
 * gigabyte line. One is made the first time a title bar is drawn and used for
 * every window afterwards: only one frame is ever being drawn at a time, and
 * nothing keeps a pointer to it once the drawing is done.
 */
static void *name_ted;

static void add(int16_t parent, int16_t child, int16_t type, void *spec,
                int16_t x, int16_t y, int16_t w, int16_t h)
{
    piece[child].used = 1;
    piece[child].parent = parent;
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
 * The words in the title bar, as something the object renderer can draw.
 *
 * Answers with null when there is nowhere to put a TEDINFO, which leaves the
 * name out of the frame and everything else in it - a title bar with no words
 * on it is worth more than no title bar at all.
 */
static void *name_of(char *name, int active)
{
    static const int16_t words[8] = {
        IBM, 0, TE_CNTR, UNTOPPED_COLOR, 0, 1, 80, 80
    };
    int16_t mine[8];
    int i;

    if (!name_ted)
        name_ted = emuvdi_tedinfo_alloc();

    if (!name_ted)
        return 0;

    for (i = 0; i < 8; i++)
        mine[i] = words[i];

    mine[3] = active ? TOPPED_COLOR : UNTOPPED_COLOR;

    emuvdi_tedinfo_set(name_ted, name ? name : "", "", "", mine, 8);

    return name_ted;
}

/*
 * Lays out the frame inside the rectangle of the window that is being shown.
 *
 * Everything is worked out inside that rectangle, so the gadgets land where
 * the application was told its work area was not. What is in it depends on
 * what the window was created with and on how much of the window the desktop
 * is showing: a title bar is only drawn when the caller says the rectangle
 * still has room for one, which it has when the desktop is not drawing a frame
 * of its own round the outside.
 */
static int lay_out(const struct aes_frame *f)
{
    int16_t kind = f->kind;
    int16_t w = f->w, h = f->h;
    int16_t handle, wchar, hchar, wbox, hbox;
    int16_t work_w, work_h;
    int16_t below;
    int have_title, have_vbar, have_hbar;
    int i;

    if (w <= 0 || h <= 0)
        return 0;

    have_title = (kind & W_STRIP) != 0 && f->name != 0;
    have_vbar = (kind & (W_UPARROW|W_DNARROW|W_VSLIDE|W_SIZE)) != 0;
    have_hbar = (kind & (W_LFARROW|W_RTARROW|W_HSLIDE|W_SIZE)) != 0;

    /* Nothing to draw. A window with no gadgets is its work area and the
     * desktop's frame round the outside, which is the whole of it. */
    if (!have_title && !have_vbar && !have_hbar)
        return 0;

    emuvdi_graf_handle(&handle, &wchar, &hchar, &wbox, &hbox);

    memset(piece, 0, sizeof piece);
    for (i = 0; i < F_COUNT; i++)
        piece[i].next = piece[i].head = piece[i].tail = NONE;

    /* The whole of what is shown, which is the parent of everything else */
    piece[F_BOX].used = 1;
    piece[F_BOX].type = G_IBOX;
    piece[F_BOX].spec = (void *)(uintptr_t)SPEC_TITLE;
    piece[F_BOX].x = f->x;
    piece[F_BOX].y = f->y;
    piece[F_BOX].w = w;
    piece[F_BOX].h = h;

    /*
     * The title bar: a strip across the top with a close box at its left end,
     * a full box at its right and the window's name between them.
     *
     * Both boxes are drawn whether or not this is the window in front, where
     * GEM drew them on the topped window alone. On an ST that was the honest
     * answer - only the top window could be closed, and clicking anywhere in
     * another one topped it first - and here it is not: every window is one of
     * the desktop's and a click lands in the one it was aimed at, so a close
     * box that is there but not drawn would be a trap.
     */
    below = 0;

    if (have_title)
    {
        int16_t left = 0, room = w;

        add(F_BOX, F_TITLE, G_BOX, (void *)(uintptr_t)SPEC_TITLE,
            0, 0, w, hbox);

        if (kind & W_CLOSE)
        {
            add(F_TITLE, F_CLOSER, G_BOXCHAR, (void *)(uintptr_t)SPEC_CLOSER,
                left, 0, wbox, hbox);
            left += wbox;
            room -= wbox;
        }

        if (kind & W_FULL)
        {
            room -= wbox;
            add(F_TITLE, F_FULLER, G_BOXCHAR, (void *)(uintptr_t)SPEC_FULLER,
                (int16_t)(left + room), 0, wbox, hbox);
        }

        if (kind & W_NAME)
        {
            void *ted = name_of(f->name, f->active);

            if (ted)
                add(F_TITLE, F_NAME, G_BOXTEXT, ted, left, 0, room, hbox);
        }

        below = hbox;
    }

    /* The work area, which is not drawn but says how much room the bars have.
     * A pixel of border all round, and then whichever bars there are. */
    work_w = w - 2;
    work_h = (int16_t)(h - below) - 2;
    if (have_vbar)
        work_w -= wbox - 1;
    if (have_hbar)
        work_h -= hbox - 1;

    if (have_vbar)
    {
        int16_t top = 0, room = work_h + 2;

        add(F_BOX, F_VBAR, G_BOX, (void *)(uintptr_t)SPEC_BAR,
            (int16_t)(1 + work_w), below, wbox, room);

        if (kind & W_UPARROW)
        {
            add(F_VBAR, F_UPARROW, G_BOXCHAR, (void *)(uintptr_t)SPEC_UPARROW,
                0, top, wbox, hbox);
            top += hbox - 1;
            room -= hbox - 1;
        }

        if (kind & W_DNARROW)
        {
            room -= hbox - 1;
            add(F_VBAR, F_DNARROW, G_BOXCHAR, (void *)(uintptr_t)SPEC_DNARROW,
                0, (int16_t)(top + room - 1), wbox, hbox);
        }

        if (kind & W_VSLIDE)
        {
            int16_t size, where;

            add(F_VBAR, F_VSLIDE, G_BOX, (void *)(uintptr_t)SPEC_SLIDE,
                0, top, wbox, room);

            elevator(room, hbox, f->vslsize, f->vslide, &size, &where);
            add(F_VSLIDE, F_VELEV, G_BOX, (void *)(uintptr_t)SPEC_ELEV,
                0, where, wbox, size);
        }
    }

    if (have_hbar)
    {
        int16_t left = 0, room = work_w + 2;

        add(F_BOX, F_HBAR, G_BOX, (void *)(uintptr_t)SPEC_BAR,
            0, (int16_t)(below + 1 + work_h), room, hbox);

        if (kind & W_LFARROW)
        {
            add(F_HBAR, F_LFARROW, G_BOXCHAR, (void *)(uintptr_t)SPEC_LFARROW,
                0, 0, wbox, hbox);
            left += wbox - 1;
            room -= wbox - 1;
        }

        if (kind & W_RTARROW)
        {
            room -= wbox - 1;
            add(F_HBAR, F_RTARROW, G_BOXCHAR, (void *)(uintptr_t)SPEC_RTARROW,
                (int16_t)(left + room - 1), 0, wbox, hbox);
        }

        if (kind & W_HSLIDE)
        {
            int16_t size, where;

            add(F_HBAR, F_HSLIDE, G_BOX, (void *)(uintptr_t)SPEC_SLIDE,
                left, 0, room, hbox);

            elevator(room, wbox, f->hslsize, f->hslide, &size, &where);
            add(F_HSLIDE, F_HELEV, G_BOX, (void *)(uintptr_t)SPEC_ELEV,
                where, 0, size, hbox);
        }
    }

    /* The size box, in the corner where the two bars meet. It only has its
     * diagonal lines in when the window is the one in front, and every window
     * here is in front of itself. */
    if (have_vbar && have_hbar)
        add(F_BOX, F_SIZER, G_BOXCHAR,
            (void *)(uintptr_t)((kind & W_SIZE) ? SPEC_SIZER : SPEC_BAR),
            (int16_t)(w - wbox), (int16_t)(h - hbox), wbox, hbox);

    return 1;
}

/*
 * Where each part of the frame actually is on the screen.
 *
 * An object's place is inside its parent, which is what makes a tree cheap to
 * move and awkward to click on, so the chain is added up once here. Both the
 * drawing and the finding come out of lay_out, which is the whole point: a
 * gadget that is drawn somewhere and found somewhere else is the kind of thing
 * nobody notices until they try to use it.
 */
static void absolute(int which, int16_t *ax, int16_t *ay)
{
    int16_t sx = 0, sy = 0;
    int walk = which;
    int steps;

    for (steps = 0; steps < F_COUNT && walk >= 0; steps++)
    {
        sx += piece[walk].x;
        sy += piece[walk].y;

        if (walk == F_BOX)
            break;

        walk = piece[walk].parent;
    }

    *ax = sx;
    *ay = sy;
}

static int inside(int which, int16_t px, int16_t py)
{
    int16_t ax, ay;

    if (!piece[which].used)
        return 0;

    absolute(which, &ax, &ay);

    return px >= ax && px < ax + piece[which].w
        && py >= ay && py < ay + piece[which].h;
}

/*
 * Which part of the frame a point is in, and where along a slider it fell.
 *
 * The elevator is looked at before the slide it sits in, the arrows before the
 * bar they are on, and the two boxes in the title bar before the title bar
 * itself, because the smaller thing is the one that was meant.
 */
int aes_frame_hit(const struct aes_frame *frame,
                  int16_t px, int16_t py, int16_t *along)
{
    static const int order[] = {
        F_CLOSER, F_FULLER, F_NAME, F_TITLE,
        F_UPARROW, F_DNARROW, F_VELEV, F_VSLIDE,
        F_LFARROW, F_RTARROW, F_HELEV, F_HSLIDE,
        F_SIZER
    };
    int i;

    if (along)
        *along = 0;

    if (!lay_out(frame))
        return FRAME_NOTHING;

    for (i = 0; i < (int)(sizeof order / sizeof order[0]); i++)
    {
        int which = order[i];

        if (!inside(which, px, py))
            continue;

        /*
         * How far along a slide the point was, in thousandths, which is what
         * an application is told and what it sets a slider with.
         *
         * Worked out for the elevator as well as the slide it sits in, and
         * against the slide either way: a press on the elevator is somebody
         * saying to put it here, and answering nought because the elevator is
         * not the slide would jump the document to the top on every touch.
         *
         * Measured against the room the elevator has to move in rather than
         * the whole slide, so that dragging it to the end means the end
         * rather than the end less its own height.
         */
        if (along
            && (which == F_VSLIDE || which == F_VELEV
                || which == F_HSLIDE || which == F_HELEV))
        {
            int up_and_down = (which == F_VSLIDE || which == F_VELEV);
            int slide = up_and_down ? F_VSLIDE : F_HSLIDE;
            int elev = up_and_down ? F_VELEV : F_HELEV;
            int16_t ax, ay;
            int16_t room, at;

            absolute(slide, &ax, &ay);

            if (up_and_down)
            {
                room = piece[slide].h - piece[elev].h;
                at = py - ay - piece[elev].h / 2;
            }
            else
            {
                room = piece[slide].w - piece[elev].w;
                at = px - ax - piece[elev].w / 2;
            }

            if (at < 0)
                at = 0;

            if (room <= 0)
                *along = 0;
            else
            {
                if (at > room)
                    at = room;

                *along = (int16_t)(((long)at * 1000 + room / 2) / room);
            }
        }

        return which;
    }

    return FRAME_NOTHING;
}

void aes_frame_draw(const struct aes_frame *frame)
{
    void *tree;
    int i;

    if (!lay_out(frame))
        return;

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
                        piece[i].spec,
                        piece[i].x, piece[i].y, piece[i].w, piece[i].h);
    }

    emuvdi_objc_draw(tree, F_BOX, 8, frame->x, frame->y, frame->w, frame->h);

    emuvdi_tree_free(tree);
}
