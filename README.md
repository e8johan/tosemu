TOSEMU
An emulated environment to TOS applications.
Copyright (C) 2014 Johan Thelin <e8johan@gmail.com>

[![Build Status](https://travis-ci.org/e8johan/tosemu.svg?branch=master)](https://travis-ci.org/e8johan/tosemu)

Introduction
============

TOSEMU aims to provide an emulated environment for executing TOS applications. 
Instead of emulating a complete TOS-compatible machine, operating system calls 
are intercepted and emulated on the host platform. The end result will be TOS 
applications running as an integrated part of the host system.

TOSEMU can be seen as a mix of 68kemu, which allows 68k applications to be 
executed in a TOS environment on another CPU platform, and wine, which lets 
Windows applications execute using a system API translation laer on the CPU 
platform they where meant for. TOSEMU executes 68k applications on non-68k 
CPUs, and provides an OS wrapper layer, translating TOS calls to system calls 
native to the host operating system.

I use LLM technology to speed things along from time to time. Feel free to join
in by adding functionality, reporting bugs or helping out in any other way.



Building
========

A simple `make` should do it. The resulting binary can be found in the bin
directory.

The VDI comes from EmuTOS rather than being written again, so the tree is no
longer self contained: it carries EmuTOS as a submodule in `3rdparty/emutos`.
Clone with

    git clone --recurse-submodules <url>

or, in a tree that is already checked out,

    git submodule update --init

The submodule is never edited. Everything that adapts EmuTOS to a hosted build
lives in `emuvdi/`, which has a README of its own explaining how. The
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

Two more are a rule rather than a size: `native-mono` and `native-color` are as
large as the display will hold, in one plane and in four. They are not Atari
screens and are not pretending to be. What they are for is a GEM application
having the room a modern display has, which is the one thing the machine could
not give it - and the rest of GEM does not mind, because a resource is measured
in characters and more of them across is simply more room.

How large that is comes out of two divisions. The compositor's own scaling is
one - a display reporting 3456x2160 at a scale of two shows a window in half of
those - and `TOSEMU_SCALE` is the other, being how many of those an ST pixel
becomes. So a 3456x2160 display at scale two gives a 576x360 screen at the
default `TOSEMU_SCALE=3`, and that screen magnified by three is 1728x1080,
which is the display. They are one setting rather than two for exactly that
reason. The width is rounded down to a multiple of sixteen, because a row of a
surface is a whole number of words, so the window can come out a little short of
the display's width and never over it.

`TOSEMU_OUTPUT` says which display to measure, by the name the compositor gives
it - `eDP-1`, `DP-1` and so on, which `wayland-info` will list. Without it, the
first one the compositor mentions. With no compositor to ask at all the size
falls back to 640x400 and the planes stay as asked, which is what happens on a
machine with no desktop and in the test suite. A display named and not there -
a monitor since unplugged, most likely - falls back the same way and says which
displays it did find, rather than quietly measuring a different one.

Asking costs one round trip at the moment the machine is decided, and no window:
`wl_output` is a global like any other and says how large it is without anything
being shown. When `tosaesd` is running it is the one that asks, because it is
the one that decides - so set these on the daemon, not on each application.

When `tosaesd` is running it is what decides, and the variable is read from its
environment rather than from each application's. The screen has to be one
screen for everything sharing it - applications lay their windows out in it and
are told where the others put theirs - so it belongs to the session, the way a
resolution belonged to a machine rather than to a program running on it.

`TOSEMU_KEYS` hands over keystrokes and `TOSEMU_CLICKS` places to click, for
when there is nobody to type or click - a test suite, mostly. Keys are
characters with `\r` for Return. Clicks are `x,y` to press and release there,
`x,y-x2,y2` to drag from one to the other, and `@x,y` to move the pointer
without pressing anything - which is not a nicety, because a GEM menu opens
when the pointer arrives among the titles rather than when it is clicked.

`TOSEMU_TRACE_INPUT` says what every wait for the mouse asked for and what it
was told, and which directories the file selector read. It is there because a
wait that answers wrongly is invisible from anywhere else: two different waits
asking the same question and one wait asking it twice look identical until
something says which happened.

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

    [input]
    keys   = \r
    clicks = 100,50 200,60

    [files]
    base = /home/me/tos

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
| `[input] keys`         | `TOSEMU_KEYS`        |
| `[input] clicks`       | `TOSEMU_CLICKS`      |
| `[files] base`         | `TOS_BASE_PATH`      |
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
to be looked at rather than checked - `./bin/tosemu demos/dialog` puts a dialog
on the screen with buttons that can be clicked. See `demos/README`.

The `make clean` target produces a clean source tree.



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
named test-* in the tests sub-directory. Run `make check` to build and run them.

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
- The modes that run a program the caller has already loaded are the exception,
  because there the child keeps the machine it inherited and every address in
  it still means what it did. What it writes there is its own, though: a fork
  copies memory rather than sharing it, so an answer a child leaves at an
  agreed address is one the parent never sees. See the `TODO`.

A program returns a word and `Pexec` reports it with the high word clear, which
is more than the eight bits of a host exit status. The child writes the value
to a pipe, and the parent reads it once the child is gone. Nothing arriving
means the child never reached `Pterm`, and `Pexec` answers `EPLFMT`.

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
lives in `tossystem.c` rather than in `main`. `Pexec` cannot start a program
from where it is called - that is inside a trap, and inside Musashi, neither of
which survives the CPU being reset under it - so it records what to run, stops
the loop, and lets the trap unwind first.

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

The contents of the `Musashi` subdirectory and `m68kconf.h`, derived from 
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
                        and two processes talking through the daemon
    make emuvdi-check   the ported VDI, built for the host and compared
                        against what it should have drawn
    make demos          the demonstration programs still compile

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
        TOSEMU_CLICKS='112,114' ./bin/tosemu demos/dialog

Every demo runs headless like that, which is how they are checked here. Run
them without `TOSEMU_NO_WINDOW` to see them as windows on the desktop:
`dialog`, `window`, `menu`, `fsel`.

**A whole session**, which is the part no test covers: a daemon, the
accessories it starts, a mark in the panel, and applications coming and going.

    make demos
    mkdir -p /tmp/gem && cp demos/DEMO.ACC /tmp/gem/
    ./bin/tosaesd -v /tmp/gem &
    ./bin/tosemu demos/menu

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
