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

#include "aesclient.h"
#include "aesproto.h"
#include "gem_p.h"
#include "tossystem.h"
#include "emuvdi/emuvdi.h"
#include "m68k.h"

/* The identifier of the running application, or -1 before it has asked for
 * one. An application that has not called appl_init has no business calling
 * anything else. */
static int16_t ap_id = -1;

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

    FUNC_TRACE_ENTER

    /*
     * The AES draws through a workstation of its own, which it opens here
     * rather than when GEM starts: an application that only ever calls the VDI
     * has no use for it, and opening one costs a screen's worth of state.
     */
    if (!gem_start())
        return AES_ERROR;

    /* Calling appl_init twice is not an error worth failing over, and an
     * application that does it means to carry on with the identifier it
     * already has rather than to be given a second one. */
    if (ap_id < 0)
    {
        emuvdi_aes_init();

        /*
         * Who this application is, which the daemon says when there is one and
         * is nought when there is not.
         *
         * The AES numbers them as it starts them, from nothing upwards, so on
         * a machine running one program at a time that program is nought
         * either way. That is not a detail: a GEM program written for such a
         * machine tests whether appl_init answered nought and stops if it did
         * not, so answering one would have it give up before drawing anything.
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

    found = aes_client_find(name);

    FUNC_TRACE_ARGS {
        printf("    application %d\n", found);
    }

    /* Not an error to report: not being there is an answer */
    return (uint32_t)(uint16_t)found;
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
