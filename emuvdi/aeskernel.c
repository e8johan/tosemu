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
void *gl_mowner;
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
WORD ev_multi(WORD flags, MOBLK *pmo1, MOBLK *pmo2, LONG tmcount,
              LONG buparm, WORD *pmomouse, WORD *rets)
{
    needs_kernel("evnt_multi");

    return 0;
}

WORD ev_button(WORD bflgclks, WORD bmask, WORD bstate, WORD *rets)
{
    needs_kernel("evnt_button");

    return 0;
}

void dsptch(void)
{
    needs_kernel("the scheduler");
}

/* Windows and the shell **************************************************/

void w_getsize(WORD which, WORD w_handle, GRECT *pt)
{
    needs_kernel("wind_get");

    pt->g_x = pt->g_y = pt->g_w = pt->g_h = 0;
}

void wm_update(WORD beg_update)
{
    needs_kernel("wind_update");
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
