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
#include "gemgsxif.h"
#include "gemobed.h"
#include "gem_rsc.h"
#include "aeskernel.h"
#include "tosvars.h"

/* tosemu's own, for which drives there are */
#include "../drives.h"
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

/* And the one tree in it that needs more than the general fixing up,
 * aes/gemfslib.c */
void fs_start(void);

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

    /*
     * The file selector on top of that, which is laid out once and here
     * because it cannot be laid out twice.
     *
     * Its tree is the most worked over in the resource - the closer, the
     * arrows, the elevator and all twenty-six drive letters are nudged into
     * place for the character size the screen turned out to have - and every
     * one of those adjustments is a step from where the object already is
     * rather than a position it is put at. So it is a thing done to a tree
     * fresh out of the resource and to no other. gem_rsc_init above copies one
     * out every time it is called, which is what makes here the right place:
     * done again on a tree that has already had it, the dialog grows by a
     * little each time and its drive letters walk off the bottom of it.
     */
    fs_start();

    /*
     * Somewhere to keep what is underneath a menu or an alert while one is up.
     *
     * The AES draws a menu straight over whatever was there and puts the
     * pixels back afterwards, so it needs a buffer to put them in, and it
     * works out how large from the character size - which is why this comes
     * after the workstation is open rather than with the rest of the setup.
     *
     * Without it the AES does not fail: bb_set finds a buffer of no length,
     * decides it can save a rectangle nought pixels high, and carries on. Its
     * own comment for that case says "this will leave droppings", and it is
     * right - what is left on the screen is the menu that was supposed to have
     * been taken away.
     */
    gsx_malloc();

    /* Which drives the file selector will offer, taken from the ones tosemu
     * is presenting rather than assumed */
    drvbits = (LONG)drive_map();
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

void emuvdi_tree_get(void *tree, int index, int16_t *next, int16_t *head,
                     int16_t *tail, int16_t *x, int16_t *y,
                     int16_t *w, int16_t *h)
{
    const OBJECT *o = (const OBJECT *)tree + index;

    if (next)
        *next = o->ob_next;
    if (head)
        *head = o->ob_head;
    if (tail)
        *tail = o->ob_tail;
    if (x)
        *x = o->ob_x;
    if (y)
        *y = o->ob_y;
    if (w)
        *w = o->ob_width;
    if (h)
        *h = o->ob_height;
}

/*
 * A USERBLK, likewise, and for the same reason: ub_code is a function pointer
 * and ub_parm a LONG, so it is eight bytes in the machine and sixteen here.
 *
 * What goes in ub_code is a 68000 address rather than anything this program
 * can jump to. That is a lie about the type and it is the honest choice all
 * the same: the field is where the machine keeps the routine, ob_user finds it
 * by walking exactly the same path the real AES walked, and the one place it
 * is read - call_usercode in gemoblib.c, which is a copy edited for this - is
 * the place that knows it has to hand the address to the emulator rather than
 * call it.
 */
void *emuvdi_userblk_alloc()
{
    USERBLK *ub = host_vdi_alloc(sizeof(USERBLK));

    if (ub)
        memset(ub, 0, sizeof(USERBLK));

    return ub;
}

