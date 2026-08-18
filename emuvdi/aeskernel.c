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
 * What the AES library files stand on, before there is a kernel to stand on.
 *
 * Everything here is either state the library files read, or a call they make
 * downwards. The state is given the value a machine with one application and
 * nothing happening would have, which is enough for drawing: an object tree
 * can be drawn without anything having been clicked. The calls are what needs
 * a kernel, and they say so rather than pretending.
 *
 * The point of the file is that the list is complete and checked by the
 * linker. When the kernel arrives it takes these over one at a time, and
 * anything it forgets fails to link.
 */

#include "emutos.h"
#include "asm.h"
#include "intmath.h"
#include "obdefs.h"
#include "struct.h"
#include "aesdefs.h"
#include "aesext.h"
#include "gemlib.h"
#include "lineavars.h"
#include "aeskernel.h"

#include <stdio.h>
#include <stdlib.h>

#include "bdosbind.h"

/* Said once each, so that a program leaning on a hole says which hole rather
 * than filling the output with it */
static void needs_kernel(const char *what)
{
    static const char *said[64];
    static int count;
    int i;

    for (i = 0; i < count; i++)
        if (said[i] == what)
            return;

    if (count < (int)(sizeof said / sizeof said[0]))
        said[count++] = what;

    fprintf(stderr, "AES: %s needs the kernel, which is not written yet\n",
            what);
}

/* Process and desktop state **********************************************/

/*
 * The AES's own globals, which EmuTOS keeps in one structure in geminit.c.
 * The library files reach into it for scratch buffers and for the resource
 * the AES draws its own dialogs from.
 */
THEGLO D;

/* The process that is running. Every library file that asks whose window or
 * whose mouse this is asks through here. */
AESPD *rlr;

/* Whether the control manager owns the mouse just now */
BOOL gl_ctmown;

/* Where the AES draws from, which the resource loader sets */
PFVOID drwaddr;

/* Input state ************************************************************/

/*
 * Where the pointer is, which keys are held and which buttons are down. The
 * compositor will say; until then nothing has moved and nothing is pressed.
 */
WORD xrat;
WORD yrat;
WORD kstate;
WORD button;

/* Mouse ownership ********************************************************/

/*
 * Whether the mouse belongs to the AES or to an application, and which
 * application. graf_mouse and the form library hand it back and forth.
 */
WORD gl_mouse;
WORD gl_prevmouse;
AESPD *gl_mowner;
void *mouse_cursor;

void set_mouse_to_arrow(void)
{
    needs_kernel("set_mouse_to_arrow");
}

void set_mouse_to_hourglass(void)
{
    needs_kernel("set_mouse_to_hourglass");
}

/* Windows ****************************************************************/

/* The menu bar tree of whichever application owns the bar. backgrcol, which
 * says what colour the desktop is painted, is not here: gemoblib.c defines
 * that one itself. */
OBJECT *gl_mntree;

/* The event kernel *******************************************************/

/*
 * form_do blocks on ev_multi, so dialogs do not work until this does. It is
 * the piece that has to wait on the compositor, the daemon and a timer at
 * once, which is why it is the kernel and not a library.
 */
/*
 * The wait every GEM application lives in, and the one form_do loops over.
 *
 * The rectangles arrive as MOBLKs, which are a rectangle and a flag saying
 * whether the wait is for the mouse going into it or coming out. They are
 * flattened here rather than carried across the seam, because the far side
 * has no reason to know what a MOBLK is.
 */
/*
 * Written under the same condition as its declaration rather than copied out
 * of it. The menu extension adds a third rectangle, and a definition that
 * disagrees with the declaration about how many arguments there are does not
 * fail to build: the call is simply assembled wrongly, and comes apart when it
 * runs.
 */
#if CONF_WITH_MENU_EXTENSION
WORD ev_multi(WORD flags, MOBLK *pmo1, MOBLK *pmo2, MOBLK *pmo3, LONG tmcount,
              LONG buparm, WORD *pmomouse, WORD *rets)
#else
WORD ev_multi(WORD flags, MOBLK *pmo1, MOBLK *pmo2, LONG tmcount,
              LONG buparm, WORD *pmomouse, WORD *rets)
#endif
{
    WORD m1[4], m2[4];
    WORD m1flags = 0, m2flags = 0;
    WORD key = 0, mx = 0, my = 0, buttons = 0, kstate = 0;
    WORD happened;

    m1[0] = m1[1] = m1[2] = m1[3] = 0;
    m2[0] = m2[1] = m2[2] = m2[3] = 0;

    if (pmo1)
    {
        m1[0] = pmo1->m_gr.g_x;
        m1[1] = pmo1->m_gr.g_y;
        m1[2] = pmo1->m_gr.g_w;
        m1[3] = pmo1->m_gr.g_h;
        m1flags = pmo1->m_out;
    }

    if (pmo2)
    {
        m2[0] = pmo2->m_gr.g_x;
        m2[1] = pmo2->m_gr.g_y;
        m2[2] = pmo2->m_gr.g_w;
        m2[3] = pmo2->m_gr.g_h;
        m2flags = pmo2->m_out;
    }

    happened = host_event_wait(flags, (flags & MU_TIMER) ? tmcount : -1,
                               pmomouse, m1, m1flags, m2, m2flags,
                               &key, &mx, &my, &buttons, &kstate);

    rets[0] = mx;
    rets[1] = my;
    rets[2] = buttons;
    rets[3] = kstate;
    rets[4] = key;
    rets[5] = buttons ? 1 : 0;      /* how many clicks, which is not counted */

    return happened;
}

