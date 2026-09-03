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
 * Bringing the desktop's clipboard into the scrap.
 *
 * The whole of the difficulty is deciding when to. Writing SCRAP.TXT whenever
 * the desktop offers something would be simple and would be wrong: two GEM
 * applications passing a scrap between themselves is the case that already
 * works, and a write landing in the middle of it replaces what one of them
 * just cut with something nobody asked for.
 *
 * The obvious guard is to ask whether the offer is our own, and it cannot
 * work. Application A copies and this process offers it to the desktop; B then
 * pastes, and from inside B that offer belongs to somebody else - A is another
 * process. An ownership test is local to a process and the case that matters
 * is across processes, so it answers wrongly exactly when it is needed.
 *
 * So the question asked here is not whose it is but which is newer. The scrap
 * file has a modification time and the offer has the moment it arrived, and
 * the desktop's only wins when it is later. That needs nothing from the daemon
 * and no agreement between processes, and it comes out right in each case:
 * A's cut is newer than the offer that produced it, so B leaves it alone; a
 * person copying in a browser is newer than either; and a cut this process
 * failed to offer at all leaves the file newer than a stale offer, so nothing
 * overwrites what could not be exported.
 *
 * The same time answers the same question going the other way. A scrap brought
 * in is stamped with the moment the offer arrived rather than with now, so a
 * file no newer than what the desktop is currently offering is that offer and
 * not a cut - and is not handed back. That has to be readable from another
 * process for the same reason: every emulator in the session watches this
 * directory, and one of them writing is a write all of them see, so the one
 * that has to recognise it is usually not the one that made it. A note kept in
 * memory cannot be read by the process that needs it. A time on the file can.
 */

#include "scrap.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <fcntl.h>
#include <sys/inotify.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <limits.h>
#include <time.h>

#include "aesclient.h"
#include "files.h"
#include "gfx.h"
#include "scraptext.h"
#include "settings.h"

#ifndef PATH_MAX
#define PATH_MAX (4096)
#endif

/* A TOS path, which is short: a drive, directories of eight and three, and a
 * separator between each. The same size aesscrp.c and the daemon use. */
#define MAX_TOS_PATH (128)

/*
 * Room for a file inside the scrap directory: a path, and a name on the end of
 * it. Longer than a path can be, so that appending the name cannot be what
 * pushes it over - which is a thing the compiler is entitled to ask about, and
 * a buffer of exactly PATH_MAX cannot answer.
 */
#define SCRAP_PATH (PATH_MAX + 32)

/*
 * Where the scrap goes when nobody has said.
 *
 * CLIPBRD on the boot drive is what GEM applications expected to find, so it
 * is what they are given. It lands inside whatever C: is - under TOS_BASE_PATH
 * when one is set, which is the case it is right for. Without one, C: is the
 * whole host file system and this is a directory at its root that nobody can
 * create; that fails, is said once, and the bridge stays idle until TOSEMU_SCRAP
 * points somewhere writable. Guessing a directory elsewhere would work more
 * often and would put a GEM application's clipboard somewhere no GEM
 * application would look for it.
 */
#define DEFAULT_SCRAP "C:\\CLIPBRD\\"

/*
 * What the two sides call text.
 *
 * SCRAP.TXT is the name GEM gave it, and every application that pastes text
 * looks for that name. Pictures are the same idea under SCRAP.IMG and are not
 * here yet.
 */
#define SCRAP_TEXT "SCRAP.TXT"

static struct {
    /* Where it is, as an application spells it, and where that lands on the
     * host. Empty until something asks. */
    char tos[MAX_TOS_PATH];
    char host[PATH_MAX + 1];

    /* The watch on that directory, and the watch descriptor within it */
    int notify;
    int watch;

    /* Said once rather than every time something looks: a scrap directory that
     * cannot be made is a thing to mention, not a thing to complain about
     * repeatedly while an application runs */
    int complained;
} scrap = { "", "", -1, -1, 0 };

/* Whether the bridge is wanted at all */
static int wanted(void)
{
    static int asked = -1;

    if (asked < 0)
        asked = !setting_flag("TOSEMU_NO_SCRAP");

    return asked;
}

/*
 * Starts watching the scrap directory, or moves the watch to where it is now.
 *
 * IN_CLOSE_WRITE rather than IN_MODIFY, which is the difference between
 * reading a file and reading half of one: SCRAP.TXT is written by an ordinary
 * program doing ordinary writes, and IN_MODIFY arrives after the first of
 * them. IN_MOVED_TO as well, because a careful program writes beside the name
 * and renames onto it - which is what put() below does, and what a GEM
 * application ought to do.
 *
 * All of it may fail, and none of the failures are worth reporting. A watch
 * that could not be set up is a session where copying out does not reach the
 * desktop, which is where every session was until recently.
 */
