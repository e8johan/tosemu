Building
========

A simple `make` should do it. The resulting binary can be found in the bin
directory.

That builds with `-O2`, which matters more here than it does for most
programs: nearly all of the work is Musashi reading one 68000 instruction after
another, so a build without it runs the emulated machine at half speed.
Assembling twenty four thousand lines with Devpac's Gen inside the emulator
takes seventy three seconds optimised and a hundred and forty seven without,
and what comes out is the same file either way.

For a build to step through, `make OPT="-O0 -g"` replaces the flag. What must
not be dropped from it is `-fno-strict-aliasing`: the VDI and the AES here are
EmuTOS's, and they read one type through a pointer to another as a matter of
course - see the note beside `OPT` in the Makefile, which says where.

The tree is arranged so that nothing built is ever next to what it was built
from. Everything somebody wrote is under `src/`, `demos/` and `tests/`;
everything the build makes goes under `build/` - the objects mirroring the
source tree in `build/obj/`, the generated sources in `build/gen/`, and the
programs run under the emulator in `build/tests/` and `build/demos/` - and the
programs that come out land in `bin/`. So a source directory listing is what
was written and nothing else, and `make clean` is the deletion of two
directories rather than a hunt.

The VDI comes from EmuTOS rather than being written again, so the tree is no
longer self contained: it carries EmuTOS as a submodule in `3rdparty/emutos`.
Clone with

    git clone --recurse-submodules <url>

or, in a tree that is already checked out,

    git submodule update --init

The submodule is never edited. Everything that adapts EmuTOS to a hosted build
lives in `src/emuvdi/`, which has a README of its own explaining how. The
`make emuvdi-check` target draws with the ported VDI and compares the result
against what it should have drawn; unlike the other tests it builds for the
host rather than for the emulated machine, because what it is checking is the
port.

A few of the files it is built from are generated rather than carried: the
AES's resource and the mouse forms are kept as a `.rsc` and a `.def` and turned
into C when EmuTOS builds, and the localisation settings come out as a header.
A fresh checkout has the inputs and none of the outputs, so the build makes
them by asking EmuTOS's own Makefile for them, with the host compiler and no
cross tools. EmuTOS ignores all of them in its `.gitignore`, so the submodule
stays as clean as it was found.

The emulated screen is never shown. It is a coordinate space and a piece of
memory: GEM lays its windows out in it and draws into it the way it always did.
What appears on the desktop are the windows themselves - one for each window a
GEM application opens, one for each dialog, one for the menu bar and one for
each menu pulled down from it - scaled up by a whole number so that an ST pixel
stays a square rather than being smoothed into a modern one. `TOSEMU_SCALE`
says by how much, and three is the default: a 640x400 screen becomes 1920x1200.
A GEM application is meant to be part of the desktop it runs on rather than a
picture of another computer. Set `TOSEMU_NO_WINDOW` to
keep the screen in memory, which is what the tests do: the emulator runs the
same either way, and the variable only decides whether anyone can see it.

Showing them needs Wayland, which is a build dependency rather than a run time
one: `wayland-client`, `xkbcommon` and `wayland-scanner` have to be there to
compile it at all, even on a machine where nobody will ever be logged in to see
a window. `make NO_WAYLAND=1` builds without them. What goes is the half of
`gfx.c` that opens windows and the question `screen.c` asks about how large the
display is, and what is left answers what the ordinary build already answers
when there is no compositor to connect to - so the emulator takes the same path
through the AES, the screen is in memory as it always was, and `make check`
passes exactly as it does otherwise. It is meant for a build server, which is
what CI runs on.

A GEM window wears its own frame. The title bar, the close box, the full box
and the size box are the ones the AES draws, and they do what a desktop's do:
the title bar drags the window and its other button brings up the desktop's
window menu, the close box asks the application to close - which is what GEM's
always did, and is why an application can ask whether you meant it - the full
box makes the window as large as the screen the AES lays windows out on, and
the size box runs the desktop's own resize drag. The desktop is asked to put
nothing round the outside, because a title bar inside a title bar is the
picture of another computer again. Say `TOSEMU_DECORATIONS=desktop` to have it
the other way round: the desktop's frame, and GEM's title bar left out of what
is shown.

Every window carries EmuTOS's generic application icon, which is what a task
bar or a window switcher shows beside its name. It is read out of the icon
resource EmuTOS ships and made larger by repeating its pixels, so what appears
in the switcher is a thirty two pixel ST icon at whatever size the desktop asked
for rather than a smoothed one. A desktop too old to have heard of window icons
shows what it showed before, which is its own placeholder. See `src/rsc/README`.

A dialog has no frame of its own to wear, so it asks the desktop for one. Not
every desktop draws them: GNOME draws no frames at all round a Wayland window
and has said it will not, which used to leave a dialog with nothing to move it
by and nothing to close it with. Where the desktop draws none, one is drawn here
instead - a GEM title bar, by the code that draws the ones on windows, so what
appears is the bar of a GEM window and not an imitation of somebody else's
desktop. `TOSEMU_DECORATIONS=gem` says to draw them that way everywhere without
asking the desktop first.

The menu bar never asks. A title bar above it would say the name of an
application whose name is already the first word on the bar, and it would put a
second strip above a strip that is one row tall by definition - so the bar wears
the smallest frame that lets it be moved and resized, on the end of the row
rather than above it: a handle where the titles have run out, hatched like the
title bar of a window in front when it is the bar you are working in, and a size
box past it at the very edge. The size box changes the width and nothing else,
because how tall a menu bar is is not a matter of taste, and what changing the
width does is show more or less of the bar - it stops at the titles, there being
no way to reach a menu whose title is off the end of the window.
`TOSEMU_DECORATIONS=desktop` gives it the desktop's frame like everything else.

A resize drag shows a rubber band, which is what an ST did: the window becomes
the size the drag has reached and what is in it is an outline of alternate black
and white pixels with the desktop showing through. The application hears nothing
until the button comes up, and is then sent GEM's own two messages - WM_SIZED,
saying what rectangle to take, and WM_REDRAW, saying to paint what is now inside
it - so it redraws its document once rather than on every frame of the drag,
which on a 68000 is the difference between a resize and a wait. The feedback is
the emulator's to give and the drawing is the application's, which is the
division the rubber band was invented for.

