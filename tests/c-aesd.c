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
 * Two applications, and the daemon between them.
 *
 * This is the same program twice. Run with no argument it is the one that
 * waits: it registers, says nothing, and blocks in evnt_mesag until something
 * arrives. Run with an argument it is the one that sends: it registers, looks
 * the first one up by name, sends it a message and stops.
 *
 * What is being tested is the part that cannot be tested with one process.
 * Every other test here runs a single application, where the identifier is
 * always nought, appl_find always answers -1, and a message posted always
 * comes back to the sender. The moment there are two, all three of those have
 * to be right or nothing arrives.
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

/* What the sender puts in the message, so that what arrives is known to be
 * what was sent rather than an empty buffer that happens to look right */
#define TOKEN (0x4E75)

int main(int argc, char **argv)
{
    short id;

    id = appl_init();

    if (argc < 2)
    {
        /* The one that waits */
        short message[8];
        int i;

        for (i = 0; i < 8; i++)
            message[i] = -1;

        check(id >= 0, 1, "the one that waits was let in");

        printf("waiting as application %d\n", id);
        fflush(stdout);

        evnt_mesag(message);

        check(message[0], TOKEN, "a message arrived from the other one");
        check(message[1] > 0, 1, "and says which application sent it");
        check(message[3], 0x1234, "with the words it was sent with intact");

        /*
         * The scrap, which the other one said where to find before it sent
         * the message. It is the daemon holding one fact both ends have to
         * have the same answer for, so reading it here is the whole test.
         */
        {
            char where[128];

            memset(where, 0, sizeof where);
            check(scrp_read(where), 1, "scrp_read answers once there is a scrap");
            check(strcmp(where, "C:\\SCRAP\\"), 0,
                  "and it is where the other application put it");
        }

        printf("1..%d\n", n);
    }
    else
    {
        /* The one that sends */
        short message[8];
        short other;
        int i;

        check(id > 0, 1, "the second application got a different identifier");

        other = appl_find(argv[1]);
        check(other >= 0, 1, "appl_find found the one that is waiting");
        check(other != id, 1, "and it is not this one");

        check(appl_find("NOSUCH  "), -1, "and answers -1 for one that is not"
                                        " there");

        for (i = 0; i < 8; i++)
            message[i] = 0;

        message[0] = TOKEN;
        message[1] = id;
        message[3] = 0x1234;

        /* Where the scrap is, which the other one reads back */
        check(scrp_write("C:\\SCRAP\\"), 1, "scrp_write says where the scrap is");

        /*
         * Starting another program. test-Pterm0 stops as soon as it starts,
         * which is what makes it safe to start from a test - what is being
         * checked is that the AES took the request and found the program,
         * not what the program then did.
         */
        check(shel_write(1, 1, 1, "test-Pterm0", ""), 1,
              "shel_write starts a program");
        check(shel_write(1, 1, 1, "NOSUCH.PRG", ""), 0,
              "and answers nought for one that is not there");

        check(appl_write(other, 16, message), 1, "appl_write took it");

        printf("1..%d\n", n);
    }

    appl_exit();

    return 0;
}
