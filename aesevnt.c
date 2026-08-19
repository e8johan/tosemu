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
 * Waiting for something to happen.
 *
 * A GEM application spends nearly all of its life inside evnt_multi, and every
 * other part of the AES is reached from what comes out of it. It is also the
 * one call that has to stop the emulated machine and wait, which on a real
 * machine the AES did by handing the processor to another application.
 *
 * There is nothing to hand it to here, so it waits the way a host program
 * does. The 68000 is already stopped: a trap is being served, and nothing in
 * the machine runs until the handler returns. So the wait is an ordinary
 * poll on this side, and the machine simply does not notice how long it took.
 *
 * That is what the loop below is, and it is built for the sources that are not
 * there yet as much as for the two that are. A compositor will bring keyboard
 * and mouse on a file descriptor, and the daemon will bring messages from
 * other applications on another; both drop into the same poll beside the
 * timer, and none of the shape here changes when they do.
 */

#include "aes_p.h"

#include <poll.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "gem_p.h"
#include "aesclient.h"
#include "gfx.h"
#include "tossystem.h"
#include "m68k.h"

/* A GEM message is eight words, and the AES never sends anything else */
#define MESSAGE_WORDS (8)
#define MESSAGE_BYTES (MESSAGE_WORDS * 2)

/*
 * What an application can be waiting for, and what it is told happened. The
 * same bits do both jobs, which is how one call serves every kind of waiting.
 * http://toshyp.atari.org/en/006004.html
 */
#define MU_KEYBD  (0x0001)
#define MU_BUTTON (0x0002)
#define MU_M1     (0x0004)
#define MU_M2     (0x0008)
#define MU_MESAG  (0x0010)
#define MU_TIMER  (0x0020)

/* The ones that need something to be watching a device */
#define MU_INPUT  (MU_KEYBD | MU_BUTTON | MU_M1 | MU_M2)

/*
 * The messages waiting for this application.
 *
 * One queue, because there is one application. It becomes a queue to a process
 * when the daemon does the delivering, and the shape of it does not change:
 * something puts messages in and evnt_mesag takes them out.
 */
#define QUEUE_LENGTH (32)

static int16_t queue[QUEUE_LENGTH][MESSAGE_WORDS];
static int queue_head;
static int queue_count;

void aes_evnt_reset()
{
    queue_head = 0;
    queue_count = 0;
}

/* Puts a message where the application will find it. Returns 0 if there is no
 * room, which is what appl_write reports as a failure. */
int aes_message_post(const int16_t *message)
{
    int slot;

    if (queue_count >= QUEUE_LENGTH)
        return 0;

    slot = (queue_head + queue_count) % QUEUE_LENGTH;
    memcpy(queue[slot], message, MESSAGE_BYTES);
    queue_count++;

    return 1;
}

static int message_take(int16_t *message)
{
    if (queue_count == 0)
        return 0;

    memcpy(message, queue[queue_head], MESSAGE_BYTES);
    queue_head = (queue_head + 1) % QUEUE_LENGTH;
    queue_count--;

    return 1;
}

/* Milliseconds since some fixed point, which is all a timeout needs */
static long now_ms()
{
    struct timespec t;

    clock_gettime(CLOCK_MONOTONIC, &t);

    return (long)t.tv_sec * 1000 + t.tv_nsec / 1000000;
}

/*
 * Waits until one of the things asked for happens, and says which.
 *
 * timeout is in milliseconds, or -1 to wait for as long as it takes. A
 * timeout of zero is a GEM idiom meaning "wait for ever" when MU_TIMER is
 * asked for, and the caller has already turned that into -1.
 */
/* Whether the pointer is inside a rectangle, or outside it, which is the two
 * things a mouse rectangle event can be waiting for */
static int in_rectangle(int16_t x, int16_t y, const int16_t *r, int16_t leaving)
{
    int inside = x >= r[0] && y >= r[1]
              && x < r[0] + r[2] && y < r[1] + r[3];

    return leaving ? !inside : inside;
}

/*
 * Whether the buttons are as somebody is waiting for them to be.
 *
 * A caller asks for a state rather than for a change: "tell me when button one
 * is down", or "when it is up". If they are already that way the answer is
 * yes, at once, and getting that wrong is what made a click need two clicks -
 * the AES asks for the button to be up after it has already seen it come up,
 * and waiting for another change means waiting for another click.
 */