static void watch_it(void)
{
    if (scrap.notify < 0)
    {
        scrap.notify = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);

        if (scrap.notify < 0)
            return;
    }

    if (scrap.watch >= 0)
    {
        inotify_rm_watch(scrap.notify, scrap.watch);
        scrap.watch = -1;
    }

    scrap.watch = inotify_add_watch(scrap.notify, scrap.host,
                                    IN_CLOSE_WRITE | IN_MOVED_TO);
}

/*
 * The scrap directory as a host path, made if it is not there.
 *
 * Answers null when there is nowhere to put one, which is a reason to do
 * nothing rather than an error to report upwards: an application that cannot
 * paste from the desktop is an application that pastes from GEM, which is what
 * it did before any of this existed.
 */
static const char *directory(void)
{
    char tos[MAX_TOS_PATH];
    char host[PATH_MAX + 1];
    struct stat about;
    size_t n;

    scrap_where(tos, sizeof tos);

    if (!tos[0])
        return 0;

    /* Only worked out again when it moves, which is almost never, but the
     * answer is a path this returns and must not go stale */
    if (strcmp(tos, scrap.tos) == 0 && scrap.host[0])
        return scrap.host;

    if (tos_path_to_host(tos, host) != 0)
        return 0;

    /* Without the separator, which is what naming a file inside it wants and
     * what comparing a path against it wants */
    n = strlen(host);
    while (n > 1 && host[n - 1] == '/')
        host[--n] = 0;

    if (stat(host, &about) != 0)
    {
        if (mkdir(host, 0700) != 0)
        {
            if (!scrap.complained)
            {
                printf("The clipboard has nowhere to keep a scrap: %s cannot "
                       "be made. Set TOSEMU_SCRAP to a directory that can.\n",
                       host);
                fflush(stdout);
                scrap.complained = 1;
            }

            return 0;
        }
    }

    snprintf(scrap.tos, sizeof scrap.tos, "%s", tos);
    snprintf(scrap.host, sizeof scrap.host, "%s", host);

    watch_it();

    return scrap.host;
}

void scrap_where(char *tos_path, size_t size)
{
    char said[MAX_TOS_PATH];
    const char *settled;

    aes_client_scrap_get(said, sizeof said);

    if (said[0])
    {
        snprintf(tos_path, size, "%s", said);
        return;
    }

    if (!wanted())
    {
        /* Nothing has said where it is, and this is not going to say either:
         * with the bridge turned off the old answer is the right one, which is
         * that there is no scrap until a GEM program makes one */
        if (size)
            tos_path[0] = 0;

        return;
    }

    settled = setting("TOSEMU_SCRAP");
    if (!settled || !settled[0])
        settled = DEFAULT_SCRAP;

    /*
     * Nobody has said, so this does - and tells the session, so that every
     * other application agrees. Which one of them got there first stops
     * mattering the moment they all have the same answer.
     */
    aes_client_scrap_set(settled);
    snprintf(tos_path, size, "%s", settled);
}

void scrap_watch(const char *tos_path)
{
    if (!wanted() || !tos_path)
        return;

    /* Where it is has changed, so where it lands has to be worked out again */
    if (strcmp(tos_path, scrap.tos) != 0)
    {
        scrap.tos[0] = 0;
        scrap.host[0] = 0;
    }

    /*
     * Worked out now rather than when something next asks, because this is
     * scrp_write arriving and what follows it is the application writing the
     * file. A watch set up after that write has missed the thing it was for.
     */
    directory();
}

/*
 * When a file was last written, as a whole number of nanoseconds, or 0 when
 * there is no such file.
 *
 * The nanoseconds are not fastidiousness. A test writes what the desktop is
 * offering and runs an emulator in the same second, and a comparison to the
 * second cannot tell those apart - so the rule this file exists to implement
 * would be deciding by whichever way the tie happened to fall.
 */
static unsigned long long written_at(const char *path)
{
    struct stat about;

    if (!path || stat(path, &about) != 0)
        return 0;

    return (unsigned long long)about.st_mtime * 1000000000ull
           + (unsigned long long)about.st_mtim.tv_nsec;
}

/* The whole of a file, with a NUL after it. Null when it cannot be read. */
static char *contents(const char *path, size_t *length)
{
    FILE *f = fopen(path, "rb");
    char *bytes;
    long size;

    if (!f)
        return 0;

    if (fseek(f, 0, SEEK_END) != 0 || (size = ftell(f)) < 0)
    {
        fclose(f);
        return 0;
    }

    rewind(f);

    bytes = malloc((size_t)size + 1);
    if (!bytes)
    {
        fclose(f);
        return 0;
    }

    *length = fread(bytes, 1, (size_t)size, f);
    bytes[*length] = 0;

    fclose(f);

    return bytes;
}

