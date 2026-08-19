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
 *
 * One kind of object cannot be copied at all, and is the reason the end of
 * this file runs the emulated CPU. A G_USERDEF object is drawn by a routine
 * belonging to the application, and that routine reads the application's own
 * tree rather than this copy - so the address of the original, which is the
 * one thing here nothing else keeps, is what the call needs.
 */

#include "aes_p.h"

#include <string.h>

#include "emuvdi/emuvdi.h"
#include "tossystem.h"
#include "cpu.h"
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

/*
 * An APPLBLK, which is what an object the application draws itself points at.
 * EmuTOS calls the same eight bytes a USERBLK; a resource editor calls the
 * object a G_PROGDEF and the AES calls it a G_USERDEF. All four names are for
 * two things: a routine, and a long the routine is handed back.
 */
#define AB_SIZE      (8)
#define AB_CODE      (0)
#define AB_PARM      (4)

/*
 * A PARMBLK, which is what the routine is handed. This is the machine's
 * layout - thirty bytes, an address and thirteen words - and it is written
 * rather than read: the AES builds one for every call.
 */
#define PB_SIZE      (30)
#define PB_TREE       (0)
#define PB_OBJ        (4)
#define PB_PREVSTATE  (6)
#define PB_CURRSTATE  (8)
#define PB_X         (10)
#define PB_Y         (12)
#define PB_W         (14)
#define PB_H         (16)
#define PB_XC        (18)
#define PB_YC        (20)
#define PB_WC        (22)
#define PB_HC        (24)
#define PB_PARM      (26)

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
 * An APPLBLK, which is what an object the application draws itself points at.
 *
 * Neither of the two longs in it means anything on this side: the first is a
 * routine in the machine's memory, and the second is whatever the application
 * wanted its routine to be told, which is usually an address in the machine as
 * well. WTEST puts the string the object used to hold there, and draws it with
 * v_gtext.
 *
 * So both are carried across as they are. The AES walks this block the way the
 * real one walked the application's, and the one place the routine is reached
 * from - host_userdef_draw below - knows that what it has is an address to
 * hand the emulator rather than one to call.
 */
