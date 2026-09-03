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
 * The clipboard in both directions, and the thing that must not break while
 * either of them works.
 *
 * This is the same program four times over, because what it has to establish
 * needs one process for some of it and two for the rest.
 *
 * With no argument it is the paste: nothing has copied anything in GEM, and
 * asking where the scrap is has to answer somewhere, and SCRAP.TXT has to be
 * there with what the desktop was offering in it, spelled the way an ST spells
 * text.
 *
 * With "copy" it is the other direction, which nothing announces. It writes a
 * scrap and waits, and what it establishes is not in here at all - an
 * application cannot see its own clipboard, so it is the Makefile that looks
 * at what the desktop was handed.
 *
 * With "send" and "wait" it is two GEM applications passing a scrap between
 * themselves while the desktop is offering something else entirely. That case
 * already worked before any of this existed and is the one most easily broken
 * by it: the sender's cut is newer than the desktop's offer, so the reader has
 * to be given the sender's and not the desktop's. Getting that backwards is
 * invisible until somebody copies in one GEM program and pastes into another
 * and gets a stranger's text, so it is worth two processes to check.
 */

#include <stdio.h>
#include <string.h>
#include <gem.h>

static int n;

static void check(long got, long want, const char *name)
{
    n++;
    if (got == want)
        printf("ok %d - %s\n", n, name);
    else
        printf("not ok %d - %s (got %ld, want %ld)\n", n, name, got, want);
}

/*
 * The scrap as bytes, said one byte at a time on failure.
 *
 * What is being compared is text with accented letters and line endings in it,
 * and both of those are invisible in a message printed as a string - which is
 * exactly the half of the conversion most likely to be wrong.
 */
static void check_bytes(const char *got, int got_length,
                        const char *want, int want_length,
                        const char *name)
{
    int i;

    n++;

    if (got_length == want_length && memcmp(got, want, want_length) == 0)
    {
        printf("ok %d - %s\n", n, name);
        return;
    }

    for (i = 0; i < got_length && i < want_length; i++)
        if (got[i] != want[i])
        {
            printf("not ok %d - %s (byte %d is 0x%02X, want 0x%02X)\n",
                   n, name, i, (unsigned char)got[i], (unsigned char)want[i]);
            return;
        }

    printf("not ok %d - %s (got %d bytes, want %d)\n", n, name,
           got_length, want_length);
}

/* Where the scrap is, with the name of the text in it on the end */
static int scrap_file(char *into)
{
    if (scrp_read(into) != 1)
        return 0;

    if (!into[0])
        return 0;

    strcat(into, "SCRAP.TXT");

    return 1;
}

/* Reads it, answering how many bytes there were, or -1 when there is no such
 * file - which is a different failure from an empty one and reads differently */
static int scrap_read(const char *path, char *into, int room)
{
    FILE *f = fopen(path, "rb");
    int got;

    if (!f)
        return -1;

    got = (int)fread(into, 1, (size_t)room, f);
    fclose(f);

    return got;
}

/* What the message says, so that what arrives is known to be what was sent */
#define TOKEN (0x4E76)

