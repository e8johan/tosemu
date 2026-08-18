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
 * The daemon, which is what several applications have in common.
 *
 * The AES was one program with every application inside it. What made them one
 * program was not the code they shared - each has its own copy of that here -
 * but the things they could only agree about by being in the same place: which
 * application is which, what the screen looks like, and messages one sends
 * another. A process per application means those have to live somewhere both
 * can reach, and this is that somewhere.
 *
 * It is deliberately small, and the reason is worth writing down. Everything
 * that can be answered inside an application is answered there: every VDI
 * call, every object drawn, every dialog run. What is left is the part that is
 * about more than one application at a time, and if this file grows much past
 * that then something has been put in it that did not need to cross a socket.
 *
 * The screen is the clearest case. It is here not because it is convenient but
 * because it must be one screen: applications lay their windows out in it and
 * are told where the others put theirs, so two that disagree about its size
 * disagree about everything. Which size it is belongs here too - that is what
 * a machine's graphics mode is, one setting for everything running on it.
 */

#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>

#include "aesproto.h"

/*
 * How many applications there can be.
 *
 * GEM had room for the desktop and a handful beside it, and an application
 * asks how many and is told. The number is not a limit worth raising for its
 * own sake: a person does not run forty GEM programs, and something that says
 * it can is saying something nobody checks.
 */
#define MAX_APPS (16)

/* The screen every application shares, until there is a way to say otherwise.
 * See the note at the top: this is where a machine's graphics mode belongs. */
#define SCREEN_WIDTH  (640)
#define SCREEN_HEIGHT (400)
#define SCREEN_PLANES (1)

static struct {
    int used;
    int fd;
    int16_t ap_id;
    char name[AESD_NAME_LEN];
} apps[MAX_APPS];

/*
 * Where the scrap is.
 *
 * Empty until somebody says, which is what GEM does too: scrp_read before any
 * scrp_write answers with nothing, and an application that gets nothing knows
 * there is nothing to paste.
 */
static char scrap_path[128];

static int listening = -1;
static char socket_path[108];
static int talkative;

static void say(const char *what, int16_t who, const char *name)
{
    if (!talkative)
        return;

    if (name)
        printf("tosaesd: %s %d (%.8s)\n", what, who, name);
    else
        printf("tosaesd: %s %d\n", what, who);

    fflush(stdout);
}

/* Sending is allowed to fail: an application that has gone away between one
 * message and the next is a normal thing to happen, not an error to report. */
static void send_to(int fd, const struct aesd_packet *p)
{
    size_t sent = 0;

    while (sent < sizeof *p)
    {
        ssize_t n = write(fd, (const char *)p + sent, sizeof *p - sent);

        if (n > 0)
        {
            sent += (size_t)n;
            continue;
        }

        if (n < 0 && errno == EINTR)
            continue;

        return;
    }
}

static int find_by_name(const char *name)
{
    int i;

    for (i = 0; i < MAX_APPS; i++)
        if (apps[i].used && !memcmp(apps[i].name, name, AESD_NAME_LEN))
            return i;

    return -1;
}

static int find_by_id(int16_t ap_id)
{
    int i;

    for (i = 0; i < MAX_APPS; i++)
        if (apps[i].used && apps[i].ap_id == ap_id)
            return i;

    return -1;
}

static void app_goodbye(int slot)
{
    if (!apps[slot].used)
        return;

    say("gone", apps[slot].ap_id, apps[slot].name);

    close(apps[slot].fd);
    memset(&apps[slot], 0, sizeof apps[slot]);
}

/*
 * A new application.
 *
 * The identifier is the slot it went into, so the first one is nought. That is
 * what the AES did - it numbered them as it started them, from nothing upwards
 * - and it matters more than it looks: a GEM program written for a machine
 * running one thing at a time tests whether appl_init answered nought, and
 * stops if it did not. When there is a desktop of our own it will be the first
 * to arrive and will take nought itself, and the applications it starts will
 * be numbered after it, which is the same rule on a busier machine.
 */
static void app_hello(int fd, const struct aesd_packet *in)
{
    struct aesd_packet out;
    int slot;

    for (slot = 0; slot < MAX_APPS; slot++)
        if (!apps[slot].used)
            break;

    memset(&out, 0, sizeof out);
    out.kind = AESD_WELCOME;
    out.screen_width = SCREEN_WIDTH;
    out.screen_height = SCREEN_HEIGHT;
    out.screen_planes = SCREEN_PLANES;
    out.apps = MAX_APPS;

    if (slot >= MAX_APPS)
    {
        /* Full. Answering with -1 is the AES saying it will not have it, which
         * an application is entitled to be told rather than left waiting for */
        out.ap_id = -1;
        send_to(fd, &out);
        return;
    }

    apps[slot].used = 1;
    apps[slot].fd = fd;
    apps[slot].ap_id = (int16_t)slot;
    memcpy(apps[slot].name, in->name, AESD_NAME_LEN);

    out.ap_id = apps[slot].ap_id;
    send_to(fd, &out);

    say("hello", apps[slot].ap_id, apps[slot].name);
}

static void app_find(int slot, const struct aesd_packet *in)
{
    struct aesd_packet out;
    int found = find_by_name(in->name);

    memset(&out, 0, sizeof out);
    out.kind = AESD_FOUND;
    out.ap_id = (found >= 0) ? apps[found].ap_id : -1;

    send_to(apps[slot].fd, &out);
}

