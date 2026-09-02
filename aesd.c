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

/* For SO_PEERCRED's struct ucred, which is how a socket says which process is
 * on the other end of it. Before every header, which is what it has to be. */
#define _GNU_SOURCE

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
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
#include <signal.h>

#include "aesproto.h"
#include "aesdtray.h"
#include "screen.h"
#include "settings.h"

/*
 * How many applications there can be.
 *
 * GEM had room for the desktop and a handful beside it, and an application
 * asks how many and is told. The number is not a limit worth raising for its
 * own sake: a person does not run forty GEM programs, and something that says
 * it can is saying something nobody checks.
 */
#define MAX_APPS (16)

/*
 * The screen every application shares, settled once when the session starts.
 *
 * See the note at the top: this is where a machine's graphics mode belongs. A
 * person changing resolution turned the machine off and on again, and this is
 * the same thing - an application is told what the screen is when it arrives
 * and is never told again, so there is nowhere for a change to be sent.
 */
static int16_t screen_width, screen_height, screen_planes;

static struct {
    int used;
    int fd;
    int16_t ap_id;
    char name[AESD_NAME_LEN];

    /* What it calls itself in the Desk menu, if it is an accessory. An
     * application that never says is not one. */
    int accessory;
    char shown[AESD_ACC_NAME_LEN];

    /* Which process it is, which the socket knows and is asked for rather
     * than believed: it is how a connection is matched to a child this daemon
     * started, so that one can be named by what it calls itself */
    pid_t pid;
} apps[MAX_APPS];

/*
 * Where the scrap is.
 *
 * Empty until somebody says, which is what GEM does too: scrp_read before any
 * scrp_write answers with nothing, and an application that gets nothing knows
 * there is nothing to paste.
 */
static char scrap_path[128];

/* The desktop's notes, which shel_get and shel_put pass about. Held rather
 * than understood: nothing here has an opinion on what is in it. */
static char notes[AESD_NOTES];
static int16_t notes_length;

static int listening = -1;
static char socket_path[108];
static int talkative;
static const char *accessory_directory;

/*
 * The accessories to see off when the session ends.
 *
 * By process rather than by connection, because stopping one is a thing to do
 * to a process: an accessory never exits on its own - that is what makes it an
 * accessory - so being asked nicely may not be enough.
 *
 * As many as there can be applications rather than as many as fit in a packet.
 * Six is how many the daemon can name to anybody, and a seventh in the
 * directory was started and then not written down, which made it exactly the
 * one nothing would ever stop.
 */
static struct {
    pid_t pid;

    /* Whether this daemon started it, which decides how to ask whether it is
     * still there: waitpid answers about a child and about nothing else */
    int ours;

    char from[64];              /* What to call it when saying something */
    char path[PATH_MAX];        /* And where it came from, so that looking
                                 * again can tell a new one from this one */
} started[MAX_APPS];

static int started_count;

/*
 * Set when somebody picks Quit or sends a signal, which is noticed at the top
 * of the loop rather than acted on where it happens: tearing the daemon down
 * inside a message from the panel would leave the panel waiting for a reply,
 * and doing it inside a signal handler is not something a handler is allowed
 * to do - closing the session waits for processes and says what it is doing.
 */
static volatile sig_atomic_t time_to_go;

/*
 * How a signal reaches the loop.
 *
 * Writing a byte to one end wakes a poll waiting on the other, and writing to
 * a pipe is one of the few things a signal handler may do. The flag on its own
 * would not be enough: a signal arriving between the test at the top of the
 * loop and the poll below it would be set and then slept through, and the
 * daemon would sit there until something else happened to say anything.
 */
static int woken[2] = { -1, -1 };

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

/* The same, for the longer thing an accessory calls itself in the Desk menu.
 * Neither field is terminated, so both are printed by length rather than
 * read to a zero. */
