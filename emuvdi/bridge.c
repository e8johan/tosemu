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
 * Between tosemu and the ported VDI.
 *
 * This is the only file that is on both sides. It is built with the EmuTOS
 * flags and headers like the rest of emuvdi, and says nothing about them in
 * emuvdi.h, so tosemu can call the VDI without a WORD or a Vwk ever reaching
 * it.
 *
 * There is very little to do, because EmuTOS's own dispatcher does it. screen()
 * in vdi_main.c reads the control array, finds the workstation the handle
 * names, fills in how many answers there are and calls the function. All that
 * is left is to say where the arrays are.
 */

#include "emutos.h"
#include "asm.h"
#include "intmath.h"
#include "tosvars.h"
#include "vdi_defs.h"
#include "lineavars.h"

#include "obdefs.h"
#include "aesdefs.h"
#include "aesext.h"
#include "struct.h"
#include "gemlib.h"
#include "gsxdefs.h"

#include <string.h>

#include "emuvdi.h"

/* EmuTOS's dispatcher, vdi_main.c */
void screen(void);

/* The AES's VDI interface, aes/gemgsxif.c */
void gsx_init(void);

/* Ours, in fonts.c and hostvars.c */
void host_font_init(void);
void host_surface_select(void *base, UWORD width, UWORD height, UWORD planes);

void emuvdi_init()
{
    host_font_init();
}

void emuvdi_surface_select(void *base, uint16_t width, uint16_t height,
                           uint16_t planes)
{
    host_surface_select(base, width, height, planes);
}

/* The AES's own resource, aes/gem_rsc.c */
void gem_rsc_init(void);
void gem_rsc_fixit(void);

void emuvdi_aes_init()
{
    gsx_init();

    /*
     * The AES's own dialogs - its alerts, its file selector, the boxes it puts
     * up when something goes wrong. They are compiled in rather than loaded,
     * but they still have to be copied into memory that can be written and
     * have their coordinates worked out for the character size the screen
     * turned out to have, which is what these two do. Without them form_alert
     * has a tree of null pointers to parse into.
     */
    gem_rsc_init();
    gem_rsc_fixit();
}

void emuvdi_graf_handle(int16_t *handle, int16_t *wchar, int16_t *hchar,
                        int16_t *wbox, int16_t *hbox)
{
    *handle = gl_handle;
    *wchar = gl_wchar;
    *hchar = gl_hchar;
    *wbox = gl_wbox;
    *hbox = gl_hbox;
}

/* The two bitmaps a call can name. They are static, so they are part of the
 * program's own data and therefore below the four gigabyte line, which is
 * where an address has to be to fit in the control array. */
static FDB mfdb[2];

void *emuvdi_mfdb(int slot)
{
    return &mfdb[slot & 1];
}

void emuvdi_mfdb_set(void *m, void *data, int16_t width, int16_t height,
                     int16_t wdwidth, int16_t standard, int16_t planes)
{
    /* FDB in gsxdefs.h is the same structure vdi_raster.c calls an MFDB. It
     * is declared in a header, which this one is not, so it is the one to
     * use. */
    FDB *f = m;

    f->fd_addr = data;
    f->fd_w = width;
    f->fd_h = height;
    f->fd_wdwidth = wdwidth;
    f->fd_stand = standard;
    f->fd_nplanes = planes;
    f->fd_r1 = f->fd_r2 = f->fd_r3 = 0;
}

void emuvdi_control_set_pointer(int16_t *control, int index, void *p)
{
    /*
     * Two words holding an address, thirty two bits of it, which is what the
     * VDI reads back and what the AES writes through ULONG_AT. Eight bytes
     * would not fit: the two bitmaps a call names are only two words apart, so
     * a wider store would run into the next one.
     */
    *(uint32_t *)&control[index] = (uint32_t)(uintptr_t)p;
}

/* EmuTOS's object library, aes/gemoblib.c */
int16_t emuvdi_screen_width()
{
    return V_REZ_HZ;
}

int16_t emuvdi_screen_height()
{
    return V_REZ_VT;
}