Which screen it is comes from `TOSEMU_SCREEN`. The ST's three are `low`,
320x200 in sixteen colours, `medium`, 640x200 in four, and `high`, 640x400 in
two; the TT's are `tt-medium`, 640x480 in sixteen, and `tt-high`, 1280x960 in
two. `high` is the default because that is what GEM applications were written
for. This is not a matter of taste: a resource is laid out in characters, and
how many fit across the screen is what decides whether a dialog fits on it at
all - an ordinary dialog is more than forty characters wide, so on the low
resolution screen the AES centres it at a negative coordinate and it hangs off
both edges with its side borders out of sight. Low resolution is useful for the
colours and not for much else.

The TT's screens are larger rather than different: everything is laid out in
characters of the same size, so an application gets more room rather than a
bigger picture. `tt-high` is 1280x960, which at the default scale would be a
window larger than most displays - set `TOSEMU_SCALE=1` with it.

Four more are a rule rather than a size: `native-mono` and `native-color` are
as large as a window may be, in one plane and in four, and `display-mono` and
`display-color` are as large as the display is. They are not Atari screens and
are not pretending to be. What they are for is a GEM application having the
room a modern display has, which is the one thing the machine could not give it
- and the rest of GEM does not mind, because a resource is measured in
characters and more of them across is simply more room.

The difference between the two pairs is the panel. A desktop keeps some of its
display for itself - a bar across the bottom, a dock down one side - and a
screen worked out from the whole display is a screen whose bottom right corner
is behind that bar, which is exactly where a GEM window keeps its size box. So
`native-mono` and `native-color` ask the compositor how large a window it is
willing to give, which is what maximising one answers and is the panel
subtracted by the only party that knows about it. `display-mono` and
`display-color` measure the display itself, panel or no panel, which is what
the first pair meant before there was any way to tell the difference.

How large either of them is comes out of two divisions. The compositor's own
scaling is one - a display reporting 3456x2160 at a scale of two shows a window
in half of those - and `TOSEMU_SCALE` is the other, being how many of those an
ST pixel becomes. So a 3456x2160 display at scale two gives a 576x360 screen at
the default `TOSEMU_SCALE=3`, and that screen magnified by three is 1728x1080,
which is the display. They are one setting rather than two for exactly that
reason. The width is rounded down to a multiple of sixteen, because a row of a
surface is a whole number of words, so the window can come out a little short of
the display's width and never over it.

`TOSEMU_OUTPUT` says which display to measure, by the name the compositor gives
it - `eDP-1`, `DP-1` and so on, which `wayland-info` will list. Without it, the
first one the compositor mentions, and for the two that maximise, whichever
display the compositor puts a new window on - which is not always the same one,
and is the display the emulator's own windows are about to appear on either
way. A maximised window cannot be asked for on a display of one's choosing:
xdg-shell has that for fullscreen and nothing of the kind for maximising. So
when a display is named, what carries across is how much room the desktop took
rather than the size it left - the same panel is on every display of a desk
that has one - and that is taken off the display that was asked for.

With no compositor to ask at all the size falls back to 640x400 and the planes
stay as asked, which is what happens on a machine with no desktop and in the
test suite. A display named and not there - a monitor since unplugged, most
likely - falls back the same way and says which displays it did find, rather
than quietly measuring a different one. So does a compositor that does not
answer the second question within a quarter of a second, or has no xdg-shell to
answer it with: `native-*` is then `display-*`, which is a panel in the way and
not a screen that failed to appear.

Asking costs two round trips at the moment the machine is decided, and no
window. `wl_output` is a global like any other and says how large it is without
anything being shown; the maximised size needs a surface, but that surface is
committed with nothing attached to it, which is a question rather than a
window, and it is destroyed as soon as the answer arrives. When `tosaesd` is
running it is the one that asks, because it is the one that decides - so set
these on the daemon, not on each application.

When `tosaesd` is running it is what decides, and the variable is read from its
environment rather than from each application's. The screen has to be one
screen for everything sharing it - applications lay their windows out in it and
are told where the others put theirs - so it belongs to the session, the way a
resolution belonged to a machine rather than to a program running on it.

`TOSEMU_MEMORY` says how much memory the machine has. The sizes the Ataris were
sold in are `512k`, a 520ST; `1m`, a 1040ST and a Falcon as it came; `2m`, a
Mega ST 2 and a TT's ST RAM; `4m`, a Mega ST 4 and the most an STE takes; and
`14m`, the most a Falcon takes. `max` is the default and is not a machine: it
is as much as the memory map has room for, which is everything below the
cartridge range, and comes to a little under sixteen megabytes. A number with a
`k` or an `m` after it - `640k`, `6m` - is a size as well, because the machines
are the sizes worth naming rather than the only ones worth having.

More memory than any Atari had is not something a program goes wrong on, which
is why `max` is the default. Asking for less is for the program that was
written for a particular machine: how much there is decides how large a
document or a picture can be, and what a program does when memory runs out is a
path through it that fifteen megabytes never take. The screen comes off the top
of whatever there is, the way it did on the machine, so a screen as large as a
modern display and a 520ST do not go together - the emulator says so rather
than starting a machine with no room to load anything into.

A TT's second sort of memory is not one of these. TT RAM is another area of the
map altogether and `Mxalloc` answering for it is what would make it real, so
what the TT contributes here is its ST RAM and no more.

`TOSEMU_KEYS` hands over keystrokes and `TOSEMU_CLICKS` places to click, for
when there is nobody to type or click - a test suite, mostly. Keys are
characters with `\r` for Return. Clicks are `x,y` to press and release there,
`x,y-x2,y2` to drag from one to the other, and `@x,y` to move the pointer
without pressing anything - which is not a nicety, because a GEM menu opens
when the pointer arrives among the titles rather than when it is clicked.

The keys that type nothing are written by name in braces: `\{left}`,
`\{right}`, `\{up}`, `\{down}`, `\{home}`, `\{backspace}`, `\{delete}`,
`\{insert}`, `\{tab}`, `\{escape}`, `\{return}`, `\{undo}`, `\{help}` and
`\{f1}` to `\{f10}`. Without them a setting can only add to the end of what it
has already added - nothing typed forwards moves a caret back - so an editor
cannot be driven at all, which is why a word processor's redrawing could not
be reproduced from a test.

`TOSEMU_TRACE_INPUT` says what every wait for the mouse asked for and what it
was told, and which directories the file selector read. It is there because a
wait that answers wrongly is invisible from anywhere else: two different waits
asking the same question and one wait asking it twice look identical until
something says which happened.

