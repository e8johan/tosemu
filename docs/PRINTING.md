Printing
========

There is a printer, and it is a CUPS queue. An application prints the way it
always did - `v_opnwk` with 21 in `work_in[0]`, which is where GDOS's
`ASSIGN.SYS` put the printer, then the same drawing calls it puts in a window,
then `v_updwk` to say the page is finished - and what comes out the other end
is a job in the queue.

Nothing has to be set up. Without a word said it is A4 at three hundred dots to
the inch, going to whichever printer CUPS calls the default one.

    [printer]
    destination = Brother_HL_L2350DW
    paper       = a4
    resolution  = 300
    # file      = /tmp/job.pdf
    # command   = lp

| in the file             | in the environment      |
| ----------------------- | ----------------------- |
| `[printer] destination` | `TOSEMU_PRINTER`        |
| `[printer] paper`       | `TOSEMU_PRINTER_PAPER`  |
| `[printer] resolution`  | `TOSEMU_PRINTER_DPI`    |
| `[printer] file`        | `TOSEMU_PRINT_FILE`     |
| `[printer] command`     | `TOSEMU_PRINT_COMMAND`  |

`a3`, `a4`, `a5`, `letter` and `legal` are the papers. A name that is not one
of them is complained about rather than guessed at, a wrong guess here being a
wasted sheet of somebody's paper.

**The paper is said here rather than asked of the queue**, and that is what
talking to CUPS through `lp` costs: the queue knows what is in its tray and
there is no way to ask it from a command line without reading something written
for a person. So this is what the sheet is, and CUPS is told the same thing
with `-o media=`, which is what stops it deciding the page does not fit and
shrinking it.

`file` names somewhere to write the job instead of printing it. That is how to
look at what would have come out - it is an ordinary PDF - and it is what the
test suite uses, printing being the one thing a test cannot check by doing.
`command` is the program the job is handed to, `lp` unless it says otherwise;
it is run directly rather than through a shell, so nothing in a setting can
turn into a command.

A document is one job rather than one job a page. `v_updwk` puts a page into
it, `v_clrwk` blanks the page for the next one - `v_form_adv` does both - and
the job goes to the queue when the printer is closed with `v_clswk`, which
closes the device and every workstation an application opened on it. A program
that prints and quits without closing gets its pages sent anyway: it meant to
print them.

The page is a bitmap, which is what a GDOS printer driver was. The drivers
Atari shipped rendered into a band of memory and sent the rows to the printer,
and nothing about the VDI is a description of a drawing that survives being
drawn - so the whole of the EmuTOS VDI serves the printer exactly as it serves
the screen, and none of it had to be written twice. What reaches CUPS is that
bitmap inside a PDF, PDF being the one format every version of CUPS takes.

**Text comes out at the printer's size**, which is the one thing that does not
follow from the above. A bitmap font is a font of a particular number of pixels
and has nothing to say about a device four times finer than the one it was
drawn for - which is exactly why GDOS had a section per device in `ASSIGN.SYS`
and why Atari sold printer fonts. The typefaces from the host do have something
to say, because a size in points becomes a size in pixels through the device's
resolution, so a document set in one of those prints at the size it says. One
set in a `.FNT` gets that font's pixels, and at three hundred dots to the inch
that is very small type. See the `TODO`.

The printer has as many colours as the screen. That is not a shrug: the VDI
maps a colour index onto a pen and a pen onto a colour, and both of those
mappings are the machine's rather than any one device's. On the default screen
that means a black and white printer, which is what a GEM application expects
to be printing on.

Two things it does not have. There is no `PRN:` - a program that prints by
writing characters to the printer port rather than by drawing on it reaches
nothing, which is what it reached before. And `v_bit_image` and `vq_scan` are
not answered: both are for a driver that could only hold a band of a page at a
time, and this one holds the page.
