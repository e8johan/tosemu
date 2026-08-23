# Working on tosemu

Notes for anyone — person or agent — picking this up cold. `README.md` says what
tosemu is and how to build it, `emuvdi/README` says how the EmuTOS VDI is
carried, and `TODO` says what is known to be missing. This file is about how
work here is done, and about the things that have already gone wrong.

## The shape of the program

tosemu translates TOS calls rather than emulating a machine. A GEM application
is real 68000 code running on Musashi; everything below it is host C. That seam
runs through the whole program and most of the interesting bugs live on it.

    main.c cpu.c memory.c      the machine and its memory
    gemdos*.c bios.c xbios*.c  the OS calls, dispatched from traps
    aes*.c                     the AES: windows, menus, objects, events
    vdi.c                      the VDI trap, which hands arrays to emuvdi
    emuvdi/                    EmuTOS's VDI and parts of its AES, hosted
    3rdparty/emutos/           the submodule. Read only. Never edited.
    gfx.c surface.c            the screen as memory, and as windows on Wayland
    screen.c                   which screen the machine has
    aesd.c aesclient.c         the daemon several emulators share, and the client
    tests/                     68000 programs run under the emulator

`3rdparty/emutos` is a checkout of somebody else's tree pinned to `VERSION_1_4`.
Nothing in it is edited — everything that adapts it lives in `emuvdi/`. It is
also the authority on what the AES and VDI are supposed to do: when something
draws wrongly, read the EmuTOS source for the call before theorising.

## House style

The comments are the most visible convention and the easiest to get wrong. They
are prose, they explain *why* rather than what, and they are written for someone
who does not already know. A comment above a function says what the function is
for and what would go wrong without it; a comment inside says why this is the
way it is done rather than the obvious way. Look at `emuvdi/gemobjop.c` or
`screen.c` before writing any.

Things that are not the style: `/* increment i */`, a comment restating the line
below it, TODO markers left in code rather than in `TODO`, jargon where a plain
sentence works, and exclamation marks.

Every file carries the GPL header with `Copyright (C) 2026 Johan Toverland
Thelin <e8johan@gmail.com>`. Put it on new files and leave it on old ones.

Code is C89-ish in shape — declarations at the top of a block — four spaces, no
tabs, braces on their own line. Match the file you are in.

## How a fix gets made

This sequence costs a few minutes and has caught more mistakes than it has cost:

1. **Reproduce it.** For anything drawn, that means a screenshot — see below.
   A theory about GEM that has not been run is a guess.
2. **Confirm against EmuTOS.** Find the call in `3rdparty/emutos/` and read what
   it actually does. Most bugs here are tosemu departing from it by one byte,
   one word, or one call in the wrong order.
3. **Fix it.**
4. **Write a test that fails without the fix.**
5. **Prove that.** Put the bug back — edit the source, rebuild, run the test —
   and watch the specific checks fail. A test that passes either way is not a
   test. Put the bug back *faithfully*: an approximation of it fails different
   checks and tells you nothing. Then restore and **rebuild again**; a stale
   binary looks exactly like a passing test.
6. **`make check`** in full, from the top.
7. **Commit one fix per commit.**

## Traps that have actually bitten

**Top-level objects do not rebuild when a header changes.** Only `emuvdi/`
objects are built with `-MMD` (`Makefile:172-177`); everything at the top level
uses make's built-in rule and tracks nothing. Edit `aesproto.h`, `screen.h`,
`gem_p.h` or any other top-level header and `make` will happily relink stale
objects. Delete them by hand:

    rm -f gem.o aesd.o aesappl.o && make

This is not theoretical — it made a mutation check appear to pass when the
mutant was live.

**The byte-order and word-size seam.** A 68000 `LONG` is four bytes, big endian.
The host's `long` is eight, little endian, and the build is 64-bit. So
`*(char *)pspec` finds the top byte of a spec on an Atari and the *last* byte
here. That exact line has been wrong twice in `emuvdi/gemobjop.c`, for two
different bytes of the same field. When EmuTOS points a narrow type at a wide
one, shift instead — ask for the byte, not for the end it happens to be at.

Related: GEM keeps addresses in 32-bit fields (`ob_spec`, `MFDB`, `USERBLK`),
which is why the build is `-fno-pie -no-pie` and why anything allocated at run
time comes from `host_vdi_alloc` with `MAP_32BIT`. See the long note in the
Makefile.

**A screen is a whole number of words across.** `surface_create` rounds the
allocation up to a word; the VDI's `v_lin_wr` rounds the row length down. Every
Atari screen was a multiple of sixteen pixels wide so they always agreed. Any
new width has to be too — `screen_from_display` rounds down for exactly this
reason.

**A live `tosaesd` decides things the tests assert.** The daemon says which
application is which and which screen the machine has, and it says it to every
emulator that finds its socket. The suite pins `TOSEMU_AESD` to a path that does
not exist (`$(NO_DAEMON)` in `tests/Makefile`, folded into `$(TOSEMU)`), so
`make check` is safe on a machine with a session running. Keep it that way: a
new check line should use `$(TOSEMU)`, and one that uses
`$(TOSEMU_FOR_A_WHILE)` has to decide for itself and say `$(NO_DAEMON)` if it
wants to be alone.