It also says which window the pointer arrived in and left, and whether a menu
that went away went away because the desktop took it. Those are the same
question asked of the desktop rather than of the application: a menu closing
by itself is either the AES deciding to close it or the compositor deciding
for us, and from a screen the two look identical. Which desktop is running
decides which it was, so this is the first thing to ask when a menu behaves
differently on two of them.

Set `TOSEMU_SCREENSHOT` to a path and the screen is written there as a
portable pixmap every time an application waits, which is how to look at what
was drawn from a terminal, or from a test, or without a desktop at all.


Settings
========

Everything above can be said in a file instead, which is what most of it wants:
which screen the machine has and where the drive is rooted do not change
between one program and the next, and having to remember them on every command
line is how they come to differ by accident.

`~/.tosemu` is read when it is there, `-c <file>` reads another instead, and
`--no-config` reads none at all. A file named with `-c` that cannot be read is
an error rather than a shrug; the one in the home directory is a file that may
be there rather than one that has to be.

    # Everything is in a section. A remark is a whole line and only a whole
    # line: a hash halfway along one is part of the value, because a path or a
    # list of clicks may have a hash in it and losing its tail is worse than
    # having to put the remark above.

    [screen]
    mode        = native-color
    scale       = 3
    window      = yes
    decorations = atari
    # output = DP-1

    [machine]
    memory = 4m

    [input]
    keys   = \r
    clicks = 100,50 200,60

    [console]
    output = screen

    [files]
    base = /home/me/tos

    [midi]
    device = hw:1,0,0

    [session]
    socket = /run/user/1000/tosaesd

    [debug]
    screenshot  = /tmp/screen.ppm
    trace-input = no
    trace-paths = no

Which is which:

| in the file            | in the environment   |
| ---------------------- | -------------------- |
| `[screen] mode`        | `TOSEMU_SCREEN`      |
| `[screen] scale`       | `TOSEMU_SCALE`       |
| `[screen] output`      | `TOSEMU_OUTPUT`      |
| `[screen] window`      | `TOSEMU_NO_WINDOW`, the other way round |
| `[screen] decorations` | `TOSEMU_DECORATIONS` |
| `[screen] picture`     | `TOSEMU_PICTURE`     |
| `[machine] memory`     | `TOSEMU_MEMORY`      |
| `[input] keys`         | `TOSEMU_KEYS`        |
| `[input] clicks`       | `TOSEMU_CLICKS`      |
| `[console] output`     | `TOSEMU_CONSOLE`     |
| `[files] base`         | `TOS_BASE_PATH`      |
| `[fonts] assign`       | `TOSEMU_FONTS_ASSIGN` |
| `[fonts] substitutes`  | `TOSEMU_FONTS_SUBSTITUTES` |
| `[printer] destination` | `TOSEMU_PRINTER`    |
| `[printer] paper`      | `TOSEMU_PRINTER_PAPER` |
| `[printer] resolution` | `TOSEMU_PRINTER_DPI` |
| `[printer] file`       | `TOSEMU_PRINT_FILE`  |
| `[printer] command`    | `TOSEMU_PRINT_COMMAND` |
| `[midi] device`        | `TOSEMU_MIDI`        |
| `[machine] interrupts` | `TOSEMU_INTERRUPTS`  |
| `[session] socket`     | `TOSEMU_AESD`        |
| `[debug] screenshot`   | `TOSEMU_SCREENSHOT`  |
| `[debug] trace-input`  | `TOSEMU_TRACE_INPUT` |
| `[debug] trace-paths`  | `TOSEMU_TRACE_PATHS` |

An environment variable overrides what the file says, because saying something
on a command line is saying it about that run in particular - a file that won
would leave no way to try anything without editing it first. Said twice in one
file, the later line is the one that meant it. A value keeps its spaces if it
is in double quotes, and a name that is not one of these is complained about
rather than ignored: a settings file quietly half read is worse than one
refused.

`window` is the only one that is not a rename. An environment variable is set
or it is not, with no room in that for saying no, which is why it is called
`TOSEMU_NO_WINDOW`; a file has room, and nobody should have to write
`no-window = no`. For it and the two `trace-` settings, `no`, `0`, `off` and
`false` mean no and anything else present means yes.

`tosaesd` reads the same file and takes the same two arguments. That is where
to put the settings for a session that has a daemon in it: the daemon is what
says which screen the machine has, to every application that arrives and to the
accessories it starts itself.

`bin/tosaesd` is the daemon several emulator processes have in common. It is
not needed to run one program: an application on its own has nobody to agree
with, so tosemu answers for a machine with one application in it and runs
exactly as it does without one. What the daemon adds is the part that cannot be
answered inside a single process - which application is which, what the screen
they share looks like, and messages one sends another - so `appl_find` and
`appl_write` to somebody else start working the moment it is there. It puts its
socket in `$XDG_RUNTIME_DIR`, and `TOSEMU_AESD` says somewhere else, which is
how the test suite runs its own without disturbing a session.

Give it a directory and it starts the accessories in it - anything ending in
`.ACC`, each in an emulator of its own. They put themselves into the Desk menu
of every application that runs afterwards, which is what an accessory is for
and why it needs something that outlives any one program to do it.

It also puts a single mark in the panel, with the accessories as a menu hanging
off it - not one icon each, because a person does not want six mystery icons
appearing because a GEM program is running. It is the only way to reach an
accessory when no application is running, which is the case the Desk menu
cannot cover: that menu belongs to an application, and if none is running there
is no menu to be in.

Tray icons are a de facto protocol rather than a standard - KDE wrote it, others
adopted it, GNOME needs an extension and some desktops have nothing of the sort
- so every failure there is quiet. No bus, no panel, or built without D-Bus at
all: the session still runs and the only thing lost is the icon.

The screen is the daemon's when there is one, and that is not for convenience:
it has to be one screen, because applications lay their windows out in it and
are told where the others put theirs, so two that disagree about its size
disagree about everything. Which size it is belongs there too, being what a
machine's graphics mode is - one setting for everything running on it.

Note that GEM has colour 0 as white and colour 1 as black, which is the
opposite way round from most things. Drawing colour 0 text in replace mode
fills the cell with white and draws the glyph in white too, and the result is
a solid block rather than a letter.

