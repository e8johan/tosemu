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
 * Application functions.
 *
 * appl_init is where an application introduces itself to the AES and is given
 * the identifier everything else about it hangs off. Identifiers are handed
 * out here for now, which is only correct while there is one application: they
 * have to be unique across every application sharing a desktop, so allocating
 * them moves to the daemon once there is one.
 */

#include "aes_p.h"

#include <string.h>

#include "aesclient.h"
#include "aesproto.h"
#include "gem_p.h"
#include "tossystem.h"
#include "emuvdi/emuvdi.h"
#include "xbios.h"
#include "m68k.h"

/* The identifier of the running application, or -1 before it has asked for
 * one. An application that has not called appl_init has no business calling
 * anything else. */
static int16_t ap_id = -1;

int16_t gem_application(void)
{
    return ap_id;
}

void gem_application_resume(int16_t id)
{
    ap_id = id;
}

void aes_appl_reset()
{
    ap_id = -1;
    aes_evnt_reset();
    aes_wind_reset();
    aes_menu_reset();
    aes_rsrc_reset();
}

/*
 * The global array an application is handed back, fifteen words describing the
 * AES and the application's place in it:
 *
 *   global[0]     the AES version
 *   global[1]     how many applications can run at once
 *   global[2]     this application's identifier
 *   global[3-4]   ap_private, which is the application's own to use
 *   global[5-6]   ap_ptree, the trees rsrc_load found
 *   global[7-8]   ap_1resv, the memory the resource was loaded into
 *   global[9-10]  ap_2resv, how long that memory is
 *   global[11-12] ap_3resv
 *   global[13-14] the largest and smallest character the AES draws with
 *
 * The last two are only there from AES 4 onwards, and reporting an AES of 1.4
 * means they are not ours to write: a binding built for an earlier AES is
 * within its rights to have reserved room for thirteen words and no more.
 */
#define AES_GLOBAL_VERSION  (0)
#define AES_GLOBAL_COUNT    (2)
#define AES_GLOBAL_ID       (4)
#define AES_GLOBAL_PRIVATE  (6)
#define AES_GLOBAL_PTREE   (10)
#define AES_GLOBAL_1RESV   (14)
#define AES_GLOBAL_2RESV   (18)
#define AES_GLOBAL_3RESV   (22)

uint32_t AES_appl_init()
{
    uint32_t g = aes_global();

    /* Calling appl_init twice is not an error worth failing over, and an
     * application that does it means to carry on with the identifier it
     * already has rather than to be given a second one. */
    int arriving = (ap_id < 0);

    FUNC_TRACE_ENTER

    if (arriving)
    {
        /*
         * Who this application is, which the daemon says when there is one and
         * is nought when there is not.
         *
         * The AES numbers them as it starts them, from nothing upwards, so on
         * a machine running one program at a time that program is nought
         * either way. That is not a detail: a GEM program written for such a
         * machine tests whether appl_init answered nought and stops if it did
         * not, so answering one would have it give up before drawing anything.
         *
         * This is the first thing done rather than something done once the
         * screen is ready, because the same answer says how large the screen
         * is. The daemon is what decides that - it has to be one screen for
         * every application sharing it - and a screen made before asking is a
         * screen made from this build's own guess, with the daemon's answer
         * arriving too late to be of any use.
         */
        ap_id = aes_client_hello(tos_program_name());

        if (ap_id < 0)
        {
            /* The daemon would not have it, which only happens when there are
             * already as many applications as it will hold */
            printf("AES appl_init: the AES is full\n");
            return AES_ERROR;
        }
    }

    /*
     * The AES draws through a workstation of its own, which it opens here
     * rather than when GEM starts: an application that only ever calls the VDI
     * has no use for it, and opening one costs a screen's worth of state.
     */
    if (!gem_start())
        return AES_ERROR;

    if (arriving)
        emuvdi_aes_init();

    m68k_write_memory_16(g + AES_GLOBAL_VERSION, AES_VERSION);
    m68k_write_memory_16(g + AES_GLOBAL_COUNT,
                         (uint16_t)aes_client_apps());
    m68k_write_memory_16(g + AES_GLOBAL_ID, (uint16_t)ap_id);
    m68k_write_memory_32(g + AES_GLOBAL_PRIVATE, 0);
    m68k_write_memory_32(g + AES_GLOBAL_PTREE, 0);
    m68k_write_memory_32(g + AES_GLOBAL_1RESV, 0);
    m68k_write_memory_32(g + AES_GLOBAL_2RESV, 0);
    m68k_write_memory_32(g + AES_GLOBAL_3RESV, 0);

    FUNC_TRACE_ARGS {
        printf("    ap_id: %d, version: 0x%x\n", ap_id, AES_VERSION);
    }

    return ap_id;
}

/*
 * appl_write - send a message to an application
 *
 * The identifier says which, and with one application running it can only be
 * this one. That is not as useless as it sounds: an application posts messages
 * to itself to drive its own redraws, and the AES posts to it for everything
 * from a menu selection to being told to quit. Routing between applications is
 * what the daemon will add, and that changes where a message goes rather than
 * what a message is.
 */
