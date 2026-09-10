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

#ifndef EMUVDI_H
#define EMUVDI_H

#include <stdint.h>

/*
 * What the rest of tosemu is allowed to know about the ported VDI.
 *
 * Everything on the other side of this header is built with its own compiler
 * flags against EmuTOS's headers, where a WORD is a type of its own and the
 * VDI's state is a hundred globals. None of that belongs in tosemu proper, so
 * this is the whole of the surface between them.
 */

/* Readies the VDI: the system fonts and the tables an open workstation
 * reports. Call once, before anything else here. */
void emuvdi_init();

/*
 * Points the VDI at a surface, which is where everything drawn from now on
 * goes. The bitmap is Atari planar - one word of each plane in turn, the
 * highest bit of a word the leftmost pixel - in host byte order.
 */
void emuvdi_surface_select(void *base, uint16_t width, uint16_t height,
                           uint16_t planes);

/*
 * The console, which is the VT52 in EmuTOS's BIOS drawing into a surface.
 *
 * It is here rather than beside the VDI because it is the same seam and the
 * same three levers: EmuTOS's code, built for the host, reached without a WORD
 * crossing over. What is on the other side is bios/vt52.c unedited and
 * emuvdi/conout.c, which puts the characters in their cells.
 *
 * Readying it takes the shape of the grid from whichever surface is selected,
 * and so does writing to it - so both want the console's surface selected
 * first. See console.c, which owns that surface and is the only caller.
 */
void emuvdi_console_init(void);
void emuvdi_console_out(int ch);

/* Cursconf, XBIOS 21: whether the cursor shows, whether it blinks and how
 * fast */
int16_t emuvdi_console_cursor(int16_t function, int16_t operand);

/*
 * How many characters have appeared on it, which is how a console with
 * something to show is told from one that has only been configured.
 */
unsigned long emuvdi_console_written(void);

/*
 * And reading it back, which is what makes the text on it selectable.
 *
 * The console draws pixels, and a cell holding an A is eight by sixteen bits
 * that happen to look like one - so the characters are kept beside them as
 * they are drawn, and this is how they are got at. See conout.c.
 *
 * The cells say how a place the pointer was becomes a place in the text: how
 * many there are and how large one is, in the console surface's own pixels.
 */
void emuvdi_console_cells(int16_t *cols, int16_t *rows,
                          int16_t *width, int16_t *height);

/*
 * The text between two cells, in reading order and spelled the way an ST
 * spells it - CR LF between lines, and the blanks a line is padded out with
 * dropped. Answers how many characters were written.
 */
int emuvdi_console_text(int16_t ax, int16_t ay, int16_t bx, int16_t by,
                        char *out, int size);

/* And turning a cell inside out, which is how a selection is shown: the same
 * way the cursor is, so nothing has to be remembered to put it back */
void emuvdi_console_invert(int16_t cx, int16_t cy);

/*
 * Serves one VDI call.
 *
 * The five arrays are the ones the caller passed, already copied out of the
 * emulated memory, and the answers are written back into them: how many
 * entries of intout and ptsout were filled in goes into control[4] and
 * control[2], and a workstation handle into control[6].
 */
void emuvdi_call(int16_t *control, int16_t *intin, int16_t *ptsin,
                 int16_t *intout, int16_t *ptsout);

/*
 * Opens the workstation the AES draws through, and works out the sizes it
 * reports: how wide and tall a character is, and how large a box has to be to
 * hold one. Call after emuvdi_init and after a surface is selected.
 */
void emuvdi_aes_init();

/*
 * What graf_handle answers with. The handle is the AES's own workstation, and
 * an application opens a virtual one against it rather than opening a physical
 * one of its own.
 */
void emuvdi_graf_handle(int16_t *handle, int16_t *wchar, int16_t *hchar,
                        int16_t *wbox, int16_t *hbox);

/*
 * Bitmaps a call names, for the raster operations.
 *
 * vro_cpyfm and its relatives are handed the address of a form definition
 * block - an MFDB - in two words of the control array, and the VDI reads it as
 * a pointer. An application's MFDB is in the emulated machine's memory and
 * describes a bitmap there, so it has to be brought across; these keep the
 * layout of the structure on this side, where the type is known.
 *
 * There are two slots because no VDI call names more than two bitmaps, a
 * source and a destination.
 */
