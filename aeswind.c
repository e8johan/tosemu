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
 * Windows.
 *
 * A GEM window is a rectangle and a list of which decorations it has. The AES
 * owns where it is and what is drawn around it; the application owns what is
 * inside, and is told to draw that again whenever something uncovers it.
 *
 * Everything here works in the virtual screen's coordinates, which is what the
 * application sees and what it draws in. That is deliberate and it is what
 * makes the eventual move to a window each on the compositor possible: a
 * window will be a surface of its own with the compositor deciding where it
 * sits, and Wayland does not let a client know or choose that. Keeping a
 * coordinate space the AES is the authority on means an application goes on
 * getting answers that agree with each other, whatever the compositor does
 * with the windows themselves.
 */

#include "aes_p.h"

#include <string.h>

#include "gem_p.h"
#include "gfx.h"
#include "emuvdi/emuvdi.h"
#include "tossystem.h"
#include "m68k.h"

/* How many windows one application can have. Real GEM had eight for everyone
 * together; this is per application until there is more than one. */
#define WINDOWS (8)

/* The message an application is sent when its close box is used */
#define WM_REDRAW (20)
#define WM_CLOSED (22)

/* What a window can have around it, which is what wind_create is told
 * http://toshyp.atari.org/en/008009.html */
#define W_NAME     (0x0001)
#define W_CLOSER   (0x0002)
#define W_FULLER   (0x0004)
#define W_MOVER    (0x0008)
#define W_INFO     (0x0010)
#define W_SIZER    (0x0020)
#define W_UPARROW  (0x0040)
#define W_DNARROW  (0x0080)
#define W_VSLIDE   (0x0100)
#define W_LFARROW  (0x0200)
#define W_RTARROW  (0x0400)
#define W_HSLIDE   (0x0800)

/* Anything that puts a strip along the top */
#define W_TITLE (W_NAME|W_CLOSER|W_FULLER|W_MOVER)
/* Anything that puts one down the right */
#define W_RIGHT (W_UPARROW|W_DNARROW|W_VSLIDE)
/* Anything that puts one along the bottom */
#define W_BOTTOM (W_SIZER|W_LFARROW|W_RTARROW|W_HSLIDE)

/* What wind_get and wind_set are asked about */
#define WF_KIND       (1)
#define WF_NAME       (2)
#define WF_INFO       (3)
#define WF_WORKXYWH   (4)
#define WF_CURRXYWH   (5)
#define WF_PREVXYWH   (6)
#define WF_FULLXYWH   (7)
#define WF_HSLIDE     (8)
#define WF_VSLIDE     (9)
#define WF_TOP       (10)
#define WF_FIRSTXYWH (11)
#define WF_NEXTXYWH  (12)
#define WF_HSLSIZE   (15)
#define WF_VSLSIZE   (16)
#define WF_SCREEN    (17)

/* Which way wind_calc is being asked to work */
#define WC_BORDER (0)
#define WC_WORK   (1)

struct window {
    int used;
    int open;
    int16_t kind;

    /* Where it is now, and where it was before it was last moved */
    int16_t x, y, w, h;
    int16_t px, py, pw, ph;

    /* Where the sliders sit and how large they are, in thousandths, which is
     * how GEM says it */
    int16_t hslide, vslide;
    int16_t hslsize, vslsize;

    uint32_t name;      /* The title and the information line, as addresses */
    uint32_t info;      /* in the machine, because that is where they live */

    /* The title again, as text, for the frame the desktop puts round it */
    char title[64];
};

static struct window windows[WINDOWS];

/* Which window is on top, or 0 for the desktop, which is always underneath */
static int16_t topped;

/* Where a window may be put, which is the whole screen below the menu bar */
static int16_t desk_x, desk_y, desk_w, desk_h;

void aes_wind_reset()
{
    memset(windows, 0, sizeof windows);
    topped = 0;
}

/*
 * The desktop, which is what is left of the screen once the menu bar has had
 * its strip. There is no menu bar yet, so it is the screen.
 */
