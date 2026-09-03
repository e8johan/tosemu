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
 * The Atari ST character set, and the two conversions across it.
 *
 * The table below was read off the system font rather than remembered. Every
 * entry from 0x7F up was checked against the glyph in
 * 3rdparty/emutos/bios/fnt_st_8x16.c, which is the same character set the
 * machine draws with, so a character claimed here is a character an
 * application would have seen on screen. That is worth the trouble: a table
 * like this is wrong quietly, and the way it shows is somebody's name coming
 * back from a paste with the wrong letter in it.
 *
 * The set is the IBM one as far as 0xAF, which is why so much of it looks
 * familiar, and then stops being: where a PC put box drawing the ST put the
 * letters a European language needs, and after those, Hebrew.
 */

#include "scraptext.h"

#include <stdlib.h>
#include <string.h>

/*
 * What each byte is, as Unicode.
 *
 * Nothing below 0x20 is here. In a file those bytes are control codes - the
 * line endings this also has to fix, and a tab - rather than the pictures the
 * font draws for them, and a clipboard carrying the two halves of the Atari
 * logo across as text would be a joke at the reader's expense.
 */
static const unsigned short codepoint[256] = {
    /* 0x00 - 0x1F: control codes, handled rather than translated */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,

    /* 0x20 - 0x7E: ASCII, which the ST spells the same way */
    0x0020, 0x0021, 0x0022, 0x0023, 0x0024, 0x0025, 0x0026, 0x0027,
    0x0028, 0x0029, 0x002A, 0x002B, 0x002C, 0x002D, 0x002E, 0x002F,
    0x0030, 0x0031, 0x0032, 0x0033, 0x0034, 0x0035, 0x0036, 0x0037,
    0x0038, 0x0039, 0x003A, 0x003B, 0x003C, 0x003D, 0x003E, 0x003F,
    0x0040, 0x0041, 0x0042, 0x0043, 0x0044, 0x0045, 0x0046, 0x0047,
    0x0048, 0x0049, 0x004A, 0x004B, 0x004C, 0x004D, 0x004E, 0x004F,
    0x0050, 0x0051, 0x0052, 0x0053, 0x0054, 0x0055, 0x0056, 0x0057,
    0x0058, 0x0059, 0x005A, 0x005B, 0x005C, 0x005D, 0x005E, 0x005F,
    0x0060, 0x0061, 0x0062, 0x0063, 0x0064, 0x0065, 0x0066, 0x0067,
    0x0068, 0x0069, 0x006A, 0x006B, 0x006C, 0x006D, 0x006E, 0x006F,
    0x0070, 0x0071, 0x0072, 0x0073, 0x0074, 0x0075, 0x0076, 0x0077,
    0x0078, 0x0079, 0x007A, 0x007B, 0x007C, 0x007D, 0x007E,

    /* 0x7F is a delta rather than the PC's house, and is a printing
     * character here rather than a delete */
    0x0394,

    /* 0x80 - 0xAF: the accented letters, as the IBM set had them */
    0x00C7, 0x00FC, 0x00E9, 0x00E2, 0x00E4, 0x00E0, 0x00E5, 0x00E7,  /* Ç ü é â ä à å ç */
    0x00EA, 0x00EB, 0x00E8, 0x00EF, 0x00EE, 0x00EC, 0x00C4, 0x00C5,  /* ê ë è ï î ì Ä Å */
    0x00C9, 0x00E6, 0x00C6, 0x00F4, 0x00F6, 0x00F2, 0x00FB, 0x00F9,  /* É æ Æ ô ö ò û ù */
    0x00FF, 0x00D6, 0x00DC, 0x00A2, 0x00A3, 0x00A5, 0x00DF, 0x0192,  /* ÿ Ö Ü ¢ £ ¥ ß ƒ */
    0x00E1, 0x00ED, 0x00F3, 0x00FA, 0x00F1, 0x00D1, 0x00AA, 0x00BA,  /* á í ó ú ñ Ñ ª º */
    0x00BF, 0x2310, 0x00AC, 0x00BD, 0x00BC, 0x00A1, 0x00AB, 0x00BB,  /* ¿ ⌐ ¬ ½ ¼ ¡ « » */

    /*
     * 0xB0 - 0xBF: where the ST parts company with the PC. A PC drew boxes
     * here; the ST spelled Portuguese, Danish and French instead, and put the
     * three marks a printer needs after them.
     */
    0x00E3, 0x00F5, 0x00D8, 0x00F8, 0x0153, 0x0152, 0x00C0, 0x00C3,  /* ã õ Ø ø œ Œ À Ã */
    0x00D5, 0x00A8, 0x00B4, 0x2020, 0x00B6, 0x00A9, 0x00AE, 0x2122,  /* Õ ¨ ´ † ¶ © ® ™ */

    /* 0xC0, 0xC1: the Dutch ligature, and then Hebrew to the end of the row */
    0x0133, 0x0132,                                                  /* ĳ Ĳ */
    0x05D0, 0x05D1, 0x05D2, 0x05D3, 0x05D4, 0x05D5,                  /* א ב ג ד ה ו */
    0x05D6, 0x05D7, 0x05D8, 0x05D9, 0x05DB, 0x05DC, 0x05DE, 0x05E0,  /* ז ח ט י כ ל מ נ */
    0x05E1, 0x05E2, 0x05E4, 0x05E6, 0x05E7, 0x05E8, 0x05E9, 0x05EA,  /* ס ע פ צ ק ר ש ת */

    /* The five letters Hebrew spells differently at the end of a word */
    0x05DF, 0x05DA, 0x05DD, 0x05E3, 0x05E5,                          /* ן ך ם ף ץ */

    0x00A7, 0x2227, 0x221E,                                          /* § ∧ ∞ */

    /* 0xE0 - 0xFF: the Greek a formula wants, and then the mathematics */
    0x03B1, 0x03B2, 0x0393, 0x03C0, 0x03A3, 0x03C3, 0x00B5, 0x03C4,  /* α β Γ π Σ σ µ τ */
    0x03A6, 0x0398, 0x03A9, 0x03B4, 0x2205, 0x03C6, 0x2208, 0x2229,  /* Φ Θ Ω δ ∅ φ ∈ ∩ */
    0x2261, 0x00B1, 0x2265, 0x2264, 0x2320, 0x2321, 0x00F7, 0x2248,  /* ≡ ± ≥ ≤ ⌠ ⌡ ÷ ≈ */
    0x00B0, 0x2219, 0x00B7, 0x221A, 0x207F, 0x00B2, 0x00B3, 0x00AF   /* ° ∙ · √ ⁿ ² ³ ¯ */
};

