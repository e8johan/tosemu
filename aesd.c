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

#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/wait.h>

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

    /* What it calls itself in the Desk menu, if it is an accessory. An
     * application that never says is not one. */
    int accessory;
    char shown[AESD_NAME_LEN];
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
static const char *accessory_directory;

static void say_of(const char *what, const char *name)
{
    if (!talkative)
        return;

    printf("tosaesd: %s %s\n", what, name);
    fflush(stdout);
}

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

static void tell_about_accessories(int only_to);

static void app_goodbye(int slot)
{
    if (!apps[slot].used)
        return;

    say("gone", apps[slot].ap_id, apps[slot].name);

    close(apps[slot].fd);
    {
        int was_accessory = apps[slot].accessory;

        memset(&apps[slot], 0, sizeof apps[slot]);

        if (was_accessory)
            tell_about_accessories(-1);
    }
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

    /* And who is already here, so that an application that starts after the
     * accessories still has them in its Desk menu */
    tell_about_accessories(slot);
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

/*
 * Who the accessories are, told to everybody.
 *
 * Not answered when asked, because nobody knows when to ask: an application
 * puts its menu bar up once and the list can change afterwards, when an
 * accessory is started or stops. So everyone is told each time it changes, and
 * an application that has just arrived is told as part of arriving.
 */
static void tell_about_accessories(int only_to)
{
    struct aesd_packet out;
    int i, n = 0;

    memset(&out, 0, sizeof out);
    out.kind = AESD_ACCESSORIES;

    for (i = 0; i < MAX_APPS && n < AESD_MAX_ACCS; i++)
    {
        if (!apps[i].used || !apps[i].accessory)
            continue;

        memcpy(out.accessory[n].name, apps[i].shown, AESD_NAME_LEN);
        out.accessory[n].ap_id = apps[i].ap_id;
        n++;
    }

    out.accessories = (int16_t)n;

    for (i = 0; i < MAX_APPS; i++)
    {
        if (!apps[i].used)
            continue;

        if (only_to >= 0 && i != only_to)
            continue;

        send_to(apps[i].fd, &out);
    }
}

static void app_accessory(int slot, const struct aesd_packet *in)
{
    apps[slot].accessory = 1;
    memcpy(apps[slot].shown, in->name, AESD_NAME_LEN);

    say("accessory", apps[slot].ap_id, apps[slot].shown);

    tell_about_accessories(-1);
}

/*
 * Starting the accessories.
 *
 * An accessory is a program like any other and is started like one: a process
 * of its own running an emulator, which says hello and registers its name the
 * same way anything else would. What makes it an accessory is that it does
 * that instead of opening a window.
 *
 * The daemon does the starting because it is what exists before any of them
 * and outlives all of them, which is what a session is. It knows nothing else
 * about the emulator - it does not link a line of it - so it finds the program
 * beside itself rather than being told where it is.
 */
static void start_accessory(const char *emulator, const char *path)
{
    pid_t child = fork();

    if (child < 0)
        return;

    if (child == 0)
    {
        /* The daemon's socket is not the child's business: it opens its own
         * when it says hello, and this one would be a second way in */
        if (listening >= 0)
            close(listening);

        execl(emulator, emulator, path, (char *)NULL);

        /* Only reached when the emulator is not where it was expected. The
         * child says so rather than disappearing, because a session with no
         * accessories and no reason given is the harder thing to work out. */
        fprintf(stderr, "tosaesd: could not start %s with %s: %s\n",
                path, emulator, strerror(errno));
        _exit(1);
    }

    say_of("started", path);
}

/* Where the emulator is, which is beside this program */
static int find_the_emulator(char *where, size_t size)
{
    char self[PATH_MAX];
    ssize_t n = readlink("/proc/self/exe", self, sizeof self - 1);
    char *slash;

    if (n <= 0)
        return 0;

    self[n] = 0;

    slash = strrchr(self, '/');
    if (!slash)
        return 0;

    *slash = 0;
    snprintf(where, size, "%s/tosemu", self);

    return access(where, X_OK) == 0;
}

/*
 * Everything in a directory that looks like an accessory.
 *
 * Which is anything ending in .ACC, in either case, because a TOS filesystem
 * did not care and the one underneath this might.
 */
static void start_the_accessories(const char *directory)
{
    char emulator[PATH_MAX];
    struct dirent *entry;
    DIR *dir;

    if (!find_the_emulator(emulator, sizeof emulator))
    {
        fprintf(stderr, "tosaesd: cannot find tosemu beside me, so there is "
                        "nothing to start the accessories with\n");
        return;
    }

    dir = opendir(directory);
    if (!dir)
    {
        fprintf(stderr, "tosaesd: no accessories in %s: %s\n",
                directory, strerror(errno));
        return;
    }

    while ((entry = readdir(dir)) != NULL)
    {
        char path[PATH_MAX];
        size_t len = strlen(entry->d_name);

        if (len < 4 || strcasecmp(entry->d_name + len - 4, ".acc") != 0)
            continue;

        snprintf(path, sizeof path, "%s/%s", directory, entry->d_name);
        start_accessory(emulator, path);
    }

    closedir(dir);
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
        else if (!accessory_directory && argv[i][0] != '-')
            accessory_directory = argv[i];
        else
        {
            printf("Usage: tosaesd [-v] [directory]\n\n"
                   "The daemon several tosemu processes have in common: which\n"
                   "application is which, what the screen looks like, and\n"
                   "messages one sends another.\n\n"
                   "A directory is looked in for accessories - anything ending\n"
                   "in .ACC - and each one is started in an emulator of its\n"
                   "own. They put themselves in the Desk menu of every\n"
                   "application that runs afterwards.\n\n"
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

    /* And now that there is somewhere for them to say hello to */
    if (accessory_directory)
        start_the_accessories(accessory_directory);

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

                case AESD_ACCESSORY:
                    app_accessory(slots[i], &in);
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