static void desk_area()
{
    int16_t handle, wchar, hchar, wbox, hbox;

    emuvdi_graf_handle(&handle, &wchar, &hchar, &wbox, &hbox);

    desk_x = 0;
    desk_y = hbox;      /* Where the menu bar will be */
    desk_w = emuvdi_screen_width();
    desk_h = emuvdi_screen_height() - hbox;
}

static struct window *window_at(int16_t handle)
{
    if (handle < 1 || handle > WINDOWS)
        return 0;

    if (!windows[handle-1].used)
        return 0;

    return &windows[handle-1];
}

/*
 * Between the whole of a window and the part the application draws in.
 *
 * Each decoration takes a strip off one edge, and which strips depends on what
 * the window was created with. A window with no decorations at all is its own
 * work area, which is how a GEM application draws on the desktop without a
 * frame around it.
 */
/*
 * The part of a window the desktop shows.
 *
 * Not the whole of it. A GEM window carries its own frame - a title bar to
 * drag it by, a close box, sliders - and the desktop this one is shown on
 * carries a frame of its own round the outside. Showing both means a title bar
 * inside a title bar, which is exactly the picture of another computer that
 * having real windows was meant to avoid.
 *
 * So the title bar is left out and the desktop's stands in for it: it drags
 * the window, it closes it, and it says what the window is called, which are
 * the three things GEM's did. Everything else in the frame stays, because
 * nothing on the desktop does those jobs - an information line says what the
 * application wants it to say, and sliders scroll a document rather than a
 * window.
 *
 * Nothing here changes what the application sees. It asked for a window of a
 * certain size at a certain place in the screen the AES keeps, and that is
 * what it has; this is only which rectangle of that screen is put in front of
 * somebody.
 */
static void window_on_show(int16_t kind, int16_t *x, int16_t *y,
                           int16_t *w, int16_t *h)
{
    int16_t handle, wchar, hchar, wbox, hbox;

    (void)x;

    if (!(kind & W_TITLE))
        return;

    emuvdi_graf_handle(&handle, &wchar, &hchar, &wbox, &hbox);

    *y += hbox;
    *h -= hbox;
}

/* Draws a window's frame where it is now, with the sliders where they are
 * now. Everything that moves one or changes the other comes through here, so
 * that what is on the screen and what the window says are never two answers. */
static void draw_frame(struct window *win)
{
    int16_t x = win->x, y = win->y, w = win->w, h = win->h;

    if (!win->open)
        return;

    window_on_show(win->kind, &x, &y, &w, &h);

    aes_frame_draw(win->kind, x, y, w, h,
                   win->hslide, win->hslsize, win->vslide, win->vslsize);
}

static void border_to_work(int16_t kind, int16_t *x, int16_t *y,
                           int16_t *w, int16_t *h)
{
    int16_t handle, wchar, hchar, wbox, hbox;

    emuvdi_graf_handle(&handle, &wchar, &hchar, &wbox, &hbox);

    if (kind & W_TITLE)
    {
        *y += hbox;
        *h -= hbox;
    }

    if (kind & W_INFO)
    {
        *y += hbox;
        *h -= hbox;
    }

    if (kind & W_RIGHT)
        *w -= wbox;

    if (kind & W_BOTTOM)
        *h -= hbox;
}

static void work_to_border(int16_t kind, int16_t *x, int16_t *y,
                           int16_t *w, int16_t *h)
{
    int16_t handle, wchar, hchar, wbox, hbox;

    emuvdi_graf_handle(&handle, &wchar, &hchar, &wbox, &hbox);

    if (kind & W_TITLE)
    {
        *y -= hbox;
        *h += hbox;
    }

    if (kind & W_INFO)
    {
        *y -= hbox;
        *h += hbox;
    }

    if (kind & W_RIGHT)
        *w += wbox;

    if (kind & W_BOTTOM)
        *h += hbox;
}

