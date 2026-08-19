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
"""Turns an SVG into the C array the tray icon is served from.

The panel is handed pixels rather than a file, so the picture has to be in the
program.  Doing that here rather than at runtime means it cannot go missing:
an icon that is looked for and not found leaves the item rendering as nothing,
which looks exactly like a session that failed to start.

The output is generated and committed, so building tosemu needs no rasteriser.
Only changing the picture does.
"""

import subprocess
import sys

# What a panel asks for.  Two sizes rather than one because a doubled display
# asks for the larger and scaling a small icon up looks like what it is.
SIZES = (22, 44)


def rasterise(svg, size):
    """The SVG at one size, as raw RGBA bytes."""
    for how in (["rsvg-convert", "-w", str(size), "-h", str(size),
                 "-f", "png", svg],
                ["magick", "-background", "none", svg,
                 "-resize", "%dx%d" % (size, size), "png:-"],
                ["inkscape", "--export-type=png", "--export-filename=-",
                 "-w", str(size), "-h", str(size), svg]):
        try:
            png = subprocess.run(how, capture_output=True, check=True).stdout
        except (FileNotFoundError, subprocess.CalledProcessError):
            continue

        # And out of PNG into plain bytes, which needs no library either
        try:
            raw = subprocess.run(
                ["magick", "png:-", "-depth", "8", "rgba:-"],
                input=png, capture_output=True, check=True).stdout
        except (FileNotFoundError, subprocess.CalledProcessError):
            continue

        if len(raw) == size * size * 4:
            return raw

    return None


def main():
    if len(sys.argv) != 3:
        sys.exit("usage: icon-to-c.py <picture.svg> <where-to-write.h>")

    svg, out = sys.argv[1], sys.argv[2]
    pictures = []

    for size in SIZES:
        raw = rasterise(svg, size)

        if raw is None:
            sys.exit("icon-to-c.py: nothing here can turn %s into pixels - "
                     "install rsvg-convert, ImageMagick or Inkscape, or leave "
                     "the generated file as it is" % svg)

        # The wire wants A R G B, most significant first, and a rasteriser
        # gives R G B A.  Premultiplied is not asked for and not done.
        argb = bytearray(len(raw))
        for i in range(0, len(raw), 4):
            argb[i + 0] = raw[i + 3]
            argb[i + 1] = raw[i + 0]
            argb[i + 2] = raw[i + 1]
            argb[i + 3] = raw[i + 2]

        pictures.append((size, bytes(argb)))

    with open(out, "w") as f:
        f.write("/*\n"
                " * The tray icon, as pixels.\n"
                " *\n"
                " * Generated from %s by rsc/icon-to-c.py - do not edit this,\n"
                " * edit the picture and run make.  It is committed so that\n"
                " * building tosemu needs no rasteriser; only changing the\n"
                " * picture does.\n"
                " */\n\n" % svg)

        f.write("static const struct {\n"
                "    int size;\n"
                "    const unsigned char *argb;\n"
                "} tray_pictures[] = {\n")

        for i, (size, _) in enumerate(pictures):
            f.write("    { %d, tray_picture_%d },\n" % (size, i))

        f.write("};\n")

        # The arrays themselves go above what refers to them
        body = ""
        for i, (size, argb) in enumerate(pictures):
            body += "static const unsigned char tray_picture_%d[] = {\n" % i
            for at in range(0, len(argb), 12):
                body += "    " + " ".join("0x%02x," % b
                                          for b in argb[at:at+12]) + "\n"
            body += "};\n\n"

        # Rewrite with the arrays first, which C requires
        f.seek(0)
        f.truncate()
        f.write("/*\n"
                " * The tray icon, as pixels.\n"
                " *\n"
                " * Generated from %s by rsc/icon-to-c.py - do not edit this,\n"
                " * edit the picture and run make.  It is committed so that\n"
                " * building tosemu needs no rasteriser; only changing the\n"
                " * picture does.\n"
                " */\n\n" % svg)
        f.write(body)
        f.write("static const struct {\n"
                "    int size;\n"
                "    const unsigned char *argb;\n"
                "} tray_pictures[] = {\n")
        for i, (size, _) in enumerate(pictures):
            f.write("    { %d, tray_picture_%d },\n" % (size, i))
        f.write("};\n")

    print("icon-to-c.py: %s -> %s (%s)"
          % (svg, out, ", ".join("%dx%d" % (s, s) for s, _ in pictures)))


if __name__ == "__main__":
    main()