int main(int argc, char **argv)
{
    char path[160];
    char got[256];
    short id;

    id = appl_init();

    if (argc > 1 && strcmp(argv[1], "send") == 0)
    {
        /*
         * One GEM application cutting something out, while the desktop is
         * offering something else. Asking where the scrap is comes first
         * because that is what a real one does, and because it is what brings
         * the desktop's offer across - so what is written here lands on top of
         * it, and is the newer of the two from that moment on.
         */
        short message[8];
        short other;
        FILE *f;
        int i;

        check(scrap_file(path), 1, "the sender was told where the scrap is");

        f = fopen(path, "wb");
        check(f != 0, 1, "and could write a scrap there");

        if (f)
        {
            fwrite("FROM GEM", 1, 8, f);
            fclose(f);
        }

        other = appl_find(argv[2]);
        check(other >= 0, 1, "and found the application waiting for it");

        for (i = 0; i < 8; i++)
            message[i] = 0;

        message[0] = TOKEN;
        message[1] = id;

        check(appl_write(other, 16, message), 1, "and said it was there");
    }
    else if (argc > 1 && strcmp(argv[1], "wait") == 0)
    {
        /* The other one, pasting what the first cut out */
        short message[8];
        int length;

        printf("waiting as application %d\n", id);
        fflush(stdout);

        evnt_mesag(message);

        check(message[0], TOKEN, "the reader heard that a scrap was written");

        check(scrap_file(path), 1, "and was told where to find it");

        length = scrap_read(path, got, sizeof got);

        /*
         * The whole point. The desktop is offering something all through this
         * test, and a paste here must not reach for it: the other GEM
         * application's cut is newer, so it is the one that is current.
         */
        check_bytes(got, length, "FROM GEM", 8,
                    "and read what the other application cut, not the desktop's");
    }
    else if (argc > 2 && strcmp(argv[1], "watch") == 0)
    {
        /*
         * An emulator that is only sitting there, watching the scrap while
         * another one brings the desktop's clipboard in.
         *
         * It says where the scrap is rather than asking, because asking is
         * what brings the clipboard across, and the whole point here is to be
         * watching when somebody else does it. What must not happen is this
         * one seeing that write, taking it for a GEM application cutting
         * something out, and handing it back to the desktop it came from -
         * which is what happens if the guard against that is a count kept in
         * the process that made the write, because this is not that process.
         *
         * Nothing here can see the answer. The Makefile looks for a file that
         * should not have been written.
         */
        check(scrp_write(argv[2]), 1, "the watcher was told where the scrap is");

        printf("1..%d\n", n);
        fflush(stdout);

        { int k; for (k = 0; k < 4; k++) evnt_timer(1000); }

        appl_exit();
        return 0;
    }
    else if (argc > 1 && strcmp(argv[1], "hang") == 0)
    {
        /*
         * Waiting for a key that cannot arrive, with a scrap directory being
         * watched.
         *
         * The emulator is supposed to notice that nothing could ever answer
         * this and say so, rather than sitting there. Asking where the scrap
         * is first is what makes it interesting: that starts the watch, which
         * puts a file descriptor in the wait that can never end one - it
         * serves the desktop, not this program - and a loop that counts
         * descriptors rather than asking which of them could answer is fooled
         * by exactly that into hanging silently.
         *
         * Nothing after this line runs. What is being checked is what the
         * emulator printed on its way out.
         */
        check(scrp_read(path), 1, "the scrap was found before the wait");

        printf("1..%d\n", n);
        fflush(stdout);

        evnt_keybd();
    }
    else if (argc > 1 && strcmp(argv[1], "copy") == 0)
    {
        /*
         * A GEM application cutting something out, which is the direction
         * nothing announces. What is being established is not in this file at
         * all - it is in what the desktop was handed, which the Makefile looks
         * at afterwards. All this has to do is write a scrap and then wait,
         * because waiting is where the watch on the directory is read, and it
         * is where a GEM program goes the moment after it has copied anything.
         */
        FILE *f;

        check(scrap_file(path), 1, "the scrap has somewhere to be cut into");

        f = fopen(path, "wb");
        check(f != 0, 1, "and could be written");

        if (f)
        {
            fwrite("\x8E" "rade\r\n", 1, 7, f);
            fclose(f);
        }

        evnt_timer(500);
    }
    else
    {
        /* Nobody has copied anything in GEM, and the desktop is offering
         * text with two letters in it that an ST spells its own way */
        static const char wanted[] =
            "\x8E" "rade\r\nv" "\x84" "nner\r\n";
        int length;

        check(scrp_read(path), 1,
              "scrp_read answers before any GEM program has copied");

        check(path[0] != 0, 1, "and names somewhere to look");

        check(scrap_file(path), 1, "the scrap has a place for text in it");

        length = scrap_read(path, got, sizeof got);

        check(length >= 0, 1, "which the desktop's offer was written into");

        check_bytes(got, length, wanted, (int)sizeof wanted - 1,
                    "spelled the way an ST spells text, with CR LF between "
                    "lines");

        /*
         * Waits, so that the watch on the scrap directory is read at least
         * once with the file that has just been put there in it. Bringing the
         * desktop's clipboard in writes into the very directory being watched,
         * and what must not happen next is that write being read back out and
         * offered to the desktop it came from. Nothing here can see that; the
         * Makefile checks it by giving this run somewhere to offer to and
         * finding nothing was.
         */
        evnt_timer(500);
    }

    printf("1..%d\n", n);

    appl_exit();

    return 0;
}
