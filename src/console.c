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
 * The console, on a screen or on a terminal.
 *
 * See console.h for what it is for. This file is the two back ends and the
 * decision between them, and nothing above it knows which one is running.
 *
 * The screen one is EmuTOS's VT52 - bios/vt52.c, compiled out of the submodule
 * - drawing through emuvdi/conout.c into a surface of the console's own. It is
 * a surface rather than the screen itself for the same reason a dialog's is:
 * what the console draws would otherwise appear inside every window showing
 * that part of the screen. See gem_dialog_begin.
 *
 * The terminal one is stdout and stdin. The only thing it has to do carefully
 * is the tty: a program waiting for a keypress wants the key when it is
 * pressed, and a terminal in its usual state hands nothing over until the line
 * is finished. So the first time anything reads the console it goes into
 * cbreak, and it is put back at the end of the run.
 */

#include "console.h"

#include <errno.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#include "config.h"
#include "gem_p.h"
#include "gfx.h"
#include "settings.h"
#include "surface.h"
#include "tossystem.h"
#include "emuvdi/emuvdi.h"

/*
 * How long the console may go without being put on the screen.
 *
 * Drawing a character is cheap and showing a window is not - every pixel of it
 * is turned into a colour and handed to the compositor - so a page of output
 * shown a character at a time would be a thousand of those. This is how stale
 * what can be seen is allowed to get, and it is short enough that output looks
 * continuous and long enough that a page costs a handful of them rather than
 * one each.
 *
 * Anything that waits shows what is there first, whatever this says, so a
 * program that writes a prompt and waits never shows the prompt late.
 */
#define STALE_MS (20)

static struct {
    /* Which of the two this is, once it has been decided */
    int decided;
    int on_screen;

    /* The screen console */
    struct surface *shows;
    int up;                     /* and there is something on it to look at */
    int windowed;               /* and a window of the desktop's showing it */
    unsigned long seen;         /* characters drawn when it last went away */
    long shown_at;              /* when the window last caught up */
    int stale;

    /* The terminal console */
    int raw;                    /* the tty has been put into cbreak */
    int ended;                  /* and has nothing more to give */
    int have;                   /* one key read ahead - see terminal_fill */
    unsigned char ahead;
    struct termios was;
} c;

/* Milliseconds since some fixed point, which is all staleness needs */
static long now_ms(void)
{
    struct timespec t;

    clock_gettime(CLOCK_MONOTONIC, &t);

    return (long)t.tv_sec * 1000 + t.tv_nsec / 1000000;
}

/* The terminal *************************************************************/

static void terminal_restore(void)
{
    if (!c.raw)
        return;

    tcsetattr(STDIN_FILENO, TCSANOW, &c.was);
    c.raw = 0;
}

/*
 * The terminal, arranged so that a key can be read as a key.
 *
 * A tty in its usual state collects a line and hands it over when Return is
 * pressed, which is line editing done by the kernel on the shell's behalf. A
 * program asking for one keypress wants none of that: it wants the key, and
 * asking whether one is waiting has to be able to answer yes before the line
 * is finished. So ICANON goes, and with it ECHO, because what is echoed is
 * this console's business - Cconin shows what was typed and Cnecin does not.
 *
 * ISIG stays, so Ctrl-C still stops the emulator. That matters more here than
 * anywhere else: a program polling the console for a key is the one place a
 * person is most likely to want out.
 *
 * Done once and undone at the end rather than around each read. A poll is a
 * poll because it happens millions of times - see the note in gemdoscon.c
 * about Crawio - and two tcsetattr calls apiece is not a price to pay for it.
 */
static void terminal_raw(void)
{
    struct termios now;

    if (c.raw)
        return;

    if (tcgetattr(STDIN_FILENO, &c.was) != 0)
        return;         /* not a terminal, so there is nothing to arrange */

    now = c.was;
    now.c_lflag &= ~(ICANON | ECHO);
    now.c_cc[VMIN] = 1;
    now.c_cc[VTIME] = 0;

    if (tcsetattr(STDIN_FILENO, TCSANOW, &now) != 0)
        return;

    c.raw = 1;
}

/*
 * Whether there is a key, and one read ahead if there is.
 *
 * Reading ahead is what tells a key from the end of the input. A pipe or a
 * file that has run out is always ready to be read and always reads nothing,
 * so asking whether something is there answers yes for ever and reading it
 * hands over nothing for ever - which is how a wait for a keypress turned into
 * a run that never ended, and how the end of a file turned into a character
 * nobody typed. The only way to know which it is is to take it.
 *
 * The end is then remembered rather than found out again, because a program
 * polling for a key polls millions of times and every one of them would be a
 * read that answers the same thing.
 */