uint32_t AES_appl_write()
{
    int16_t to = aes_intin(0);
    int16_t length = aes_intin(1);
    uint32_t buffer = aes_addrin(0);
    int16_t message[8];
    int i;

    FUNC_TRACE_ENTER_ARGS {
        printf("    to: %d, length: %d\n", to, length);
    }

    if (ap_id < 0)
    {
        halt_execution();
        printf("AES appl_write before appl_init\n");
        return AES_ERROR;
    }

    /* A message is always eight words. A length saying otherwise is an
     * application built for an AES that had longer ones, which this is not. */
    if (length != (int16_t)sizeof message)
    {
        halt_execution();
        printf("AES appl_write of %d bytes, and a message is %d\n",
               length, (int)sizeof message);
        return AES_ERROR;
    }

    for (i = 0; i < 8; i++)
        message[i] = (int16_t)m68k_read_memory_16(buffer + 2*i);

    /*
     * To itself or to somebody else.
     *
     * An application posting to itself is not a curiosity: it is how one drives
     * its own redraws, and how the AES tells it a menu was picked or that it
     * is to quit. Those go straight into the queue it is waiting on, and no
     * daemon is involved even when there is one - a message from an
     * application to itself has nothing to arbitrate.
     */
    if (to == ap_id)
        return aes_message_post(message) ? AES_E_OK : AES_ERROR;

    if (!aes_client_send(to, message))
    {
        /*
         * Nobody there. GEM answers nought rather than stopping, because an
         * application that has gone away between one message and the next is
         * an ordinary thing to happen and the sender is expected to cope.
         */
        FUNC_TRACE_ARGS {
            printf("    nobody is application %d\n", to);
        }

        return AES_ERROR;
    }

    return AES_E_OK;
}

/*
 * appl_find - which application answers to a name
 *
 * Eight characters padded with spaces, and -1 when none does. An application
 * looking for another by name is how two of them arrange to talk without
 * either having started the other.
 */
uint32_t AES_appl_find()
{
    uint32_t address = aes_addrin(0);
    char name[AESD_NAME_LEN + 1];
    int16_t found;
    int i;

    for (i = 0; i < AESD_NAME_LEN; i++)
        name[i] = (char)m68k_read_memory_8(address + i);
    name[AESD_NAME_LEN] = 0;

    FUNC_TRACE_ENTER_ARGS {
        printf("    name: [%s]\n", name);
    }

    /*
     * "?AGI" is not an application. It is how a program asks an AES older
     * than 4.0 whether it has appl_getinfo anyway, and nought is yes - which
     * is the question this AES, reporting 1.4, is going to be asked.
     */
    if (strncmp(name, "?AGI", 5) == 0)
        return 0;

    found = aes_client_find(name);

    FUNC_TRACE_ARGS {
        printf("    application %d\n", found);
    }

    /* Not an error to report: not being there is an answer */
    return (uint32_t)(uint16_t)found;
}

/*
 * appl_search - who else is here, one at a time
 *
 * Mode nought asks for the first and one for the next, and each answer is a
 * name, a kind and an identifier, until there are none left; mode two asks
 * for the shell, of which there is none. The list is the daemon's, which is
 * the only thing that knows who is in the session. Without one there is this
 * application and nobody else.
 *
 * http://toshyp.atari.org/en/00500c.html
 */
#define APP_FIRST       (0)
#define APP_NEXT        (1)

#define APP_APPLICATION (2)
#define APP_ACCESSORY   (4)

static int16_t search_next;

uint32_t AES_appl_search()
{
    int16_t mode = aes_intin(0);
    uint32_t buffer = aes_addrin(0);
    char name[AESD_NAME_LEN];
    int accessory = 0;
    int16_t found;
    int i;

    FUNC_TRACE_ENTER_ARGS {
        printf("    mode: %d\n", mode);
    }

    if (mode == APP_FIRST)
        search_next = 0;
    else if (mode != APP_NEXT)
        return AES_ERROR;

    if (aes_client_connected())
        found = aes_client_search(search_next, name, &accessory);
    else if (search_next == 0)
    {
        found = ap_id;
        memcpy(name, tos_program_name(), AESD_NAME_LEN);
        accessory = tos_is_accessory();
    }
    else
        found = -1;

    if (found < 0)
        return AES_ERROR;

    search_next++;

    for (i = 0; i < AESD_NAME_LEN; i++)
        m68k_write_memory_8(buffer + i, (uint8_t)name[i]);
    m68k_write_memory_8(buffer + AESD_NAME_LEN, 0);

    aes_set_intout(1, accessory ? APP_ACCESSORY : APP_APPLICATION);
    aes_set_intout(2, found);

    return AES_E_OK;
}

