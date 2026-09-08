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
 * A printer, as a VDI device. See prndev.h for what this is and printer.c for
 * the sheet of paper at the other end.
 *
 * The whole of the trick here is that a printer workstation is one of EmuTOS's
 * *virtual* workstations wearing a different hat. EmuTOS has exactly one
 * physical workstation, phys_work, and vdi_v_opnwk reinitialises it and throws
 * away every virtual workstation open against it - so letting an application's
 * v_opnwk for the printer reach EmuTOS would take the screen away from the AES
 * in the middle of a program. A virtual workstation, on the other hand, is
 * opened, closed, chained and dispatched to exactly as one would want a
 * printer's to be, and the only thing about it that says "screen" is the
 * memory the drawing goes into and the tables it reports.
 *
 * Both of those are variables, and there is only one set of them: v_bas_ad,
 * V_REZ_HZ, v_planes and their relations describe the surface, and DEV_TAB,
 * SIZ_TAB and INQ_TAB describe the device. So a call on a printer handle
 * points them at the page for the length of the call and puts them back
 * afterwards. That is what bind and unbind below are, and it is why nothing
 * else in the VDI or the AES has to know that a second device exists.
 */

#include "emutos.h"
#include "asm.h"
#include "intmath.h"
#include "tosvars.h"
#include "vdi_defs.h"
#include "lineavars.h"
#include "xbiosbind.h"

#include <string.h>

#include "prndev.h"
#include "gdos.h"
#include "emuvdi.h"

/* tosemu's own, in plain types */
#include "../printer.h"

/* EmuTOS's dispatcher, vdi_main.c */
void screen(void);

/* Ours, in hostvars.c */
void host_surface_select(void *base, UWORD width, UWORD height, UWORD planes);

/*
 * Which devices are printers.
 *
 * These are GDOS's numbers, the ones an ASSIGN.SYS writes its sections under:
 * 1 to 10 are screens, 11 to 20 plotters, 21 to 30 printers, and above that
 * metafiles, cameras and tablets. 21 is the one every application asks for,
 * being what the drivers Atari shipped were installed as.
 *
 * All ten are the same printer here. A machine of the period could have
 * several drivers installed and choose between them; what is at the end of
 * this is one CUPS destination, and pretending there are ten of it would be a
 * choice an application could make and nothing could honour.
 */
#define FIRST_PRINTER_DEVICE (21)
#define LAST_PRINTER_DEVICE  (30)

/* The physical workstation, which every virtual one is opened against */
#define PHYS_HANDLE (1)

/* The opcodes and escapes this is about */
#define V_OPNWK_ID   (1)
#define V_CLSWK_ID   (2)
#define V_CLRWK_ID   (3)
#define V_UPDWK_ID   (4)
#define V_ESCAPE_ID  (5)
#define V_OPNVWK_ID  (100)
#define V_CLSVWK_ID  (101)

#define ESC_FORM_ADV      (20)
#define ESC_OUTPUT_WINDOW (21)
#define ESC_CLEAR_LIST    (22)

/*
 * How many workstations may be open on the printer at once.
 *
 * More than one is not a curiosity: an application opens a physical
 * workstation on the printer and may then open virtual ones against it for
 * the different attributes it wants to draw with, which is what virtual
 * workstations are for. Eight is more than any program of the period used and
 * small enough to search without thinking about it.
 */
#define PRINTER_HANDLES (8)

static struct {
    WORD handle[PRINTER_HANDLES];
    int handles;

    UBYTE *page;
    UWORD width, height, planes;
    int dpi;

    /* What the device reports about itself, kept here while the screen has
     * the globals */
    WORD dev_tab[45];
    WORD siz_tab[12];
    WORD inq_tab[45];

    /* Whether anything has been drawn since the last page went out, which is
     * how closing a workstation knows whether there is a page to send */
    int drawn;
} printer;

/* And what the screen was, while the printer has them */
static struct {
    int bound;

    UBYTE *base;
    UWORD width, height, planes;

    WORD dev_tab[45];
    WORD siz_tab[12];
    WORD inq_tab[45];

    Vwk *cur_work;
} screen_was;

/* Which handles are the printer's ******************************************/

static int is_printer_handle(WORD handle)
{
    int i;

    for (i = 0; i < printer.handles; i++)
        if (printer.handle[i] == handle)
            return 1;

    return 0;
}

static void remember_handle(WORD handle)
{
    if (handle <= 0 || printer.handles >= PRINTER_HANDLES
        || is_printer_handle(handle))
        return;

    printer.handle[printer.handles++] = handle;
}

static void forget_handle(WORD handle)
{
    int i;

    for (i = 0; i < printer.handles; i++)
        if (printer.handle[i] == handle)
        {
            printer.handles--;
            printer.handle[i] = printer.handle[printer.handles];
            return;
        }
}