static int buttons_are(int16_t buttons, int16_t mask, int16_t state)
{
    /* A mask of nothing is a caller that does not care */
    if (mask == 0)
        return 1;

    return (buttons & mask) == (state & mask);
}

/*
 * The button state a wait was last answered with, and whether there was one.
 *
 * A wait is answered by the state as it is, which is right and is what makes
 * one click do one click's worth of work. But the same state must not answer
 * twice: a button is down for as long as somebody holds it, and answering
 * "down" every time it is asked turns one press into a hundred - the file
 * selector walks into the same folder until the path it is building runs off
 * the end of the buffer the application gave it.
 *
 * So a state answers once. After that the wait sleeps until something changes,
 * which is the release, or the next press after it.
 */
static int state_answered;
static int16_t answered_with;

static int16_t wait_for(int16_t wanted, long timeout, int16_t *message,
                        const int16_t *m1, int16_t m1flags,
                        const int16_t *m2, int16_t m2flags,
                        int16_t bmask, int16_t bstate,
                        uint16_t *key, int16_t *mx, int16_t *my,
                        int16_t *buttons)
{
    long deadline = (timeout < 0) ? -1 : now_ms() + timeout;

    /*
     * Whatever was drawn since the last wait goes on the screen now. An
     * application draws and then waits, over and over, so this is where a
     * picture is finished as far as anyone watching is concerned.
     */
    gem_present();

    /*
     * The state that answered the last question is stale once something else
     * has happened, so move on by one before answering this one.
     */
    if (state_answered)
    {
        int16_t b, bx, by;

        if (gfx_button_take(&b, &bx, &by))
            state_answered = 0;
    }

    for (;;)
    {
        struct pollfd fds[2];
        int nfds = 0;
        long left;
        int wayland, daemon;
        int wayland_slot = -1, daemon_slot = -1;

        /*
         * The menu bar, before the application hears about anything.
         *
         * It belongs to the AES rather than to the application, so it is
         * watched whatever the application asked for: one waiting only for a
         * message still gets its menus, and what it eventually hears is the
         * message saying what was chosen.
         */
        {
            int16_t px, py, pb;

            gfx_mouse(&px, &py, &pb);

            if (aes_menu_arrived(px, py))
            {
                aes_menu_click();
                continue;
            }
        }

        if ((wanted & MU_MESAG) && message_take(message))
            return MU_MESAG;

        if ((wanted & MU_KEYBD) && gfx_key_take(key))
            return MU_KEYBD;

        /*
         * The buttons.
         *
         * A wait is for a state - "tell me when button one is down", or "when
         * it is up" - and it is answered the moment the button is that way,
         * not only when it next changes. Getting that wrong is what made a
         * click need two clicks: the AES asks for the button to be up after it
         * has already seen it come up, and waiting for another change means
         * waiting for another click.
         */
        if (wanted & MU_BUTTON)
        {
            int16_t now, nx, ny;

            gfx_mouse(&nx, &ny, &now);

            /*
             * A press answers once. A release answers as often as it is asked.
             *
             * The two are not the same question. A press is something that
             * happened and is acted on - the file selector walks into the
             * folder under the pointer - so answering the same press twice
             * walks in twice, and a button is held for a tenth of a second,
             * which is long enough to do it hundreds of times. A release is a
             * condition rather than an event: fm_button asks whether the
             * button is up after gr_watchbox has already waited for it to come
             * up, and refusing the second of those leaves it waiting for ever.
             *
             * On a machine of the period this never came up. The AES answered
             * both from the level and got away with it, because redrawing a
             * file list took long enough that a person had let go by the time
             * it asked again.
             */
            if (buttons_are(now, bmask, bstate)
                && !(state_answered && now == answered_with
                     && (bstate & bmask) != 0))
            {
                if (buttons)
                    *buttons = now;
                if (mx)
                    *mx = nx;
                if (my)
                    *my = ny;

                state_answered = 1;
                answered_with = now;

                return MU_BUTTON;
            }
        }

        if (wanted & (MU_M1|MU_M2))
        {
            int16_t px, py, pb;
            int16_t which = 0;

            gfx_mouse(&px, &py, &pb);

            if ((wanted & MU_M1) && in_rectangle(px, py, m1, m1flags))
                which = MU_M1;
            else if ((wanted & MU_M2) && in_rectangle(px, py, m2, m2flags))
                which = MU_M2;

            if (which)
            {
                /*
                 * Where the pointer is, which is the answer as much as the
                 * fact that it arrived: the menu works out which title it is
                 * over from this, and cannot be told twice.
                 */
                if (mx)
                    *mx = px;
                if (my)
                    *my = py;
                if (buttons)
                    *buttons = pb;

                return which;
            }
        }

        /*
         * Nothing that was wanted is true of things as they are, so move on to
         * the next thing that happened and ask again.
         *
         * This is the other half of waiting for a state. The state has to be
         * allowed to move on or a button held down answers every wait for a
         * press for ever, with the release sitting behind it in the queue
         * unlooked at - but it must not move on before the state it is in has
         * been considered, or a wait is answered by where the pointer went
         * next rather than by where it is.
         *
         * There is a queue at all because a click can be quicker than anyone
         * looking. Press and release both arriving between two rounds would
         * otherwise be a button that was never down.
         */
        if (gfx_motion_take())
            continue;

        {
            int16_t b, bx, by;

            /*
             * Whether or not this caller cares about the button. On an ST
             * nobody has to drain anything: the button is a level the keyboard
             * processor updates, and a wait either likes what it finds or
             * sleeps. Draining only for callers who asked leaves the rest
             * stuck behind a release nobody wanted - and everything queued
             * behind it, including where the pointer was going next.
             */
            /* Something happened, so the state is new and may answer again */
            if (gfx_button_take(&b, &bx, &by))
            {
                state_answered = 0;
                continue;
            }
        }

        /*
         * The compositor's connection, which has to be listened to whether or
         * not the application asked for anything from it: it is where a ping
         * arrives, and a ping that goes unanswered is how a window comes to be
         * declared not responding.
         */
        /*
         * The screen's window being closed is the machine being told to stop.
         * Halting is not enough from in here: this is inside a trap the AES
         * will not return from, so it stops the way the case below does.
         */
        if (gfx_fd() >= 0 && !gfx_showing())
        {
            printf("The window was closed.\n");
            fflush(stdout);

            halt_execution();
            exit(0);
        }

        wayland = gfx_fd();
        if (wayland >= 0)
        {
            gfx_flush();
            wayland_slot = nfds;
            fds[nfds].fd = wayland;
            fds[nfds].events = POLLIN;
            nfds++;
        }

        /*
         * And the daemon, which has to be listened to whether or not the
         * application asked for a message: another application can send one at
         * any time, and one that arrives while nobody is reading would sit in
         * the socket until something else woke us.
         */
        daemon = aes_client_fd();
        if (daemon >= 0)
        {
            daemon_slot = nfds;
            fds[nfds].fd = daemon;
            fds[nfds].events = POLLIN;
            nfds++;
        }

        if (deadline < 0)
            left = -1;
        else
        {
            left = deadline - now_ms();
            if (left <= 0)
                return (wanted & MU_TIMER) ? MU_TIMER : 0;
        }

        /*
         * Nothing is watching a device yet, so there is nothing in the set and
         * this is a sleep. The compositor's connection and the daemon's socket
         * go here, and then it is a wait on all three at once.
         */
        if (nfds == 0 && left < 0)
        {
            /*
             * Waiting for ever with nothing that could end the wait: no
             * window, so no keyboard and no mouse, and no timer to give up
             * after.
             *
             * Halting is not enough here. The emulated machine stops when a
             * trap returns and the loop notices, and this is inside a trap
             * that the AES will not return from - form_do would go straight
             * back to waiting, and say so again, for ever. There is provably
             * no way forward, so this is where it ends.
             */
            printf("AES: an application is waiting for the keyboard or the "
                   "mouse, and there is no window for either to arrive at.\n"
                   "Run it where there is a compositor, or give it something "
                   "to work with through TOSEMU_KEYS or TOSEMU_CLICKS.\n");
            fflush(stdout);

            halt_execution();
            exit(1);
        }

        poll(fds, nfds, (left < 0) ? -1 : (int)left);

        if (wayland_slot >= 0 && (fds[wayland_slot].revents & POLLIN))
            gfx_dispatch();

        if (daemon_slot >= 0 && (fds[daemon_slot].revents & POLLIN))
            aes_client_pump();
    }
}

