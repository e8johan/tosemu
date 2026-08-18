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
#include <string.h>
#include <time.h>

#include "gem_p.h"
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
static int16_t wait_for(int16_t wanted, long timeout, int16_t *message)
{
    long deadline = (timeout < 0) ? -1 : now_ms() + timeout;

    /*
     * Whatever was drawn since the last wait goes on the screen now. An
     * application draws and then waits, over and over, so this is where a
     * picture is finished as far as anyone watching is concerned.
     */
    gfx_present();

    for (;;)
    {
        struct pollfd fds[1];
        int nfds = 0;
        long left;
        int wayland;

        if ((wanted & MU_MESAG) && message_take(message))
            return MU_MESAG;

        /*
         * The compositor's connection, which has to be listened to whether or
         * not the application asked for anything from it: it is where a ping
         * arrives, and a ping that goes unanswered is how a window comes to be
         * declared not responding.
         */
        wayland = gfx_fd();
        if (wayland >= 0)
        {
            gfx_flush();
            fds[nfds].fd = wayland;
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
             * Waiting for ever for something that cannot arrive. Saying so is
             * better than hanging: the application has asked for an event from
             * a device nothing is reading, and no amount of waiting will
             * produce one.
             */
            halt_execution();
            printf("AES: an application is waiting for an event that nothing "
                   "can deliver yet\n");
            return 0;
        }

        poll(fds, nfds, (left < 0) ? -1 : (int)left);

        if (wayland >= 0 && (fds[0].revents & POLLIN))
            gfx_dispatch();
    }
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
        wait_for(MU_TIMER, ms, 0);
    else
        gfx_present();

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

    if (wait_for(MU_MESAG, -1, message) != MU_MESAG)
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
    long timeout;

    FUNC_TRACE_ENTER_ARGS {
        printf("    wanted: 0x%x, timer: %ld ms\n", wanted, ms);
    }

    /*
     * An application asking for a keypress or the mouse is asking for
     * something no part of this is reading yet. Waiting would be waiting for
     * ever, so it is told which piece is missing instead.
     */
    if (wanted & MU_INPUT)
    {
        halt_execution();
        printf("AES evnt_multi was asked for keyboard or mouse events, which "
               "need the compositor\n");
        return 0;
    }

    /* A timer of zero milliseconds means no timer at all rather than one that
     * has already expired */
    if (!(wanted & MU_TIMER) || ms == 0)
        timeout = -1;
    else
        timeout = ms;

    happened = wait_for(wanted, timeout, message);

    if (happened == MU_MESAG)
        message_to_application(buffer, message);

    /*
     * Where the mouse was and what was held down when it happened. None of it
     * moves yet, so it is all zero, but the application is told the same
     * number of things either way.
     */
    aes_set_intout(1, 0);   /* mouse x */
    aes_set_intout(2, 0);   /* mouse y */
    aes_set_intout(3, 0);   /* buttons */
    aes_set_intout(4, 0);   /* shift, control and alt */
    aes_set_intout(5, 0);   /* the key */
    aes_set_intout(6, 0);   /* how many clicks */

    return happened;
}