/* The page ****************************************************************/

/*
 * Somewhere to draw, and what the device says about itself once there is.
 *
 * The tables start as the screen's and are then corrected, rather than being
 * written out from nothing, because almost everything in them is a property of
 * the VDI rather than of the device: how many line styles there are, how many
 * fill patterns, whether text can be rotated, which fonts are loaded. All of
 * that is the same code drawing on different memory. What differs is how large
 * the page is and how large a dot on it is, and those are the four entries
 * corrected below.
 *
 * The printer has as many colours as the screen, and that is not laziness. The
 * VDI maps a colour index onto a pen through MAP_COL and a pen onto a colour
 * through the palette, and both of those are the machine's rather than any one
 * device's - MAP_COL is even built from how many colours there are. Two
 * devices disagreeing about that would need two of each, and what it would buy
 * is a colour printer on a monochrome machine, which no Atari had either.
 */
static int page_ready(void)
{
    int width, height, words_per_line;
    void *base;

    if (printer.page)
        return 1;

    base = printer_page(v_planes, &width, &height, &words_per_line);
    if (!base)
        return 0;

    printer.page = (UBYTE *)base;
    printer.width = (UWORD)width;
    printer.height = (UWORD)height;
    printer.planes = v_planes;
    printer.dpi = printer_dpi();

    memcpy(printer.dev_tab, DEV_TAB, sizeof printer.dev_tab);
    memcpy(printer.siz_tab, SIZ_TAB, sizeof printer.siz_tab);
    memcpy(printer.inq_tab, INQ_TAB, sizeof printer.inq_tab);

    printer.dev_tab[0] = (WORD)(width - 1);
    printer.dev_tab[1] = (WORD)(height - 1);

    /*
     * The device is exact, and a dot on it is 25.4 millimetres divided by how
     * many there are to the inch, in thousandths of a millimetre. Those two
     * numbers are what an application divides one by the other to find out
     * whether the device's pixels are square, and what the VDI's own circles
     * and the AES's window gadgets are shaped by.
     */
    printer.dev_tab[2] = 0;
    printer.dev_tab[3] = (WORD)(25400 / printer.dpi);
    printer.dev_tab[4] = printer.dev_tab[3];

    return 1;
}

/* Pointing the VDI at the page, and back again *****************************/

static void bind(void)
{
    screen_was.base = v_bas_ad;
    screen_was.width = V_REZ_HZ;
    screen_was.height = V_REZ_VT;
    screen_was.planes = v_planes;
    screen_was.cur_work = CUR_WORK;

    memcpy(screen_was.dev_tab, DEV_TAB, sizeof screen_was.dev_tab);
    memcpy(screen_was.siz_tab, SIZ_TAB, sizeof screen_was.siz_tab);
    memcpy(screen_was.inq_tab, INQ_TAB, sizeof screen_was.inq_tab);

    memcpy(DEV_TAB, printer.dev_tab, sizeof printer.dev_tab);
    memcpy(SIZ_TAB, printer.siz_tab, sizeof printer.siz_tab);
    memcpy(INQ_TAB, printer.inq_tab, sizeof printer.inq_tab);

    host_surface_select(printer.page, printer.width, printer.height,
                        printer.planes);

    /*
     * And how large a point is, which is the whole difference between text
     * that prints and text that comes out an eighth of the size it should.
     *
     * A bitmap font is a font of a particular number of pixels and has nothing
     * to say about a device four times finer than the one it was drawn for -
     * which is exactly why GDOS had a section per device in ASSIGN.SYS and why
     * Atari sold printer fonts. The outline faces do have something to say,
     * because a size in points becomes a size in pixels through the device's
     * resolution, and this is where that resolution is said.
     */
    gdos_fsm_device_dpi(printer.dpi, printer.dpi);

    screen_was.bound = 1;
}

static void unbind(void)
{
    if (!screen_was.bound)
        return;

    memcpy(printer.dev_tab, DEV_TAB, sizeof printer.dev_tab);
    memcpy(printer.siz_tab, SIZ_TAB, sizeof printer.siz_tab);
    memcpy(printer.inq_tab, INQ_TAB, sizeof printer.inq_tab);

    memcpy(DEV_TAB, screen_was.dev_tab, sizeof screen_was.dev_tab);
    memcpy(SIZ_TAB, screen_was.siz_tab, sizeof screen_was.siz_tab);
    memcpy(INQ_TAB, screen_was.inq_tab, sizeof screen_was.inq_tab);

    host_surface_select(screen_was.base, screen_was.width, screen_was.height,
                        screen_was.planes);

    gdos_fsm_device_dpi(0, 0);

    CUR_WORK = screen_was.cur_work;
    screen_was.bound = 0;
}

