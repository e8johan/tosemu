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
 * The file selector, which is how a GEM application opens a file.
 *
 * One call does all of it. The AES puts up the box, lists the directory, lets
 * you walk into folders and change drives, and answers with a path and a name.
 * An application does not draw any of it and does not read any directories: it
 * hands over two buffers and reads them afterwards.
 *
 * The buffers are the interesting part and the reason this shows the selector
 * twice. The path goes in as well as coming out, so an application that keeps
 * it between calls opens the second time where the person left off the first -
 * which is the whole of what "remembers the last directory" meant in 1988, and
 * is done by passing the same buffer again rather than by anything the AES
 * remembers.
 *
 * The second call is the other one, fsel_exinput, which is the same thing with
 * a line of the application's own words at the top. A program asking where to
 * save something says so, rather than showing the same box it showed for
 * opening.
 */

#include <gem.h>
#include <stdio.h>
#include <string.h>

static short control[5], global[15], intin[16], intout[7];
static long addrin[3], addrout[1];
static AESPB pb = { control, global, intin, intout, addrin, addrout };

static short call_aes(short op, short ni, short no, short ai, short ao)
{
    control[0] = op; control[1] = ni; control[2] = no;
    control[3] = ai; control[4] = ao;
    intout[0] = -1;
    aes(&pb);
    return intout[0];
}

/*
 * A path and a name, kept between the two calls.
 *
 * The path holds a directory and the pattern of what to show in it, which is
 * why it starts as a directory with *.* on the end rather than as a directory.
 * The name is what was chosen, and starts empty because nothing has been.
 */
static char path[128] = "C:\\*.*";
static char name[16] = "";

/* Says what came back, which is two things: whether anything was chosen at
 * all, and what it was */
static void report(const char *which, short worked, short chosen)
{
    if (!worked)
    {
        printf("%s: the AES would not put the selector up\n", which);
        return;
    }

    if (!chosen)
    {
        printf("%s: cancelled, and the path is still %s\n", which, path);
        return;
    }

    if (!name[0])
    {
        printf("%s: chose the directory %s, with no file in it\n", which, path);
        return;
    }

    printf("%s: chose %s in %s\n", which, name, path);
}

int main(int argc, char **argv)
{
    short wchar, hchar, wbox, hbox;
    short work_in[11], work_out[57];
    short handle;
    short worked;
    int i;

    appl_init();

    handle = graf_handle(&wchar, &hchar, &wbox, &hbox);
    for (i = 0; i < 10; i++)
        work_in[i] = 1;
    work_in[10] = 2;
    v_opnvwk(work_in, &handle, work_out);

    /* fsel_input: the path and the name, and nothing else */
    addrin[0] = (long)path;
    addrin[1] = (long)name;
    worked = call_aes(90, 0, 2, 2, 0);
    report("open", worked, intout[1]);

    /*
     * And again, with something to say. The same buffers go back in, so this
     * one opens wherever the last one was left - walk into a folder in the
     * first and the second starts there.
     */
    addrin[0] = (long)path;
    addrin[1] = (long)name;
    addrin[2] = (long)"Where should it be saved?";
    worked = call_aes(91, 0, 2, 3, 0);
    report("save", worked, intout[1]);

    v_clsvwk(handle);
    appl_exit();

    return 0;
}
