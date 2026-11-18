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
 * The one line a traced OS call comes to. See calltrace.h.
 *
 * The shape is oTOSis's strace, which kept a format for each call's arguments
 * and answer beside its entry in the dispatch table, and named the error an
 * answer was rather than leaving a person to look -33 up.
 */

#include "calltrace.h"

#include <stdio.h>
#include <string.h>

#include "cpu.h"
#include "memory.h"

/* How much of a string argument is shown before the rest is left out */
#define STRING_SHOWN (40)

/*
 * The errors TOS answers with, by name. The BIOS's are the first seventeen,
 * GEMDOS's begin at -32, and the rest are MiNT's. -6 is the BIOS's seek
 * error and MiNT's ESPIPE, and is named for what tosemu uses it for.
 */
static const struct
{
    int32_t code;
    const char *name;
} errors[] = {
    {  -1, "ERROR" },   {  -2, "EDRVNR" },  {  -3, "EUNCMD" },
    {  -4, "E_CRC" },   {  -5, "EBADRQ" },  {  -6, "ESPIPE" },
    {  -7, "EMEDIA" },  {  -8, "ESECNF" },  {  -9, "EPAPER" },
    { -10, "EWRITF" },  { -11, "EREADF" },  { -12, "EWRPRO" },
    { -14, "E_CHNG" },  { -15, "EUNDEV" },  { -16, "EBADSF" },
    { -17, "EOTHER" },  { -25, "EINVAL" },
    { -32, "EINVFN" },  { -33, "EFILNF" },  { -34, "EPTHNF" },
    { -35, "ENHNDL" },  { -36, "EACCDN" },  { -37, "EIHNDL" },
    { -39, "ENSMEM" },  { -40, "EIMBA" },   { -46, "EDRIVE" },
    { -47, "ECWD" },    { -48, "ENSAME" },  { -49, "ENMFIL" },
    { -58, "ELOCKED" }, { -59, "ENSLOCK" }, { -64, "ERANGE" },
    { -65, "EINTRN" },  { -66, "EPLFMT" },  { -67, "EGSBF" },
    { -68, "EBREAK" },  { -69, "EXCPT" },   { -70, "EPTHOV" },
    { -80, "ELOOP" },   { -81, "EPIPE" },   { -200, "EMOUNT" }
};

static const char *error_name(int32_t code)
{
    size_t i;

    for (i = 0; i < sizeof errors / sizeof errors[0]; i++)
        if (errors[i].code == code)
            return errors[i].name;

    return NULL;
}

/* Appending to the line without running off the end of it */
static void add(char *buf, size_t size, size_t *at, const char *text)
{
    size_t len = strlen(text);

    if (*at >= size)
        return;

    if (len > size - 1 - *at)
        len = size - 1 - *at;

    memcpy(buf + *at, text, len);
    *at += len;
    buf[*at] = 0;
}

/*
 * A string in the machine, quoted, with what is not printable escaped and the
 * end of a long one left off.
 *
 * Read only where it is plain memory, a byte at a time, so that a pointer that
 * is not one shows as an address rather than stopping the emulator, and so
 * that the trace never reads something whose reading does something - a
 * cartridge key, or the memory midivec points at.
 */
static void add_string(char *buf, size_t size, size_t *at, uint32_t address)
{
    char piece[16];
    int i;

    if (address == 0)
    {
        add(buf, size, at, "NULL");
        return;
    }

    if (!tos_mem_span(address, 1))
    {
        snprintf(piece, sizeof piece, "0x%x", address);
        add(buf, size, at, piece);
        return;
    }

    add(buf, size, at, "\"");

    for (i = 0; i < STRING_SHOWN; i++)
    {
        const uint8_t *p = tos_mem_span(address + i, 1);

        if (!p || *p == 0)
            break;

        if (*p == '"' || *p == '\\')
            snprintf(piece, sizeof piece, "\\%c", *p);
        else if (*p == '\r')
            strcpy(piece, "\\r");
        else if (*p == '\n')
            strcpy(piece, "\\n");
        else if (*p < ' ' || *p >= 0x7f)
            snprintf(piece, sizeof piece, "\\x%02x", *p);
        else
            snprintf(piece, sizeof piece, "%c", *p);

        add(buf, size, at, piece);
    }

    add(buf, size, at, "\"");

    if (i == STRING_SHOWN)
        add(buf, size, at, "...");
}

void calltrace_args(char *buf, size_t size, const char *spec)
{
    size_t at = 0;
    int offset = 2;     /* past the function number */
    char piece[32];

    if (size == 0)
        return;
    buf[0] = 0;

    if (!spec)
    {
        add(buf, size, &at, "...");
        return;
    }

    for (; *spec && *spec != '='; spec++)
    {
        if (at > 0)
            add(buf, size, &at, ", ");

        switch (*spec)
        {
        case 'w':
            snprintf(piece, sizeof piece, "%d", peek_s16(offset));
            offset += 2;
            break;
        case 'x':
            snprintf(piece, sizeof piece, "0x%x", peek_u16(offset));
            offset += 2;
            break;
        case 'c':
        {
            uint16_t ch = peek_u16(offset);

            if ((ch & 0xff) >= ' ' && (ch & 0xff) < 0x7f && ch < 0x100)
                snprintf(piece, sizeof piece, "'%c'", ch);
            else
                snprintf(piece, sizeof piece, "0x%x", ch);
            offset += 2;
            break;
        }
        case 'l':
            snprintf(piece, sizeof piece, "%d", peek_s32(offset));
            offset += 4;
            break;
        case 'L':
        case 'p':
            snprintf(piece, sizeof piece, "0x%x", peek_u32(offset));
            offset += 4;
            break;
        case 's':
            add_string(buf, size, &at, peek_u32(offset));
            offset += 4;
            continue;
        default:
            snprintf(piece, sizeof piece, "?");
            break;
        }

        add(buf, size, &at, piece);
    }
}

/* What the answer is, from the part of the spec after the '=' */
static char answer_kind(const char *spec)
{
    const char *eq = spec ? strchr(spec, '=') : NULL;

    return eq ? eq[1] : 'e';
}

int calltrace_no_return(const char *spec)
{
    return answer_kind(spec) == '-';
}

void calltrace_line(const char *name, const char *args, const char *spec,
                    uint32_t r)
{
    int32_t value = (int32_t)r;
    const char *error;

    switch (answer_kind(spec))
    {
    case '-':
        printf("%s(%s)\n", name, args);
        break;
    case 'a':
    case 'x':
        printf("%s(%s) = 0x%x\n", name, args, r);
        break;
    case 'k':
        printf("%s(%s) = 0x%08x\n", name, args, r);
        break;
    default:
        error = value < 0 ? error_name(value) : NULL;
        if (error)
            printf("%s(%s) = %d %s\n", name, args, value, error);
        else
            printf("%s(%s) = %d\n", name, args, value);
        break;
    }
}
