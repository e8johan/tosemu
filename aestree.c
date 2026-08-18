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
 * Bringing an object tree across.
 *
 * A dialog is a tree of OBJECTs, and an application hands the AES the address
 * of its root. The tree is in the machine's memory: its words are the other
 * way round, and the pointers in it are 68000 addresses, so none of it can be
 * read by the AES as it stands.
 *
 * So it is copied. That is more work than it was for a bitmap, because a tree
 * is not one block of anything: an object's ob_spec is a string for some kinds
 * and a structure for others and a packed set of colours for the rest, and
 * which of the three it is depends on the object's type. Copying it means
 * knowing what each kind means.
 *
 * What comes back matters as much as what goes in. The AES writes to a tree -
 * a button that was pressed comes back SELECTED, an edited field comes back
 * with different text - so the parts an application can expect to have changed
 * are written back into its own memory afterwards. The parts it cannot are
 * not, which is what keeps a copy from quietly becoming the original.
 */

#include "aes_p.h"

#include <string.h>

#include "emuvdi/emuvdi.h"
#include "tossystem.h"
#include "m68k.h"

/* An OBJECT in the machine, and where each field is inside one */
#define OB_SIZE     (24)
#define OB_NEXT      (0)
#define OB_HEAD      (2)
#define OB_TAIL      (4)
#define OB_TYPE      (6)
#define OB_FLAGS     (8)
#define OB_STATE    (10)
#define OB_SPEC     (12)
#define OB_X        (16)
#define OB_Y        (18)
#define OB_WIDTH    (20)
#define OB_HEIGHT   (22)

/* A TEDINFO, which is what the text kinds of object point at */
#define TE_SIZE     (28)
#define TE_PTEXT     (0)
#define TE_PTMPLT    (4)
#define TE_PVALID    (8)
#define TE_FONT     (12)
#define TE_JUST     (16)
#define TE_COLOR    (18)
#define TE_THICK    (22)
#define TE_TXTLEN   (24)
#define TE_TMPLEN   (26)

/* The object types, obdefs.h */
#define G_BOX       (20)
#define G_TEXT      (21)
#define G_BOXTEXT   (22)
#define G_IMAGE     (23)
#define G_USERDEF   (24)
#define G_IBOX      (25)
#define G_BUTTON    (26)
#define G_BOXCHAR   (27)
#define G_STRING    (28)
#define G_FTEXT     (29)
#define G_FBOXTEXT  (30)
#define G_ICON      (31)
#define G_TITLE     (32)
#define G_CICON     (33)

#define LASTOB      (0x0020)

/* No tree in a resource file is anywhere near this large, and a tree that
 * claims to be has a loop in it rather than that many objects */
#define MAX_OBJECTS (512)

/*
 * How many objects to leave after the end of a tree - enough for the Desk
 * menu's separator and one entry for every accessory that can register.
 */
#define TREE_HEADROOM (16)

/* The longest string worth bringing across. GEM strings are short by nature -
 * they have to fit in a dialog - and one that is not is a pointer into
 * something that is not a string. */
#define MAX_STRING  (256)

static struct {
    uint32_t tos;           /* Where the tree is in the machine */
    void *host;             /* And where the copy is */
    int count;              /* How many objects it has */

    /* Everything allocated for it, so that it can all be let go at once */
    void *blocks[MAX_OBJECTS * 2];
    int block_count;
} tree;

/* Said once rather than for every object in every tree that has one */
static void said_userdef(void)
{
    static int said;

    if (said)
        return;

    said = 1;

    printf("AES: this tree has objects the application draws itself, which is "
           "not implemented - they are drawn as empty boxes\n");
}

static void *tree_alloc(long size)
{
    void *block;

    if (tree.block_count >= (int)(sizeof tree.blocks / sizeof tree.blocks[0]))
        return 0;

    /* Below the four gigabyte line, because the address goes into an ob_spec,
     * which is a LONG - see emuvdi/README */
    block = host_vdi_alloc(size);
    if (!block)
        return 0;

    tree.blocks[tree.block_count++] = block;

    return block;
}

