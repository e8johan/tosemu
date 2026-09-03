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
 * Resource files.
 *
 * A GEM application does not build its dialogs in code. It draws them in a
 * resource editor, which writes a .RSC file, and at startup it calls rsrc_load
 * and then asks for each tree by number. Almost every real application works
 * this way, so until this exists almost none of them can start.
 *
 * A resource file is nearly ready to use as it stands: it is the structures
 * laid end to end, in the machine's own byte order, with every pointer written
 * as an offset from the start of the file. Loading one is reading it into
 * memory and turning those offsets into addresses. That is all rsrc_load does,
 * and all this does.
 *
 * It happens in the machine's memory rather than in ours, which is the whole
 * shape of this file. The application is given the address of a tree and hands
 * it straight back to objc_draw or form_do, so the tree has to be somewhere it
 * can hold the address of - and in the layout it expects, which is the 68000's
 * twenty-four byte OBJECT rather than the one this program is compiled for.
 * Reading the file into our memory and copying it across afterwards would mean
 * writing the marshaller in aestree.c a second time, backwards.
 *
 * The other thing the coordinates need is the character size, because a
 * resource editor stores them in characters and the AES stores them in pixels.
 * A resource is therefore worth nothing until a workstation is open, which is
 * why EmuTOS keeps that fixup separate and why rsrc_obfix exists at all.
 */

#include "aes_p.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

#include "emuvdi/emuvdi.h"
#include "files.h"
#include "gemdosmem_p.h"
#include "tossystem.h"
#include "m68k.h"

/* The header at the front of the file, as word offsets into it doubled - see
 * the RSHDR structure. Everything in it is a word. */
#define RSH_VRSN     (0)
#define RSH_OBJECT   (2)
#define RSH_TEDINFO  (4)
#define RSH_ICONBLK  (6)
#define RSH_BITBLK   (8)
#define RSH_FRSTR   (10)
#define RSH_STRING  (12)
#define RSH_IMDATA  (14)
#define RSH_FRIMG   (16)
#define RSH_TRINDEX (18)
#define RSH_NOBS    (20)
#define RSH_NTREE   (22)
#define RSH_NTED    (24)
#define RSH_NIB     (26)
#define RSH_NBB     (28)
#define RSH_NSTRING (30)
#define RSH_NIMAGES (32)
#define RSH_RSSIZE  (34)
#define RSH_SIZE    (36)

/*
 * How large each of these is in the machine, which is not how large it is
 * here: a pointer there is four bytes and here it is eight. The file was
 * written for the machine, so these are the machine's.
 */
#define OB_SIZE     (24)
#define TE_SIZE     (28)
#define IB_SIZE     (34)
#define BI_SIZE     (14)

/* Where the pointers are inside those */
#define OB_TYPE      (6)
#define OB_SPEC     (12)
#define OB_X        (16)

#define TE_PTEXT     (0)
#define TE_PTMPLT    (4)
#define TE_PVALID    (8)
#define TE_TXTLEN   (24)
#define TE_TMPLEN   (26)

#define IB_PMASK     (0)
#define IB_PDATA     (4)
#define IB_PTEXT     (8)

#define BI_PDATA     (0)

/* The object types whose ob_spec is not a pointer, obdefs.h */
#define G_BOX       (20)
#define G_IBOX      (25)
#define G_BOXCHAR   (27)

/* What rsrc_gaddr and rsrc_saddr call the parts of a resource, aesdefs.h */
#define R_TREE       (0)
#define R_OBJECT     (1)
#define R_TEDINFO    (2)
#define R_ICONBLK    (3)
#define R_BITBLK     (4)
#define R_STRING     (5)
#define R_IMAGEDATA  (6)
#define R_OBSPEC     (7)
#define R_TEPTEXT    (8)
#define R_TEPTMPLT   (9)
#define R_TEPVALID  (10)
#define R_IBPMASK   (11)
#define R_IBPDATA   (12)
#define R_IBPTEXT   (13)
#define R_BIPDATA   (14)
#define R_FRSTR     (15)
#define R_FRIMG     (16)

/* A resource file that claims to be larger than this is not one */
#define MAX_RESOURCE (4L * 1024 * 1024)

/* The one loaded, because an application has one at a time */
static uint32_t resource;

void aes_rsrc_reset()
{
    resource = 0;
}

/* A word of the header */
static uint16_t header(int field)
{
    return (uint16_t)m68k_read_memory_16(resource + field);
}