static void say_shown(const char *what, int16_t who, const char *shown)
{
    if (!talkative)
        return;

    printf("tosaesd: %s %d (%.*s)\n", what, who, AESD_ACC_NAME_LEN, shown);
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

/*
 * Telling the accessories that an application has gone.
 *
 * On an ST there was one screen and one application at a time, so a program
 * ending took everything on the screen with it - including whatever an
 * accessory had opened while it ran. AC_CLOSE is how the accessory finds out,
 * so that it forgets a window rather than going on believing in one.
 *
 * Here every window is the desktop's and an accessory's window survives its
 * host perfectly well, so this is less load-bearing than it was. It is still
 * what an accessory is written to expect, and one that never hears it is one
 * that will not open its window a second time.
 */
static void tell_accessories_it_closed(int16_t which)
{
    struct aesd_packet out;
    int i;

    memset(&out, 0, sizeof out);
    out.kind = AESD_DELIVER;
    out.message[0] = 41;                /* AC_CLOSE */
    out.message[1] = which;

    for (i = 0; i < MAX_APPS; i++)
    {
        if (!apps[i].used || !apps[i].accessory)
            continue;

        /* Its own menu entry, which is what the message says: an accessory is
         * allowed more than one and has to know which went away */
        out.message[3] = 0;

        send_to(apps[i].fd, &out);
    }
}

static void app_goodbye(int slot)
{
    int was_accessory;
    int16_t was;

    if (!apps[slot].used)
        return;

    say("gone", apps[slot].ap_id, apps[slot].name);

    close(apps[slot].fd);

    was_accessory = apps[slot].accessory;
    was = apps[slot].ap_id;
    memset(&apps[slot], 0, sizeof apps[slot]);

    if (was_accessory)
    {
        tell_about_accessories(-1);
        return;
    }

    /* An application ending is what an accessory is told about. An accessory
     * ending is not: it is the thing that would have been told. */
    tell_accessories_it_closed(was);
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
    struct ucred who;
    socklen_t how_much = sizeof who;
    int slot;

    for (slot = 0; slot < MAX_APPS; slot++)
        if (!apps[slot].used)
            break;

    memset(&out, 0, sizeof out);
    out.kind = AESD_WELCOME;
    out.screen_width = screen_width;
    out.screen_height = screen_height;
    out.screen_planes = screen_planes;
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

    /* Which process it is, so that an accessory started here can be named by
     * what it calls itself rather than by the file it came out of */
    if (getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &who, &how_much) == 0)
        apps[slot].pid = who.pid;

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
/*
 * Somebody picked an accessory out of the panel.
 *
 * The same thing as picking it out of a Desk menu, and it arrives as the same
 * message: an accessory cannot tell the two apart and has no reason to. What
 * the panel adds is that it works when no application is running, which is the
 * case a Desk menu cannot cover.
 */
static int still_here(void);
static void start_the_accessories(const char *directory);

/*
 * Picked Look again, which reads the directory and starts whatever is in it
 * that is not running already.
 *
 * The accessories are read once when the session starts, and a person who
 * drops another one in afterwards has no way to say so - short of starting it
 * by hand against the right socket, which works and is not something to have
 * to know. This is that, from the menu.
 *
 * Anything that has stopped is forgotten first, so that an accessory which
 * fell over can be started again by the same menu entry that started it.
 */
static void look_again(void)
{
    if (!accessory_directory)
        return;

    still_here();

    start_the_accessories(accessory_directory);
}

/* Picked Quit, which is noticed rather than acted on here - see time_to_go */
static void quit_from_the_panel(void)
{
    time_to_go = 1;
}

static void picked_in_the_panel(int16_t ap_id)
{
    struct aesd_packet out;
    int to = find_by_id(ap_id);

    if (to < 0)
        return;

    memset(&out, 0, sizeof out);
    out.kind = AESD_DELIVER;
    out.message[0] = 40;                /* AC_OPEN */
    out.message[3] = 0;

    say_shown("opening", apps[to].ap_id, apps[to].shown);

    send_to(apps[to].fd, &out);
}

/* And the panel's copy of the list, which is the same list */
static void tell_the_panel(void)
{
    const char *names[AESD_MAX_ACCS];
    int16_t ids[AESD_MAX_ACCS];
    int i, n = 0;

    for (i = 0; i < MAX_APPS && n < AESD_MAX_ACCS; i++)
    {
        if (!apps[i].used || !apps[i].accessory)
            continue;

        names[n] = apps[i].shown;
        ids[n] = apps[i].ap_id;
        n++;
    }

    tray_accessories(names, ids, n);
}

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

        memcpy(out.accessory[n].name, apps[i].shown, AESD_ACC_NAME_LEN);
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

    /* The panel shows the same list, and only needs telling when it changed
     * rather than when one application is being caught up */
    if (only_to < 0)
        tell_the_panel();
}

