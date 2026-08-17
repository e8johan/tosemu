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

/* The GEM trap and its parameter blocks.
 *
 * GEMDOS, BIOS and XBIOS take their arguments on the stack. GEM does not, so
 * the way arguments arrive is worth a test of its own before anything is built
 * on top of it. The first half of this builds the parameter block by hand,
 * which is both the check and the documentation of what the trap expects. The
 * second half goes through the bindings, because that is how an application
 * will actually arrive.
 *
 * A test that stops the emulator prints nothing further, so the count at the
 * end is what says the whole file ran rather than only the part before an
 * unimplemented call.
 */

#include <stdio.h>
#include <gem.h>

/* What appl_init reports for an AES that has only just been started */
#define WANT_VERSION (0x0140)
#define WANT_APPS    (1)

static int n;

static void check(long got, long want, const char *name)
{
    n++;
    if (got == want)
        printf("ok %d - %s\n", n, name);
    else
        printf("not ok %d - %s (got %ld, want %ld)\n", n, name, got, want);
}

/* The six arrays of an AES parameter block, at the sizes the AES bindings
 * reserve for them */
static short control[5];
static short global[15];
static short intin[16];
static short intout[7];
static long addrin[3];
static long addrout[1];

static AESPB pb = { control, global, intin, intout, addrin, addrout };

/*
 * An AES call as the trap sees it: the function number and the length of each
 * argument array go in the control array, and the answer comes back in
 * intout[0].
 */
static short call_aes(short opcode, short n_intin, short n_intout,
                      short n_addrin, short n_addrout)
{
    control[0] = opcode;
    control[1] = n_intin;
    control[2] = n_intout;
    control[3] = n_addrin;
    control[4] = n_addrout;

    intout[0] = -1; /* So that an answer is told apart from no answer */

    aes(&pb);

    return intout[0];
}

int main(int argc, char **argv)
{
    short id;
    int i;

    /* An identifier that has not been handed out yet, so that the array being
     * written is told apart from it happening to hold the right thing */
    for (i = 0; i < 15; i++)
        global[i] = -2;

    /* 10 appl_init, by hand */
    id = call_aes(10, 0, 1, 0, 0);
    check(id > 0, 1, "appl_init answers in intout with an identifier");
    check(global[0], WANT_VERSION, "appl_init reports the AES version");
    check(global[1], WANT_APPS, "appl_init reports how many applications fit");
    check(global[2], id, "appl_init puts the identifier in the global array");
    check(global[5], 0, "appl_init clears the resource tree pointer");
    check(global[6], 0, "appl_init clears the resource tree pointer");

    /* An AES of this version does not own the last two words of the global
     * array, so an application built against an older binding does not have
     * them written over */
    check(global[13], -2, "appl_init leaves the AES 4 globals alone");
    check(global[14], -2, "appl_init leaves the AES 4 globals alone");

    /* 19 appl_exit, by hand */
    check(call_aes(19, 0, 1, 0, 0), 1, "appl_exit answers success");

    /* 17 appl_yield - stubbed, because an application yielding when it is the
     * only one has already had its turn */
    check(call_aes(10, 0, 1, 0, 0) > 0, 1, "appl_init again for appl_yield");
    check(call_aes(17, 0, 1, 0, 0), 1, "appl_yield answers success");
    check(call_aes(19, 0, 1, 0, 0), 1, "appl_exit after appl_yield");

    /* And now the same thing the way an application does it */
    id = appl_init();
    check(id > 0, 1, "appl_init through the binding gives an identifier");
    check(gl_ap_version, WANT_VERSION, "the binding sees the AES version");
    check(_AESapid, id, "the binding sees its own identifier");
    check(appl_exit(), 1, "appl_exit through the binding succeeds");

    /* The VDI has its own parameter block, a different shape from the AES one.
     * None of it is implemented yet, but v_updwk is a call with nothing to do
     * rather than a call that is missing, so it has to come back. Reaching the
     * count below is what proves it did. */
    v_updwk(1);
    check(1, 1, "v_updwk returns through the VDI trap");

    printf("1..%d\n", n);

    return 0;
}