/*
 * Turning one offset into an address.
 *
 * Minus one is how the file says there is nothing there, and it has to stay
 * that way: the AES and the application both test for it.
 */
static int fix_long(uint32_t at)
{
    uint32_t value = m68k_read_memory_32(at);

    if (value == 0xffffffff)
        return 0;

    m68k_write_memory_32(at, resource + value);

    return 1;
}

/* The address of one of a kind of thing, by number */
static uint32_t item(int type, uint16_t index)
{
    switch (type)
    {
        case R_TREE:
            return m68k_read_memory_32(resource + header(RSH_TRINDEX)
                                       + index * 4);

        case R_OBJECT:
            return resource + header(RSH_OBJECT) + index * OB_SIZE;

        case R_TEDINFO:
        case R_TEPTEXT:     /* te_ptext is the first thing in a TEDINFO */
            return resource + header(RSH_TEDINFO) + index * TE_SIZE;

        case R_ICONBLK:
        case R_IBPMASK:     /* and ib_pmask the first in an ICONBLK */
            return resource + header(RSH_ICONBLK) + index * IB_SIZE;

        case R_BITBLK:
        case R_BIPDATA:     /* and bi_pdata the first in a BITBLK */
            return resource + header(RSH_BITBLK) + index * BI_SIZE;

        case R_OBSPEC:
            return item(R_OBJECT, index) + OB_SPEC;

        case R_TEPTMPLT:
            return item(R_TEDINFO, index) + TE_PTMPLT;
        case R_TEPVALID:
            return item(R_TEDINFO, index) + TE_PVALID;

        case R_IBPDATA:
            return item(R_ICONBLK, index) + IB_PDATA;
        case R_IBPTEXT:
            return item(R_ICONBLK, index) + IB_PTEXT;

        case R_FRSTR:
            return resource + header(RSH_FRSTR) + index * 4;
        case R_FRIMG:
            return resource + header(RSH_FRIMG) + index * 4;

        /*
         * These two are asked for by what they point at rather than by where
         * the pointer is, because a free string is a string and not somewhere
         * a string is kept.
         */
        case R_STRING:
            return m68k_read_memory_32(resource + header(RSH_FRSTR)
                                       + index * 4);
        case R_IMAGEDATA:
            return m68k_read_memory_32(resource + header(RSH_FRIMG)
                                       + index * 4);

        default:
            return 0;
    }
}

/* How many there are of a kind of thing, for bounds checking what an
 * application asks for */
static uint16_t count_of(int type)
{
    switch (type)
    {
        case R_TREE:
            return header(RSH_NTREE);
        case R_OBJECT:
        case R_OBSPEC:
            return header(RSH_NOBS);
        case R_TEDINFO:
        case R_TEPTEXT:
        case R_TEPTMPLT:
        case R_TEPVALID:
            return header(RSH_NTED);
        case R_ICONBLK:
        case R_IBPMASK:
        case R_IBPDATA:
        case R_IBPTEXT:
            return header(RSH_NIB);
        case R_BITBLK:
        case R_BIPDATA:
            return header(RSH_NBB);
        case R_STRING:
        case R_FRSTR:
            return header(RSH_NSTRING);
        case R_IMAGEDATA:
        case R_FRIMG:
            return header(RSH_NIMAGES);
        default:
            return 0;
    }
}

/* The length of a string in the machine, which is what a TEDINFO records
 * beside the pointer to it */
static int16_t string_length(uint32_t at)
{
    int16_t n = 0;

    while (n < 1024 && m68k_read_memory_8(at + n))
        n++;

    return (int16_t)(n + 1);
}

/*
 * A coordinate, from what a resource editor stores to what the AES uses.
 *
 * The editor works in characters and puts the number of them in the low byte,
 * with any odd pixels on top of that in the high one. Which character size to
 * multiply by depends on whether it is an across or a down, which is why this
 * is told which of the four it is doing.
 *
 * Eighty characters across means the whole width, whatever the screen is. That
 * is how a dialog that fills the screen is drawn on a screen the person who
 * drew it never saw.
 */