`make demos` builds the programs in `demos/`, which are GEM applications meant
to be looked at rather than checked - `./bin/tosemu build/demos/dialog` puts a
dialog on the screen with buttons that can be clicked. See `demos/README`.

The `make clean` target produces a clean source tree, which it does by deleting
`build/` and `bin/` - the two directories everything built is in.



Usage
=====

TOSEMU takes a single command line argument, the location of a TOS application. 
It is also possible to use TOSEMU with the binfmt support in the Linux kernel. I
 use the following line to enable this:

  `echo ':tos:M::\x60\x1a:\xff\xff:/path/to/binary/tosemu:' | sudo tee /proc/sys/fs/binfmt_misc/register`

This will allow you to execute TOS binaries as if they where native.

The application is handed the environment tosemu was started with, so a
variable an application looks for can be set from the host shell. Lattice C's
compiler for instance finds its header files through `INCLUDE`.

A TOS command line lives in the basepage and holds no more than 126 characters.
Arguments beyond that are dropped, which is all a TOS program can be handed.
Applications that need more, such as linkers, read a control file instead, and
compiler drivers use the ARGV convention: a length byte of 127 says the
arguments were passed through an `ARGV` variable in the environment instead,
which tosemu carries from one program to the next as it stands.



The console
===========

An ST's console was its own screen and keyboard, reached through the VT52 in the
BIOS, and it was the same screen GEM drew on. Programs that were both are the
rule rather than the exception for software of the period: an assembler with a
GEM editor round it writes its listing over the desktop, waits for a key and
leaves the application to redraw.

There are two places it can go here and the difference is not a preference.

On the screen, which is what a GEM program gets: a window of its own, the text
drawn with the machine's own 8x16 font, the Atari character set, and the VT52
escape sequences meaning what they meant. It is the faithful answer and it is
where the person is already looking. The window appears when a character
actually reaches it - a program that only sets the wrap mode has said nothing -
and goes when the application next waits for a GEM event, which is where an ST
redrew over the text.

On the terminal tosemu was started from, which is what a program run from a
shell gets. That is where console output has always gone, it is what a Makefile
reads and what a redirect captures, and the bytes are passed through as they
were written. The terminal goes into cbreak while the program is running so that
a keypress is a keypress rather than a line, and is put back at the end.

Which one it is comes from whether GEM has been started - by this program or by
the one that ran it, so GenST's assembler counts as a GEM program even though it
never calls GEM itself - and from whether there is a compositor to put a window
on. `TOSEMU_CONSOLE` says it outright: `screen` asks for a console screen even
with no desktop to show it on, where it is drawn and can be looked at with
`TOSEMU_SCREENSHOT`, and `terminal` asks for the terminal on a machine that has
one, which is what somebody redirecting an assembler's listing into a file
wants.

A program waiting for a key with no way for one to arrive is stopped and told
so, rather than left waiting: no window and no keyboard, or standard input that
has ended. That is the same answer `evnt_multi` gives for the same dead end.

Copy and paste
--------------

The text in a console window can be selected and copied, which is the one thing
a terminal does that the screen console would otherwise not.

Drag across it with the left button and the cells turn inside out to show what
is selected; letting go puts the text on the desktop's clipboard. A click that
went nowhere selects nothing rather than copying the character under it, and the
right button takes a selection away. The middle button pastes, the way it does
in a terminal: what the desktop is offering arrives as keys, so a program gets
it through whatever it is already using to read the keyboard.

Letting go is what copies, rather than a key combination, because every key
belongs to the program - it is sitting in a console read waiting for one, and
any this took would be one it never saw. A terminal can afford Ctrl-Shift-C
because the shell underneath it is not listening for Ctrl-Shift-C.

What is copied is the characters, not the pixels. A cell holding an A is eight
by sixteen bits that happen to look like one, so the characters are kept beside
them as they are drawn - which is also why the text a line was padded out with
does not come along, and why a selection three lines tall is three lines rather
than a hundred and fifty spaces.

None of it is a GEM cut. No application made the selection and none knows it
happened, so nothing is written to the scrap directory and no other GEM program
can paste it; it goes straight to the desktop, where a person put it.


The keyboard
============

A keypress reaches a GEM application as one word: which key, and what it typed.
Both halves are the machine's rather than the desktop's. The key is a place on
an ST keyboard, which is a number a Linux keyboard happens to agree with for
the whole main block, and the character is a byte of the Atari character set,
which is what the desktop's Unicode is turned into.

The layout is the desktop's and it decides what gets typed: a Swedish keyboard
types the ST's byte for a with a ring, and a character the ST has no byte for
types nothing rather than a question mark.

What it does not decide is the shortcuts. A menu shortcut on Alternate and a
letter arrives as the key with nothing typed, which is how TOS said a key had
been pressed rather than typed, and an application gets the letter back by
looking the key up in the keyboard table - `Keytbl`, which hands out what each
key of the machine types unshifted, shifted and with caps lock down. So that
table is built from the layout on the desktop, and the letter printed on the
key somebody pressed is the letter the application matches against its own
menu. It works on any layout for that reason rather than by accident.

Where there is no desktop to ask - a test, a terminal, a machine with nobody
logged in - the table is the American one an ST was sold with. `-v` says which
of the two a run is using.

The rest of what a modifier does is TOS's and is done as TOS did it: Shift and
a function key is F11 to F20 rather than F1 to F10 with a shift held, Control
and the left, right or Home key is a key of its own, Control and a character is
that character with its top three bits taken off, and Shift and Tab is a tab -
which is what a dialog reads to go back a field rather than on to the next one.

An ST's Undo and Help have nowhere to be on a modern keyboard, so Print Screen
and Scroll Lock stand in for them. A compositor may well want Print Screen for
itself, in which case Undo has to be reached some other way; that is the
compositor's to decide.






Programs that stay
===========
A TSR loads, installs itself into the machine, and stays there so that the next
program can use it. The AUTO folder was a list of them, and anything that added
a capability to the machine - a RAM disk, a printer spooler, a MIDI kernel -
arrived that way.

`-r` (or `--resident`) names one, and may be said more than once, in the order
they are to load:

    tosemu -r MROS/MROS3_31 CUBASE.PRG

They go into the machine before the program named last, each staying where it
is, and the program somebody wanted is loaded above them. It is one machine and
one address space, which is what makes a resident worth having: the program
that runs next can see what it installed, call it through a vector it left
behind, and read what it wrote.