/*
 * Object trees, built here because this is the side that knows what an OBJECT
 * looks like. It is 24 bytes in the machine and 32 in this program, for want
 * of a LONG being the same width in both.
 */
void *emuvdi_tree_alloc(int count)
{
    OBJECT *tree = host_vdi_alloc((long)count * sizeof(OBJECT));

    if (tree)
        memset(tree, 0, (size_t)count * sizeof(OBJECT));

    return tree;
}

void emuvdi_tree_free(void *tree)
{
    host_vdi_free(tree);
}

void emuvdi_tree_set(void *tree, int index, int16_t next, int16_t head,
                     int16_t tail, uint16_t type, uint16_t flags,
                     uint16_t state, void *spec, int16_t x, int16_t y,
                     int16_t w, int16_t h)
{
    OBJECT *o = (OBJECT *)tree + index;

    o->ob_next = next;
    o->ob_head = head;
    o->ob_tail = tail;
    o->ob_type = type;
    o->ob_flags = flags;
    o->ob_state = state;
    o->ob_spec = (LONG)(uintptr_t)spec;
    o->ob_x = x;
    o->ob_y = y;
    o->ob_width = w;
    o->ob_height = h;
}

uint16_t emuvdi_tree_state(void *tree, int index)
{
    return ((OBJECT *)tree + index)->ob_state;
}

void *emuvdi_tedinfo_alloc()
{
    TEDINFO *ted = host_vdi_alloc(sizeof(TEDINFO));

    if (ted)
        memset(ted, 0, sizeof(TEDINFO));

    return ted;
}

void emuvdi_tedinfo_set(void *t, char *text, char *tmplt, char *valid,
                        const int16_t *words, int word_count)
{
    TEDINFO *ted = t;
    WORD *fields = &ted->te_font;
    int i;

    ted->te_ptext = text;
    ted->te_ptmplt = tmplt;
    ted->te_pvalid = valid;

    /* The eight words after the three pointers, in the order they are
     * declared, which is the order they arrive in from the machine */
    for (i = 0; i < word_count && i < 8; i++)
        fields[i] = words[i];
}

char *emuvdi_tedinfo_text(void *t)
{
    return ((TEDINFO *)t)->te_ptext;
}

/* EmuTOS's alert library, aes/gemfmalt.c */
WORD fm_alert(WORD defbut, char *palstr);

int16_t emuvdi_form_alert(int16_t default_button, char *text)
{
    return fm_alert(default_button, text);
}

/* EmuTOS's form library, aes/gemfmlib.c */
WORD fm_do(OBJECT *tree, WORD start);

int16_t emuvdi_form_do(void *tree, int16_t start)
{
    return fm_do(tree, start);
}

/* EmuTOS's object library, aes/gemoblib.c */
void ob_draw(OBJECT *tree, WORD obj, WORD depth);

void emuvdi_objc_draw(void *tree, int16_t start, int16_t depth,
                      int16_t x, int16_t y, int16_t w, int16_t h)
{
    GRECT clip;

    clip.g_x = x;
    clip.g_y = y;
    clip.g_w = w;
    clip.g_h = h;

    /*
     * The clipping rectangle is the AES's own rather than an argument to the
     * drawing: gsx_sclip sets it on the workstation the AES draws through, and
     * everything after it obeys until it is set again.
     */
    gsx_sclip(&clip);

    ob_draw(tree, start, depth);
}

void emuvdi_call(int16_t *control, int16_t *intin, int16_t *ptsin,
                 int16_t *intout, int16_t *ptsout)
{
    CONTRL = control;
    INTIN = intin;
    PTSIN = ptsin;
    INTOUT = intout;
    PTSOUT = ptsout;

    screen();
}

/*
 * The two ranges EmuTOS's jump tables cover, 1 to 39 and 100 to 134. screen()
 * simply returns for anything else, which would leave a caller waiting for an
 * answer that never comes and no sign of why, so tosemu asks first.
 */
int emuvdi_implements(int16_t opcode)
{
    if (opcode >= 1 && opcode <= 39)
        return 1;

    if (opcode >= 100 && opcode <= 134)
        return 1;

    return 0;
}