/* Copies a string out of the machine, stopping at the end of it or at the
 * point where it stops being believable */
static char *string_in(uint32_t address)
{
    char text[MAX_STRING];
    char *copy;
    int i;

    if (address == 0)
        return 0;

    for (i = 0; i < MAX_STRING - 1; i++)
    {
        text[i] = (char)m68k_read_memory_8(address + i);
        if (text[i] == 0)
            break;
    }
    text[i] = 0;

    copy = tree_alloc(i + 1);
    if (!copy)
        return 0;

    memcpy(copy, text, i + 1);

    return copy;
}

/*
 * A TEDINFO and the three strings hanging off it.
 *
 * te_ptext is the one that matters both ways: it is what an editable field
 * holds, so the AES writes to it and the application reads it back. The other
 * two are the template and the validation, which the application owns and the
 * AES only reads.
 */
static void *tedinfo_in(uint32_t address, char **text_out,
                        uint32_t *tos_text_out, int16_t *text_len)
{
    void *ted;
    int16_t words[8];
    int i;

    if (address == 0)
        return 0;

    ted = emuvdi_tedinfo_alloc();
    if (!ted)
        return 0;

    tree.blocks[tree.block_count++] = ted;

    *tos_text_out = m68k_read_memory_32(address + TE_PTEXT);
    *text_out = string_in(*tos_text_out);
    *text_len = (int16_t)m68k_read_memory_16(address + TE_TXTLEN);

    /* The eight words after the three pointers, in the order they are
     * declared there and here */
    for (i = 0; i < 8; i++)
        words[i] = (int16_t)m68k_read_memory_16(address + TE_FONT + i*2);

    emuvdi_tedinfo_set(ted, *text_out,
                       string_in(m68k_read_memory_32(address + TE_PTMPLT)),
                       string_in(m68k_read_memory_32(address + TE_PVALID)),
                       words, 8);

    return ted;
}

/* Where each object's editable text came from and went, so that it can be put
 * back where the application will look for it */
static struct {
    uint32_t tos_text;      /* In the machine */
    char *host_text;        /* And ours */
    int16_t length;
} texts[MAX_OBJECTS];

/*
 * Brings the tree at the given address across, and answers with somewhere the
 * AES can read it, or null after saying why not.
 */
