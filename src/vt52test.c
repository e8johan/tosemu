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
 * The VT52 said in ANSI, for a terminal.
 *
 * Built for the host rather than for the emulated machine, because the
 * translation only happens when standard output is a terminal and the suite
 * writes every program's output into a file - which is the other half of
 * this, and the half make check already covers: a file gets the bytes as they
 * were written. So the translation is a function of its own and the bytes are
 * handed to it here.
 *
 * What is checked is what a terminal would be sent for what a TOS program
 * writes: the escapes it can express as their ANSI selves, ESC Y's two
 * numbers, the ones it cannot and the control characters the VT52 ignored
 * dropped, the Atari character set as UTF-8, and what a program left changed
 * put back at the end.
 */

#include "vt52ansi.h"

#include <stdio.h>
#include <string.h>

static int n;
static int fails;

/* What a run of bytes comes to, with a fresh translation each time */
static const char *sent(const char *bytes, size_t length,
                        struct vt52_ansi *t)
{
    static char out[512];
    size_t at = 0, i;

    memset(t, 0, sizeof *t);

    for (i = 0; i < length; i++)
        at += vt52_ansi_byte(t, (unsigned char)bytes[i], out + at);

    out[at] = 0;
    return out;
}

/* Shown with the escapes visible, so a failure can be read */
static void show(const char *s, char *into, size_t room)
{
    size_t at = 0;

    for (; *s && at + 5 < room; s++)
    {
        if (*s == 0x1b)
            at += (size_t)snprintf(into + at, room - at, "\\e");
        else if ((unsigned char)*s < ' ' || (unsigned char)*s >= 0x7f)
            at += (size_t)snprintf(into + at, room - at, "\\x%02x",
                                   (unsigned char)*s);
        else
            into[at++] = *s;
    }
    into[at] = 0;
}

static void check(const char *bytes, size_t length, const char *want,
                  const char *name)
{
    struct vt52_ansi t;
    const char *got = sent(bytes, length, &t);
    char g[256], w[256];

    n++;
    if (strcmp(got, want) == 0)
        printf("ok %d - %s\n", n, name);
    else
    {
        fails++;
        show(got, g, sizeof g);
        show(want, w, sizeof w);
        printf("not ok %d - %s (got %s, want %s)\n", n, name, g, w);
    }
}

#define CHECK(bytes, want, name) check(bytes, sizeof bytes - 1, want, name)

int main(void)
{
    struct vt52_ansi t;
    char out[VT52_ANSI_MAX];
    size_t len;

    CHECK("hello\r\n", "hello\r\n", "text and a new line go as they are");
    CHECK("\033E", "\033[H\033[2J",
          "ESC E clears the screen and goes home rather than printing an E");
    CHECK("\033v", "\033[?7h",
          "ESC v turns wrapping on rather than printing a v - GenST's");
    CHECK("\033A\033B\033C\033D", "\033[A\033[B\033[C\033[D",
          "the four cursor moves are the four ANSI ones");
    CHECK("\033Y\x25\x2a", "\033[6;11H",
          "ESC Y takes a row and a column, each thirty two on, from one");
    CHECK("\033Yab", "\033[66;67H", "and the numbers go past nine");
    CHECK("\033pX\033q", "\033[7mX\033[27m", "reverse video, and back");
    CHECK("\033K\033J\033l", "\033[K\033[J\033[2K\r", "the erases");
    CHECK("\033b\x01" "a\033c\x00" "b", "ab",
          "the colours are dropped, the number with them");
    CHECK("\033Za", "a", "an escape the VT52 has not got is dropped");
    CHECK("\x01\x02\x7" "\t", "\x7\t",
          "control characters it ignored are dropped, the bell and tab not");
    CHECK("\x0b\x0c", "\n\n", "a vertical tab and a form feed are line feeds");
    CHECK("\x84\x94\x9e", "\xc3\xa4\xc3\xb6\xc3\x9f",
          "the Atari set's a, o and sharp s are their UTF-8");

    /* And what a program leaves behind */
    sent("\033f\033w\033p", 9, &t);
    len = vt52_ansi_restore(&t, out);
    out[len] = 0;
    n++;
    if (strcmp(out, "\033[?25h\033[?7h\033[27m") == 0)
        printf("ok %d - a hidden cursor, no wrapping and reverse video are "
               "put back at the end\n", n);
    else
    {
        fails++;
        printf("not ok %d - what is put back at the end\n", n);
    }

    sent("\033f\033e", 4, &t);
    n++;
    if (vt52_ansi_restore(&t, out) == 0)
        printf("ok %d - and nothing is when the program put it back "
               "itself\n", n);
    else
    {
        fails++;
        printf("not ok %d - and nothing is when the program put it back "
               "itself\n", n);
    }

    printf("1..%d\n", n);

    return fails != 0;
}