**A program `Pexec`d with mode 0 cannot stay**, and this is the one place
residency has a hole in it. That child is a forked host process with a machine
of its own, so anything it keeps is kept in that machine and goes when it ends.
`Ptermres` from it says so and terminates cleanly, rather than leaving the
program that started it believing a TSR is there when it is not. What to do
about it is to name the program with `-r` instead. A program run with mode 4
or 6 is in its caller's machine, and stays there the way it would on TOS.

MIDI
====

There is a MIDI port, and it goes wherever the setting says. An application
sends the way it always did - `Bconout` on device 3 a byte at a time, or
`Midiws` for a whole message - and the bytes reach whatever is plugged into the
host.

Nothing is set up by default. Without a word said there is no port, every byte
written is discarded and nothing ever arrives, which is what an ST with nothing
in the socket did.

    [midi]
    device = hw:1,0,0

| in the file            | in the environment  |
| ---------------------- | ------------------- |
| `[midi] device`        | `TOSEMU_MIDI`       |
| `[machine] interrupts` | `TOSEMU_INTERRUPTS` |

How it is spelled picks what kind of port it is, and the prefix is not
optional:

- `hw:1,0,0` is an ALSA raw device - the interface itself. `aplaymidi -l` lists
  them. This is the faithful one: an ST's MIDI port was a serial line and so is
  this, so running status, active sensing and a system exclusive dump of any
  length all pass through untouched, nothing along the way trying to understand
  them. A raw device is one program at a time.
- `seq:20:0`, or `seq:` and a port's name, is the ALSA sequencer. `aconnect -l`
  lists those. It reaches software synthesisers as readily as hardware and it
  appears in a patchbay by name. `seq:` on its own makes the port and connects
  it to nothing, for something else to connect to afterwards.
- `file:incoming.bin,sent.bin` is neither, and it is not a lesser port. It is
  how the traffic is looked at without a synthesiser to look at it on - by a
  test, on a machine with no sound hardware, or by somebody who wants to see
  what a program actually sent. Either half may be left out.

**The prefix has to be there**, because a sequencer port named rather than
numbered has nothing in its spelling to mark it as one. Without the rule a
mistyped `hw:` would quietly become a sequencer port connected to nothing,
which from the outside is indistinguishable from a working port with a silent
synthesiser on the end of it.

Built without ALSA - `make NO_ALSA=1`, or on a machine that has no
`libasound2-dev` - `file:` still works and the other two are refused with a
line saying why. A build server has no MIDI interface and should not need one.

The key in the cartridge port
=============================

Some programs were sold with a key that plugged into the ROM cartridge port,
and will not start without one. It is not a serial number or a licence file: it
is a chip on the bus that answers a question, and the program asks by reading
the port and listens to one bit of what comes back.

`--dongle` plugs one in. There is one, and it is the red key Cubase 3 came
with:

    tosemu -r MROS/MROS3_31 --dongle cubase CUBASE.PRG

Nothing is plugged in by default, and an empty port is not the same as no port:
with nothing asked for, the cartridge range is not mapped at all and a program
that reads it stops the emulator the way any other unmapped address does.

Plugging the key in also gives the machine a clock, the same as naming a MIDI
port does and for the same reason: there is one key and it belongs to a
sequencer. MROS, the MIDI kernel Cubase loads, puts its own handler on Timer B
and runs it at a kilohertz, and everything Cubase does about time is counted
there - so on a machine that never interrupts it opens its windows and its
menus and can never play a note. See *A machine* below for what that turns on.

The key is an Altera 5C060, which is a sixteen bit state machine clocked by
every access to the port. Address bit 8 is the question and data bit 8 is the
answer, so what a program gets back depends on every question it has asked
since the machine started. What is here is the device's own equations, taken
from the de-fused contents of its fuse map, and checked against an independent
reconstruction of the same chip before it was believed - `bin/dongletest` keeps
that check, and `tests/c-dongle.c` asks the same questions through the bus to
catch the ways the wiring rather than the chip can be wrong.

The black key Cubase 2 came with is a different and much smaller machine, and
is not modelled. Asking for it says so rather than handing over this one, which
would answer confidently and wrongly.

This is for running software you have on a machine you have, in place of
hardware that is thirty years old and mostly dead. It is not a way around
owning the thing in the first place.

A machine
=========

Asking for a MIDI port also gives the machine a 68901 MFP at `0xFFFA00` and the
two 6850 ACIAs at `0xFFFC00`, because a program with a synthesiser to talk to is
one that will be programming timers and hanging handlers off them. The timers
run against the host's clock, the system timer comes up set to two hundred hertz
the way TOS left it, and the counter at `0x4BA` counts.

So does plugging in the key from the cartridge port, for the same reason from
the other end - see *The key in the cartridge port* above.

`[machine] interrupts = yes` turns that on without either, which is how the
test suite reaches any of it.

And so does a program putting a handler of its own on the system timer, part
way through a run. On an ST that timer ran whatever anybody asked for, so no
program ever asked, and a handler put there is the only way one has of saying
it wants it. A debugger is the usual case - MonST gives the screen back to the
program it is running forty ticks after starting it. Every program starts at
the interrupt mask TOS gave it, three, so that one that asks this way hears the
answer.

It is off otherwise, and that is deliberate rather than cautious: it changes
what the machine *is* rather than what it is plugged into, and the overwhelming
majority of TOS programs neither want nor tolerate one that interrupts them.

Interrupts are real ones. `Xbtimer` sets a timer going and hangs a routine off
its vector, and from then on the routine is called - between the instructions of
a running program, and while the application is asleep in `evnt_multi`, which is
where a sequencer spends most of its time. A handler must clear its own
in-service bit the way every TOS handler does, or the MFP holds its channel off
and it is called exactly once - except one that chains to the system timer's,
which ends the interrupt for it the way TOS's does.

Bytes arriving go the whole way round: the ACIA says one is there, the MFP
raises its channel, and whatever is on `midivec` puts it in the buffer `Iorec`
handed out - which is what `Bconstat` and `Bconin` then read, and what a program
watching a stream of notes reads directly. A program that replaces `midivec` is
called instead, and one that chains to what was there finds a real routine
rather than address nought.