WORD ev_button(WORD bflgclks, WORD bmask, WORD bstate, WORD *rets)
{
    /* Waiting for the buttons alone, which is evnt_multi with one thing in
     * the mask */
#if CONF_WITH_MENU_EXTENSION
    return ev_multi(MU_BUTTON, 0, 0, 0, 0L, 0L, 0, rets);
#else
    return ev_multi(MU_BUTTON, 0, 0, 0L, 0L, 0, rets);
#endif
}

/*
 * Giving the processor to whoever is waiting for it, which is what an
 * application does while it watches for a button to come up. There is one
 * application, so it has already had its turn - the same answer appl_yield
 * gives, and for the same reason.
 *
 * The waiting itself is not lost by this being empty: the loops that call it
 * go on to ev_multi, which is where the waiting happens.
 */
void dsptch(void)
{
}

/* The control manager *****************************************************/

/*
 * Who owns the mouse and the area outside the windows.
 *
 * On a real machine these hand a shared screen back and forth between the
 * applications sharing it. There is one application, so it owns everything
 * whenever it asks, and the handing back and forth is nothing to do.
 *
 * They stop being nothing when the daemon exists: that is what makes the
 * screen shared, and these are how it is shared.
 */
void ct_chgown(AESPD *mpd, GRECT *pr)
{
    (void)mpd;
    (void)pr;
}

void ct_mouse(WORD grabit)
{
    gl_ctmown = grabit ? TRUE : FALSE;
}

void get_ctrl(GRECT *pt)
{
    /* The area the control manager has, which is all of it */
    pt->g_x = 0;
    pt->g_y = 0;
    pt->g_w = V_REZ_HZ;
    pt->g_h = V_REZ_VT;
}

void get_mown(AESPD **pmown)
{
    *pmown = gl_mowner;
}

/* The fork queue, which is how the AES defers work out of an interrupt. There
 * are no interrupts here, so nothing is ever deferred and the queue is always
 * empty. */
void fq(void)
{
}

/* Windows and the shell **************************************************/

void w_getsize(WORD which, WORD w_handle, GRECT *pt)
{
    needs_kernel("wind_get");

    pt->g_x = pt->g_y = pt->g_w = pt->g_h = 0;
}

/*
 * Taking and returning the right to draw outside one's own windows, which is
 * what form_do does around a dialog. It is a lock held against the other
 * applications, and there is one application, so taking it always succeeds.
 *
 * It stops being nothing the moment the daemon exists - see AES_wind_update,
 * which says the same thing at the other end.
 */
void wm_update(WORD beg_update)
{
    (void)beg_update;
}

/*
 * Painting the desktop behind everything, and telling a window it has been
 * uncovered. Both are the window manager's, which is not written yet.
 */
void w_drawdesk(GRECT *pc)
{
    needs_kernel("drawing the desktop");
    (void)pc;
}

void w_update(WORD bottom, GRECT *pt, WORD top, BOOL moved)
{
    needs_kernel("redrawing what a window uncovered");
    (void)bottom; (void)pt; (void)top; (void)moved;
}

WORD sh_find(char *pspec)
{
    needs_kernel("shel_find");

    return 0;
}

/* Interrupt entry points *************************************************/

/*
 * The keyboard, mouse and wheel handlers EmuTOS hangs off the IKBD vectors.
 * Nothing interrupts here: input will arrive from the compositor and be put
 * into the event queue directly, so these exist only because the tables that
 * name them are built.
 */
void far_bcha(void)
{
}

void far_mcha(void)
{
}

void aes_wheel(void)
{
}

/* Memory *****************************************************************/

/*
 * The resource loader asks GEMDOS for memory to read a file into. That memory
 * belongs to the AES rather than to the application, so it comes from the host
 * heap, for the same reason a virtual workstation does - see bdosbind.h.
 */
void *dos_alloc_anyram(LONG nbytes)
{
    /* Below the four gigabyte line, because a resource tree's addresses end
     * up in the LONG fields of the objects in it - see host_vdi_alloc */
    return host_vdi_alloc(nbytes);
}

WORD dos_free(void *maddr)
{
    host_vdi_free(maddr);

    return 0;
}

/*
 * Reading a resource file.
 *
 * rsrc_load takes a TOS path, and turning one of those into a host file is
 * GEMDOS's business - which drive is current, which directory, and the
 * case insensitive walk that finds the file whatever the application called
 * it. tosemu has all of that already; what is missing is the kernel that
 * knows which application is asking.
 */
LONG dos_open(char *pname, WORD access)
{
    needs_kernel("rsrc_load");

    return -33;     /* file not found */
}

WORD dos_close(WORD handle)
{
    return -37;     /* invalid handle */
}

LONG dos_read(WORD handle, LONG cnt, void *pbuffer)
{
    return -37;
}

LONG dos_lseek(WORD handle, WORD smode, LONG sofst)
{
    return -37;
}

/* Calling back into the machine ******************************************/

/*
 * A G_USERDEF object draws itself, through a routine the application supplied.
 * That routine is 68000 code, so calling it means handing the parameter block
 * and the address to the emulator and letting the CPU run until it returns -
 * the way XBIOS_Supexec already does. Until that is wired up, a user drawn
 * object is left blank rather than drawn wrongly.
 */
WORD host_call_userdef(USERBLK *ub, PARMBLK *pb)
{
    needs_kernel("drawing a G_USERDEF object");

    return 0;
}