/* Four words of an answer, which is what most of wind_get is */
static void answer_rect(int16_t x, int16_t y, int16_t w, int16_t h)
{
    aes_set_intout(1, x);
    aes_set_intout(2, y);
    aes_set_intout(3, w);
    aes_set_intout(4, h);
}

/* wind_create *************************************************************/

uint32_t AES_wind_create()
{
    int16_t kind = aes_intin(0);
    int i;

    FUNC_TRACE_ENTER_ARGS {
        printf("    kind: 0x%x, %d,%d %dx%d\n", kind, aes_intin(1),
               aes_intin(2), aes_intin(3), aes_intin(4));
    }

    if (!gem_start())
        return -1;

    for (i = 0; i < WINDOWS; i++)
    {
        if (windows[i].used)
            continue;

        memset(&windows[i], 0, sizeof windows[i]);

        windows[i].used = 1;
        windows[i].kind = kind;

        /* The largest it may become, which is what it was created with */
        windows[i].x = aes_intin(1);
        windows[i].y = aes_intin(2);
        windows[i].w = aes_intin(3);
        windows[i].h = aes_intin(4);

        /* A slider that has not been set covers everything and sits at the
         * start, which is what an application that never sets one wants */
        windows[i].hslsize = 1000;
        windows[i].vslsize = 1000;

        return i + 1;
    }

    /* Out of windows, which GEM reports as a handle nobody can use */
    return -1;
}

/* wind_open ***************************************************************/

uint32_t AES_wind_open()
{
    struct window *win = window_at(aes_intin(0));

    FUNC_TRACE_ENTER_ARGS {
        printf("    handle: %d, %d,%d %dx%d\n", aes_intin(0), aes_intin(1),
               aes_intin(2), aes_intin(3), aes_intin(4));
    }

    if (!win)
        return AES_ERROR;

    win->x = aes_intin(1);
    win->y = aes_intin(2);
    win->w = aes_intin(3);
    win->h = aes_intin(4);

    win->px = win->x;
    win->py = win->y;
    win->pw = win->w;
    win->ph = win->h;

    win->open = 1;
    topped = aes_intin(0);

    /*
     * And a window of the desktop's to show it in. The whole of the window
     * goes in, frame and all, because that frame is GEM's and the application
     * put it there - what the desktop adds around the outside is its own.
     */
    /* The frame first, so that there is something in the window the moment it
     * appears rather than a flash of whatever was there before */
    draw_frame(win);

    {
        int16_t sx = win->x, sy = win->y, sw = win->w, sh = win->h;

        window_on_show(win->kind, &sx, &sy, &sw, &sh);
        gfx_window_open(aes_intin(0), win->title, sx, sy, sw, sh);
    }

    /*
     * And tell the application to paint it.
     *
     * A window arrives empty. The AES draws the frame, because the frame is
     * the AES's, and everything inside it belongs to the application - which
     * finds out that there is something to paint the same way it finds out
     * about every other time: a message saying which window and which part of
     * it. An application that draws only when asked, which is most of them,
     * shows nothing at all without this.
     */
    {
        int16_t message[8];
        int i;

        for (i = 0; i < 8; i++)
            message[i] = 0;

        message[0] = WM_REDRAW;
        message[3] = aes_intin(0);
        message[4] = win->x;
        message[5] = win->y;
        message[6] = win->w;
        message[7] = win->h;

        aes_message_post(message);
    }

    return AES_E_OK;
}

/*
 * The desktop's own close box, standing in for the one GEM draws.
 *
 * An application is sent exactly what it would have been sent had its own
 * closer been clicked, because as far as it is concerned that is what
 * happened. Nothing closes here: a GEM window is closed by the application
 * asking for it, and an application is entitled to ask something first.
 */
void host_window_closed(int16_t handle)
{
    int16_t message[8];
    int i;

    for (i = 0; i < 8; i++)
        message[i] = 0;

    message[0] = WM_CLOSED;
    message[3] = handle;

    aes_message_post(message);
}