/* What the AES reaches when it sends an application a message */
void host_message_post(const int16_t *message)
{
    aes_message_post(message);
}

/*
 * What the AES library files reach when they wait.
 *
 * form_do is a loop over evnt_multi, and evnt_multi in EmuTOS's library
 * reaches ev_multi, which reaches this. Everything a wait needs - the queue,
 * the timer, the compositor - is on this side of the seam, so this is where it
 * ends up rather than somewhere in emuvdi.
 */
int16_t host_event_wait(int16_t wanted, int32_t timeout, int16_t *message,
                        const int16_t *m1, int16_t m1flags,
                        const int16_t *m2, int16_t m2flags,
                        int16_t bmask, int16_t bstate,
                        int16_t *key, int16_t *mx, int16_t *my,
                        int16_t *buttons, int16_t *kstate)
{
    uint16_t k = 0;
    int16_t happened;

    /* Where the pointer is now, unless a button event says where it was when
     * it happened, which wait_for fills in over the top of these */
    gfx_mouse(mx, my, buttons);

    happened = wait_for(wanted, timeout, message, m1, m1flags, m2, m2flags,
                        bmask, bstate, &k, mx, my, buttons);

    *key = (int16_t)k;
    *kstate = (int16_t)gfx_kstate();

    return happened;
}