A period sequencer does run on it. Cubase 3.01 starts - with MROS loaded by
`--resident` and the key in the port - reaches its event loop with a window and
a menu bar, and MROS's own handler on Timer B is called at the kilohertz it
asked for: 997.4 a second against 999.0 programmed, over a minute, with the
shortfall in the first seconds rather than accumulating.

**What has not been tried is real hardware, or playing anything.** Nothing has
been loaded into Cubase and no note has been asked of it, so the path from its
handler out through the port has never been walked by the program it was built
for. And the sequencer backend was only ever watched through ALSA's own
loopback, which is not an interface with a cable in it. See `MIDI.md` on the
`midi` branch for what would want checking first.

What the machine says it is
==========================

Every TOS has a header at the bottom of its ROM and `_sysbase` points at it, so
a program that wants to know what it is running on reads the version out of
there and branches. This machine says **TOS 3.06**, and the branch is the whole
point: MROS, the MIDI kernel Cubase loads, sets itself up through the calls the
system offers at 3.00 and above, and below it goes looking for Atari TOS's own
code in the ROM to copy and patch. There is no ROM here to find, and no
graceful failure on that path either.

`Sversion` and the AES still answer for themselves, which looks like the
machine contradicting itself and is the opposite. Each of the three says what
the layer under it implements, because that is what a program asking any one of
them is really asking: the AES here is an AES 1.4, and an application told
otherwise would be entitled to call `appl_getinfo` or `menu_popup`, which are
named and not written.

The header answers what this machine can answer truthfully - the version, its
own address, and where the low memory the system keeps ends - and leaves the
rest at nought rather than filling it with something plausible. There is no
reset handler, no GEM memory usage block and no GEMDOS pool here, and inventing
addresses for them would turn "there is none" into a pointer somebody follows.

Road Map
========

The first stage will be to get a very basic TOS application to execute using 
TOSEMU. Having achieved this, more and more complex apps will be supported by 
extending the available system calls.

When basic applications are useable, a server/client architecture enabling 
intra-app communication as well as desktop accessory applications will be 
implemented.



Tests
=====

The tests subdirectory contains test applications used during the development 
of tosemu. The tests are compiled with the m68k-atari-mint cross-tools built by
 Vincent Rivière. Please visit the following web site for more information:

  http://vincent.riviere.free.fr/soft/m68k-atari-mint/

To build the tests, simply run make tests, this will result in a set of binaries
named test-* in `build/tests`, which is also where they are run: a test writes
files, opens sockets and Pexecs its siblings, and all of that belongs somewhere
that can be deleted rather than among the sources. Run `make check` to build and
run them.

Self-hosted tests
-----------------

The `tests/devpac` subdirectory contains the same test cases written in the
syntax of HiSoft Devpac 3.10's Gen assembler. These are not cross assembled on
the host - they are built by running `GEN.TTP` itself inside tosemu, so the test
run exercises a real, non-trivial TOS application as well as the binaries it
produces.

These tests need a Devpac 3.10 installation. Point `TOS_ROOT` at the directory
containing `devpac31`, which defaults to a `tos_root` next to the tosemu source
tree:

  `make devpac-check TOS_ROOT=/path/to/tos_root`

Note that Gen only recognises CR and CR/LF as line terminators, so the sources
in `tests/devpac` are stored with CR/LF endings.

The `tests/lattice` subdirectory does the same for C. It holds Lattice C 5.60
versions of the `c-*` test cases, built by running Lattice's own tools inside
tosemu. Two of them are built by driving `LC1.TTP`, `LC2.TTP` and `CLINK.TTP`
one at a time, and one by `LC.TTP`, the driver that looks those passes up along
`PATH` and `Pexec`s each of them itself, so a failure says which of the two
broke. Unlike Gen, LC1 is happy with plain LF line endings.

A source file built through the driver has to fit the 8.3 of a TOS file system.
The command line `LC.TTP` hands a pass is sized for one, and a longer name
reaches the compiler cut short.

These tests need a Lattice C 5.60 installation. Point `TOS_ROOT` at the
directory containing `lattice`, which defaults to a `tos_root` next to the
tosemu source tree:

  `make lattice-check TOS_ROOT=/path/to/tos_root`

LC1 finds the standard headers through the `INCLUDE` environment variable and
its message file `lc1.lc` along `PATH`, both of which the makefile sets, as
absolute paths - a TOS program expects a `PATH` entry to name a drive rather
than to be relative to where it was started. CLink is handed its startup module
and library through a control file rather than on the command line, which only
holds 126 characters.

Lattice also ships `MAKE.TTP`, HiSoft's make, and it runs: with `TOS_BASE_PATH`
pointing at the directory holding `lattice`, its own examples build where they
sit, out of the rules in `BIN/DEFAULT.MK` and with no makefile of their own.

  ```
  cd tos_root/lattice/EXAMPLES/WTEST
  env -u SHELL TOS_BASE_PATH=/path/to/tos_root \
      PATH='C:\LATTICE\BIN\' INCLUDE='C:\LATTICE\H\' LIB='C:\LATTICE\LIB\' \
      DEFAULT_MK='C:\LATTICE\BIN\DEFAULT.MK' \
      CFLAGS='-b4 -r6 -v -d2 -m0 -rs -fm' LDFLAGS='-lg' \
      tosemu ../../BIN/MAKE.TTP -e wtest.prg
  ```

Three things about that are worth knowing before spending an evening on them.
`SHELL` has to be unset: make runs every recipe through it if it is there, and
the host's `/bin/bash` is not a TOS program, so make looks for `bash.ttp`,
gives up and reports an error code without ever saying what it could not find.
`-e` is what lets `CFLAGS` and `LDFLAGS` come from the environment; make takes
no `VAR=value` on its command line. And the target has to be lower case,
because the suffix rules in `DEFAULT.MK` are, and make matches them exactly
even though the file system does not.

`DEFAULT.MK` has rules for `.prg`, `.ttp`, `.tos`, `.app` and `.gtp`, but not
for `.acc`, so an accessory - `EXAMPLES/CLOCK`, whose `.PRJ` builds one - is
the one thing to drive `LCC.TTP` for directly:

  ```
  tosemu ../../BIN/LCC.TTP -b4 -r6 -w -d2 -m0 -rr -fm -lg -oCLOCK.ACC CLOCK.C
  ```