static void *userblk_in(uint32_t address)
{
    void *block;

    if (address == 0)
        return 0;

    if (tree.block_count >= (int)(sizeof tree.blocks / sizeof tree.blocks[0]))
        return 0;

    block = emuvdi_userblk_alloc();
    if (!block)
        return 0;

    tree.blocks[tree.block_count++] = block;

    emuvdi_userblk_set(block,
                       m68k_read_memory_32(address + AB_CODE),
                       (int32_t)m68k_read_memory_32(address + AB_PARM));

    return block;
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
                host_spec = userblk_in(spec);
                if (!host_spec)
                {
                    /*
                     * Nothing to call, so it is drawn as an empty box. A box
                     * is visible and the right size, which is what keeps the
                     * dialog round it usable and says plainly that something
                     * is missing; leaving it as an object the AES will try to
                     * call ends in reaching through a pointer to nothing.
                     */
                    type = (type & 0xff00) | G_BOX;
                    host_spec = (void *)(uintptr_t)0x00011100L;
                }
                break;

            default:
                /*
                 * Icons and images, which point at structures that point at
                 * bitmaps. Neither is brought across yet, so neither can be
                 * left as what it is: the AES reads an ICONBLK or a BITBLK
                 * through the pointer before it draws anything, and a 68000
                 * address means something else entirely here.
                 *
                 * So it becomes an empty box, the same as a G_USERDEF with
                 * nothing to call - visible, the right size, and holding the
                 * shape of the dialog together while plainly saying that
                 * something is missing.
                 */
                type = (type & 0xff00) | G_BOX;
                host_spec = (void *)(uintptr_t)0x00011100L;
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

/* Calling back into the machine *******************************************/

/*
 * Drawing an object the application draws itself.
 *
 * Everywhere else in tosemu the machine calls the host: an application
 * executes a trap, the emulator stops, a function here runs and the emulator
 * starts again. This is the one place it goes the other way. The AES is
 * halfway through drawing a dialog, on the host, and the next thing to be
 * drawn is a routine belonging to the application - so the CPU has to be
 * started again and run to the end of that routine while this call is still on
 * the stack, and then stopped again where it was.
 *
 * The routine is called the way GEM called it, which is the way C calls
 * anything: the address of a PARMBLK pushed, the routine's own address in a0,
 * the answer in d0. What it is not given is a real return address, because
 * there is nowhere in the machine for it to come back to. It is given zero,
 * and the loop below stops when the program counter reaches it - so the rts at
 * the end of the routine is what ends the loop, and a routine that returns to
 * anywhere else runs until the count runs out.
 */

/* Where a routine returns to, which is not an address anything runs at */
#define USERDEF_RETURN (0)

/*
 * How long a routine may run for.
 *
 * It draws one object, and the drawing itself is VDI calls that cost the
 * machine one instruction each, so a real one is hundreds of instructions and
 * a slow one thousands. Something in the millions is not drawing, it is a
 * routine that lost its return address, and it has to be stopped rather than
 * left to run for ever inside a call the emulator cannot be interrupted from.
 */
#define USERDEF_STEPS (10000000L)

static int running;

int aes_userdef_running(void)
{
    return running;
}

/* An object is drawn many times over, so a reason for not drawing one is a
 * reason for not drawing it again and again. Said once each. */
static void said(const char *why)
{
    static const char *before[4];
    static int count;
    int i;

    for (i = 0; i < count; i++)
        if (before[i] == why)
            return;

    if (count < (int)(sizeof before / sizeof before[0]))
        before[count++] = why;

    printf("AES: an object the application draws itself, %s - it is left "
           "blank\n", why);
}

int16_t host_userdef_draw(const struct host_userdef *call)
{
    uint32_t d[8], a[8], pc, sr;
    uint32_t block;
    long steps;
    int16_t answer;
    int i;

    /* A block with nothing in it, which is the application's mistake rather
     * than a hole here - on an ST it would jump to whatever is at zero */
    if (call->code == 0)
    {
        said("with no routine in it");
        return call->currstate;
    }

    /*
     * The tree the routine is given has to be the application's own. It is
     * handed an index into it and expected to read the object at that index -
     * WTEST reads ob_flags to see whether the button is the default one - and
     * the only tree it can read is the one in its own memory.
     *
     * So a tree this side has no address for is one the routine cannot be
     * called about. That does not happen while an application is drawing its
     * own dialog; it would happen if the AES drew a tree of its own that had
     * one of these in it, which none of them do.
     */
    if (call->tree != tree.host || tree.tos == 0)
    {
        said("in a tree the application has no address for");
        return call->currstate;
    }

    /*
     * One at a time. The routine could draw another tree, and drawing it would
     * take the copy this one is being drawn out of away underneath it.
     */
    if (running)
    {
        said("from inside another one");
        return call->currstate;
    }

    for (i = 0; i < 8; i++)
        d[i] = m68k_get_reg(0, M68K_REG_D0 + i);
    for (i = 0; i < 8; i++)
        a[i] = m68k_get_reg(0, M68K_REG_A0 + i);
    pc = m68k_get_reg(0, M68K_REG_PC);
    sr = m68k_get_reg(0, M68K_REG_SR);

    /*
     * The block goes on the machine's stack, below whatever is there, which is
     * where the AES put it on an ST: ob_user declares a PARMBLK as a local.
     * Rounding the address down keeps it even, because a word written to an
     * odd address is an address error on a 68000.
     */
    block = (m68k_get_reg(0, M68K_REG_A7) - PB_SIZE) & ~1u;
    m68k_set_reg(M68K_REG_A7, block);

    m68k_write_memory_32(block + PB_TREE, tree.tos);
    m68k_write_memory_16(block + PB_OBJ, (uint16_t)call->obj);
    m68k_write_memory_16(block + PB_PREVSTATE, (uint16_t)call->prevstate);
    m68k_write_memory_16(block + PB_CURRSTATE, (uint16_t)call->currstate);
    m68k_write_memory_16(block + PB_X, (uint16_t)call->x);
    m68k_write_memory_16(block + PB_Y, (uint16_t)call->y);
    m68k_write_memory_16(block + PB_W, (uint16_t)call->w);
    m68k_write_memory_16(block + PB_H, (uint16_t)call->h);
    m68k_write_memory_16(block + PB_XC, (uint16_t)call->xc);
    m68k_write_memory_16(block + PB_YC, (uint16_t)call->yc);
    m68k_write_memory_16(block + PB_WC, (uint16_t)call->wc);
    m68k_write_memory_16(block + PB_HC, (uint16_t)call->hc);
    m68k_write_memory_32(block + PB_PARM, (uint32_t)call->parm);

    push_u32(block);
    push_u32(USERDEF_RETURN);

    /* a0 held the routine when GEM jumped to it, and a routine written in
     * assembly is entitled to have read it there rather than off the stack */
    m68k_set_reg(M68K_REG_A0, call->code);
    m68k_set_reg(M68K_REG_PC, call->code);

#ifdef ENABLE_AES_TRACE
    printf("AES userdef 0x%x: object %d, %04x to %04x, %d,%d %dx%d\n",
           call->code, call->obj, (unsigned)call->prevstate,
           (unsigned)call->currstate, call->x, call->y, call->w, call->h);
#endif

    running = 1;

    for (steps = 0; steps < USERDEF_STEPS; steps++)
    {
        if (m68k_get_reg(0, M68K_REG_PC) == USERDEF_RETURN)
            break;

        /*
         * Anything the routine does that stops the machine - an unimplemented
         * call, a bad address - stops this as well. Carrying on would run the
         * rest of the routine after the emulator had already given up on it.
         */
        if (execution_halted())
            break;

        m68k_execute(1);
    }

    running = 0;

    answer = (int16_t)m68k_get_reg(0, M68K_REG_D0);

    if (steps >= USERDEF_STEPS)
    {
        halt_execution();
        printf("AES: the routine at 0x%x that draws object %d ran for %ld "
               "instructions without returning\n",
               call->code, call->obj, USERDEF_STEPS);
        answer = call->currstate;
    }

    /*
     * Back to where the AES was. Restoring a7 is what takes the block and the
     * two longs pushed on top of it away again, and restoring the program
     * counter is what puts the machine back at the trap it is still inside.
     */
    for (i = 0; i < 8; i++)
        m68k_set_reg(M68K_REG_D0 + i, d[i]);
    for (i = 0; i < 8; i++)
        m68k_set_reg(M68K_REG_A0 + i, a[i]);
    m68k_set_reg(M68K_REG_PC, pc);
    m68k_set_reg(M68K_REG_SR, sr);

    return answer;
}