/* wind_close **************************************************************/

uint32_t AES_wind_close()
{
    struct window *win = window_at(aes_intin(0));

    FUNC_TRACE_ENTER

    if (!win)
        return AES_ERROR;

    win->open = 0;

    if (topped == aes_intin(0))
        topped = 0;

    gfx_window_close(aes_intin(0));

    return AES_E_OK;
}

/* wind_delete *************************************************************/

uint32_t AES_wind_delete()
{
    struct window *win = window_at(aes_intin(0));

    FUNC_TRACE_ENTER

    if (!win)
        return AES_ERROR;

    memset(win, 0, sizeof *win);

    if (topped == aes_intin(0))
        topped = 0;

    gfx_window_close(aes_intin(0));

    return AES_E_OK;
}

/* wind_get ****************************************************************/

uint32_t AES_wind_get()
{
    int16_t handle = aes_intin(0);
    int16_t what = aes_intin(1);
    struct window *win;

    FUNC_TRACE_ENTER_ARGS {
        printf("    handle: %d, what: %d\n", handle, what);
    }

    desk_area();

    /* The desktop is a window like any other as far as this is concerned, and
     * is what an application asks about to find out how large the screen is */
    if (handle == 0)
    {
        switch (what)
        {
            case WF_WORKXYWH:
            case WF_CURRXYWH:
            case WF_FULLXYWH:
                answer_rect(desk_x, desk_y, desk_w, desk_h);
                return AES_E_OK;
            case WF_SCREEN:
                /* Where the AES keeps its own buffer, which it does not have
                 * one of here. Answering with nothing is how an application is
                 * told to do without it. */
                answer_rect(0, 0, 0, 0);
                return AES_E_OK;
            case WF_TOP:
                aes_set_intout(1, topped);
                return AES_E_OK;
        }
    }

    win = window_at(handle);
    if (!win)
        return AES_ERROR;

    switch (what)
    {
        case WF_KIND:
            aes_set_intout(1, win->kind);
            break;

        case WF_WORKXYWH:
        {
            int16_t x = win->x, y = win->y, w = win->w, h = win->h;

            border_to_work(win->kind, &x, &y, &w, &h);
            answer_rect(x, y, w, h);
            break;
        }

        case WF_CURRXYWH:
            answer_rect(win->x, win->y, win->w, win->h);
            break;

        case WF_PREVXYWH:
            answer_rect(win->px, win->py, win->pw, win->ph);
            break;

        case WF_FULLXYWH:
            answer_rect(desk_x, desk_y, desk_w, desk_h);
            break;

        /*
         * What of the window is visible, as a list of rectangles ending in an
         * empty one. On a real machine that list is what is left after every
         * window above has been cut out of it, which is why it is a list.
         *
         * Here a window will be a surface of its own and the compositor does
         * the covering, so nothing is ever cut out and the list is the whole
         * work area and then nothing. That is not a simplification of the
         * answer, it is what the answer is when no window overlaps.
         */
        case WF_FIRSTXYWH:
        {
            int16_t x = win->x, y = win->y, w = win->w, h = win->h;

            border_to_work(win->kind, &x, &y, &w, &h);
            answer_rect(x, y, w, h);
            break;
        }

        case WF_NEXTXYWH:
            answer_rect(0, 0, 0, 0);
            break;

        case WF_TOP:
            aes_set_intout(1, topped);
            break;

        case WF_HSLIDE:
            aes_set_intout(1, win->hslide);
            break;

        case WF_VSLIDE:
            aes_set_intout(1, win->vslide);
            break;

        case WF_HSLSIZE:
            aes_set_intout(1, win->hslsize);
            break;

        case WF_VSLSIZE:
            aes_set_intout(1, win->vslsize);
            break;

        default:
            halt_execution();
            printf("AES wind_get was asked about %d, which is not implemented\n",
                   what);
            return AES_ERROR;
    }

    return AES_E_OK;
}