/*
 * appl_getinfo - what this AES can do, asked about one area at a time
 *
 * The AES of TOS 4 and after, which answers four words for each of fifteen
 * questions, and which an older AES can have as well: an application finds
 * out by appl_find("?AGI"), see above. The answers are what tosemu does, and
 * most of them are no - which is the point of asking, since an application
 * told no takes the path that is there instead of calling what is not.
 *
 * http://toshyp.atari.org/en/005001.html
 */
#define AES_LARGEFONT   (0)
#define AES_SMALLFONT   (1)
#define AES_SYSTEM      (2)
#define AES_LANGUAGE    (3)
#define AES_PROCESS     (4)
#define AES_PCGEM       (5)
#define AES_INQUIRE     (6)
#define AES_WDIALOG     (7)
#define AES_MOUSE       (8)
#define AES_MENU        (9)
#define AES_SHELL      (10)
#define AES_WINDOW     (11)
#define AES_MESSAGES   (12)
#define AES_OBJECTS    (13)
#define AES_FORM       (14)

/* The one message of AES 4's that tosemu sends: tosaesd asks everyone to go
 * with it when it ends */
#define MSG_AP_TERM    (0x0008)

/* The three system fonts, as emuvdi_system_font numbers them */
#define FONT_6X6       (0)
#define FONT_8X8       (1)
#define FONT_8X16      (2)

/*
 * A font the AES draws with: its height as vst_height takes it, which is the
 * top line of the font's header, and its identifier. The type is nought, a
 * system font, there being no other kind the AES uses.
 */
static void font_answer(int which)
{
    struct emuvdi_font font;

    if (!emuvdi_system_font(which, &font))
        return;

    aes_set_intout(1, (int16_t)font.words[2]);
    aes_set_intout(2, font.id);
    aes_set_intout(3, 0);
}

uint32_t AES_appl_getinfo()
{
    int16_t type = aes_intin(0);
    int16_t handle, wchar, hchar, wbox, hbox;
    int i;

    FUNC_TRACE_ENTER_ARGS {
        printf("    type: %d\n", type);
    }

    if (type < AES_LARGEFONT || type > AES_FORM)
        return AES_ERROR;

    /* Nought is no, and what most of the answers are */
    for (i = 1; i <= 4; i++)
        aes_set_intout(i, 0);

    switch (type)
    {
    case AES_LARGEFONT:
        /* Whichever the AES lays its dialogs out in, which depends on how
         * tall the screen is */
        emuvdi_graf_handle(&handle, &wchar, &hchar, &wbox, &hbox);
        font_answer(hchar >= 16 ? FONT_8X16 : FONT_8X8);
        break;

    case AES_SMALLFONT:
        font_answer(FONT_6X6);
        break;

    case AES_SYSTEM:
        /*
         * The resolution as the AES numbers it, which is Getrez's plus two;
         * the sixteen colours an object can be drawn in; colour icons, which
         * are loaded and drawn in black and white, so no; and the resource
         * format that has them, which rsrc_load reads.
         */
        aes_set_intout(1, (int16_t)(xbios_getrez() + 2));
        aes_set_intout(2, 16);
        aes_set_intout(3, 0);
        aes_set_intout(4, 1);
        break;

    case AES_LANGUAGE:
        aes_set_intout(1, 0);           /* English */
        break;

    case AES_PROCESS:
        /*
         * Every application is a process of its own, running alongside the
         * others whether or not anything yields, so the multitasking is
         * real. appl_search is there; appl_find knows nothing of MiNT's
         * process numbers, and rsrc_rcfix is not written.
         */
        aes_set_intout(1, 1);
        aes_set_intout(3, 1);
        break;

    case AES_SHELL:
        /*
         * shel_write knows modes nought and one, and both start the program
         * straight away - nought without stopping the caller, which is what
         * the second word says, and one immediately, which is the third.
         */
        aes_set_intout(1, 1);
        aes_set_intout(2, 1);
        aes_set_intout(3, 0);
        break;

    case AES_MESSAGES:
        aes_set_intout(1, MSG_AP_TERM);
        break;

    /*
     * And the rest are no throughout: no PC-GEM calls, none of the -1
     * inquiries, none of the dialog libraries, no saving and restoring of
     * the mouse form, no popups or submenus or extra words in MN_SELECTED,
     * none of the wind_get and wind_set extensions or the gadgets beyond
     * GEM's own, no three dimensional objects and no flying dialogs.
     */
    case AES_PCGEM:
    case AES_INQUIRE:
    case AES_WDIALOG:
    case AES_MOUSE:
    case AES_MENU:
    case AES_WINDOW:
    case AES_OBJECTS:
    case AES_FORM:
        break;
    }

    return AES_E_OK;
}

/* And the other end of appl_init, which lets the daemon forget about us */
void aes_appl_finished()
{
    aes_client_close();
}

uint32_t AES_appl_exit()
{
    FUNC_TRACE_ENTER

    /* Leaving without having arrived */
    if (ap_id < 0)
        return AES_ERROR;

    ap_id = -1;

    return AES_E_OK;
}