**Never kill a running `tosaesd`.** It is probably the session someone is using.

**A compositor is not something a test can arrange.** One machine has two
displays and a build server has none, so anything that asks Wayland has to be
checked with `$(NO_DISPLAY)` for what it does when there is nothing to ask, and
the arithmetic checked separately — that is what `bin/screentest` is for.

## Tests

Two kinds, and they are built differently.

**Under the emulator**, in `tests/`: 68000 programs cross-compiled with
`m68k-atari-mint-gcc`. C tests are named `c-something.c` and built as
`test-c-something`; the ones that use GEM link `-lgem`. Add the name to
`CTESTNAME`, add a `-lgem` rule if it needs one, and add a line to `check:`.

**On the host**: `bin/vditest` (`make emuvdi-check`) draws with the ported VDI
and diffs against `emuvdi/vditest.expected`; `bin/screentest`
(`make screen-check`) checks the display arithmetic. These are for the things an
application cannot reach.

Both use the TAP idiom:

    ok 3 - Dgetpath reports the current directory
    not ok 4 - and spells it with backslashes (got /home/x, want no slash)
    1..4

The count line at the end is not decoration. An unimplemented GEM call halts the
emulator, which then prints nothing further, so `grep -q '^1\.\.'` is what says
the whole file ran rather than the part before the crash. Every check line in
`tests/Makefile` should assert both no `not ok` and the presence of the count.

Name a check by what it establishes, not what it calls — "clicking the close box
went up a folder", not "test fsel_input". The `(got X, want Y)` on failure is
what makes a broken build diagnosable from CI output alone.

Two more suites are not part of `make check`, because they need period software
installed: `make devpac-check` assembles its tests by running HiSoft Devpac's
`GEN.TTP` inside the emulator, and `make lattice-check` compiles its own with
Lattice C 5.60's tools. They are worth more than the binaries they produce —
each puts a real, non-trivial TOS application through the emulator. Run them
when a change touches GEMDOS or process handling. They want `TOS_ROOT` pointing
at an installation; their Makefiles say how.

## Seeing what was drawn

The emulated screen is memory. To look at it:

    TOSEMU_NO_WINDOW=1 TOSEMU_SCREENSHOT=/tmp/shot.ppm bin/tosemu prog.prg

It is written every time the application waits, so the file holds the last thing
that was on screen. Upscale it with nearest-neighbour to read it — and on the ST
medium screen, whose pixels are half as wide as they are tall, scale x2 y4 or
everything looks wrong in a way that is not the bug you are chasing.

`TOSEMU_KEYS` and `TOSEMU_CLICKS` stand in for a person. Clicks are `x,y` to
press and release, `@x,y` to move without pressing (a GEM menu opens on the
pointer arriving, not on a click), `!x,y` to press and hold, `x,y-x2,y2` to
drag. The whole list is parsed once at startup.

There is a quirk worth knowing: a wait answered by the button state rather than
by the queue drops the next queued event, so a press following a dialog that
just closed does not survive into the next one. Tests that open a dialog
repeatedly throw away a leading click for this reason — see the header of
`tests/c-fsel.c`. It affects injected input, not a real person.

Coordinates in check lines are pixels on whichever screen the test runs on, so a
click worked out on one screen silently misses on another. Take them off a
screenshot of the screen the test will actually use.

## Commits

One fix per commit. The subject is `area: what was wrong` — lower case after the
colon, past tense, naming the defect and not the change:

    AES: the character in a box was read off the wrong end
    GEMDOS: Dgetpath answered with the host's separators
    GEM: the screen was one no GEM application can use

The body is prose. It explains what was wrong, why it was wrong, why nobody had
noticed, and what the fix does — in that order, roughly. It is written for
someone reading `git log` in two years with no other context. Areas seen so far:
`AES`, `GEM`, `GEMDOS`, `cpu`, `tosaesd`, `tray`, `rsc`, `TODO`.

End the message with:

    Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>

Do not commit `.o` files, anything in `bin/`, or generated sources — `.gitignore`
covers them. Do not commit changes inside `3rdparty/`.

## Screens

`TOSEMU_SCREEN` picks one, and `screen.c` holds the table. The ST's `low`,
`medium` and `high`; the TT's `tt-medium` and `tt-high`; and `native-mono` and
`native-color`, which are as large as the display will hold. `high` is the
default because it is what GEM applications were written for — 640x400, eighty
characters across. A dialog out of a resource is measured in characters, so how
many fit is what decides whether it fits at all.

The daemon decides when there is one, including for the accessories it starts,
so `TOSEMU_SCREEN`, `TOSEMU_SCALE` and `TOSEMU_OUTPUT` go on `tosaesd` in a
session that has one.

Adding a screen is a line in that table only when it is the same planes in
another shape. The comment above the table says which of the machines' other
modes are not there and what each of them would need first — the short version
is that eight planes needs a palette the VDI was not built with, and the
Falcon's needs a surface that is not planes at all.