/* evnt_timer **************************************************************/

uint32_t AES_evnt_timer()
{
    uint16_t lo = (uint16_t)aes_intin(0);
    uint16_t hi = (uint16_t)aes_intin(1);
    long ms = ((long)hi << 16) | lo;

    FUNC_TRACE_ENTER_ARGS {
        printf("    %ld ms\n", ms);
    }

    /* Zero is not a wait at all, it is a way of asking whether anything else
     * is pending, and there is nothing else to be pending yet */
    if (ms > 0)
        wait_for(MU_TIMER, ms, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
    else
        gem_present();

    return AES_E_OK;
}

/* evnt_mesag **************************************************************/

/* Copies a message into the buffer the application named in addrin */
static void message_to_application(uint32_t buffer, const int16_t *message)
{
    int i;

    for (i = 0; i < MESSAGE_WORDS; i++)
        m68k_write_memory_16(buffer + 2*i, (uint16_t)message[i]);
}

uint32_t AES_evnt_mesag()
{
    uint32_t buffer = aes_addrin(0);
    int16_t message[MESSAGE_WORDS];

    FUNC_TRACE_ENTER

    if (wait_for(MU_MESAG, -1, message, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0) != MU_MESAG)
        return AES_ERROR;

    message_to_application(buffer, message);

    return AES_E_OK;
}

/* evnt_multi **************************************************************/

uint32_t AES_evnt_multi()
{
    int16_t wanted = aes_intin(0);
    uint16_t lo = (uint16_t)aes_intin(14);
    uint16_t hi = (uint16_t)aes_intin(15);
    long ms = ((long)hi << 16) | lo;
    uint32_t buffer = aes_addrin(0);
    int16_t message[MESSAGE_WORDS];
    int16_t happened;
    int16_t m1[4], m2[4], m1flags, m2flags;
    int16_t mx, my, buttons;
    uint16_t key = 0;
    long timeout;
    int i;

    FUNC_TRACE_ENTER_ARGS {
        printf("    wanted: 0x%x, timer: %ld ms\n", wanted, ms);
    }

    /* A timer of zero milliseconds means no timer at all rather than one that
     * has already expired */
    if (!(wanted & MU_TIMER) || ms == 0)
        timeout = -1;
    else
        timeout = ms;

    /* The two rectangles, which MU_M1 and MU_M2 wait for the pointer to enter
     * or to leave. The flag before each says which of the two. */
    m1flags = aes_intin(4);
    for (i = 0; i < 4; i++)
        m1[i] = aes_intin(5 + i);
    m2flags = aes_intin(9);
    for (i = 0; i < 4; i++)
        m2[i] = aes_intin(10 + i);

    gfx_mouse(&mx, &my, &buttons);

    happened = wait_for(wanted, timeout, message, m1, m1flags, m2, m2flags,
                        aes_intin(2), aes_intin(3),
                        &key, &mx, &my, &buttons);

    if (happened == MU_MESAG)
        message_to_application(buffer, message);

    /* Where the mouse was and what was held down when it happened, which an
     * application reads whatever it was waiting for */

    aes_set_intout(1, mx);
    aes_set_intout(2, my);
    aes_set_intout(3, buttons);
    aes_set_intout(4, (int16_t)gfx_kstate());
    aes_set_intout(5, (int16_t)key);
    aes_set_intout(6, 1);   /* how many clicks, which is not counted yet */

    return happened;
}


/* The waits that ask for one thing ****************************************/

/*
 * evnt_multi is the general case and these are the particular ones, so each is
 * that call with everything it is not waiting for left out. They are worth
 * having as themselves rather than as advice to use evnt_multi, because a GEM
 * program that only wants a keypress writes evnt_keybd and would otherwise
 * stop dead on a call that is a line long.
 */

uint32_t AES_evnt_keybd()
{
    uint16_t key = 0;
    int16_t mx = 0, my = 0, buttons = 0;

    FUNC_TRACE_ENTER

    if (!gem_start())
        return AES_ERROR;

    wait_for(MU_KEYBD, -1, 0, 0, 0, 0, 0, 0, 0, &key, &mx, &my, &buttons);

    FUNC_TRACE_ARGS {
        printf("    key: 0x%x\n", key);
    }

    return key;
}

/*
 * Waiting for the button to be a certain way.
 *
 * The mask says which buttons are being asked about and the state says how
 * they are to be, so this waits for "button one down" or "button one up"
 * rather than for a click. How many clicks were wanted is asked for and
 * answered, and answered with one: counting them means timing them, and
 * nothing yet asks how quickly two presses followed each other.
 */
uint32_t AES_evnt_button()
{
    int16_t clicks = aes_intin(0);
    int16_t mask = aes_intin(1);
    int16_t state = aes_intin(2);
    uint16_t key = 0;
    int16_t mx = 0, my = 0, buttons = 0;

    FUNC_TRACE_ENTER_ARGS {
        printf("    %d clicks, mask 0x%x, state 0x%x\n", clicks, mask, state);
    }

    if (!gem_start())
        return AES_ERROR;

    gfx_mouse(&mx, &my, &buttons);

    wait_for(MU_BUTTON, -1, 0, 0, 0, 0, 0, mask, state,
             &key, &mx, &my, &buttons);

    aes_set_intout(1, mx);
    aes_set_intout(2, my);
    aes_set_intout(3, buttons);
    aes_set_intout(4, (int16_t)gfx_kstate());

    return 1;
}

/* Waiting for the pointer to arrive in a rectangle, or to leave one */
uint32_t AES_evnt_mouse()
{
    int16_t leaving = aes_intin(0);
    int16_t rect[4];
    uint16_t key = 0;
    int16_t mx = 0, my = 0, buttons = 0;
    int i;

    for (i = 0; i < 4; i++)
        rect[i] = aes_intin(1 + i);

    FUNC_TRACE_ENTER_ARGS {
        printf("    wait to %s %d,%d %dx%d\n", leaving ? "leave" : "enter",
               rect[0], rect[1], rect[2], rect[3]);
    }

    if (!gem_start())
        return AES_ERROR;

    gfx_mouse(&mx, &my, &buttons);

    wait_for(MU_M1, -1, 0, rect, leaving, 0, 0, 0, 0,
             &key, &mx, &my, &buttons);

    gfx_mouse(&mx, &my, &buttons);

    aes_set_intout(1, mx);
    aes_set_intout(2, my);
    aes_set_intout(3, buttons);
    aes_set_intout(4, (int16_t)gfx_kstate());

    return 1;
}

/*
 * How quickly two presses have to follow each other to be one double click.
 *
 * Asked for and set, on a scale of nought to four. Nothing here times a click
 * yet, so the number is remembered and given back rather than acted on - an
 * application that asks is told what it last set, which is what it is entitled
 * to expect, and one that sets it is not stopped.
 */
uint32_t AES_evnt_dclick()
{
    int16_t wanted = aes_intin(0);
    int16_t setting = aes_intin(1);
    static int16_t speed = 3;

    FUNC_TRACE_ENTER_ARGS {
        printf("    %s, %d\n", setting ? "set" : "ask", wanted);
    }

    if (setting && wanted >= 0 && wanted <= 4)
        speed = wanted;

    return (uint32_t)(uint16_t)speed;
}