static void app_send(int slot, const struct aesd_packet *in)
{
    struct aesd_packet out;
    int to = find_by_id(in->ap_id);

    (void)slot;

    if (to < 0)
        return;         /* Gone since the sender last looked */

    memset(&out, 0, sizeof out);
    out.kind = AESD_DELIVER;
    memcpy(out.message, in->message, sizeof out.message);

    send_to(apps[to].fd, &out);
}

static void app_scrap_get(int slot)
{
    struct aesd_packet out;

    memset(&out, 0, sizeof out);
    out.kind = AESD_SCRAP;
    memcpy(out.path, scrap_path, sizeof out.path);

    send_to(apps[slot].fd, &out);
}

static void app_scrap_set(int slot, const struct aesd_packet *in)
{
    (void)slot;

    memcpy(scrap_path, in->path, sizeof scrap_path);
    scrap_path[sizeof scrap_path - 1] = 0;

    if (talkative)
    {
        printf("tosaesd: the scrap is in %s\n", scrap_path);
        fflush(stdout);
    }
}

static void tidy_up(void)
{
    if (listening >= 0)
        close(listening);

    if (socket_path[0])
        unlink(socket_path);
}

static void asked_to_stop(int signal)
{
    (void)signal;

    tidy_up();
    _exit(0);
}

int main(int argc, char **argv)
{
    struct sockaddr_un where;
    const char *said;
    int i;

    for (i = 1; i < argc; i++)
    {
        if (!strcmp(argv[i], "-v"))
            talkative = 1;
        else
        {
            printf("Usage: tosaesd [-v]\n\n"
                   "The daemon several tosemu processes have in common: which\n"
                   "application is which, what the screen looks like, and\n"
                   "messages one sends another.\n\n"
                   "TOSEMU_AESD says where to put the socket, and defaults to\n"
                   "$XDG_RUNTIME_DIR/" AESD_SOCKET_NAME ".\n");
            return 1;
        }
    }

    said = getenv("TOSEMU_AESD");
    if (said && *said)
        snprintf(socket_path, sizeof socket_path, "%s", said);
    else
    {
        const char *dir = getenv("XDG_RUNTIME_DIR");

        if (!dir || !*dir)
            dir = "/tmp";

        snprintf(socket_path, sizeof socket_path, "%s/%s", dir,
                 AESD_SOCKET_NAME);
    }

    listening = socket(AF_UNIX, SOCK_STREAM, 0);
    if (listening < 0)
    {
        perror("tosaesd: socket");
        return 1;
    }

    memset(&where, 0, sizeof where);
    where.sun_family = AF_UNIX;
    snprintf(where.sun_path, sizeof where.sun_path, "%s", socket_path);

    /*
     * A socket left behind by a daemon that did not get to tidy up would stop
     * this one starting for ever. Taking it away is safe because nobody is
     * listening on it - if somebody were, binding would fail anyway.
     */
    unlink(socket_path);

    if (bind(listening, (struct sockaddr *)&where, sizeof where) < 0)
    {
        perror("tosaesd: bind");
        return 1;
    }

    /* Nobody else's business */
    chmod(socket_path, 0600);

    if (listen(listening, MAX_APPS) < 0)
    {
        perror("tosaesd: listen");
        tidy_up();
        return 1;
    }

    signal(SIGINT, asked_to_stop);
    signal(SIGTERM, asked_to_stop);
    signal(SIGPIPE, SIG_IGN);   /* An application going away mid-write */

    if (talkative)
    {
        printf("tosaesd: listening on %s\n", socket_path);
        fflush(stdout);
    }

    for (;;)
    {
        struct pollfd fds[MAX_APPS + 1];
        int slots[MAX_APPS + 1];
        int n = 0;

        fds[n].fd = listening;
        fds[n].events = POLLIN;
        slots[n] = -1;
        n++;

        for (i = 0; i < MAX_APPS; i++)
        {
            if (!apps[i].used)
                continue;

            fds[n].fd = apps[i].fd;
            fds[n].events = POLLIN;
            slots[n] = i;
            n++;
        }

        if (poll(fds, n, -1) < 0)
        {
            if (errno == EINTR)
                continue;

            perror("tosaesd: poll");
            break;
        }

        for (i = 0; i < n; i++)
        {
            struct aesd_packet in;
            ssize_t got;

            if (!(fds[i].revents & (POLLIN|POLLHUP|POLLERR)))
                continue;

            if (slots[i] < 0)
            {
                int fd = accept(listening, 0, 0);

                if (fd >= 0)
                {
                    /*
                     * Not in the table until it says who it is. A connection
                     * that never gets round to that is not an application.
                     */
                    struct aesd_packet hello;
                    ssize_t k = read(fd, &hello, sizeof hello);

                    if (k == (ssize_t)sizeof hello && hello.kind == AESD_HELLO)
                        app_hello(fd, &hello);
                    else
                        close(fd);
                }

                continue;
            }

            got = read(apps[slots[i]].fd, &in, sizeof in);
            if (got != (ssize_t)sizeof in)
            {
                app_goodbye(slots[i]);
                continue;
            }

            switch (in.kind)
            {
                case AESD_FIND:
                    app_find(slots[i], &in);
                    break;

                case AESD_SEND:
                    app_send(slots[i], &in);
                    break;

                case AESD_SCRAP_GET:
                    app_scrap_get(slots[i]);
                    break;

                case AESD_SCRAP_SET:
                    app_scrap_set(slots[i], &in);
                    break;

                default:
                    /* Something this daemon does not know about, from an
                     * emulator built at a different time. Ignoring it is
                     * better than throwing the application out over it. */
                    break;
            }
        }
    }

    tidy_up();

    return 0;
}
