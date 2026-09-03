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
 * Turning what a GEM application cut out into what the desktop expects.
 *
 * Built for the host, for the same reason bin/settingstest is: an emulated
 * program can be handed a converted scrap and say what it read, but it cannot
 * see whether the bytes in between were the right ones, and it cannot be given
 * a malformed UTF-8 sequence to be unbothered by. Both ends of the conversion
 * are reachable from here and neither is reachable from there.
 *
 * The round trip over every printing character is the check worth having. A
 * table like this goes wrong in one entry rather than all of them, and one
 * entry is exactly what an example-by-example test misses.
 */

#include "scraptext.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int n;
static int fails;

static void check(long got, long want, const char *name)
{
    n++;
    if (got == want)
        printf("ok %d - %s\n", n, name);
    else
    {
        fails++;
        printf("not ok %d - %s (got %ld, want %ld)\n", n, name, got, want);
    }
}

static void check_str(const char *got, const char *want, const char *name)
{
    n++;
    if (got && want && strcmp(got, want) == 0)
        printf("ok %d - %s\n", n, name);
    else
    {
        fails++;
        printf("not ok %d - %s (got [%s], want [%s])\n", n, name,
               got ? got : "nothing", want ? want : "nothing");
    }
}

/*
 * The same for text with a NUL or a stray byte in it, which is most of what is
 * interesting here. Says where they first differ rather than showing both:
 * a line ending is one byte in the middle of a paragraph and the eye slides
 * straight over it.
 */
static void check_bytes(const char *got, size_t got_length,
                        const char *want, size_t want_length,
                        const char *name)
{
    size_t i;

    n++;

    if (got && got_length == want_length &&
        memcmp(got, want, want_length) == 0)
    {
        printf("ok %d - %s\n", n, name);
        return;
    }

    fails++;

    if (!got)
    {
        printf("not ok %d - %s (got nothing)\n", n, name);
        return;
    }

    for (i = 0; i < got_length && i < want_length; i++)
        if (got[i] != want[i])
        {
            printf("not ok %d - %s (byte %d is 0x%02X, want 0x%02X)\n",
                   n, name, (int)i, (unsigned char)got[i],
                   (unsigned char)want[i]);
            return;
        }

    printf("not ok %d - %s (got %d bytes, want %d)\n", n, name,
           (int)got_length, (int)want_length);
}

/* Both directions take a length, and every literal here is a string, so this
 * saves saying strlen at every call */
static char *out(const char *atari, size_t *length)
{
    return scrap_text_to_utf8(atari, strlen(atari), length);
}

static char *in(const char *utf8, size_t *length)
{
    return scrap_text_from_utf8(utf8, strlen(utf8), length);
}