#define EMUVDI_MFDB_SOURCE      (0)
#define EMUVDI_MFDB_DESTINATION (1)

void *emuvdi_mfdb(int slot);

/* A bitmap of null means the screen, which is the convention an application
 * uses to say "where I can see it" rather than a bitmap of its own */
void emuvdi_mfdb_set(void *mfdb, void *data, int16_t width, int16_t height,
                     int16_t wdwidth, int16_t standard, int16_t planes);

/* Where in the control array a call expects each bitmap's address */
void emuvdi_control_set_pointer(int16_t *control, int index, void *p);

/*
 * A colour register, as something that can be put on a screen.
 *
 * The VDI works in colour indices and the hardware in pen numbers, and neither
 * is a colour. This turns an index into the colour it stands for, which is
 * what a compositor wants.
 */
uint32_t emuvdi_palette_argb(int index);

/*
 * Memory below the four gigabyte line, which is where anything whose address
 * ends up in one of GEM's thirty two bit fields has to live - an object tree's
 * strings among them. See emuvdi/README.
 */
void *host_vdi_alloc(long size);
void host_vdi_free(void *block);

/* How large the screen the AES lays windows out on is */
int16_t emuvdi_screen_width();
int16_t emuvdi_screen_height();

/*
 * Draws part of an object tree, clipped to a rectangle. This is EmuTOS's own
 * object library doing the drawing - the boxes, the borders, the text and the
 * three dimensional edges of every GEM dialog ever written - which is the
 * reason for carrying it.
 *
 * The tree is one aes_tree_in built, so its pointers are addresses on this
 * side rather than in the machine.
 */
/*
 * Building an object tree where the AES can read it.
 *
 * An OBJECT is not the same shape on both sides. EmuTOS says a LONG is a
 * signed 32 bit word and writes it as long, which is four bytes on a 68000 and
 * eight here, so an OBJECT is 24 bytes in the machine and 32 in this program.
 * Nothing can be copied across as a block, and the layout is not something the
 * other side of this header should have to know: it says what the fields are
 * and this side puts them where they go.
 */
void *emuvdi_tree_alloc(int count);
void emuvdi_tree_free(void *tree);

void emuvdi_tree_set(void *tree, int index, int16_t next, int16_t head,
                     int16_t tail, uint16_t type, uint16_t flags,
                     uint16_t state, void *spec, int16_t x, int16_t y,
                     int16_t w, int16_t h);

/* The state is the one field the AES writes back: a pressed button comes back
 * selected, and a checked menu entry comes back checked */
uint16_t emuvdi_tree_state(void *tree, int index);

/*
 * And reading an object back, for a tree this program did not build.
 *
 * The menu bar's is the one that has to be read: it is the application's tree,
 * and where the last of its titles ends is what says how narrow the bar may be
 * made. Anything not wanted is asked for with a null.
 */
void emuvdi_tree_get(void *tree, int index, int16_t *next, int16_t *head,
                     int16_t *tail, int16_t *x, int16_t *y,
                     int16_t *w, int16_t *h);

/*
 * A USERBLK, which is what an object the application draws itself points at.
 *
 * It holds two things and neither means anything on this side: a routine that
 * is 68000 code, and a long the application chose, which is usually an address
 * in the machine as well. Both are carried across unchanged rather than
 * translated, because the routine is the one that reads them and it reads them
 * back where they came from - see host_userdef_draw in aestree.c.
 */
void *emuvdi_userblk_alloc();
void emuvdi_userblk_set(void *block, uint32_t code, int32_t parm);

/*
 * And calling that routine, which is the one thing in the AES that runs the
 * emulated CPU from inside a call rather than the other way round.
 *
 * A PARMBLK is not the same shape in the two places either - pb_tree is a
 * pointer and pb_parm a LONG - and neither half of it can be handed over as it
 * stands: the tree in it is this side's copy, and what the routine has to be
 * given is the application's own, which only aestree.c knows the address of.
 * So the fields go across one at a time and the block the routine reads is
 * built in the machine's memory, in the machine's layout.
 *
 * This is declared here rather than in aeskernel.h with the rest of the seam
 * because both halves have to name it, and aeskernel.h is written in EmuTOS's
 * types. What it answers with is the state the object is to be drawn in, which
 * is what the routine returns.
 */