static void app_accessory(int slot, const struct aesd_packet *in)
{
    apps[slot].accessory = 1;
    memcpy(apps[slot].shown, in->shown, AESD_ACC_NAME_LEN);

    say_shown("accessory", apps[slot].ap_id, apps[slot].shown);

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

    if (started_count < (int)(sizeof started / sizeof started[0]))
    {
        const char *leaf = strrchr(path, '/');

        started[started_count].pid = child;
        started[started_count].ours = 1;
        snprintf(started[started_count].from, sizeof started[started_count].from,
                 "%s", leaf ? leaf + 1 : path);
        snprintf(started[started_count].path, sizeof started[started_count].path,
                 "%s", path);
        started_count++;
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
/* Whether this one is already running, which is what makes looking again safe
 * to do twice */
static int already_running(const char *path)
{
    int i;

    for (i = 0; i < started_count; i++)
        if (started[i].pid > 0 && !strcmp(started[i].path, path))
            return 1;

    return 0;
}

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

        /* One that is already running is left alone, so that looking again is
         * a thing a person can do without counting how many times */
        if (already_running(path))
            continue;

        start_accessory(emulator, path);
    }

    closedir(dir);
}

static void app_notes_get(int slot)
{
    struct aesd_packet out;

    memset(&out, 0, sizeof out);
    out.kind = AESD_NOTES_ARE;
    out.notes_length = notes_length;
    memcpy(out.notes, notes, sizeof out.notes);

    send_to(apps[slot].fd, &out);
}

static void app_notes_set(int slot, const struct aesd_packet *in)
{
    (void)slot;

    notes_length = in->notes_length;
    if (notes_length < 0 || notes_length > AESD_NOTES)
        notes_length = AESD_NOTES;

    memcpy(notes, in->notes, sizeof notes);
}

/*
 * Ending the session.
 *
 * Everybody is asked first, with the message GEM has for exactly this, and
 * given a moment to go of their own accord. Then whatever this daemon started
 * and is still there is stopped, because an accessory that does not handle
 * being asked would otherwise outlive the session that started it and sit
 * there registered to a daemon that has gone.
 *
 * Applications are asked and not stopped. One that ignores the message is
 * somebody's work with unsaved changes in it, and a program the person started
 * themselves is theirs to close.
 */
/*
 * What an accessory is called, for saying something about it.
 *
 * What it calls itself if it got as far as registering, and the file it came
 * out of if it did not - which is the case that matters, because one that
 * never registered is exactly the one somebody will want named when it has to
 * be stopped.
 */
static const char *name_of(int which)
{
    static char trimmed[AESD_ACC_NAME_LEN + 1];
    int i, n;

    for (i = 0; i < MAX_APPS; i++)
    {
        if (!apps[i].used || !apps[i].accessory
            || apps[i].pid != started[which].pid)
            continue;

        memcpy(trimmed, apps[i].shown, AESD_ACC_NAME_LEN);
        trimmed[AESD_ACC_NAME_LEN] = 0;

        /* The name is padded out for the menu, which is not how it should read
         * in a sentence */
        for (n = AESD_ACC_NAME_LEN; n > 0 && trimmed[n-1] == ' '; n--)
            trimmed[n-1] = 0;

        for (n = 0; trimmed[n] == ' '; n++)
            ;

        if (trimmed[n])
            return trimmed + n;
    }

    return started[which].from;
}

/* Collects whichever have gone, and says how many are left */
static int still_here(void)
{
    int i, left = 0;

    for (i = 0; i < started_count; i++)
    {
        if (started[i].pid <= 0)
            continue;

        /*
         * Asked one way of a child and another of anything else, and the
         * difference matters: waitpid says "no such thing" about a process
         * this daemon did not start, which would read as "it has gone" for one
         * that is sitting right there. A child is asked with waitpid all the
         * same, because that is also what collects it.
         */
        if (started[i].ours)
        {
            if (waitpid(started[i].pid, 0, WNOHANG) != 0)
                started[i].pid = 0;     /* gone, and collected */
            else
                left++;
        }
        else if (kill(started[i].pid, 0) < 0 && errno == ESRCH)
            started[i].pid = 0;
        else
            left++;
    }

    return left;
}