static int terminal_fill(void)
{
    struct pollfd waiting;
    unsigned char ch;
    ssize_t n;

    if (c.have)
        return 1;

    if (c.ended)
        return 0;

    terminal_raw();

    waiting.fd = STDIN_FILENO;
    waiting.events = POLLIN;
    waiting.revents = 0;

    if (poll(&waiting, 1, 0) <= 0)
        return 0;

    n = read(STDIN_FILENO, &ch, 1);

    if (n == 1)
    {
        c.have = 1;
        c.ahead = ch;
        return 1;
    }

    /* A signal arriving in the middle of a read is not the end of the input,
     * so leave it to whoever asks next */
    if (n < 0 && errno == EINTR)
        return 0;

    c.ended = 1;

    return 0;
}

static int terminal_ready(void)
{
    return terminal_fill();
}

/* One byte from the terminal, or -1 when there is nothing and none was to be
 * waited for */
static int terminal_byte(int wait)
{
    for (;;)
    {
        struct pollfd waiting;

        if (terminal_fill())
        {
            c.have = 0;
            return c.ahead;
        }

        if (!wait || c.ended)
            return -1;

        waiting.fd = STDIN_FILENO;
        waiting.events = POLLIN;
        waiting.revents = 0;

        poll(&waiting, 1, -1);
    }
}

/* The screen ***************************************************************/

/*
 * Somewhere to draw and, if there is a compositor, somewhere to show it.
 *
 * The surface is the size of the screen, so the console is as many characters
 * across as an ST's was on the same screen - eighty by twenty five in the high
 * resolution GEM was written for. It is not the screen itself: see the note at
 * the top of the file.
 */
static int screen_start(void)
{
    struct surface *screen;

    if (c.shows)
        return 1;

    if (!gem_start())
        return 0;

    screen = gem_screen_surface();
    if (!screen)
        return 0;

    c.shows = surface_create(surface_width(screen), surface_height(screen),
                             surface_planes(screen));
    if (!c.shows)
        return 0;

    /* The VT52 readies itself against whatever is selected, which is where it
     * takes the shape of the grid from */
    surface_select(c.shows);
    emuvdi_console_init();
    surface_select(screen);

    return 1;
}

/*
 * Everything the console has drawn, on the desktop.
 *
 * The window is opened here rather than when the console was written to,
 * because a program that writes an escape sequence and nothing else has not
 * said anything: GenST sets the wrap mode at startup and goes on being an
 * editor. So the window waits until a character has actually appeared, which
 * is what emuvdi_console_written counts, or until something asks for a key -
 * because a person cannot press one at a window that is not there.
 *
 * Counted since the console last went away rather than since the run started,
 * so a program that comes back to GEM and then writes another escape sequence
 * does not get the window back for it.
 */
static void screen_show(int wanted)
{
    if (!c.shows)
        return;

    if (!c.up)
    {
        if (!wanted && emuvdi_console_written() == c.seen)
            return;

        c.up = 1;
    }

    /* A window for it when there is a desktop to put one on. Without one the
     * console is still up, still drawn and still read from - it is only
     * unseen, which is the same arrangement the screen itself has */
    if (gfx_showing() && !c.windowed)
    {
        gfx_console_open(c.shows, surface_width(c.shows),
                         surface_height(c.shows));
        c.windowed = 1;
    }

    gfx_present();

    /*
     * And into a file, for whoever is watching without a desktop.
     *
     * gem_present does the same for the screen and would do it for the console
     * as well, but only when the application stops to wait for GEM - and a
     * program that has dropped to the console is not waiting for GEM, so for
     * the case this is most wanted in it would never happen.
     */
    {
        const char *shot = setting("TOSEMU_SCREENSHOT");

        if (shot)
            surface_write_ppm(c.shows, shot);
    }

    c.shown_at = now_ms();
    c.stale = 0;
}

static void screen_out(int ch)
{
    /* Borrowed and given back, the way a printer workstation borrows it - see
     * emuvdi/prndev.c. Everything the VDI draws goes to whatever was selected
     * last, and the application is entitled to go on drawing where it was */
    struct surface *was = surface_selected();

    surface_select(c.shows);
    emuvdi_console_out(ch);
    surface_select(was ? was : gem_screen_surface());

    c.stale = 1;

    /* Caught up with often enough to look continuous, rather than once a
     * character - see STALE_MS */
    if (now_ms() - c.shown_at >= STALE_MS)
        screen_show(0);
}

/* Which console this is *****************************************************/

