Fonts
=====

There are three layers of them and they stack. The system font is always there;
Atari `.FNT` files are added by pointing tosemu at an `ASSIGN.SYS`; typefaces
from the host are added by FreeType and fontconfig, and are there with no setup
at all. An application sees one list with all three in it.

`vq_gdos` is what an application asks first, and it is answered honestly:
`'_FNT'` when there are fonts from files, `'_FSM'` when there is an outline
engine as well, and nothing at all - which leaves the -2 the caller put in `d0`
- when there is neither. It is only said when it is true, because a program
told there is a GDOS calls things that then have to be there.

The system font
---------------

Nothing has to be done, and nothing can take it away. Three fonts are compiled
in from EmuTOS - 6x6, 8x8 and 8x16 - and they are what a program gets when it
has asked for nothing else. The face is number 1 and it is always the first
entry in the list, whatever else is loaded, so `vst_font(handle, 1)` gets back
to it from anywhere.

A `.FNT` whose header carries face id 1 adds to it rather than beside it: the
sizes in the file become extra sizes of the system face, and it is not counted
as a face of its own. That is one of the things GDOS was for. Such a font
should cover the same characters as the face it joins, though, because it may
then be chosen for any string in that face - one that covers less will measure
nought for everything it does not have.

No font Atari shipped does this - Swiss is 2, Dutch 14, Typewriter 15 - and
the host typefaces are numbered from 5001, well clear of anything a font file
will bring with it.

Atari .FNT files
----------------

`ASSIGN.SYS` is the list, exactly as it was on a real machine, and a font
directory copied off its floppy works as it stands. Given an installation like
the one Microsoft Write shipped:

    mswrite/
      ASSIGN.SYS
      GDOS.SYS/
        ATSS10.FNT  ATSS12.FNT  ATTR10.FNT  ...
      WRITE.PRG

run the program from that directory and there is nothing to configure:

    cd mswrite
    tosemu WRITE.PRG

`ASSIGN.SYS` is looked for in four places, in this order:

1. what `[fonts] assign` says,
2. the directory the program was loaded from,
3. the directory the process is standing in,
4. the root of `C:`.

So to use one installation's fonts with a program somewhere else, name it:

    TOSEMU_FONTS_ASSIGN=/path/to/mswrite/ASSIGN.SYS tosemu SOMETHING.PRG

or put it in a settings file:

    [fonts]
    assign = /path/to/mswrite/ASSIGN.SYS

The `PATH` line inside the file says where the fonts themselves are, and in
every installation that shipped it names a floppy - `A:\GDOS.SYS`. There is no
`A:` here, so the drive is dropped and what is left of the path is looked for
beside `ASSIGN.SYS` itself: `A:\GDOS.SYS` finds the `GDOS.SYS` directory next
to the file that named it. Nothing has to be edited. A `PATH` that does name a
directory which exists is used as written.

**Which fonts you get depends on the screen**, and that is not tosemu being
clever - it is what the file says. `ASSIGN.SYS` has a section per device, and a
screen's device number is its resolution plus two, which is the same number the
AES opens the physical workstation with:

| `TOSEMU_SCREEN`             | planes | section |
| --------------------------- | ------ | ------- |
| `high`, `tt-high`, `native-mono`, `display-mono` | 1 | 4 |
| `medium`                    | 2      | 3       |
| `low`, `tt-medium`, `native-color`, `display-color` | 4 | 2 |

It matters because the sections usually hold different fonts. A medium
resolution screen puts two hundred lines where a high resolution one puts four
hundred, so its fonts are half as tall at the same width, and Atari shipped a
second set for it. A screen with no section of its own falls back to the first
screen section in the file.

Nothing is read until an application calls `vst_load_fonts`, so a program that
never asks for fonts does not pay for them.

**To check it worked**, ask for the paths to be traced:

    TOSEMU_TRACE_PATHS=1 tosemu WRITE.PRG

    tosemu: ./ASSIGN.SYS names 9 fonts for device 4, in ./GDOS.SYS