/*
 * The accessories this daemon did not start, added to the ones it did.
 *
 * One can be started by hand against the socket, which is how a new one gets
 * tried before it is dropped in the directory, and it registers itself the
 * same way any other does. It is still an accessory, and that is the whole
 * argument for stopping it: an application left running when the daemon has
 * gone is somebody's work with a window on the screen, but an accessory is
 * reached only by being told to open, and there is nothing left to tell it. It
 * would sit there for ever with no way in and no way out.
 *
 * They go in the same list so that the closing below is one loop and not two,
 * and the only thing that differs about them is that they are not children.
 */
static void also_the_ones_started_by_hand(void)
{
    int i, j;

    for (i = 0; i < MAX_APPS; i++)
    {
        if (!apps[i].used || !apps[i].accessory || apps[i].pid <= 0)
            continue;

        for (j = 0; j < started_count; j++)
            if (started[j].pid == apps[i].pid)
                break;

        if (j < started_count)
            continue;

        if (started_count >= (int)(sizeof started / sizeof started[0]))
            return;

        started[started_count].pid = apps[i].pid;
        started[started_count].ours = 0;
        started[started_count].path[0] = 0;

        /* Only ever used if it stops answering before it is named, since one
         * that registered is named by what it calls itself */
        snprintf(started[started_count].from,
                 sizeof started[started_count].from, "%.*s", AESD_NAME_LEN,
                 apps[i].name);
        started_count++;
    }
}

/* Waits a while for one to go, and says whether it did */
static int gone_within(int which, int tenths)
{
    int i;

    for (i = 0; i < tenths; i++)
    {
        still_here();

        if (started[which].pid <= 0)
            return 1;

        usleep(100 * 1000);
    }

    still_here();

    return started[which].pid <= 0;
}

/*
 * Ending the session.
 *
 * Everybody is asked first, with the message GEM has for exactly this, and
 * given a moment to go of their own accord. Then every accessory that is still
 * there is stopped, because one that does not handle being asked would
 * otherwise outlive the session it belongs to.
 *
 * Applications are asked and not stopped. One that ignores the message is
 * somebody's work with unsaved changes in it, and a program the person started
 * themselves is theirs to close. An accessory is not that, whoever started it:
 * it has no window and is reached only by being told to open one, so a daemon
 * going away leaves it with nothing that could ever ask.
 */
static void everybody_out(void)
{
    struct aesd_packet out;
    int i;

    memset(&out, 0, sizeof out);
    out.kind = AESD_DELIVER;
    out.message[0] = 50;                /* AP_TERM */

    for (i = 0; i < MAX_APPS; i++)
        if (apps[i].used)
            send_to(apps[i].fd, &out);

    also_the_ones_started_by_hand();

    /*
     * What each is called, taken now rather than when its turn comes.
     *
     * Waiting for one collects every other that has gone in the meantime, and
     * a name is looked up through the connection that has gone with it - so by
     * the time the second is reached there is nothing left to ask. Two
     * accessories and one line was the first thing this got wrong.
     */
    {
        static char names[MAX_APPS][64];

        for (i = 0; i < started_count; i++)
            snprintf(names[i], sizeof names[i], "%s", name_of(i));

        for (i = 0; i < started_count; i++)
            snprintf(started[i].from, sizeof started[i].from, "%s", names[i]);
    }

    /*
     * One line each, and it is worth the trouble: what it says is whether an
     * accessory went when it was asked, which is the difference between one
     * that is well behaved and one that has to be stopped every time. A single
     * line saying the session closed would hide that.
     *
     * Said whether or not the daemon was asked to be talkative, because this
     * answers something the person just clicked rather than being chatter.
     */
    for (i = 0; i < started_count; i++)
    {
        printf("Asking %s to quit.", started[i].from);
        fflush(stdout);

        /*
         * One that went while another was being waited for is not skipped: it
         * did what it was asked, and the line saying so is the whole point.
         *
         * Two seconds, which is a long time for a program with nothing to save
         * and no time at all for one that is thinking about it.
         */
        if (started[i].pid <= 0 || gone_within(i, 20))
        {
            printf(" [ ok ]\n");
            fflush(stdout);
            continue;
        }

        kill(started[i].pid, SIGTERM);

        if (gone_within(i, 10))
        {
            printf(" [ stopped ]\n");
            fflush(stdout);
            continue;
        }

        kill(started[i].pid, SIGKILL);

        /* Waited for only if it is a child, because that is the only kind of
         * process there is anything to wait for. One that is not is gone all
         * the same - nothing survives this - and somebody else collects it. */
        if (started[i].ours)
            waitpid(started[i].pid, 0, 0);

        started[i].pid = 0;

        printf(" [ killed ]\n");
        fflush(stdout);
    }

    started_count = 0;
}