static void fix_coordinate(uint32_t at, int which,
                           int16_t wchar, int16_t hchar, int16_t width)
{
    int16_t stored = (int16_t)m68k_read_memory_16(at);
    int16_t chars = stored & 0xff;
    int16_t pixels = (stored >> 8) & 0xff;

    if (pixels > 128)
        pixels -= 256;

    switch (which)
    {
        case 0: chars *= wchar; break;                      /* x */
        case 1: chars *= hchar; break;                      /* y */
        case 2: chars = (chars == 80) ? width
                                      : chars * wchar; break;   /* width */
        case 3: chars *= hchar; break;                      /* height */
    }

    m68k_write_memory_16(at, (uint16_t)(chars + pixels));
}

/*
 * The objects, which are done apart from everything else.
 *
 * Their coordinates need the character size, and that is not known until a
 * workstation is open. An application is allowed to load its resource before
 * opening one - and the AES itself has to, because its own dialogs are in a
 * resource it needs before it can say how large a character is.
 */
static void fix_objects()
{
    int16_t handle, wchar, hchar, wbox, hbox;
    uint16_t objects = header(RSH_NOBS);
    uint16_t i;

    emuvdi_graf_handle(&handle, &wchar, &hchar, &wbox, &hbox);

    for (i = 0; i < objects; i++)
    {
        uint32_t object = item(R_OBJECT, i);
        uint16_t type = (uint16_t)m68k_read_memory_16(object + OB_TYPE) & 0xff;
        int which;

        for (which = 0; which < 4; which++)
            fix_coordinate(object + OB_X + which*2, which,
                           wchar, hchar, emuvdi_screen_width());

        /*
         * ob_spec is a pointer for most kinds of object and a packed set of
         * colours for the three kinds of box. Fixing up a colour word as
         * though it were an offset makes a wild pointer that still points at
         * something, which is the kind of mistake that draws almost right.
         */
        switch (type)
        {
            case G_BOX:
            case G_IBOX:
            case G_BOXCHAR:
                break;
            default:
                fix_long(object + OB_SPEC);
                break;
        }
    }
}

/* rsrc_load ***************************************************************/

/* Reads the whole file in beside the header we already have, or says why not */
static int read_resource(FILE *f, uint32_t size)
{
    uint8_t *copy = malloc(size);
    uint32_t i;

    if (!copy)
        return 0;

    if (fread(copy, 1, size, f) != size)
    {
        free(copy);
        return 0;
    }

    for (i = 0; i < size; i++)
        m68k_write_memory_8(resource + i, copy[i]);

    free(copy);

    return 1;
}

uint32_t AES_rsrc_load()
{
    uint32_t name_address = aes_addrin(0);
    char name[PATH_MAX + 1], host[PATH_MAX + 1];
    uint32_t size;
    FILE *f;
    uint16_t i, n;

    for (i = 0; i < sizeof name - 1; i++)
    {
        name[i] = (char)m68k_read_memory_8(name_address + i);
        if (name[i] == 0)
            break;
    }
    name[i] = 0;

    FUNC_TRACE_ENTER_ARGS {
        printf("    file: %s\n", name);
    }

    if (resource)
        AES_rsrc_free();

    if (tos_path_to_host(name, host) != 0)
        return 0;

    f = fopen(host, "rb");
    if (!f)
        return 0;

    /*
     * How large it is, which the file says itself rather than being however
     * large the file happens to be: what is after the end of a resource is not
     * part of it.
     */
    {
        uint8_t head[RSH_SIZE];

        if (fread(head, 1, sizeof head, f) != sizeof head)
        {
            fclose(f);
            return 0;
        }

        size = (uint32_t)((head[RSH_RSSIZE] << 8) | head[RSH_RSSIZE + 1]);
    }

    if (size < RSH_SIZE || size > MAX_RESOURCE)
    {
        fclose(f);
        printf("AES rsrc_load: %s says it is %u bytes, which is not a "
               "resource file\n", name, size);
        return 0;
    }

    resource = mem_alloc(size);
    if (!resource)
    {
        fclose(f);
        printf("AES rsrc_load: no room in the machine for %u bytes\n", size);
        return 0;
    }

    rewind(f);
    if (!read_resource(f, size))
    {
        fclose(f);
        mem_free(resource);
        resource = 0;
        return 0;
    }
    fclose(f);

    /*
     * And now the offsets become addresses, in the order EmuTOS does it. The
     * trees first, because everything else is reached through them.
     */
    n = header(RSH_NTREE);
    for (i = 0; i < n; i++)
        fix_long(resource + header(RSH_TRINDEX) + i*4);

    n = header(RSH_NTED);
    for (i = 0; i < n; i++)
    {
        uint32_t ted = item(R_TEDINFO, i);

        /* The text and the template also record how long they are, which the
         * editor does not write down */
        if (fix_long(ted + TE_PTEXT))
            m68k_write_memory_16(ted + TE_TXTLEN,
                (uint16_t)string_length(m68k_read_memory_32(ted + TE_PTEXT)));
        if (fix_long(ted + TE_PTMPLT))
            m68k_write_memory_16(ted + TE_TMPLEN,
                (uint16_t)string_length(m68k_read_memory_32(ted + TE_PTMPLT)));
        fix_long(ted + TE_PVALID);
    }

    n = header(RSH_NIB);
    for (i = 0; i < n; i++)
    {
        uint32_t icon = item(R_ICONBLK, i);

        fix_long(icon + IB_PMASK);
        fix_long(icon + IB_PDATA);
        fix_long(icon + IB_PTEXT);
    }

    n = header(RSH_NBB);
    for (i = 0; i < n; i++)
        fix_long(item(R_BITBLK, i) + BI_PDATA);

    n = header(RSH_NSTRING);
    for (i = 0; i < n; i++)
        fix_long(resource + header(RSH_FRSTR) + i*4);

    n = header(RSH_NIMAGES);
    for (i = 0; i < n; i++)
        fix_long(resource + header(RSH_FRIMG) + i*4);

    fix_objects();

    /*
     * Where the trees are, which the application reads out of the global array
     * rather than asking for. It is one of the few things in there the AES
     * writes after appl_init.
     */
    {
        uint32_t trees = resource + header(RSH_TRINDEX);

        m68k_write_memory_16(aes_global() + 5*2, (uint16_t)(trees >> 16));
        m68k_write_memory_16(aes_global() + 6*2, (uint16_t)(trees & 0xffff));
    }

    return AES_E_OK;
}