/* wind_set ****************************************************************/

uint32_t AES_wind_set()
{
    int16_t handle = aes_intin(0);
    int16_t what = aes_intin(1);
    struct window *win = window_at(handle);

    FUNC_TRACE_ENTER_ARGS {
        printf("    handle: %d, what: %d\n", handle, what);
    }

    /* Setting something about the desktop is how the menu bar and the desktop
     * background are put up, neither of which exists yet */
    if (handle == 0)
        return AES_E_OK;

    if (!win)
        return AES_ERROR;

    switch (what)
    {
        case WF_NAME:
        {
            int i;

            /* The title, which stays where the application put it: it owns
             * the string and may change it without telling anyone */
            win->name = ((uint32_t)(uint16_t)aes_intin(2) << 16)
                      | (uint16_t)aes_intin(3);

            /* And a copy, because the frame the desktop draws needs the text
             * rather than an address in a machine it cannot read */
            for (i = 0; i < (int)sizeof win->title - 1; i++)
            {
                win->title[i] = (char)m68k_read_memory_8(win->name + i);
                if (win->title[i] == 0)
                    break;
            }
            win->title[i] = 0;

            gfx_window_title(handle, win->title);
            break;
        }

        case WF_INFO:
            win->info = ((uint32_t)(uint16_t)aes_intin(2) << 16)
                      | (uint16_t)aes_intin(3);
            break;

        case WF_CURRXYWH:
            win->px = win->x;
            win->py = win->y;
            win->pw = win->w;
            win->ph = win->h;

            win->x = aes_intin(2);
            win->y = aes_intin(3);
            win->w = aes_intin(4);
            win->h = aes_intin(5);

            draw_frame(win);

            {
                int16_t sx = win->x, sy = win->y, sw = win->w, sh = win->h;

                window_on_show(win->kind, &sx, &sy, &sw, &sh);
                gfx_window_move(handle, sx, sy, sw, sh);
            }
            break;

        /* The four that move a slider or change how large it is. Each one
         * changes what the frame looks like, so each one redraws it. */
        case WF_HSLIDE:
            win->hslide = aes_intin(2);
            draw_frame(win);
            break;

        case WF_VSLIDE:
            win->vslide = aes_intin(2);
            draw_frame(win);
            break;

        case WF_HSLSIZE:
            win->hslsize = aes_intin(2);
            draw_frame(win);
            break;

        case WF_VSLSIZE:
            win->vslsize = aes_intin(2);
            draw_frame(win);
            break;

        case WF_TOP:
            topped = handle;
            break;

        default:
            halt_execution();
            printf("AES wind_set was asked to set %d, which is not "
                   "implemented\n", what);
            return AES_ERROR;
    }

    return AES_E_OK;
}

/* wind_calc ***************************************************************/

uint32_t AES_wind_calc()
{
    int16_t which = aes_intin(0);
    int16_t kind = aes_intin(1);
    int16_t x = aes_intin(2);
    int16_t y = aes_intin(3);
    int16_t w = aes_intin(4);
    int16_t h = aes_intin(5);

    FUNC_TRACE_ENTER_ARGS {
        printf("    %s, kind 0x%x, %d,%d %dx%d\n",
               which == WC_BORDER ? "work to border" : "border to work",
               kind, x, y, w, h);
    }

    if (!gem_start())
        return AES_ERROR;

    if (which == WC_BORDER)
        work_to_border(kind, &x, &y, &w, &h);
    else
        border_to_work(kind, &x, &y, &w, &h);

    answer_rect(x, y, w, h);

    return AES_E_OK;
}

/* wind_update *************************************************************/