int main(void)
{
    char *s;
    size_t length;

    /* Text that means the same on both machines */
    s = out("Hello", &length);
    check_str(s, "Hello", "plain ASCII goes out unchanged");
    check(length, 5, "and is as long as it was");
    free(s);

    s = in("Hello", &length);
    check_str(s, "Hello", "and comes back unchanged");
    free(s);

    /* The accented letters, which are the point of the exercise */
    s = out("\x80\x81\x82", &length);
    check_str(s, "\xC3\x87\xC3\xBC\xC3\xA9", "the accented letters go out as UTF-8");
    free(s);

    s = in("\xC3\x87\xC3\xBC\xC3\xA9", &length);
    check_bytes(s, length, "\x80\x81\x82", 3, "and come back as the ST spells them");
    free(s);

    /* The two the ST puts where a PC had box drawing */
    s = out("\xB4\xB5", &length);
    check_str(s, "\xC5\x93\xC5\x92", "oe and OE are the ST's, not the PC's");
    free(s);

    /* And the one the PC used for a house */
    check(scrap_text_codepoint(0x7F), 0x0394, "0x7F is a delta");

    s = out("\x7F", &length);
    check_str(s, "\xCE\x94", "and goes out as one");
    free(s);

    /*
     * Line endings, which is the other half of the difference. Checked by the
     * byte rather than as strings: what is being compared is text with a
     * newline in the middle of it, and a failure printed as two lines of a
     * message is a failure nobody can read out of a build log.
     */
    s = out("a\r\nb", &length);
    check_bytes(s, length, "a\nb", 3, "CR LF becomes one LF");
    free(s);

    s = out("a\rb", &length);
    check_bytes(s, length, "a\nb", 3, "and so does a CR on its own");
    free(s);

    s = out("a\nb", &length);
    check_bytes(s, length, "a\nb", 3, "an LF already on its own is left alone");
    free(s);

    s = in("a\nb", &length);
    check_bytes(s, length, "a\r\nb", 4, "an LF coming in becomes CR LF");
    free(s);

    s = in("a\r\nb", &length);
    check_bytes(s, length, "a\r\nb", 4, "and a CR LF is not doubled");
    free(s);

    s = in("a\n\nb", &length);
    check_bytes(s, length, "a\r\n\r\nb", 6, "a blank line survives as one");
    free(s);

    /* Tabs are text; the rest of the control codes are not */
    s = out("a\tb", &length);
    check_str(s, "a\tb", "a tab is text and stays");
    free(s);

    s = out("a\x01\x02z", &length);
    check_str(s, "az", "the pictures the ST draws for control codes do not");
    free(s);

    s = in("a\x01z", &length);
    check_str(s, "az", "and a control code coming in is dropped");
    free(s);

    /* What neither machine can say to the other */
    s = in("a\xE6\x97\xA5z", &length);
    check_str(s, "a?z", "a character the ST has no letter for becomes a question mark");
    free(s);

    s = in("a\xFFz", &length);
    check_str(s, "a?z", "and so does a byte that is not UTF-8 at all");
    free(s);

    s = in("a\xE6\x97z", &length);
    check_str(s, "a??z", "a sequence that stops early does not eat what follows");
    free(s);

    /* Overlong forms and surrogates are two spellings of one character, and
     * letting both in is how something gets past a filter that checked one */
    s = in("\xC0\xAF", &length);
    check_str(s, "??", "an overlong form is refused rather than decoded");
    free(s);

    s = in("\xED\xA0\x80", &length);
    check_str(s, "???", "and so is a surrogate");
    free(s);

    /* A note about the encoding is not a character */
    s = in("\xEF\xBB\xBFHello", &length);
    check_str(s, "Hello", "a byte order mark is not text");
    free(s);

    /* A NUL would end the text as far as GEM is concerned */
    s = scrap_text_from_utf8("a\0z", 3, &length);
    check_bytes(s, length, "az", 2, "a NUL is dropped rather than carried across");
    free(s);

    /*
     * Every printing character, both ways. This is the check that catches a
     * single wrong entry, which is the way a table like this actually fails -
     * and it catches two entries claiming the same character as well, because
     * the second of them comes back as the first.
     */
    {
        int c;
        int wrong = 0;
        int missing = 0;

        for (c = 0x20; c < 0x100; c++)
        {
            char one[2];
            char *utf8;
            char *back;
            size_t utf8_length, back_length;

            if (!scrap_text_codepoint((unsigned char)c))
            {
                missing++;
                continue;
            }

            one[0] = (char)c;
            one[1] = 0;

            utf8 = scrap_text_to_utf8(one, 1, &utf8_length);
            if (!utf8)
                continue;

            back = scrap_text_from_utf8(utf8, utf8_length, &back_length);

            if (!back || back_length != 1 || (unsigned char)back[0] != c)
            {
                if (!wrong)
                    printf("# first to fail is 0x%02X, which came back as "
                           "0x%02X\n", c,
                           back && back_length ? (unsigned char)back[0] : 0);
                wrong++;
            }

            free(utf8);
            free(back);
        }

        check(wrong, 0, "every printing character survives the round trip");
        check(missing, 0, "and every one of them has a character to be");
    }

    /* A few the table would be wrong about quietly */
    check(scrap_text_codepoint(0x9E), 0x00DF, "0x9E is a sharp s");
    check(scrap_text_codepoint(0xBD), 0x00A9, "0xBD is a copyright sign");
    check(scrap_text_codepoint(0xC2), 0x05D0, "0xC2 is an aleph");
    check(scrap_text_codepoint(0xE6), 0x00B5, "0xE6 is a micro sign");
    check(scrap_text_codepoint(0xFF), 0x00AF, "0xFF is a macron");
    check(scrap_text_codepoint(0x00), 0, "and nothing below a space is a character");

    printf("1..%d\n", n);

    return fails ? 1 : 0;
}