The options in both come from the example's own `.PRJ` file, which is what the
HiSoft editor built it with. The driver works out the startup module and the
libraries from them and from the extension of the output: `-rr` and `.ACC`
between them pick `CSRACC.O` with `LCGSR.LIB` and `LCSR.LIB`, which is exactly
what `CLOCK.PRJ` lists.



Hacking
=======

Tracing
-------

The `config.h` file contains defines for enabling extremely verbose trace
messages. This is a great tool when debugging a subsystem, e.g. bios or aes.

There is no dependecy to `config.h`, so a clean build is needed for changes to 
take effect.

Unimplemented functions
-----------------------

No documented BIOS or XBIOS call halts the emulator. An application that asks
about hardware tosemu does not have gets the documented answer meaning "this
did not happen", which is not the same as pretending it succeeded: `Flopwr`
reports that the drive is not ready rather than that it wrote something.

Those answers live in the function tables in `bios.c` and `xbios.c` rather than
in a function each. A row carrying `FN_STUB` and a value is the whole of that
call's behaviour, and the trace says which ones an application relied on:

    Stubbed Blitmode (0x40)
    Return from Blitmode: 0 = 0x0

A row carrying `FN_HALT` with no implementation still stops the emulator, as
does any function id that is not in the table at all. Both mean an application
went somewhere nobody has looked at yet.

Where a call has to answer through a pointer rather than in D0, it needs a real
implementation even when the answer is "nothing" - see `Getmpb` in `bios.c`.

Endianess
---------

As the m68k is a big endian architecture, while the current development 
architecture, x86, is little endian, conversion is sometimes needed. However,
in most cases, it is not.

When interacting with the CPU through the `m68k_read_*`, `m68k_write_*`,
`pop_*`, `peek_*`, `push_*`, all values are expected to be in host endianess,
i.e. no conversion is necessary. When interacting directly with the TOS memory,
i.e. manipulating a memory area such as `te->appmem`, endianess is a factor.
Here, the functions `endianize_16` and `endianize_32` help with the conversion.
By always using these methods, it will be possible to run tosemu on host
systems that are either big or little endian.

File names
----------

TOS file systems are case insensitive, and most TOS applications rely on this,
happily asking for `FILE.TXT` when the file is called `file.txt` - Gen for
instance upper cases every include file name. Host file systems usually are not
case insensitive, so `path_from_tos` in `gemdosfile.c` resolves each path
component against the host, accepting a component that only differs in case.
Components without a match are left alone, so `Fcreate` and `Dcreate` still
create names exactly as the application spelled them.

C: is the whole host file system, so a path that starts at the root of the
drive starts at the host root. `Dgetpath` hands out such a path, and an
application that builds a file name from it has to arrive back at the same
file. A `TOS_BASE_PATH` moves that root, and then the drive begins there
instead: only a path starting at the root of the drive is placed under it, a
relative one stays relative to where the application is, `Dgetpath` reports
the part that is on the drive rather than the whole host path, and anything
resolving outside the base is refused rather than reached.

Either separator is accepted. TOS spells a path with a backslash and most
applications do, but one that takes a path from somewhere else - an
environment variable, a makefile, a command line typed by a person - gets
whichever was in it, and both name the same file.

`TOSEMU_TRACE_PATHS` prints every path that is resolved and what it became.
A program that cannot find a file usually says nothing about which file, and
the list of names it tried is the whole answer.

Processes
---------

An application owns the whole of the emulated machine, and everything the
emulator knows about one - the memory map, the allocator, the handle table - is
a single set of variables. Two applications cannot share that, so `Pexec` forks
the host process, and the child throws away the machine it inherited and builds
a new one around the program it was asked to run. Nested `Pexec` then costs
nothing extra, and the parent is left exactly as it was.

That also settles what a child inherits, and it lands close to TOS:

- The file handles carry over, positions and all, which is what an `Fforce`
  before a `Pexec` is for and how a compiler driver hands a pass its output.
- The environment carries over, or is replaced by the one `Pexec` was given.
- The current directory does **not** carry back. Real GEMDOS keeps one
  directory per drive globally, so a child's `Dsetpath` outlives it; here each
  process has its own, as under MiNT.
- Memory, the DTA and the screen do not carry over at all. The child gets a
  machine of its own, so an address the parent allocated means nothing to it.

A program returns a word and `Pexec` reports it with the high word clear, which
is more than the eight bits of a host exit status. The child writes the value
to a pipe, and the parent reads it once the child is gone. Nothing arriving
means the child never reached `Pterm`, and `Pexec` answers `EPLFMT`.

Modes 4 and 6, which run a program the caller has already loaded, do not fork.
They exist because TOS had one address space - two programs pass structures
back and forth through it, and a debugger catches its program's exceptions in
handlers of its own - so the child runs on the same processor in the same
machine, the way EmuTOS's `proc_go` starts one. The caller's registers, where
it was, its basepage, standard handles, DTA, drive and directory are put aside
until the child's `Pterm`, and then put back, with the child's value in d0.
Every block of memory and every file handle belongs to the program that asked
for it, so what the child allocated or opened and left behind goes with it;
mode 6 also hands the child the block it was loaded into. An application the
child introduced to the AES and never took away is dropped. `etv_term` is not
called.

The modes that load a program without running it, and the ones that make room
for one, take memory from the same allocator `Malloc` uses and build the
basepage there. They do not copy the environment: the basepage names the block
the caller gave, or the caller's own, and both of them are looking at the same
memory, so it has to still be there when the program starts.

The asynchronous MiNT modes leave the child running and answer with its
process id, which `Pwait`, `Pwait3` and `Pwaitpid` collect. A TOS process id is
a word where a host one is not, so what an application is told is the host id
narrowed to fit, and two processes on a busy machine can end up with the same
one. Those calls report a return value the way MiNT does, moved up a byte
inside the low word, so only eight bits of it survive where mode 0 reports the
whole word. A child that outlives its parent is left to the host to reap.

Because the loop has to be able to hand the machine to a different program, it
lives in `tossystem.c` rather than in `main`. `Pexec` cannot build a machine
from where it is called - that is inside a trap, and inside Musashi, neither of
which survives the CPU being reset under it - so it records what to run, stops
the loop, and lets the trap unwind first. Modes 4 and 6 build nothing, and only
point the processor somewhere else, which a trap can do.

Variable Scope
--------------