uint32_t AES_wind_update()
{
    FUNC_TRACE_ENTER

    /*
     * Taking and giving back the right to draw outside one's own windows, and
     * the right to the mouse while doing it. Both were locks held against the
     * other applications, and taking one always succeeds here.
     *
     * The daemon did not change that, which is worth saying because it was
     * expected to. The lock existed because every application drew into one
     * screen: two of them painting outside their windows at once painted over
     * each other. Here each application has a screen of its own and what
     * reaches the desktop are its windows, so there is nothing for the lock to
     * protect - two applications cannot reach each other'"'"'s pixels to spoil
     * them. Building a lock across a socket for that would be ceremony.
     *
     * It becomes real again when windows of different applications overlap and
     * have to be composed from one surface rather than shown side by side. See
     * the TODO.
     */
    return AES_E_OK;
}



/* wind_find and wind_new **************************************************/

/*
 * wind_find - which window a point is in
 *
 * The one in front that covers it, and nought for the desktop where none does.
 * An application uses it to work out what a click landed on when it was
 * watching the whole screen rather than one of its own windows.
 *
 * Windows here are the desktop's and it decides which is in front, so the
 * answer comes from the AES's own idea of the order rather than from anything
 * the compositor knows. That is the same answer an application would have got
 * on an ST, and it is the answer it is asking about: it wants to know which of
 * its windows a coordinate in the screen belongs to.
 */
uint32_t AES_wind_find()
{
    int16_t x = aes_intin(0);
    int16_t y = aes_intin(1);
    int16_t handle;

    FUNC_TRACE_ENTER_ARGS {
        printf("    at %d,%d\n", x, y);
    }

    /* The topped one first, because it is the one in front */
    if (topped > 0)
    {
        struct window *win = window_at(topped);

        if (win && win->open
            && x >= win->x && x < win->x + win->w
            && y >= win->y && y < win->y + win->h)
            return (uint32_t)(uint16_t)topped;
    }

    for (handle = 1; handle < WINDOWS; handle++)
    {
        struct window *win = window_at(handle);

        if (!win || !win->open)
            continue;

        if (x >= win->x && x < win->x + win->w
            && y >= win->y && y < win->y + win->h)
            return (uint32_t)(uint16_t)handle;
    }

    /* The desktop, which is what is there when no window is */
    return 0;
}

/*
 * wind_new - take every window away and start again
 *
 * What an application calls when it is about to give up and does not trust
 * itself to have closed everything, and what a desktop calls between one
 * program and the next. It is wind_close and wind_delete for all of them at
 * once, without the application having to remember which it had.
 */
uint32_t AES_wind_new()
{
    int16_t handle;

    FUNC_TRACE_ENTER

    for (handle = 1; handle < WINDOWS; handle++)
    {
        struct window *win = window_at(handle);

        if (!win || !win->used)
            continue;

        if (win->open)
            gfx_window_close(handle);

        memset(win, 0, sizeof *win);
    }

    topped = 0;

    return AES_E_OK;
}


/* Clicking the frame ******************************************************/

/*
 * What each gadget is worth, as a message.
 *
 * The names here are aesframe.c's, which are about what a thing looks like,
 * and the messages are GEM's, which are about what it means. Keeping the two
 * apart is what lets the frame be drawn differently one day without the
 * meaning of clicking on it changing.
 */
#define WM_ARROWED (24)
#define WM_HSLID   (25)
#define WM_VSLID   (26)
#define WM_SIZED   (27)

/* What WM_ARROWED says happened, http://toshyp.atari.org/en/005010.html */
#define WA_UPPAGE  (0)
#define WA_DNPAGE  (1)
#define WA_UPLINE  (2)
#define WA_DNLINE  (3)
#define WA_LFPAGE  (4)
#define WA_RTPAGE  (5)
#define WA_LFLINE  (6)
#define WA_RTLINE  (7)

/* The pieces of the frame, named as aesframe.c numbers them */
enum {
    HIT_NOTHING = -1,
    HIT_BOX,
    HIT_VBAR, HIT_UPARROW, HIT_DNARROW, HIT_VSLIDE, HIT_VELEV,
    HIT_HBAR, HIT_LFARROW, HIT_RTARROW, HIT_HSLIDE, HIT_HELEV,
    HIT_SIZER
};