void emuvdi_userblk_set(void *block, uint32_t code, int32_t parm)
{
    USERBLK *ub = block;

    ub->ub_code = (WORD (*)(PARMBLK *))(uintptr_t)code;
    ub->ub_parm = parm;
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

void *emuvdi_bitblk_alloc()
{
    BITBLK *bi = host_vdi_alloc(sizeof(BITBLK));

    if (bi)
        memset(bi, 0, sizeof(BITBLK));

    return bi;
}

void emuvdi_bitblk_set(void *blk, void *data, const int16_t *words, int count)
{
    BITBLK *bi = blk;
    WORD *fields = &bi->bi_wb;
    int i;

    bi->bi_pdata = data;

    /* The five words after the pointer, in the order they are declared, which
     * is the order they arrive in from the machine */
    for (i = 0; i < count && i < 5; i++)
        fields[i] = words[i];
}

void *emuvdi_iconblk_alloc()
{
    /* The larger of the two, so that the same block serves a G_ICON and a
     * G_CICON: the colour list is what a CICONBLK adds, and it stays empty
     * until there is something to put in it */
    CICONBLK *cib = host_vdi_alloc(sizeof(CICONBLK));

    if (cib)
        memset(cib, 0, sizeof(CICONBLK));

    return cib;
}

void emuvdi_iconblk_set(void *blk, void *mask, void *data, char *text,
                        const int16_t *words, int count)
{
    ICONBLK *ib = blk;      /* which a CICONBLK begins with */
    WORD *fields = &ib->ib_char;
    int i;

    ib->ib_pmask = mask;
    ib->ib_pdata = data;
    ib->ib_ptext = text;

    /* And the eleven words after the three pointers, likewise */
    for (i = 0; i < count && i < 11; i++)
        fields[i] = words[i];
}

/* EmuTOS's alert library, aes/gemfmalt.c */
WORD fm_alert(WORD defbut, char *palstr);

int16_t emuvdi_form_alert(int16_t default_button, char *text)
{
    return fm_alert(default_button, text);
}

/* EmuTOS's menu library, aes/gemmnlib.c */
void mn_bar(OBJECT *tree, WORD showit);
WORD mn_do(WORD *ptitle, WORD *pitem);
void mn_init(void);

void emuvdi_menu_bar(void *tree, int16_t showit)
{
    static int started;

    if (!started)
    {
        mn_init();
        started = 1;
    }

    mn_bar(tree, showit);
}

int16_t emuvdi_menu_do(int16_t *title, int16_t *item)
{
    WORD t = 0, i = 0;
    WORD chosen;

    chosen = mn_do(&t, &i);

    *title = t;
    *item = i;

    return chosen;
}

extern MOBLK gl_ctwait;

/* EmuTOS's menu library, aes/gemmnlib.c (our copy of it) */
BOOL do_chg(OBJECT *tree, WORD iitem, UWORD chgvalue,
            WORD dochg, WORD dodraw, WORD chkdisabled);

/*
 * Ticking an entry, greying one out, and putting a title back to normal.
 *
 * All three are the same thing - a bit of an object's state, set or cleared,
 * with or without redrawing it - which is why they are one function here as
 * they are one line each in the AES.
 */
int16_t emuvdi_menu_change(void *tree, int16_t object, uint16_t bit,
                           int16_t set, int16_t draw, int16_t only_if_enabled)
{
    return do_chg(tree, object, bit, set, draw, only_if_enabled);
}

int16_t emuvdi_menu_height()
{
    return gl_hbox;
}

/*
 * The part of the bar the titles are in, which mn_bar works out and puts away
 * for whoever is watching the mouse. On real GEM that is the control manager,
 * which waits for the pointer to arrive in this rectangle and runs the menu
 * when it does.
 */
void emuvdi_menu_active(int16_t *x, int16_t *y, int16_t *w, int16_t *h)
{
    *x = gl_ctwait.m_gr.g_x;
    *y = gl_ctwait.m_gr.g_y;
    *w = gl_ctwait.m_gr.g_w;
    *h = gl_ctwait.m_gr.g_h;
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


/* EmuTOS's graphics library, aes/gemgrlib.c *******************************/

void gr_rubbox(WORD xorigin, WORD yorigin, WORD wmin, WORD hmin,
               WORD *pwend, WORD *phend);
void gr_dragbox(WORD w, WORD h, WORD sx, WORD sy, GRECT *pc,
                WORD *pdx, WORD *pdy);
void gr_movebox(WORD w, WORD h, WORD srcx, WORD srcy, WORD dstx, WORD dsty);
void gr_growbox(GRECT *po, GRECT *pt);
void gr_shrinkbox(GRECT *po, GRECT *pt);
WORD gr_watchbox(OBJECT *tree, WORD obj, WORD instate, WORD outstate);
WORD gr_slidebox(OBJECT *tree, WORD parent, WORD obj, WORD isvert);

/*
 * The boxes an application drags about.
 *
 * All of them follow the mouse and draw an outline that is exclusive-ored on
 * and off again, which is how a drag was shown on a machine that could not
 * afford to redraw anything. The drawing goes into the screen the AES keeps,
 * so it is visible wherever that screen is being shown - inside a window - and
 * not where it is not. A rubber band pulled out inside a window is the case
 * that matters and is the case that works.
 */
void emuvdi_graf_rubberbox(int16_t x, int16_t y, int16_t wmin, int16_t hmin,
                           int16_t *w, int16_t *h)
{
    WORD ww = 0, hh = 0;

    gr_rubbox(x, y, wmin, hmin, &ww, &hh);

    *w = ww;
    *h = hh;
}

void emuvdi_graf_dragbox(int16_t w, int16_t h, int16_t x, int16_t y,
                         int16_t bx, int16_t by, int16_t bw, int16_t bh,
                         int16_t *ex, int16_t *ey)
{
    GRECT bound;
    WORD dx = 0, dy = 0;

    bound.g_x = bx;
    bound.g_y = by;
    bound.g_w = bw;
    bound.g_h = bh;

    gr_dragbox(w, h, x, y, &bound, &dx, &dy);

    *ex = dx;
    *ey = dy;
}

void emuvdi_graf_movebox(int16_t w, int16_t h, int16_t sx, int16_t sy,
                         int16_t dx, int16_t dy)
{
    gr_movebox(w, h, sx, sy, dx, dy);
}

void emuvdi_graf_growbox(int16_t sx, int16_t sy, int16_t sw, int16_t sh,
                         int16_t fx, int16_t fy, int16_t fw, int16_t fh)
{
    GRECT from, to;

    from.g_x = sx; from.g_y = sy; from.g_w = sw; from.g_h = sh;
    to.g_x = fx; to.g_y = fy; to.g_w = fw; to.g_h = fh;

    gr_growbox(&from, &to);
}

void emuvdi_graf_shrinkbox(int16_t fx, int16_t fy, int16_t fw, int16_t fh,
                           int16_t sx, int16_t sy, int16_t sw, int16_t sh)
{
    GRECT from, to;

    from.g_x = fx; from.g_y = fy; from.g_w = fw; from.g_h = fh;
    to.g_x = sx; to.g_y = sy; to.g_w = sw; to.g_h = sh;

    gr_shrinkbox(&from, &to);
}

int16_t emuvdi_graf_watchbox(void *tree, int16_t obj,
                             int16_t instate, int16_t outstate)
{
    return gr_watchbox(tree, obj, instate, outstate);
}

int16_t emuvdi_graf_slidebox(void *tree, int16_t parent, int16_t obj,
                             int16_t vertical)
{
    return gr_slidebox(tree, parent, obj, vertical);
}


/* EmuTOS's object library, the rest of it *********************************/

void ob_offset(OBJECT *tree, WORD obj, WORD *px, WORD *py);
WORD ob_find(OBJECT *tree, WORD current, WORD depth, WORD mx, WORD my);
void ob_change(OBJECT *tree, WORD obj, UWORD state, WORD redraw);

/* The rectangle drawing is kept inside, for the calls that draw without
 * being objc_draw */
void emuvdi_set_clip(int16_t x, int16_t y, int16_t w, int16_t h)
{
    GRECT clip;

    clip.g_x = x;
    clip.g_y = y;
    clip.g_w = w;
    clip.g_h = h;

    gsx_sclip(&clip);
}

void emuvdi_objc_offset(void *tree, int16_t obj, int16_t *x, int16_t *y)
{
    WORD ox = 0, oy = 0;

    ob_offset(tree, obj, &ox, &oy);

    *x = ox;
    *y = oy;
}

int16_t emuvdi_objc_find(void *tree, int16_t start, int16_t depth,
                         int16_t x, int16_t y)
{
    return ob_find(tree, start, depth, x, y);
}

void emuvdi_objc_change(void *tree, int16_t obj, uint16_t state, int16_t draw)
{
    ob_change(tree, obj, state, draw);
}


/* EmuTOS's form library, the rest of it ***********************************/

WORD fm_keybd(OBJECT *tree, WORD obj, WORD *pchar, WORD *pnew_obj);
WORD fm_button(OBJECT *tree, WORD new_obj, WORD clks, WORD *pnew_obj);
WORD fm_error(WORD n);
WORD ob_edit(OBJECT *tree, WORD obj, WORD in_char, WORD *idx, WORD kind);

int16_t emuvdi_form_keybd(void *tree, int16_t obj, int16_t *key,
                          int16_t *next)
{
    WORD k = *key, n = *next;
    WORD carry;

    carry = fm_keybd(tree, obj, &k, &n);

    *key = k;
    *next = n;

    return carry;
}

int16_t emuvdi_form_button(void *tree, int16_t obj, int16_t clicks,
                           int16_t *next)
{
    WORD n = *next;
    WORD carry;

    carry = fm_button(tree, obj, clicks, &n);

    *next = n;

    return carry;
}

int16_t emuvdi_form_error(int16_t which)
{
    return fm_error(which);
}

int16_t emuvdi_objc_edit(void *tree, int16_t obj, int16_t key,
                         int16_t *index, int16_t what)
{
    WORD i = *index;
    WORD answer;

    answer = ob_edit(tree, obj, key, &i, what);

    *index = i;

    return answer;
}


/* EmuTOS's file selector, aes/gemfslib.c **********************************/

WORD fs_input(char *pipath, char *pisel, WORD *pbutton, char *pilabel);

/*
 * Putting the file selector up.
 *
 * The path and the name go in and come back changed, because that is how the
 * call answers: the application hands over the buffers it will read afterwards
 * and the AES fills them in. The button says whether anything was chosen at
 * all, which is a different question from whether the call worked.
 */
int16_t emuvdi_fsel_input(char *path, char *name, int16_t *button,
                          char *label)
{
    WORD chosen = 0;
    WORD answer;

    /*
     * And a window to put it in.
     *
     * The selector calls fm_dial itself rather than going out through the trap
     * and back, so the promotion an application's own dialogs get from
     * form_dial does not happen to this one - it would draw into the screen
     * the AES keeps and be seen by nobody. Its rectangle is worked out the
     * same way it works its own out, which is the tree centred on the screen.
     */
    {
        OBJECT *tree = rs_trees[FSELECTR];
        GRECT where;

        ob_center(tree, &where);

        host_dialog_begin(where.g_x, where.g_y, where.g_w, where.g_h);
    }

    answer = fs_input(path, name, &chosen, label);

    host_dialog_end();

    *button = chosen;

    return answer;
}


/* Accessories in the Desk menu, aes/gemmnlib.c ****************************/

WORD mn_register(WORD pid, char *pstr);
void mn_init(void);
extern WORD gl_dafirst;
extern OBJECT *gl_mntree;

/*
 * Telling the AES who the accessories are, so that it splices them into the
 * Desk menu when a bar goes up.
 *
 * It keeps the pointer rather than the words - that is what Atari's did and
 * what EmuTOS copied - so the name has to stay where it was put for as long as
 * the bar is up. Whoever calls this owns that.
 */
int16_t emuvdi_menu_register(char *name)
{
    return mn_register(0, name);
}

void emuvdi_menu_forget_accessories(void)
{
    mn_init();
}

/*
 * Says the menu tree has gone.
 *
 * The AES keeps a pointer to whichever tree is the menu bar, and the tree it
 * is given here is a copy that is let go of as soon as it has been drawn. Left
 * alone that pointer outlives what it points at, and the next thing to look at
 * the bar - registering an accessory does, because it splices the name in -
 * walks memory that has been handed back.
 */
void emuvdi_menu_forget_tree(void)
{
    gl_mntree = NULL;
}

/* The first Desk menu entry that is an accessory rather than the application's
 * own. Everything at or after it was spliced in. */
int16_t emuvdi_menu_first_accessory(void)
{
    return gl_dafirst;
}