static void tidy_up(void)
{
    if (listening >= 0)
        close(listening);

    if (socket_path[0])
        unlink(socket_path);
}

/*
 * Interrupted, which is asked for here rather than done here.
 *
 * Leaving at once looks like the tidy thing and is not: the socket goes, the
 * daemon goes, and every accessory it started is still sitting there talking
 * to nothing. An accessory does not stop on its own - that is what makes it an
 * accessory - so it has to be stopped, and stopping it is the loop's job. This
 * says so and gets out of the way, which makes Ctrl-C the same door as Quit in
 * the panel rather than a second one that skips the closing.
 */
static void asked_to_stop(int signal)
{
    (void)signal;

    time_to_go = 1;

    if (woken[1] >= 0)
    {
        /* What is in the byte does not matter, only that something arrived. A
         * pipe already holding one wakes the poll just as well, so there is
         * nothing to do about a write that does not fit. */
        ssize_t said = write(woken[1], "", 1);

        (void)said;
    }
}

int main(int argc, char **argv)
{
    struct sockaddr_un where;
    const char *said;
    const char *config = 0;
    int no_config = 0;
    int i;

    for (i = 1; i < argc; i++)
    {
        if (!strcmp(argv[i], "-v"))
            talkative = 1;
        else if (!strcmp(argv[i], "--no-config"))
            no_config = 1;
        else if ((!strcmp(argv[i], "-c") || !strcmp(argv[i], "--config"))
                 && i + 1 < argc)
            config = argv[++i];
        else if (!strncmp(argv[i], "--config=", 9))
            config = argv[i] + 9;
        else if (!accessory_directory && argv[i][0] != '-')
            accessory_directory = argv[i];
        else
        {
            printf("Usage: tosaesd [-v] [-c <file>] [--no-config] "
                   "[directory]\n\n"
                   "The daemon several tosemu processes have in common: which\n"
                   "application is which, what the screen looks like, and\n"
                   "messages one sends another.\n\n"
                   "A directory is looked in for accessories - anything ending\n"
                   "in .ACC - and each one is started in an emulator of its\n"
                   "own. They put themselves in the Desk menu of every\n"
                   "application that runs afterwards.\n\n"
                   "Settings come from %s when there is one, and an\n"
                   "environment variable overrides what it says. This is\n"
                   "where to set them for a session: the daemon is what says\n"
                   "which screen the machine has, to every application that\n"
                   "arrives and to the accessories it starts itself.\n\n"
                   "TOSEMU_AESD, or [session] socket, says where to put the\n"
                   "socket, and defaults to $XDG_RUNTIME_DIR/"
                   AESD_SOCKET_NAME ".\n",
                   settings_default_path() ? settings_default_path()
                                           : "the settings file");
            return 1;
        }
    }

    /*
     * The settings before anything reads one, which here is the very next
     * line: which screen this session has is one of them.
     */
    if (no_config)
        settings_ignore_file();
    else if (!settings_load(config))
        return 1;

    /* Which machine this session is, before anybody can arrive to be told.
     * The screens that are as large as the display ask the compositor here,
     * which is also the only place that can: an accessory the daemon starts
     * has to be told the same screen as everything else, and it is this
     * process that tells it. */
    screen_mode(&screen_width, &screen_height, &screen_planes);

    said = setting("TOSEMU_AESD");
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
     * Whether one is already running, asked by trying to talk to it.
     *
     * This has to come first and the reason is not obvious. Unlinking the
     * socket does not fail when somebody is listening on it - it takes the
     * name away and leaves them holding a socket nobody can reach - so a
     * second daemon started by accident would quietly orphan the first, along
     * with every application talking to it. Binding would not have complained
     * either, because by then there is nothing at that name to complain about.
     */
    {
        int asking = socket(AF_UNIX, SOCK_STREAM, 0);

        if (asking >= 0)
        {
            int answered = connect(asking, (struct sockaddr *)&where,
                                   sizeof where) == 0;

            close(asking);

            if (answered)
            {
                printf("tosaesd: one is already running on %s\n", socket_path);
                return 0;
            }
        }
    }

    /*
     * Nobody answered, so anything at that name is what a daemon that did not
     * get to tidy up left behind, and it would stop this one starting for
     * ever.
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

    /*
     * The way a signal gets a word in, made before anything can send one.
     *
     * Nothing else may have it: an accessory is started by forking, so a write
     * end left open in the child would be a second daemon as far as the pipe
     * is concerned. And the handler must not be able to wait, which is what a
     * pipe nobody is emptying would otherwise make it do.
     */
    if (pipe2(woken, O_CLOEXEC | O_NONBLOCK) < 0)
    {
        perror("tosaesd: pipe");
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

    /*
     * A mark in the panel, which is how the accessories are reached when no
     * application is running. It is allowed to fail and says so once: a
     * desktop without tray icons is a session that is otherwise fine.
     */
    tray_open(picked_in_the_panel, quit_from_the_panel,
              accessory_directory ? look_again : 0);

    /* And now that there is somewhere for them to say hello to */
    if (accessory_directory)
        start_the_accessories(accessory_directory);

    for (;;)
    {
        struct pollfd fds[MAX_APPS + 3];
        int slots[MAX_APPS + 3];
        int panel_slot = -1;
        int wake_slot;
        int panel = tray_fd();
        int n = 0;

        fds[n].fd = listening;
        fds[n].events = POLLIN;
        slots[n] = -1;
        n++;

        /* The end a signal is heard on, so that one arriving while this is
         * asleep is what wakes it rather than the next thing anybody says */
        wake_slot = n;
        fds[n].fd = woken[0];
        fds[n].events = POLLIN;
        slots[n] = -3;
        n++;

        /* The panel, when there is one to talk to */
        if (panel >= 0)
        {
            panel_slot = n;
            fds[n].fd = panel;
            fds[n].events = POLLIN;
            slots[n] = -2;
            n++;
        }

        for (i = 0; i < MAX_APPS; i++)
        {
            if (!apps[i].used)
                continue;

            fds[n].fd = apps[i].fd;
            fds[n].events = POLLIN;
            slots[n] = i;
            n++;
        }

        if (time_to_go)
            break;

        if (poll(fds, n, -1) < 0)
        {
            if (errno == EINTR)
                continue;

            perror("tosaesd: poll");
            break;
        }

        /*
         * The panel first, and whether or not it said anything: libdbus keeps
         * messages of its own that have to be let out, and a reply that is
         * never flushed is a panel waiting for an answer that was written.
         */
        tray_pump();

        for (i = 0; i < n; i++)
        {
            struct aesd_packet in;
            ssize_t got;

            /* The panel is pumped above, and the byte a signal wrote has done
             * its work by getting this far: the flag it set is read at the top
             * of the loop, which is the next thing to happen. */
            if (i == panel_slot || i == wake_slot)
                continue;

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

                case AESD_NOTES_GET:
                    app_notes_get(slots[i]);
                    break;

                case AESD_NOTES_SET:
                    app_notes_set(slots[i], &in);
                    break;

                default:
                    /* Something this daemon does not know about, from an
                     * emulator built at a different time. Ignoring it is
                     * better than throwing the application out over it. */
                    break;
            }
        }
    }

    if (time_to_go)
        everybody_out();

    tray_close();
    tidy_up();

    /*
     * Said last, so that a daemon which stopped somewhere on the way out can
     * be told from one that finished. Without it the difference between "it
     * is taking a while" and "it is stuck" is invisible from outside.
     */
    if (time_to_go)
    {
        printf("Terminating daemon.\n");
        fflush(stdout);
    }

    return 0;
}