static void send_to_owner(int16_t what, int16_t handle, int16_t a, int16_t b,
                          int16_t c, int16_t d)
{
    int16_t message[8];
    int i;

    for (i = 0; i < 8; i++)
        message[i] = 0;

    message[0] = what;
    message[3] = handle;
    message[4] = a;
    message[5] = b;
    message[6] = c;
    message[7] = d;

    aes_message_post(message);
}

/*
 * A press somewhere, which may have been on a window's frame.
 *
 * The AES does not scroll anything itself and never did: it works out which
 * gadget was pressed and tells the application, which knows what its document
 * is and how far a line of it goes. So all of this ends in a message.
 *
 * The one thing it does do is move the elevator, because an application that
 * is told where the slider went expects to see it there - and it would
 * otherwise have to set it back itself in answer to every drag.
 */
int aes_wind_frame_press(int16_t x, int16_t y)
{
    int16_t handle;

    for (handle = 1; handle < WINDOWS; handle++)
    {
        struct window *win = window_at(handle);
        int16_t sx, sy, sw, sh, along = 0;
        int hit;

        if (!win || !win->open)
            continue;

        sx = win->x; sy = win->y; sw = win->w; sh = win->h;
        window_on_show(win->kind, &sx, &sy, &sw, &sh);

        if (x < sx || x >= sx + sw || y < sy || y >= sy + sh)
            continue;

        hit = aes_frame_hit(win->kind, sx, sy, sw, sh,
                            win->hslide, win->hslsize,
                            win->vslide, win->vslsize, x, y, &along);

        switch (hit)
        {
            case HIT_UPARROW:
                send_to_owner(WM_ARROWED, handle, WA_UPLINE, 0, 0, 0);
                return 1;
            case HIT_DNARROW:
                send_to_owner(WM_ARROWED, handle, WA_DNLINE, 0, 0, 0);
                return 1;
            case HIT_LFARROW:
                send_to_owner(WM_ARROWED, handle, WA_LFLINE, 0, 0, 0);
                return 1;
            case HIT_RTARROW:
                send_to_owner(WM_ARROWED, handle, WA_RTLINE, 0, 0, 0);
                return 1;

            /*
             * The slide either side of the elevator is a page rather than a
             * line, which is what makes a long document reachable without
             * dragging: above the elevator is back a screenful, below it is on
             * a screenful.
             */
            case HIT_VSLIDE:
                send_to_owner(WM_ARROWED, handle,
                              (along < win->vslide) ? WA_UPPAGE : WA_DNPAGE,
                              0, 0, 0);
                return 1;
            case HIT_HSLIDE:
                send_to_owner(WM_ARROWED, handle,
                              (along < win->hslide) ? WA_LFPAGE : WA_RTPAGE,
                              0, 0, 0);
                return 1;

            /*
             * The elevator itself, which is where a person says how far
             * through the document they want to be. The slider is moved here
             * as well as reported, because being told where it went and seeing
             * it stay put is the one thing that would look broken.
             */
            case HIT_VELEV:
                win->vslide = along;
                draw_frame(win);
                send_to_owner(WM_VSLID, handle, along, 0, 0, 0);
                return 1;
            case HIT_HELEV:
                win->hslide = along;
                draw_frame(win);
                send_to_owner(WM_HSLID, handle, along, 0, 0, 0);
                return 1;

            case HIT_SIZER:
                /*
                 * Resizing is the desktop's here - a window is a window of its
                 * own and is resized by its edges - so this says the size it
                 * already has rather than a new one. An application that
                 * redraws for it loses nothing; one that ignores it loses
                 * nothing either.
                 */
                send_to_owner(WM_SIZED, handle, win->x, win->y,
                              win->w, win->h);
                return 1;

            default:
                /* Inside the window, but on the part that is the
                 * application's. It hears about that as a click, not as a
                 * message. */
                return 0;
        }
    }

    return 0;
}
