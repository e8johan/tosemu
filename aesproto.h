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

#ifndef AESPROTO_H
#define AESPROTO_H

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * What the emulator says to the daemon, and the daemon back.
 *
 * The AES was one program with every application inside it, sharing its
 * variables. Here each application is a process of its own, so the things they
 * shared have to be somewhere both can reach - which application is which,
 * whose turn it is, and messages one sends another. That somewhere is the
 * daemon, and this is everything they say about it.
 *
 * Drawing is not here and never will be. A VDI call is a function call into
 * this process that writes into memory this process owns; putting a socket in
 * that path would make every line drawn a round trip. The daemon arbitrates
 * what is shared and is never in a drawing path.
 *
 * One structure for every message rather than a length and a body, because
 * every message is small and a fixed size cannot be got wrong. Both ends are
 * the same build on the same machine, so the words are in host order and there
 * is nothing to swap: this is not a wire format, it is two processes of one
 * program talking.
 */

#define AESD_NAME_LEN (8)

/*
 * What an accessory calls itself in the Desk menu, which is a longer thing
 * than a name.
 *
 * A name is eight characters because that is what GEM looks an application up
 * by. This is words a person reads, and GEM never measured them - it kept the
 * pointer menu_register was given and let the menu grow to fit. What limited
 * them in practice was the width of the Desk menu, and twenty characters is
 * wider than one fits in a low resolution screen.
 */
#define AESD_ACC_NAME_LEN (20)

enum {
    /*
     * A new application. It says what it is called - eight characters padded
     * with spaces, which is what GEM calls a name - and is told which
     * application it is and what the screen it is sharing looks like.
     */
    AESD_HELLO = 1,
    AESD_WELCOME,

    /* Asking which application answers to a name, and being told, or told -1
     * when none does */
    AESD_FIND,
    AESD_FOUND,

    /* A message for another application, and one arriving for this one. The
     * eight words are GEM's, and the daemon does not look inside them. */
    AESD_SEND,
    AESD_DELIVER,

    /*
     * Where the scrap is, and saying where it is now.
     *
     * The scrap is how one GEM application cuts something out and another
     * pastes it in, and it works by both of them writing files into the same
     * directory. Which directory that is is the only part they have to agree
     * about, so it is the only part that is here - the files themselves are
     * files, and GEMDOS already knows how to write those.
     */
    AESD_SCRAP_GET,
    AESD_SCRAP_SET,
    AESD_SCRAP,

    /*
     * An accessory saying so, and everybody being told who they are.
     *
     * An accessory is an application that never has a window of its own until
     * somebody picks it out of the Desk menu, and the Desk menu is in every
     * application's menu bar. So the list has to reach all of them, which
     * makes it the daemon's: one says what it is called, and everyone finds
     * out, including the ones that started before it.
     */
    AESD_ACCESSORY,
    AESD_ACCESSORIES,

    /*
     * The desktop's own notes, which shel_get reads and shel_put writes.
     *
     * A buffer holding what an ST kept in DESKTOP.INF - which windows were
     * open, what the icons were called. The AES neither reads it nor writes
     * it, it holds it, so it is a place to put something rather than a
     * setting; and it belongs to the session rather than to an application,
     * because one writes it and another expects to read what was written.
     */
    AESD_NOTES_GET,
    AESD_NOTES_SET,
    AESD_NOTES_ARE,
};

/* How much of it there is. An ST gave the desktop a kilobyte and nothing
 * expects more. */
#define AESD_NOTES (1024)

/* As many as GEM would load, and the number the AES splices into the Desk
 * menu, so the two agree by construction */
#define AESD_MAX_ACCS (6)

struct aesd_packet {
    int16_t kind;

    /* Whose it is: the application being told about, asked after, or sent to */
    int16_t ap_id;

    /*
     * The screen every application shares.
     *
     * It is the daemon's because it has to be one screen: applications lay
     * their windows out in it and are told where the others put theirs, and
     * two that disagree about how large it is disagree about everything. It is
     * sent when an application arrives rather than asked for, because there is
     * nothing it can usefully do before it knows.
     */
    int16_t screen_width;
    int16_t screen_height;
    int16_t screen_planes;

    /* How many applications can be running at once, which GEM reports in the
     * global array and some applications believe */
    int16_t apps;

    char name[AESD_NAME_LEN];

    /* And what an accessory calls itself in the Desk menu, which is not its
     * name: one is what appl_find looks it up by and the other is what a
     * person picks it out by, and an accessory has both */
    char shown[AESD_ACC_NAME_LEN];

    int16_t message[8];

    /* A path, for the things that are named by one. TOS paths are short - a
     * drive, and directories of eight and three - so this is generous. */
    char path[128];

    /* Who the accessories are, which everybody is told and nobody asks for:
     * the list changes when one arrives or goes away, and an application
     * cannot know when that was */
    int16_t accessories;
    struct {
        char name[AESD_ACC_NAME_LEN];
        int16_t ap_id;
    } accessory[AESD_MAX_ACCS];

    /* The desktop's notes, and how much of them is meant */
    int16_t notes_length;
    char notes[AESD_NOTES];
};

/*
 * Where to find the daemon.
 *
 * In the runtime directory, because that is where a socket belonging to one
 * person logged in once belongs, and it is cleaned up when they log out.
 * TOSEMU_AESD names another, which is what lets a test run its own daemon
 * without touching the one the person is using.
 */
#define AESD_SOCKET_NAME "tosaesd"

/*
 * Which screen the machine has, which is the one thing about it an application
 * cannot be told twice.
 *
 * The three the ST had, named the way it named them. This is not a preference:
 * a GEM application is laid out in characters and assumes how many of them fit
 * across, because the resource editor it was drawn in had a screen in mind. A
 * dialog forty-five characters wide is an ordinary dialog on a screen eighty
 * characters across and does not fit at all on one that is forty, where the
 * AES centres it at a negative coordinate and it hangs off both edges.
 *
 * So high is the default, being the one GEM applications were written for. Low
 * is for the things that need colours to be worth testing - the AES draws in
 * sixteen of them and a monochrome screen has two - and medium is here because
 * the machine had it.
 *
 * It lives with the protocol because it is the daemon that decides: the screen
 * has to be one screen for everything running, and two processes that read the
 * environment separately are two processes that can disagree about it. Both
 * ends read it the same way so that a session with no daemon in it gets the
 * same machine as a session with one.
 */
static inline void aesd_screen_mode(int16_t *width, int16_t *height,
                                    int16_t *planes)
{
    static const struct {
        const char *name;
        int16_t width, height, planes;
    } modes[] = {
        { "low",    320, 200, 4 },
        { "medium", 640, 200, 2 },
        { "high",   640, 400, 1 },
    };
    const int high = 2;
    const char *want = getenv("TOSEMU_SCREEN");
    int i;

    for (i = 0; want && i < (int)(sizeof modes / sizeof modes[0]); i++)
    {
        if (strcmp(want, modes[i].name) != 0)
            continue;

        *width = modes[i].width;
        *height = modes[i].height;
        *planes = modes[i].planes;
        return;
    }

    /* Said and not understood, which is worth a word: a misspelt resolution
     * that quietly becomes the usual one is a machine that is not the one that
     * was asked for, and everything drawn on it is the wrong size */
    if (want)
        fprintf(stderr, "TOSEMU_SCREEN: no screen is called '%s'. "
                        "There is low, medium and high.\n", want);

    *width = modes[high].width;
    *height = modes[high].height;
    *planes = modes[high].planes;
}

#endif /* AESPROTO_H */