/*
 * Writes the file GEM will open.
 *
 * Through a temporary name and a rename, because the alternative is a window
 * in which SCRAP.TXT exists and is half of itself. An application looking for
 * a scrap does not knock first - it opens what it finds - and a rename is the
 * one way to put a file somewhere without it ever being seen incomplete.
 */
static int put(const char *dir, const char *name,
               const char *bytes, size_t length, unsigned long long when)
{
    char temporary[SCRAP_PATH];
    char final[SCRAP_PATH];
    struct timespec stamp[2];
    FILE *f;

    snprintf(temporary, sizeof temporary, "%s/.scrap-%d", dir, (int)getpid());
    snprintf(final, sizeof final, "%s/%s", dir, name);

    f = fopen(temporary, "wb");
    if (!f)
        return 0;

    if (length && fwrite(bytes, 1, length, f) != length)
    {
        fclose(f);
        remove(temporary);
        return 0;
    }

    if (fclose(f) != 0)
    {
        remove(temporary);
        return 0;
    }

    /*
     * Stamped with the moment the desktop's offer arrived rather than with
     * now, which is how anybody can tell afterwards where this file came from.
     *
     * That question has to be answerable from another process. Every emulator
     * in the session is watching this directory, and a write is a write to all
     * of them: without this, one of them bringing the desktop's clipboard in
     * looks to the rest exactly like a GEM application cutting something out,
     * and they hand it straight back to the desktop it came from. A count kept
     * in this process cannot see that, because the process it needs to know
     * about is not this one.
     *
     * Failing is harmless. The file keeps the time it was written, which is
     * later than the offer, and the worst of it is the desktop being told
     * something it already knew.
     */
    /*
     * Stamped before the rename rather than after it, so that the file arrives
     * under its own name already carrying the answer. Every emulator in the
     * session is woken by that rename, and one that looked in between would
     * see a scrap stamped with now and conclude somebody had cut it.
     */
    stamp[0].tv_sec = (time_t)(when / 1000000000ull);
    stamp[0].tv_nsec = (long)(when % 1000000000ull);
    stamp[1] = stamp[0];

    utimensat(AT_FDCWD, temporary, stamp, 0);

    if (rename(temporary, final) != 0)
    {
        remove(temporary);
        return 0;
    }

    return 1;
}

/*
 * The names text goes by on a desktop.
 *
 * Five for one thing, because there is no single answer everything accepts.
 * The first is what anything written this century asks for; the last three are
 * what X11 called them, and are still what a program running under Xwayland
 * looks for. Offering all five costs nothing - they are names for the same
 * bytes - and leaving them off means a paste that silently does nothing in
 * whichever program wanted the name that was missing.
 */
static const char *const text_kinds[] = {
    "text/plain;charset=utf-8",
    "text/plain",
    "UTF8_STRING",
    "STRING",
    "TEXT"
};

#define TEXT_KINDS ((int)(sizeof text_kinds / sizeof text_kinds[0]))

/* Whether the stand-in is being used instead of a desktop, and what it names */
static const char *standing_in(void)
{
    const char *said = setting("TOSEMU_SCRAP_IN");

    return (said && said[0]) ? said : 0;
}

/*
 * When what the desktop is offering arrived, or 0 when it is offering nothing
 * that can be used.
 *
 * The stand-in wins when there is one, so that a test decides what the desktop
 * is doing rather than whatever is really on the clipboard of the machine the
 * suite happens to be running on.
 */
static unsigned long long offer_when(void)
{
    const char *file = standing_in();
    int i;

    if (file)
        return written_at(file);

    for (i = 0; i < TEXT_KINDS; i++)
        if (gfx_selection_has(text_kinds[i]))
            return gfx_selection_when();

    return 0;
}

/* And the offer itself, as UTF-8. Allocates. */
static char *offer_utf8(size_t *length)
{
    const char *file = standing_in();
    int i;

    if (file)
        return contents(file, length);

    for (i = 0; i < TEXT_KINDS; i++)
    {
        void *bytes;

        if (gfx_selection_has(text_kinds[i])
            && gfx_selection_take(text_kinds[i], &bytes, length))
            return bytes;
    }

    return 0;
}

void scrap_refresh(void)
{
    unsigned long long when;
    const char *dir;
    char target[SCRAP_PATH];
    char *utf8;
    char *atari;
    size_t utf8_length, atari_length;

    if (!wanted())
        return;

    /*
     * The directory first, and before asking whether there is anything to
     * bring in, because settling on it is also what starts the watch on it.
     * An application that asks where the scrap is and then writes one - which
     * is most of them, scrp_write being optional - would otherwise never be
     * watched, and its copy would never reach the desktop.
     */
    dir = directory();
    if (!dir)
        return;

    when = offer_when();
    if (!when)
        return;

    snprintf(target, sizeof target, "%s/%s", dir, SCRAP_TEXT);

    /*
     * The rule this file is for. Not newer means somebody else's cut is the
     * current one - most likely another GEM application's, which is the case
     * that must not be trodden on.
     */
    if (when <= written_at(target))
        return;

    utf8 = offer_utf8(&utf8_length);
    if (!utf8)
        return;

    atari = scrap_text_from_utf8(utf8, utf8_length, &atari_length);
    free(utf8);

    if (!atari)
        return;

    put(dir, SCRAP_TEXT, atari, atari_length, when);

    free(atari);
}