unsigned scrap_text_codepoint(unsigned char atari)
{
    return codepoint[atari];
}

/*
 * The other way round, by looking through the table.
 *
 * A hundred and ninety comparisons for a character that is not ASCII, which
 * would be indefensible in a drawing loop and does not matter here: this runs
 * once over what somebody copied, and somebody copying a megabyte of Greek is
 * not the case to build an index for.
 */
static int atari_for(unsigned cp)
{
    int i;

    if (cp >= 0x20 && cp < 0x7F)
        return (int)cp;

    for (i = 0x7F; i < 256; i++)
        if (codepoint[i] == cp)
            return i;

    return -1;
}

/* How many bytes UTF-8 spends on a character */
static int utf8_length(unsigned cp)
{
    if (cp < 0x80)
        return 1;
    if (cp < 0x800)
        return 2;
    if (cp < 0x10000)
        return 3;

    return 4;
}

static char *utf8_put(char *at, unsigned cp)
{
    if (cp < 0x80)
    {
        *at++ = (char)cp;
    }
    else if (cp < 0x800)
    {
        *at++ = (char)(0xC0 | (cp >> 6));
        *at++ = (char)(0x80 | (cp & 0x3F));
    }
    else if (cp < 0x10000)
    {
        *at++ = (char)(0xE0 | (cp >> 12));
        *at++ = (char)(0x80 | ((cp >> 6) & 0x3F));
        *at++ = (char)(0x80 | (cp & 0x3F));
    }
    else
    {
        *at++ = (char)(0xF0 | (cp >> 18));
        *at++ = (char)(0x80 | ((cp >> 12) & 0x3F));
        *at++ = (char)(0x80 | ((cp >> 6) & 0x3F));
        *at++ = (char)(0x80 | (cp & 0x3F));
    }

    return at;
}

/*
 * One character out of UTF-8, saying how many bytes it took.
 *
 * Anything malformed is one byte long and no character, so that a caller
 * stepping through cannot be made to stand still by a bad byte and cannot be
 * walked off the end by a truncated sequence. Overlong forms and the surrogate
 * range are refused for the same reason anything else refuses them: they are
 * two spellings of one character, and letting both in is how a filter gets
 * past something that checked only the other.
 */
