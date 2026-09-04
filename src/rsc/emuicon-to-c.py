#!/usr/bin/env python3
#
# TOSEMU - an emulated environment for TOS applications
# Copyright (C) 2026 Johan Toverland Thelin <e8johan@gmail.com>
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License
# as published by the Free Software Foundation; either version 2
# of the License, or (at your option) any later version.
#
"""Turns one icon out of an EmuTOS icon resource into the C array a
compositor is handed.

A window of tosemu's is a window of a GEM application, and what a task bar
should show beside its name is the picture the desktop it came from would have
shown: EmuTOS's generic application icon.  Nothing else here can supply that.
An icon theme has never heard of the program, and a desktop entry file would
have to be installed before the icons meant anything - which is a thing an
emulator run out of its build directory cannot rely on.

So the picture is taken from the resource the EmuTOS desktop reads its own
icons from and compiled in, the same bargain rsc/icon-to-c.py makes for the
tray: a picture that is part of the program cannot go missing.

Only the one size is written out, which is the size the icon was drawn at.
What a compositor asks for is not known until it has been asked, and making it
larger is repeating pixels rather than resampling them - so gfx.c does that at
the sizes it turns out to want.  See icon_make there.
"""

import struct
import sys

# An icon is black on white paper, which is how the mono desktop drew it and
# what makes it legible on a pale panel and a dark one alike.  Drawing the ink
# and leaving the paper transparent would put black on black half the time.
INK = (0x00, 0x00, 0x00)
PAPER = (0xff, 0xff, 0xff)

# The resource header, and the ICONBLK: three file offsets standing in for the
# mask, the data and the name, then the eleven words of the ICONBLK proper.
# See rsdefs.h and obdefs.h in EmuTOS.
RSHDR = ">10H8h"
ICONBLK = ">3I11h"
ICONBLK_SIZE = 34


def icon(rsc, index):
    """The mask, the data and the size of one icon in a resource file."""
    d = open(rsc, "rb").read()

    hdr = struct.unpack(RSHDR, d[:struct.calcsize(RSHDR)])
    iconblk, nib = hdr[3], hdr[13]

    if index >= nib:
        sys.exit("emuicon-to-c.py: %s holds %d icons, so there is no %d"
                 % (rsc, nib, index))

    at = iconblk + index * ICONBLK_SIZE
    fields = struct.unpack(ICONBLK, d[at:at + ICONBLK_SIZE])
    pmask, pdata, ptext = fields[0], fields[1], fields[2]
    width, height = fields[8], fields[9]

    if width % 8:
        sys.exit("emuicon-to-c.py: icon %d in %s is %d pixels wide, which is "
                 "not a whole number of bytes" % (index, rsc, width))

    # A window icon has to be square, which is the protocol's requirement and
    # not this file's - a compositor hands back an invalid_buffer error for
    # anything else, and it would arrive at run time on somebody's desktop
    if width != height:
        sys.exit("emuicon-to-c.py: icon %d in %s is %dx%d, and a window icon "
                 "has to be square" % (index, rsc, width, height))

    # An icon's name is only ever read to say which picture was taken, which
    # is worth saying: the index is a number in a Makefile and the name is not
    name = d[ptext:d.index(b"\0", ptext)].decode("latin-1")
    span = (width // 8) * height

    return (d[pmask:pmask + span], d[pdata:pdata + span], width, height, name)


def argb(mask, data, width, height):
    """The icon as the pixels the wire wants.

    A row of a GEM icon is bits, most significant first, and both halves are
    read together: a bit of the data is ink, a bit of the mask with no data
    under it is paper, and neither is the desktop showing through.
    """
    out = bytearray()
    stride = width // 8

    for y in range(height):
        for x in range(width):
            bit = 0x80 >> (x % 8)
            byte = y * stride + x // 8

            if data[byte] & bit:
                out += bytes((0xff,) + INK)
            elif mask[byte] & bit:
                out += bytes((0xff,) + PAPER)
            else:
                out += b"\0\0\0\0"

    return bytes(out)


def main():
    if len(sys.argv) != 4:
        sys.exit("usage: emuicon-to-c.py <emuicon.rsc> <icon> <where.h>")

    rsc, index, out = sys.argv[1], int(sys.argv[2], 0), sys.argv[3]
    mask, data, width, height, name = icon(rsc, index)
    pixels = argb(mask, data, width, height)

    with open(out, "w") as f:
        f.write("/*\n"
                " * The window icon, as pixels: alpha, red, green and blue,\n"
                " * a row at a time.\n"
                " *\n"
                " * Generated from icon %d (%s) of\n"
                " * %s by rsc/emuicon-to-c.py.\n"
                " * Do not edit this - the picture is EmuTOS's and belongs to\n"
                " * the submodule.  See src/rsc/README.\n"
                " */\n\n" % (index, name, rsc))

        f.write("#define WINDOW_ICON_SIZE %d\n\n" % width)
        f.write("static const unsigned char window_icon_argb[] = {\n")
        for chunk in range(0, len(pixels), 12):
            f.write("    " + " ".join("0x%02x," % b
                                      for b in pixels[chunk:chunk + 12]) + "\n")
        f.write("};\n")

    print("emuicon-to-c.py: %s icon %d (%s) -> %s (%dx%d)"
          % (rsc, index, name, out, width, height))


if __name__ == "__main__":
    main()
