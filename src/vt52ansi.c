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
 * The VT52 said in ANSI. See vt52ansi.h.
 *
 * The table of what each escape becomes is EmuTOS's, from bios/vt52.c, with
 * three differences that come from this being a terminal somebody is working
 * in rather than a serial line somebody is logging:
 *
 * Clearing the screen and going home do what they say. EmuTOS turns them into
 * a new line, so that a log is not rubbed out; here a program that clears the
 * screen to draw a menu on it wants it cleared.
 *
 * Wrapping is the DEC private mode, ESC [ ? 7, which is the one terminals
 * honour. ESC [ 7 h, which EmuTOS sends, is a different mode altogether.
 *
 * The colours are dropped. ESC b and ESC c name an entry in the ST's palette,
 * which is the program's, and the terminal's colours are the person's: EmuTOS
 * sends 30 plus the number, which turns the high resolution screen's black,
 * pen one, into red. There is no right answer to translate to, so there is no
 * answer.
 */

#include "vt52ansi.h"

#include <stdio.h>
#include <string.h>

#include "scraptext.h"

#define ESC (0x1b)

/* Where in a sequence a byte arrives */
enum
{
    NORMAL = 0,
    ESCAPE,             /* after ESC */
    ROW,                /* after ESC Y */
    COLUMN,             /* after ESC Y and the row */
    COLOUR              /* after ESC b or ESC c, whose number is dropped */
};

/* What ESC and a letter become, where it becomes anything */
static const struct
{
    char letter;
    const char *ansi;
} escapes[] = {
    { 'A', "\033[A" },              /* cursor up */
    { 'B', "\033[B" },              /* cursor down */
    { 'C', "\033[C" },              /* cursor right */
    { 'D', "\033[D" },              /* cursor left */
    { 'E', "\033[H\033[2J" },       /* clear the screen and go home */
    { 'H', "\033[H" },              /* home */
    { 'I', "\033M" },               /* up a line, scrolling at the top */
    { 'J', "\033[J" },              /* erase to the end of the screen */
    { 'K', "\033[K" },              /* erase to the end of the line */
    { 'L', "\033[L" },              /* insert a line */
    { 'M', "\033[M" },              /* delete a line */
    { 'd', "\033[1J" },             /* erase from the top to the cursor */
    { 'e', "\033[?25h" },           /* show the cursor */
    { 'f', "\033[?25l" },           /* hide it */
    { 'j', "\0337" },               /* save where the cursor is */
    { 'k', "\0338" },               /* and go back there */
    { 'l', "\033[2K\r" },           /* erase the line, cursor to its start */
    { 'o', "\033[1K" },             /* erase from the start of the line */
    { 'p', "\033[7m" },             /* reverse video */
    { 'q', "\033[27m" },            /* and back */
    { 'v', "\033[?7h" },            /* wrap at the end of a line */
    { 'w', "\033[?7l" }             /* and do not */
};

/* A character as UTF-8, which is at most three bytes for the Atari set */
static size_t utf8(unsigned cp, char *out)
{
    if (cp < 0x80)
    {
        out[0] = (char)cp;
        return 1;
    }

    if (cp < 0x800)
    {
        out[0] = (char)(0xc0 | (cp >> 6));
        out[1] = (char)(0x80 | (cp & 0x3f));
        return 2;
    }

    out[0] = (char)(0xe0 | (cp >> 12));
    out[1] = (char)(0x80 | ((cp >> 6) & 0x3f));
    out[2] = (char)(0x80 | (cp & 0x3f));
    return 3;
}

/* What the program turned on or off, from the escape that did it */
static void remember(struct vt52_ansi *t, char letter)
{
    switch (letter)
    {
    case 'e': t->cursor_hidden = 0; break;
    case 'f': t->cursor_hidden = 1; break;
    case 'v': t->unwrapped = 0;     break;
    case 'w': t->unwrapped = 1;     break;
    case 'p': t->reversed = 1;      break;
    case 'q': t->reversed = 0;      break;
    }
}

static size_t escaped(struct vt52_ansi *t, unsigned char letter, char *out)
{
    size_t i;

    t->state = NORMAL;

    switch (letter)
    {
    case 'Y':
        t->state = ROW;
        return 0;
    case 'b':
    case 'c':
        t->state = COLOUR;
        return 0;
    }

    for (i = 0; i < sizeof escapes / sizeof escapes[0]; i++)
        if (escapes[i].letter == (char)letter)
        {
            remember(t, (char)letter);
            strcpy(out, escapes[i].ansi);
            return strlen(out);
        }

    return 0;           /* one the terminal has nothing for */
}

size_t vt52_ansi_byte(struct vt52_ansi *t, unsigned char ch, char *out)
{
    unsigned cp;

    switch (t->state)
    {
    case ESCAPE:
        return escaped(t, ch, out);

    case ROW:
        t->row = ch < 32 ? 0 : ch - 32;
        t->state = COLUMN;
        return 0;

    case COLUMN:
        t->state = NORMAL;
        return (size_t)snprintf(out, VT52_ANSI_MAX, "\033[%d;%dH",
                                t->row + 1, (ch < 32 ? 0 : ch - 32) + 1);

    case COLOUR:
        t->state = NORMAL;
        return 0;
    }

    if (ch == ESC)
    {
        t->state = ESCAPE;
        return 0;
    }

    /*
     * The control characters the VT52 acted on: the bell, backspace, tab,
     * line feed and carriage return, with the vertical tab and the form feed
     * being line feeds to it. Everything else below a space it threw away.
     */
    if (ch < ' ')
    {
        switch (ch)
        {
        case 7: case 8: case 9: case 10: case 13:
            out[0] = (char)ch;
            return 1;
        case 11: case 12:
            out[0] = '\n';
            return 1;
        default:
            return 0;
        }
    }

    if (ch < 0x7f)
    {
        out[0] = (char)ch;
        return 1;
    }

    /* The Atari's own half of the set, and the one above ASCII's end */
    cp = scrap_text_codepoint(ch);

    return utf8(cp ? cp : '?', out);
}

size_t vt52_ansi_restore(struct vt52_ansi *t, char *out)
{
    out[0] = 0;

    if (t->cursor_hidden)
        strcat(out, "\033[?25h");
    if (t->unwrapped)
        strcat(out, "\033[?7h");
    if (t->reversed)
        strcat(out, "\033[27m");

    t->cursor_hidden = t->unwrapped = t->reversed = 0;

    return strlen(out);
}