/* Sending a page **********************************************************/

/*
 * The page, on its way to the printer.
 *
 * The palette goes with it because a surface holds pens rather than colours -
 * the plane bits of a pixel are a hardware register number - and only the VDI
 * knows what each one was set to. It is gathered here rather than in
 * printer.c so that nothing on that side has to know what a pen is.
 */
static int send_page(void)
{
    unsigned int argb[256];
    int colours = numcolors;
    int i;

    if (colours < 1)
        colours = 1;
    if (colours > (int)(sizeof argb / sizeof argb[0]))
        colours = (int)(sizeof argb / sizeof argb[0]);

    for (i = 0; i < colours; i++)
        argb[i] = emuvdi_palette_argb(i) & 0xffffffu;

    if (!printer_page_out(argb, colours))
        return 0;

    printer.drawn = 0;

    return 1;
}

/*
 * Whether an opcode puts marks on the page.
 *
 * This decides whether closing a workstation has a page to send, so it has to
 * be the drawing and nothing else: an application that asks a question after
 * printing its last page - which the bindings do, several of them ending in a
 * vq_ of some sort - would otherwise be handed a second copy of it.
 */
static int draws(WORD opcode)
{
    switch (opcode)
    {
        case 6:     /* v_pline */
        case 7:     /* v_pmarker */
        case 8:     /* v_gtext */
        case 9:     /* v_fillarea */
        case 10:    /* v_cellarray */
        case 11:    /* the generalised drawing primitives */
        case 103:   /* v_contourfill */
        case 109:   /* vro_cpyfm */
        case 114:   /* vr_recfl */
        case 121:   /* vrt_cpyfm */
            return 1;
    }

    return 0;
}

/* Opening one *************************************************************/

/*
 * The screen's physical workstation, opened because the printer's is a virtual
 * one and there has to be something to open it against.
 *
 * A GEM application has already done this - the AES opens one when appl_init
 * is called - but a program that only prints need never have touched the
 * screen, and then DEV_TAB is a table of noughts and the workstation this is
 * about to open would report a device with no resolution and no fonts.
 *
 * INTIN is the application's, so its ten attribute defaults are borrowed and
 * the device number put back afterwards. What matters is that the number is
 * this screen's own, because vdi_v_opnwk reads it as a request for a video
 * mode and would otherwise be asked to change one.
 */
static void open_the_screen(WORD *control, WORD *intin)
{
    WORD was_opcode = control[0];
    WORD was_handle = control[6];
    WORD was_device = intin[0];

    intin[0] = (WORD)(Getrez() + 2);
    control[0] = V_OPNWK_ID;
    control[6] = 0;

    screen();

    intin[0] = was_device;
    control[0] = was_opcode;
    control[6] = was_handle;
}

/* v_opnwk answers with a handle of nought when it will not open one */
static int refuse(WORD *control)
{
    control[2] = 0;
    control[4] = 0;
    control[6] = 0;

    return 1;
}

static int open_printer(WORD *control, WORD *intin)
{
    WORD handle;

    if (intin[0] < FIRST_PRINTER_DEVICE || intin[0] > LAST_PRINTER_DEVICE)
        return 0;                   /* not a printer: somebody else's call */

    if (!get_vwk_by_handle(PHYS_HANDLE))
        open_the_screen(control, intin);

    if (printer.handles >= PRINTER_HANDLES)
        return refuse(control);

    if (!page_ready())
        return refuse(control);

    /*
     * Opened as a virtual workstation, because that is what it is. The
     * application asked for a physical one and gets everything it expects from
     * one - a handle of its own, the device's tables in work_out, attributes
     * that start where its work_in said - because v_opnvwk answers with
     * exactly the same things. What it does not get is EmuTOS deciding that
     * opening a physical workstation means starting the VDI again.
     */
    bind();

    control[0] = V_OPNVWK_ID;
    screen();
    control[0] = V_OPNWK_ID;

    handle = control[6];

    /*
     * And clipped to the page, which is the one attribute a printer's
     * workstation does not start with the same value as a screen's.
     *
     * A workstation opens with clipping off, and with it off the VDI does not
     * check where it is drawing: a row below the last one is written past the
     * end of the memory the device has. On a screen that is a program drawing
     * outside a screen it was told the size of. On a printer it is a program
     * drawing outside a page whose size is a setting on this machine - the
     * same document is one page tall on A4 and another on Letter - so the
     * program cannot have been written for it. Every printer driver clipped to
     * the page for that reason: a driver owned the buffer, and there was
     * nothing past the bottom of it to draw on.
     */
    if (handle > 0)
    {
        Vwk *vwk = get_vwk_by_handle(handle);

        if (vwk)
            vwk->clip = TRUE;
    }

    unbind();

    if (handle <= 0)
        return refuse(control);

    remember_handle(handle);

    return 1;
}