uint32_t AES_rsrc_free()
{
    FUNC_TRACE_ENTER;

    if (!resource)
        return 0;

    mem_free(resource);
    resource = 0;

    m68k_write_memory_16(aes_global() + 5*2, 0);
    m68k_write_memory_16(aes_global() + 6*2, 0);

    return AES_E_OK;
}

/* Asking for one thing out of it ******************************************/

uint32_t AES_rsrc_gaddr()
{
    int16_t type = aes_intin(0);
    int16_t index = aes_intin(1);
    uint32_t address;

    FUNC_TRACE_ENTER_ARGS {
        printf("    type: %d, index: %d\n", type, index);
    }

    if (!resource)
        return 0;

    if (index < 0 || (uint16_t)index >= count_of(type))
        return 0;

    address = item(type, (uint16_t)index);
    if (!address)
        return 0;

    aes_set_addrout(0, address);

    return AES_E_OK;
}

uint32_t AES_rsrc_saddr()
{
    int16_t type = aes_intin(0);
    int16_t index = aes_intin(1);
    uint32_t address = aes_addrin(0);

    FUNC_TRACE_ENTER_ARGS {
        printf("    type: %d, index: %d, to: 0x%x\n", type, index, address);
    }

    if (!resource)
        return 0;

    if (index < 0 || (uint16_t)index >= count_of(type))
        return 0;

    /*
     * Only the two that are a pointer somebody may want to replace. The rest
     * of a resource is the resource, and letting an application point one of
     * those somewhere else is letting it break the tree it is about to draw.
     */
    switch (type)
    {
        case R_STRING:
            m68k_write_memory_32(resource + header(RSH_FRSTR) + index*4,
                                 address);
            break;
        case R_IMAGEDATA:
            m68k_write_memory_32(resource + header(RSH_FRIMG) + index*4,
                                 address);
            break;
        default:
            return 0;
    }

    return AES_E_OK;
}

uint32_t AES_rsrc_obfix()
{
    uint32_t tree = aes_addrin(0);
    int16_t object = aes_intin(0);
    int16_t handle, wchar, hchar, wbox, hbox;
    int which;

    FUNC_TRACE_ENTER_ARGS {
        printf("    tree: 0x%x, object: %d\n", tree, object);
    }

    if (!tree || object < 0)
        return 0;

    emuvdi_graf_handle(&handle, &wchar, &hchar, &wbox, &hbox);

    for (which = 0; which < 4; which++)
        fix_coordinate(tree + object*OB_SIZE + OB_X + which*2, which,
                       wchar, hchar, emuvdi_screen_width());

    return AES_E_OK;
}