static int utf8_take(const unsigned char *from, size_t left, unsigned *cp)
{
    unsigned char c = from[0];
    unsigned value;
    int length, i;

    if (c < 0x80)
    {
        *cp = c;
        return 1;
    }

    if ((c & 0xE0) == 0xC0)
    {
        length = 2;
        value = c & 0x1F;
    }
    else if ((c & 0xF0) == 0xE0)
    {
        length = 3;
        value = c & 0x0F;
    }
    else if ((c & 0xF8) == 0xF0)
    {
        length = 4;
        value = c & 0x07;
    }
    else
    {
        return -1;
    }

    if ((size_t)length > left)
        return -1;

    for (i = 1; i < length; i++)
    {
        if ((from[i] & 0xC0) != 0x80)
            return -1;

        value = (value << 6) | (from[i] & 0x3F);
    }

    if (value < (unsigned)(length == 2 ? 0x80 : length == 3 ? 0x800 : 0x10000))
        return -1;

    if (value >= 0xD800 && value <= 0xDFFF)
        return -1;

    if (value > 0x10FFFF)
        return -1;

    *cp = value;

    return length;
}

char *scrap_text_to_utf8(const char *atari, size_t length, size_t *out_length)
{
    const unsigned char *from = (const unsigned char *)atari;
    size_t needed = 0;
    size_t i;
    char *out, *at;

    /*
     * Measured before it is written, so that the buffer is the size the answer
     * needs rather than a guess that has to be grown. A character can take
     * three bytes where it took one, and most take one, so neither the length
     * nor three times it is close enough to be worth the arithmetic.
     */
    for (i = 0; i < length; i++)
    {
        unsigned char c = from[i];

        if (c == '\r')
        {
            /* CR LF is one ending, not two. A CR on its own is one as well:
             * some applications wrote them that way and the desktop has no
             * spelling for a line ending that is not LF. */
            if (i + 1 < length && from[i + 1] == '\n')
                i++;

            needed += 1;
        }
        else if (c == '\n' || c == '\t')
        {
            needed += 1;
        }
        else if (codepoint[c])
        {
            needed += utf8_length(codepoint[c]);
        }
    }

    out = malloc(needed + 1);
    if (!out)
        return 0;

    at = out;

    for (i = 0; i < length; i++)
    {
        unsigned char c = from[i];

        if (c == '\r')
        {
            if (i + 1 < length && from[i + 1] == '\n')
                i++;

            *at++ = '\n';
        }
        else if (c == '\n' || c == '\t')
        {
            *at++ = (char)c;
        }
        else if (codepoint[c])
        {
            at = utf8_put(at, codepoint[c]);
        }
    }

    *at = 0;

    if (out_length)
        *out_length = (size_t)(at - out);

    return out;
}

char *scrap_text_from_utf8(const char *utf8, size_t length, size_t *out_length)
{
    const unsigned char *from = (const unsigned char *)utf8;
    size_t i;
    size_t needed = 0;
    char *out, *at;

    /*
     * A byte order mark is not a character, it is a note about the encoding,
     * and one left in the text would come out as the question mark for
     * something the ST has never heard of. Desktops still put them on.
     */
    if (length >= 3 && from[0] == 0xEF && from[1] == 0xBB && from[2] == 0xBF)
    {
        from += 3;
        length -= 3;
    }

    /* An LF becomes two bytes, and nothing becomes more than two, so this is
     * the size rather than an estimate of it */
    needed = length * 2;

    out = malloc(needed + 1);
    if (!out)
        return 0;

    at = out;
    i = 0;

    while (i < length)
    {
        unsigned cp;
        int took = utf8_take(from + i, length - i, &cp);
        int c;

        if (took < 0)
        {
            /* A byte that is not part of anything. Say so once and carry on:
             * the rest of the text is still worth having. */
            *at++ = '?';
            i++;
            continue;
        }

        i += (size_t)took;

        if (cp == '\n')
        {
            /* The ST writes both halves. A CR already in front of this one is
             * not doubled - some desktops send CR LF and the ST would show the
             * second ending as a blank line. */
            if (at > out && at[-1] == '\r')
                *at++ = '\n';
            else
            {
                *at++ = '\r';
                *at++ = '\n';
            }

            continue;
        }

        if (cp == '\r')
        {
            *at++ = '\r';
            continue;
        }

        if (cp == '\t')
        {
            *at++ = '\t';
            continue;
        }

        /*
         * Everything else that is not a printing character goes. A NUL would
         * end the text where the ST is concerned, and the rest are pictures
         * in the ST font rather than the controls they are here.
         */
        if (cp < 0x20 || cp == 0x7F)
            continue;

        c = atari_for(cp);

        *at++ = (char)(c < 0 ? '?' : c);
    }

    *at = 0;

    if (out_length)
        *out_length = (size_t)(at - out);

    return out;
}