/* Closing one *************************************************************/

/*
 * v_clswk, which closes the device rather than a workstation on it.
 *
 * Every virtual workstation an application opened against the printer goes
 * with it - that is what closing a physical workstation means, and EmuTOS's
 * own v_clswk does the same for the screen. Doing it one at a time is what
 * there is to do here, the printer's workstations all being virtual ones as
 * far as EmuTOS is concerned.
 */
static int close_printer(WORD *control)
{
    WORD was_handle = control[6];
    WORD open[PRINTER_HANDLES];
    int count = printer.handles;
    int i;

    if (printer.drawn)
        send_page();

    memcpy(open, printer.handle, sizeof open);

    control[0] = V_CLSVWK_ID;

    for (i = 0; i < count; i++)
    {
        control[6] = open[i];
        screen();
    }

    control[0] = V_CLSWK_ID;
    control[6] = was_handle;

    printer.handles = 0;

    /*
     * The job ends with the device rather than with each page, which is what
     * makes a document one job in the queue instead of one job a page. An
     * application that prints five pages and closes is five pages of one job.
     */
    printer_job_end();

    return 1;
}

/* The escapes a printer has ***********************************************/

static int escape(WORD *control)
{
    control[2] = 0;
    control[4] = 0;

    switch (control[5])
    {
        case ESC_FORM_ADV:
            /*
             * The paper comes out and the next sheet goes in. It does not by
             * itself print anything - v_updwk is what does that, and an
             * application that prints properly calls the two in that order -
             * but one that only ever says form advance would otherwise print
             * nothing at all, so anything still on the page goes with it.
             */
            if (printer.drawn)
                send_page();

            printer_page_clear();
            printer.drawn = 0;
            return 1;

        case ESC_OUTPUT_WINDOW:
            /*
             * A rectangle of the page, printed. The whole sheet goes here and
             * the rectangle is not read, because this is what a driver with a
             * band buffer offered a program that could not hold a page: there
             * is a whole page here, so what would be sent for the rectangle is
             * the page it is part of.
             */
            if (printer.drawn)
                send_page();
            return 1;

        case ESC_CLEAR_LIST:
            printer_page_clear();
            printer.drawn = 0;
            return 1;
    }

    return 0;
}

/* What the rest of emuvdi calls *******************************************/

int prndev_bind(WORD *control)
{
    if (screen_was.bound)
        return 0;

    if (!printer.page || !is_printer_handle(control[6]))
        return 0;

    bind();

    return 1;
}

void prndev_unbind(WORD *control)
{
    WORD handle = control[6];

    if (!screen_was.bound)
        return;

    /*
     * A virtual workstation opened against the printer is the printer's too,
     * and one closed against it stops being. Both are read out of the control
     * array after the call rather than before it: opening says which handle it
     * turned out to be there and nowhere else.
     */
    if (control[0] == V_OPNVWK_ID)
        remember_handle(handle);

    unbind();

    if (control[0] == V_CLSVWK_ID)
    {
        forget_handle(handle);

        if (printer.handles == 0)
            printer_job_end();
    }
}

int prndev_served(WORD *control, WORD *intin, WORD *intout, WORD *ptsout)
{
    (void)intout;
    (void)ptsout;

    if (control[0] == V_OPNWK_ID)
        return open_printer(control, intin);

    if (!screen_was.bound)
        return 0;

    switch (control[0])
    {
        case V_CLSWK_ID:
            return close_printer(control);

        case V_CLRWK_ID:
            /*
             * Blanking the page. EmuTOS's own would do exactly this - it
             * clears the surface the globals point at, which is the page - but
             * saying it here is what also forgets that anything was drawn, and
             * a page nobody has drawn on is not one to send.
             */
            printer_page_clear();
            printer.drawn = 0;
            control[2] = 0;
            control[4] = 0;
            return 1;

        case V_UPDWK_ID:
            send_page();
            control[2] = 0;
            control[4] = 0;
            return 1;

        case V_ESCAPE_ID:
            if (escape(control))
                return 1;
            break;
    }

    if (draws(control[0]))
        printer.drawn = 1;

    return 0;
}

void prndev_reset(void)
{
    unbind();

    printer.handles = 0;
    printer.drawn = 0;
    printer.page = 0;

    printer_job_end();
}

void prndev_forget(void)
{
    unbind();

    printer.handles = 0;
    printer.drawn = 0;
    printer.page = 0;

    printer_job_forget();
}