and to look at the font files themselves, without an emulator in the way:

    bin/gdostest --list mswrite/GDOS.SYS/*.FNT

    ATSS10.FNT  id   2  Swiss       10 point  32-254  cell 13x13  Intel
    ATTR24.FNT  id  14  Dutch       24 point  32-254  cell 30x30  Intel

A file that is not a font is named on stderr and skipped rather than taken for
one, so a wrong line in a font list costs that font and nothing else.

Typefaces from the host
-----------------------

This is the SpeedoGDOS half: a face asked for by name, at any size, rather than
at the sizes somebody put font files on the disk for. It needs FreeType and
fontconfig at build time - `install-deps.sh` gets them - and there is nothing
to set up: a build that has them offers these faces to every program.

The names are Bitstream's, because SpeedoGDOS was Bitstream's, and nobody has
those files any more. Each is asked of fontconfig as the host family that came
from the same drawings:

| what a document asks for | asked of the host as |
| ------------------------ | -------------------- |
| `Swiss 721`              | Nimbus Sans          |
| `Dutch 801`              | Nimbus Roman         |
| `Courier 10 Pitch`       | Nimbus Mono PS       |
| `Monospace 821`          | Nimbus Mono PS       |
| `Zapf Humanist`          | Nimbus Sans          |

**To offer a font of your own**, write a file of substitutions. One face to a
line: what a document asks for, an equals sign, and the family to ask the host
for. Lines beginning with `;` or `#` are remarks.

    # ~/.tosemu-fonts
    Swiss 721        = DejaVu Sans
    Courier 10 Pitch = DejaVu Sans Mono
    Fancy            = Liberation Serif

Then name that file, either for one run:

    TOSEMU_FONTS_SUBSTITUTES=~/.tosemu-fonts tosemu SOMETHING.PRG

or in a settings file, for every run:

    [fonts]
    substitutes = /home/you/.tosemu-fonts

A name already in the table is repointed - `Swiss 721` above now comes from
DejaVu rather than Nimbus - and a name that is not is added as a new face, so
`Fancy` appears in the list an application shows. That is both how a mapping
you disagree with is corrected and how a font of your own is exposed to
programs that never heard of it.

The right hand side is anything fontconfig understands, so a generic name such
as `sans-serif` works as well as a family. TrueType, OpenType and Type 1 are
all the same to it: what matters is that fontconfig can find it, which
`fc-match "the name"` will tell you before tosemu is involved.

The list is this table rather than every font the host has, and that is
deliberate: an application shows the faces in a menu and picks out of it by
number, and several hundred faces it has never heard of is not what a program
written for a machine with four of them expects. If you want one of yours, add
a line for it.

**To check what each name actually resolved to**, run the host test:

    bin/gdostest

    # Swiss 721            set in Nimbus Sans
    # Dutch 801            set in Nimbus Roman
    # Courier 10 Pitch     set in Nimbus Mono PS

fontconfig always answers, so a family nobody has quietly becomes its idea of
the nearest thing - which is why this prints the name that came back rather
than the name that was asked for.

Text is drawn without antialiasing, and that is a decision rather than a gap:
an Atari screen is a few planes of indexed colour whose palette the application
chose, so there are no greys to draw the edges in. SpeedoGDOS rendered
monochrome on this hardware too.

What an application sees
------------------------

All three layers arrive in one list, in this order: the system face, then the
faces from files, then the faces from the host. An application asks for the
list with `vqt_name` and chooses out of it by number, and none of those numbers
mean anything to it beyond being what it was given.

`vst_load_fonts` must be called before any of it - that is the order
SpeedoGDOS documented, and until it has been called a workstation has only the
faces it was opened with. What it answers is how many faces the program now has
that it did not have before, counting both the ones from files and the ones
from the host. That one number is how applications decide whether there are
fonts at all: Atari Works puts up "Can not find graphics FONTS on your system"
when it comes back nought.

**The substitution shows, and it is meant to.** An application lays a page out
from what `vqt_extent` tells it, so a face whose letters are a fraction wider
moves every line break in the document. Real SpeedoGDOS did the same when a
font was missing. It is not a fault to be hidden - it is the reason the fonts
from files are the ones worth getting exactly right, those being what a
document of the period was actually set in.

Two things an application may notice are missing. There is no cookie jar, so a
program that looks for the `FSMC` cookie rather than calling `vq_gdos` will go
on believing there is no GDOS. And `vqt_fontheader`, `v_getbitmap_info` and
`v_getoutline` are refused rather than answered, because each hands something
back through a buffer in the application's own memory.