struct host_userdef {
    uint32_t code;              /* the routine, in the machine's memory */
    const void *tree;           /* the tree being drawn, this side's copy */
    int16_t obj;                /* which object in it */
    int16_t prevstate;          /* what it was, and what it is to become */
    int16_t currstate;
    int16_t x, y, w, h;         /* where it is */
    int16_t xc, yc, wc, hc;     /* and what the drawing is clipped to */
    int32_t parm;               /* the long the application put in ub_parm */
};

int16_t host_userdef_draw(const struct host_userdef *call);

/* Whether one of those routines is running now, which is how an AES call made
 * from inside one is caught before it walks over the tree being drawn */
int aes_userdef_running(void);

/* A TEDINFO, which the text kinds of object point at. The three strings are
 * the text, the template and the validation. */
void *emuvdi_tedinfo_alloc();
void emuvdi_tedinfo_set(void *ted, char *text, char *tmplt, char *valid,
                        const int16_t *words, int word_count);
char *emuvdi_tedinfo_text(void *ted);

/*
 * A BITBLK, which a G_IMAGE points at: a monochrome form, where in it the
 * picture starts, and the colour to draw it in. The five words are bi_wb to
 * bi_color, in the order they are declared.
 */
void *emuvdi_bitblk_alloc();
void emuvdi_bitblk_set(void *blk, void *data, const int16_t *words, int count);

/*
 * An ICONBLK, which a G_ICON points at: a mask, an image and a label. The
 * eleven words are ib_char to ib_htext, likewise.
 *
 * What is allocated is a CICONBLK, which is an ICONBLK with the colour
 * versions of the same icon hanging off the end, because that is what a
 * G_CICON points at and one routine draws both. An icon whose colour list is
 * empty is drawn from the mask and the image, which is what a G_ICON is and
 * what a G_CICON keeps them for.
 */
void *emuvdi_iconblk_alloc();
void emuvdi_iconblk_set(void *blk, void *mask, void *data, char *text,
                        const int16_t *words, int count);

/*
 * Runs a dialog: draws it, waits, and answers with the object that ended it.
 * This is EmuTOS's form library, which is a loop over the same waiting an
 * application does for itself.
 */
int16_t emuvdi_form_do(void *tree, int16_t start);

/*
 * Whether a dialog has the screen, which is the AES's own count of how many
 * forms are being run: form_do puts it up on the way in and takes it down on
 * the way out, and an alert and the file selector are both a form_do.
 *
 * It is what says a modal dialog is up, and the AES uses it for that itself -
 * see the note above aes_menu_arrived, which is the one thing here that asks.
 */
int16_t emuvdi_form_showing(void);

/*
 * Puts a menu bar up along the top of the screen, or takes it away. The tree
 * is one aes_tree_in built, and its first two entries are the bar itself and
 * the row of titles along it.
 */
void emuvdi_menu_bar(void *tree, int16_t showit);

/*
 * Runs the menu, once a press has landed in the bar: follows the pointer down
 * whichever title it is over, and answers with what was chosen. 0 means the
 * menu was left without choosing anything.
 */
int16_t emuvdi_menu_do(int16_t *title, int16_t *item);

/* How tall the menu bar is, which is the height of one box */
int16_t emuvdi_menu_height();
int16_t emuvdi_menu_register(char *name);
void emuvdi_menu_forget_accessories(void);
void emuvdi_menu_forget_tree(void);
int16_t emuvdi_menu_first_accessory(void);

void emuvdi_menu_active(int16_t *x, int16_t *y, int16_t *w, int16_t *h);
int16_t emuvdi_menu_change(void *tree, int16_t object, uint16_t bit,
                           int16_t set, int16_t draw, int16_t only_if_enabled);

/* The boxes an application drags about, aes/gemgrlib.c */
void emuvdi_graf_rubberbox(int16_t x, int16_t y, int16_t wmin, int16_t hmin,
                           int16_t *w, int16_t *h);
void emuvdi_graf_dragbox(int16_t w, int16_t h, int16_t x, int16_t y,
                         int16_t bx, int16_t by, int16_t bw, int16_t bh,
                         int16_t *ex, int16_t *ey);