/*
 * Hands what a GEM application cut out to the desktop.
 *
 * A file for now, named by TOSEMU_SCRAP_OUT, standing in for a compositor the
 * way TOSEMU_SCRAP_IN stands in for one on the way in. What replaces it is a
 * selection taken on the seat, and the shape of this does not change when it
 * does: read the file, spell it the way the desktop spells text, hand over the
 * bytes.
 */
static void offer_text(void)
{
    char source[SCRAP_PATH];
    const char *where;
    char *atari;
    char *utf8;
    size_t atari_length, utf8_length;
    FILE *f;

    where = setting("TOSEMU_SCRAP_OUT");

    snprintf(source, sizeof source, "%s/%s", scrap.host, SCRAP_TEXT);

    /*
     * What came from the desktop does not go back to it.
     *
     * A scrap brought in is stamped with the moment the offer arrived, so a
     * file no newer than the current offer is that offer rather than anything
     * a GEM application cut. Asked this way round rather than by remembering
     * what this process wrote, because every emulator in the session is
     * watching this directory and sees the write - so the one that has to
     * recognise it is usually not the one that made it.
     *
     * Not newer rather than exactly equal, because a file system that keeps
     * times less finely than a nanosecond will have rounded the stamp down,
     * and a cut a person actually made is later than the offer by however long
     * it took them to choose Copy.
     */
    if (written_at(source) <= offer_when())
        return;

    atari = contents(source, &atari_length);
    if (!atari)
        return;

    utf8 = scrap_text_to_utf8(atari, atari_length, &utf8_length);
    free(atari);

    if (!utf8)
        return;

    /* Where a test looks, there being no desktop in one to look at */
    if (where && where[0])
    {
        f = fopen(where, "wb");

        if (f)
        {
            if (utf8_length)
                fwrite(utf8, 1, utf8_length, f);

            fclose(f);
        }
    }

    /*
     * And the desktop itself. This can refuse - no compositor, or nothing the
     * person has done that this program saw, which is the serial a selection
     * has to be taken with - and a refusal is not worth reporting: the scrap
     * is still on disk and another GEM application can still paste it.
     */
    {
        struct gfx_offer what;

        what.mimes = text_kinds;
        what.mimes_n = TEXT_KINDS;
        what.bytes = utf8;
        what.length = utf8_length;

        gfx_selection_give(&what, 1);
    }

    free(utf8);
}

int scrap_fd(void)
{
    return scrap.notify;
}

void scrap_pump(void)
{
    /*
     * Enough for several events at once, and aligned the way the header asks:
     * a name is part of the event rather than a pointer to one, so the reads
     * have to land somewhere a struct may legally start.
     */
    union {
        struct inotify_event event;
        char space[4096];
    } buffer;

    ssize_t got;

    if (scrap.notify < 0)
        return;

    while ((got = read(scrap.notify, buffer.space, sizeof buffer.space)) > 0)
    {
        ssize_t at = 0;

        while (at + (ssize_t)sizeof(struct inotify_event) <= got)
        {
            const struct inotify_event *e =
                (const struct inotify_event *)(buffer.space + at);

            at += (ssize_t)sizeof(struct inotify_event) + (ssize_t)e->len;

            if (!e->len || strcasecmp(e->name, SCRAP_TEXT) != 0)
                continue;

            offer_text();
        }
    }
}

void scrap_refresh_for(const char *host_path)
{
    const char *dir;
    size_t n;

    if (!wanted() || !host_path)
        return;

    /*
     * Asked before the directory is worked out, so that a file operation that
     * has nothing to do with the scrap - which is nearly all of them - costs
     * one comparison rather than a stat and a mkdir.
     */
    if (!scrap.host[0])
    {
        char tos[MAX_TOS_PATH];

        aes_client_scrap_get(tos, sizeof tos);

        /* Nothing has settled on a scrap yet, and a file being opened is not
         * a reason to settle on one: only an application asking for the scrap
         * is, and that goes through scrap_where */
        if (!tos[0])
            return;
    }

    dir = directory();
    if (!dir)
        return;

    n = strlen(dir);

    if (strncmp(host_path, dir, n) != 0 || host_path[n] != '/')
        return;

    /* Something inside the scrap directory is about to be read, which is the
     * moment a paste is happening whether or not it said so */
    scrap_refresh();
}