void *aes_tree_in(uint32_t address)
{
    void *objects;
    int i;

    aes_tree_done();

    tree.tos = address;

    if (address == 0)
        return 0;

    /*
     * How many objects there are is not written down anywhere: a tree ends at
     * the object whose flags say it is the last one. Counting first means the
     * copy can be one block, which is what lets the AES walk it by index the
     * way it expects to.
     */
    for (i = 0; i < MAX_OBJECTS; i++)
    {
        uint16_t flags = (uint16_t)m68k_read_memory_16(address + i*OB_SIZE
                                                       + OB_FLAGS);
        if (flags & LASTOB)
            break;
    }

    if (i >= MAX_OBJECTS)
    {
        halt_execution();
        printf("AES: an object tree with no last object in %d of them, which "
               "is a loop rather than a tree\n", MAX_OBJECTS);
        return 0;
    }

    tree.count = i + 1;

    /*
     * Room after the end of it.
     *
     * A menu tree is not only read: the AES splices entries into the Desk menu
     * for the accessories that have registered, writing objects after the ones
     * the application supplied. A tree from a resource file has blank entries
     * waiting for them; one built by hand may not, and running off the end of
     * the copy would be this emulator's memory rather than the application's.
     *
     * The spare objects are not copied back, because the application does not
     * know about them.
     */
    objects = emuvdi_tree_alloc(tree.count + TREE_HEADROOM);
    if (!objects)
    {
        halt_execution();
        printf("AES: no room to copy a tree of %d objects\n", tree.count);
        return 0;
    }
    tree.blocks[tree.block_count++] = objects;

    memset(texts, 0, sizeof texts);

    for (i = 0; i < tree.count; i++)
    {
        uint32_t from = address + i*OB_SIZE;
        uint16_t type = (uint16_t)m68k_read_memory_16(from + OB_TYPE);
        uint32_t spec = m68k_read_memory_32(from + OB_SPEC);
        void *host_spec;

        /*
         * ob_spec is a different thing for every kind of object. Getting this
         * wrong does not fail: a colour word read as an address is a wild
         * pointer that usually still points at something.
         */
        switch (type & 0xff)
        {
            case G_BOX:
            case G_IBOX:
            case G_BOXCHAR:
                /* Not a pointer at all - the colours and the border width,
                 * packed into the long */
                host_spec = (void *)(uintptr_t)spec;
                break;

            case G_STRING:
            case G_BUTTON:
            case G_TITLE:
                host_spec = string_in(spec);
                break;

            case G_TEXT:
            case G_BOXTEXT:
            case G_FTEXT:
            case G_FBOXTEXT:
                host_spec = tedinfo_in(spec, &texts[i].host_text,
                                       &texts[i].tos_text, &texts[i].length);
                break;

            case G_USERDEF:
                /*
                 * An object the application draws itself.
                 *
                 * Its ob_spec points at a routine in the machine's memory, and
                 * calling it means more than reaching it: what it is handed is
                 * a block describing what to draw, and in that block is the
                 * address of the tree - the application's own tree, not this
                 * copy of it, because that is the only one it can read. None
                 * of that is written yet.
                 *
                 * So it is drawn as an empty box instead. A box is visible and
                 * the right size, which is what makes the dialog round it
                 * usable and says plainly that something is missing; leaving
                 * it as an object the AES will try to call ends in reaching
                 * through a pointer to nothing.
                 */
                said_userdef();
                type = (type & 0xff00) | G_BOX;
                host_spec = (void *)(uintptr_t)0x00011100L;
                break;

            default:
                /*
                 * Icons and images, which point at structures that point at
                 * bitmaps. Neither is brought across yet, and one that is not
                 * is left pointing at nothing rather than at the wrong thing:
                 * a 68000 address means something else entirely here.
                 */
                host_spec = 0;
                break;
        }

        emuvdi_tree_set(objects, i,
                        (int16_t)m68k_read_memory_16(from + OB_NEXT),
                        (int16_t)m68k_read_memory_16(from + OB_HEAD),
                        (int16_t)m68k_read_memory_16(from + OB_TAIL),
                        type,
                        (uint16_t)m68k_read_memory_16(from + OB_FLAGS),
                        (uint16_t)m68k_read_memory_16(from + OB_STATE),
                        host_spec,
                        (int16_t)m68k_read_memory_16(from + OB_X),
                        (int16_t)m68k_read_memory_16(from + OB_Y),
                        (int16_t)m68k_read_memory_16(from + OB_WIDTH),
                        (int16_t)m68k_read_memory_16(from + OB_HEIGHT));
    }

    tree.host = objects;

    return objects;
}

/*
 * Puts back what the AES may have changed.
 *
 * Only two things: the state of an object, which is how a pressed button and a
 * checked menu entry are reported, and the text of an editable field. An
 * application does not expect anything else about its tree to have moved, and
 * writing back more than changed would overwrite whatever it did to the tree
 * while the AES had a copy.
 */
void aes_tree_out()
{
    void *objects = tree.host;
    int i;

    if (!objects)
        return;

    for (i = 0; i < tree.count; i++)
    {
        uint32_t to = tree.tos + i*OB_SIZE;

        m68k_write_memory_16(to + OB_STATE, emuvdi_tree_state(objects, i));

        if (texts[i].host_text && texts[i].tos_text)
        {
            const char *text = texts[i].host_text;
            int n;

            for (n = 0; n < MAX_STRING; n++)
            {
                m68k_write_memory_8(texts[i].tos_text + n,
                                    (uint8_t)text[n]);
                if (text[n] == 0)
                    break;
            }
        }
    }
}

void aes_tree_done()
{
    int i;

    for (i = 0; i < tree.block_count; i++)
        host_vdi_free(tree.blocks[i]);

    memset(&tree, 0, sizeof tree);
}