void emuvdi_graf_movebox(int16_t w, int16_t h, int16_t sx, int16_t sy,
                         int16_t dx, int16_t dy);
void emuvdi_graf_growbox(int16_t sx, int16_t sy, int16_t sw, int16_t sh,
                         int16_t fx, int16_t fy, int16_t fw, int16_t fh);
void emuvdi_graf_shrinkbox(int16_t fx, int16_t fy, int16_t fw, int16_t fh,
                           int16_t sx, int16_t sy, int16_t sw, int16_t sh);
int16_t emuvdi_graf_watchbox(void *tree, int16_t obj,
                             int16_t instate, int16_t outstate);
int16_t emuvdi_graf_slidebox(void *tree, int16_t parent, int16_t obj,
                             int16_t vertical);

/*
 * Puts up an alert - the box with an icon, a line or three of text and up to
 * three buttons - and answers with which button was pressed, counting from
 * one. The string is the packed form an application writes it in, like
 * "[1][This went wrong][OK|Cancel]".
 *
 * Unlike a dialog this saves what is underneath and puts it back, rather than
 * reserving the screen with form_dial, so it draws onto the screen itself.
 */
int16_t emuvdi_form_alert(int16_t default_button, char *text);

int16_t emuvdi_form_keybd(void *tree, int16_t obj, int16_t *key, int16_t *next);
int16_t emuvdi_form_button(void *tree, int16_t obj, int16_t clicks,
                           int16_t *next);
int16_t emuvdi_form_error(int16_t which);
int16_t emuvdi_objc_edit(void *tree, int16_t obj, int16_t key,
                         int16_t *index, int16_t what);

int16_t emuvdi_fsel_input(char *path, char *name, int16_t *button,
                          char *label);

void emuvdi_set_clip(int16_t x, int16_t y, int16_t w, int16_t h);
void emuvdi_objc_offset(void *tree, int16_t obj, int16_t *x, int16_t *y);
int16_t emuvdi_objc_find(void *tree, int16_t start, int16_t depth,
                         int16_t x, int16_t y);
void emuvdi_objc_change(void *tree, int16_t obj, uint16_t state, int16_t draw);

void emuvdi_objc_draw(void *tree, int16_t start, int16_t depth,
                      int16_t x, int16_t y, int16_t w, int16_t h);

/*
 * Whether the VDI has a function for this opcode. Everything it does not is
 * either a call no driver ever implemented or one of the GDOS extensions,
 * which arrive on their own range well above the VDI proper.
 */
int emuvdi_implements(int16_t opcode);

/*
 * Every printer workstation forgotten, and whatever was being printed through
 * one sent on its way. For the machine going away with one still open, which
 * is a program that printed and did not close what it printed through - see
 * emuvdi/prndev.h.
 */
void emuvdi_printer_reset(void);

/*
 * And the same for a child of fork, which has a copy of the parent's page and
 * the parent's half written job and owns neither: everything forgotten and
 * nothing sent. See gem_forget.
 */
void emuvdi_printer_forget(void);

/*
 * Whether there are fonts on the disk for this machine, which is the whole of
 * what vq_gdos has to answer.
 *
 * An application asks before it asks for anything else, and takes one road or
 * the other from the answer. Saying yes on a machine where no ASSIGN.SYS could
 * be found would send it down the road with nothing at the end of it, so this
 * is what the answer is made of rather than a build time decision.
 */
int emuvdi_gdos_installed(void);

/*
 * And whether the outline half of it is there, which is the difference between
 * telling an application it has FontGDOS and telling it it has SpeedoGDOS.
 * The second promises the calls above 230, so it is not said unless there is
 * something behind them.
 */
int emuvdi_gdos_scalable(void);

/*
 * The keyboard table an ST keyboard had: what each of its keys types
 * unshifted, shifted, and with caps lock down, a hundred and twenty eight
 * bytes to a table.
 *
 * It is what an application reads through XBIOS Keytbl, and it is EmuTOS's -
 * see emuvdi/keytables.c, which says which set is taken and why. What is
 * handed out is built from the layout the person is typing on where there is
 * one to ask about; this is the answer when there is not.
 */
void emuvdi_keyboard_tables(const uint8_t **norm, const uint8_t **shift,
                            const uint8_t **caps);

#endif /* EMUVDI_H */