/*
 * On the screen for a GEM program, and on the terminal for one a person ran
 * from a shell.
 *
 * Two questions, and both have to be yes. There has to be a desktop to put a
 * console window on, which rules out a test, a build server and a machine with
 * nobody logged in. And the program has to be one that came out of GEM, which
 * is what gem_ever_started answers: a GEM application that drops to the
 * console has taken the screen over and the person is looking at the screen,
 * where a .TTP somebody typed the name of is talking to the shell they typed
 * it in and should go on doing so.
 *
 * That second half is what makes the two cases this has to serve both work.
 * GenST assembles by running its assembler as a child, and the child never
 * calls GEM in its life - but it was started by an editor that has the screen,
 * so its output belongs there. The same assembler run from a shell as GEN.TTP
 * belongs in the shell, and its output is something a Makefile reads.
 *
 * It is decided once and stands for the run, because a program whose output
 * moved halfway through would be worse than either. TOSEMU_CONSOLE says it
 * outright: a screen console can be asked for on a machine with no compositor,
 * where it is drawn and seen only in a screenshot, and the terminal can be
 * asked for by somebody redirecting a GEM assembler's listing into a file.
 */
static int console_wanted_on_screen(void);

/*
 * The end of the run, however it comes.
 *
 * An application finishes by calling Pterm, which is exit and not a return
 * from anything - so there is no place in the emulator where a program is
 * known to have stopped, and this is registered rather than called. Two things
 * have to happen there: the last moment's worth of output goes on the screen,
 * which is what STALE_MS leaves outstanding, and the terminal goes back to how
 * it was found.
 */
static void console_finish(void)
{
    if (c.stale)
        screen_show(0);

    terminal_restore();
}

static int console_wanted_on_screen(void)
{
    const char *want = setting("TOSEMU_CONSOLE");

    if (!want)
        return gfx_possible() && gem_ever_started();

    if (strcmp(want, "screen") == 0)
        return 1;

    if (strcmp(want, "terminal") == 0)
        return 0;

    printf("tosemu: console = %s, which is neither screen nor terminal, "
           "so the terminal it is\n", want);

    return 0;
}

static void decide(void)
{
    /*
     * Outside the state below, because a child of fork inherits both and only
     * one of them is forgotten. What atexit was told stays told across a fork,
     * so saying it twice would run the ending twice.
     */
    static int registered;

    if (c.decided)
        return;

    c.decided = 1;

    if (!registered)
    {
        registered = 1;
        atexit(console_finish);
    }

    c.on_screen = console_wanted_on_screen();

    /* Asking for a screen console and finding no room for one is worth saying,
     * because what happens instead is that the output appears somewhere the
     * person was not looking */
    if (c.on_screen && !screen_start())
    {
        printf("Console: there was no room for a console screen, so the "
               "terminal it is\n");
        c.on_screen = 0;
    }
}

/* What a console is for ****************************************************/

void console_out(int ch)
{
    decide();

    if (c.on_screen)
        screen_out(ch & 0xff);
    else
        putchar(ch & 0xff);
}

/*
 * A key from whichever console this is.
 *
 * The screen one reads the window's keyboard, which is the same queue the AES
 * takes its keys off - a console window is a window, and a key pressed in it
 * arrives the way any other does. The terminal one reads stdin.
 */
static uint32_t screen_key(int wait)
{
    for (;;)
    {
        uint16_t key;
        struct pollfd waiting;
        int fd;

        /* What is on the console before a key is asked for, so that a prompt
         * is on the screen before anybody is expected to answer it */
        if (c.stale || (wait && !c.up))
            screen_show(wait);

        gfx_dispatch_ready();

        if (gfx_key_take(&key))
            return ((uint32_t)(key >> 8) << 16) | (key & 0xff);

        if (!wait)
            return 0;

        fd = gfx_fd();

        /*
         * Waiting for a key with nothing that could ever deliver one.
         *
         * The same dead end evnt_multi reaches when an application waits for
         * the keyboard with no window for one to arrive at, and it ends the
         * same way. A console read does not return until it has a key, so
         * there is no answer to give and nothing further to try.
         */
        if (fd < 0)
        {
            printf("Console: a program is waiting for a key and there is no "
                   "window for one to arrive at.\n"
                   "Run it where there is a compositor, or give it something "
                   "to work with through TOSEMU_KEYS.\n");
            fflush(stdout);

            halt_execution();
            exit(1);
        }

        waiting.fd = fd;
        waiting.events = POLLIN;
        waiting.revents = 0;

        poll(&waiting, 1, -1);

        gfx_dispatch();
    }
}

static uint32_t terminal_key(int wait)
{
    int ch = terminal_byte(wait);

    if (ch >= 0)
        return (uint32_t)ch;

    /*
     * Nothing, and nothing that could ever arrive: the input has ended and a
     * program that will not go on without a key would sit here for ever. Said
     * and stopped, for the reason evnt_multi says its version.
     */
    if (wait && c.ended)
    {
        printf("Console: a program is waiting for a key and the input has "
               "ended, so no key can arrive.\n"
               "Give it something on standard input, or run it where there "
               "is a compositor to type at.\n");
        fflush(stdout);

        halt_execution();
        exit(1);
    }

    return 0;
}