In order to provide abstraction and separation of namespaces, different 
subsystems are separated into different code modules. Header files ending with 
`_p.h` are local to such a namespace, i.e. `gemdos_p.h` is local to GEMDOS. 
Depending on the complexity of the subsystems, further subdivision is possible, 
i.e. GEMDOS and XBIOS are split into multiple modules while BIOS is not.

A subsystem that another one has to ask something of gets a second, public
header holding just that much. `drives.h` is the whole of what BIOS may know
about the GEMDOS drive table, which is otherwise private to `gemdosdrive_p.h`.



Licensing
=========

TOSEMU is available under a GPLv2 license. Please refer to the source code and 
the COPYING file for further details.



Additional Licenses
-------------------

TOSEMU depends on other components available under other licenses than GPLv2. 
These are listed below:

The contents of the `src/Musashi` subdirectory and `src/m68kconf.h`, derived from 
https://github.com/kstenerud/Musashi, is subject to the following license:

> MUSASHI
> Version 3.4
> 
> A portable Motorola M680x0 processor emulation engine.
> Copyright 1998-2001 Karl Stenerud.  All rights reserved.
> 
> Permission is hereby granted, free of charge, to any person obtaining a copy
> of this software and associated documentation files (the "Software"), to deal
> in the Software without restriction, including without limitation the rights
> to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
> copies of the Software, and to permit persons to whom the Software is
> furnished to do so, subject to the following conditions:
> 
> The above copyright notice and this permission notice shall be included in
> all copies or substantial portions of the Software.


Testing it
----------

Four layers, cheapest first. The first three need nothing but the machine you
are on; the last needs a compositor and a pair of eyes.

**The automated ones.** These are what CI runs and what should pass before
anything is committed:

    make check          the emulated machine: GEMDOS, BIOS, XBIOS, AES, VDI,
                        printing, and two processes talking through the daemon
    make emuvdi-check   the ported VDI, built for the host and compared
                        against what it should have drawn
    make demos          the demonstration programs still compile

`make check` cannot print and is not allowed to: the printer is whichever queue
CUPS calls the default one, so the suite pins the command at `/bin/true` and
the one test that means to print writes its job to a file and reads it back.

`make check` builds its tests with the m68k-atari-mint cross compiler and runs
them inside the emulator, so a failure there is a failure of the thing being
tested rather than of the test.

None of them needs a compositor. CI passes `NO_WAYLAND=1` because a build
server has no Wayland to build against, and the tests come out the same: they
all run with the screen in memory, which is the part being checked.

**The self hosted ones**, which need the period tool chains under `tos_root`:

    make devpac-check    Devpac's assembler, run inside tosemu, assembling
                         its own examples
    make lattice-check   Lattice C 5.60's compiler and linker, likewise

These are the honest end of the test suite: a compiler is a large, unforgiving
program that uses a great deal of GEMDOS, and one that runs is worth more than
any number of unit tests.

**The demonstrations**, which draw something and can be driven without a mouse:

    make demos
    TOSEMU_NO_WINDOW=1 TOSEMU_SCREENSHOT=/tmp/shot.ppm \
        TOSEMU_CLICKS='112,114' ./bin/tosemu build/demos/dialog

Every demo runs headless like that, which is how they are checked here. Run
them without `TOSEMU_NO_WINDOW` to see them as windows on the desktop:
`dialog`, `window`, `menu`, `fsel`.

**A whole session**, which is the part no test covers: a daemon, the
accessories it starts, a mark in the panel, and applications coming and going.

    make demos
    mkdir -p /tmp/gem && cp build/demos/DEMO.ACC /tmp/gem/
    ./bin/tosaesd -v /tmp/gem &
    ./bin/tosemu build/demos/menu

To find out whether one is already running, start another: it says so and
stops rather than taking the first one's place.

To add an accessory to a session that is already going, drop it in the
directory and pick "Look for new accessories" from the panel's menu. Anything
already running is left alone, so it can be picked as often as you like. One
started by hand works too, and is what that entry does:

    TOSEMU_AESD=/tmp/gem/aesd.socket ./bin/tosemu /tmp/gem/CLOCK.ACC &

which is worth knowing when there is no panel to pick things from.

The daemon says what it is doing with `-v`. What to look for:

  - it starts DEMO.ACC and says so, and the accessory says it registered
  - a mark appears in the panel, and its menu lists the accessory
  - the menu demo's own Desk menu has About, a separator, and Demo in it
  - picking Demo either way prints a line from the accessory rather than
    doing anything to the application, which is the whole point of one
  - closing the application leaves the accessory running, and the panel is
    still the way to reach it
  - Quit, at the bottom of the panel's menu, ends the session and says what
    happened to each accessory on the way:

        Asking Clock to quit. [ ok ]
        Asking Stubborn to quit. [ stopped ]
        Terminating daemon.

    `ok` went when it was asked, `stopped` had to be told, `killed` would not
    go at all. An accessory never exits on its own - that is what makes it an
    accessory - so the difference is worth seeing

Without a daemon everything still runs - one application on its own is the
ordinary way to use this - and the only things missing are the ones that need
more than one program to exist.

**When something is wrong**, three variables say what is happening:
`TOSEMU_TRACE_INPUT` for every wait for the mouse and what it was told,
`TOSEMU_SCREENSHOT` for what was drawn, and `-v` on the daemon for who is
connected. The first exists because a wait that answers wrongly is invisible
from anywhere else.

And `-v` on the emulator, which is a level rather than a switch: each one
added is the same run looked at closer.

  - `-v` says what the session was configured with - every setting that was
    said, which of the environment and the file said it, and which screen it
    came out as. A setting is read once, deep inside whatever cared about it,
    and never mentioned again, so this is the only place the answer is in one
    piece. The screen is said where it is settled rather than where it is
    asked for, because a daemon can overrule what any of it wanted
  - `-vv` adds every OS call the program makes - GEMDOS, BIOS, XBIOS, AES and
    VDI, with their arguments on the way in and their answer on the way out.
    This is the story of what the program asked for, which is usually enough:
    a program that has stopped has stopped after something
  - `-vvv` adds every instruction it runs, disassembled. Hundreds of thousands
    of lines for a program that prints one, so it is for when the story of
    what was asked for is not enough and the question is where it was asked

`-v -v` is `-vv`, for anyone who spells it that way. The tracing is built in
at every level and costs a test on the way into an OS call; `config.h` is
where a sub-system can be left out of the build altogether, which is worth
doing only if that ever turns out to matter.