uint32_t console_key(int wait)
{
    decide();

    /* Whatever was written is on the screen before anybody is asked to answer
     * it, which for the terminal means out of the buffer it is sitting in */
    if (!c.on_screen)
        fflush(stdout);

    return c.on_screen ? screen_key(wait) : terminal_key(wait);
}

int console_ready(void)
{
    decide();

    if (!c.on_screen)
        return terminal_ready();

    if (c.stale)
        screen_show(0);

    gfx_dispatch_ready();

    return gfx_key_ready();
}

/*
 * A line, with the editing GEMDOS did for Cconrs.
 *
 * On an ST this was the BDOS reading keys one at a time and drawing what it
 * read, which is why a program that wanted the line editing got it on the
 * screen the console was on. Here it is the same loop for the same reason:
 * neither console has a kernel behind it doing this, the terminal least of
 * all now that it is in cbreak.
 *
 * Backspace rubs a character out, Return ends the line, and Ctrl-U throws the
 * line away - which is what an ST did with Ctrl-X, but a person on a terminal
 * has spent thirty years pressing Ctrl-U and both are free.
 */
int console_line(char *buffer, int max)
{
    int length = 0;

    if (max <= 0)
        return 0;

    for (;;)
    {
        uint32_t key = console_key(1);
        int ch = key & 0xff;

        if (ch == '\r' || ch == '\n')
        {
            /* The line ends on the screen as well, or whatever is written
             * next carries on along the same row */
            console_out('\r');
            console_out('\n');
            break;
        }

        if (ch == '\b' || ch == 0x7f)
        {
            if (length > 0)
            {
                length--;
                console_out('\b');
                console_out(' ');
                console_out('\b');
            }
            continue;
        }

        if (ch == 0x15)         /* Ctrl-U */
        {
            while (length > 0)
            {
                length--;
                console_out('\b');
                console_out(' ');
                console_out('\b');
            }
            continue;
        }

        /* Anything else that is not a character is a key with no character -
         * an arrow, a function key - and there is nothing to put in a line */
        if (ch < ' ')
            continue;

        if (length >= max)
            continue;

        buffer[length++] = (char)ch;
        console_out(ch);
    }

    return length;
}

int16_t console_cursor(int16_t function, int16_t operand)
{
    decide();

    /*
     * The terminal has a cursor of its own and it is not this one. Turning it
     * off would be reaching into somebody's terminal on behalf of a program
     * that meant its own screen, so the blink rate is remembered - which is
     * the one thing Cursconf can be asked as well as told - and the rest is
     * accepted and discarded.
     */
    if (!c.on_screen)
    {
        static int16_t rate = 30;

        if (function == 4)
            rate = operand;

        return (function == 5) ? rate : 0;
    }

    return emuvdi_console_cursor(function, operand);
}

struct surface *console_showing(void)
{
    return c.up ? c.shows : 0;
}

void console_settle(void)
{
    if (!c.up)
        return;

    /*
     * The application is waiting for GEM again, so it has finished with the
     * console. On an ST this is where its own redraw covered the text over.
     *
     * What is drawn is left where it is rather than cleared, so a program that
     * goes back to the console carries on where it left off - which is what an
     * ST did, the text being on a screen nobody had erased.
     */
    if (c.windowed)
        gfx_console_close();

    c.windowed = 0;
    c.up = 0;
    c.seen = emuvdi_console_written();
}

void console_forget(void)
{
    /*
     * A child of fork has a copy of the parent's console and owns none of it.
     * The window belongs to the parent and gfx_forget has already let go of
     * the connection it was on, so there is nothing here to close - only the
     * copy of the surface to give back, and the decision to make again, this
     * being a different program from now on.
     */
    if (c.shows)
        surface_free(c.shows);

    c.shows = 0;
    c.up = 0;
    c.windowed = 0;
    c.seen = 0;
    c.shown_at = 0;
    c.stale = 0;

    c.decided = 0;
    c.on_screen = 0;

    /*
     * The terminal is not forgotten with the rest. It belongs to the process
     * rather than to the program, and this process shares it with the one that
     * forked - so what it looked like before anybody touched it is still what
     * it has to be put back to, and an input that has ended has ended for both.
     */
}

void console_close(void)
{
    console_finish();

    if (c.windowed)
        gfx_console_close();

    if (c.shows)
        surface_free(c.shows);

    memset(&c, 0, sizeof c);
}
