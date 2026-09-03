# Where everything is.
#
# src/ is what somebody wrote, build/ is what the compiler made of it, and
# nothing is ever in both: objects, generated sources and the dependency files
# beside them all land under build/. That is what makes a clean tree one
# directory deletion rather than a hunt through the sources for what does not
# belong there, and it is what makes a source directory listing readable.
#
# bin/ is the other half of the same idea - the programs that come out, which
# are the part somebody runs rather than the part the build had to make on the
# way there.
SRC   = src
BUILD = build
OBJ   = $(BUILD)/obj
GEN   = $(BUILD)/gen
BIN   = bin

# Source files for TOS emulator, named as they are found under $(SRC)
SOURCEFILES = main.c gemdos.c gemdosmem.c gemdoscon.c gemdosfile.c gemdosdrive.c gemdosproc.c \
              xbios.c xbiosscreen.c xbiossys.c xbiosdev.c bios.c \
              gem.c aesclient.c aes.c aesappl.c aesevnt.c aesgraf.c aeswind.c aesmenu.c aesframe.c aesfsel.c aesobjc.c aesrsrc.c aesscrp.c aesshel.c aestree.c vdi.c surface.c \
              gfx.c screen.c settings.c scrap.c scraptext.c scrapimg.c \
              tossystem.c utils.c memory.c cpu.c

# Hand-written Musashi files
MUSASHIFILES = Musashi/m68kcpu.c Musashi/m68kdasm.c

# Generated Musashi files. They are written rather than kept, so they live
# under $(GEN) with everything else nobody wrote - the header among them, which
# is why it is named separately from the sources that become objects.
MUSASHIGENERATEDSOURCES = $(GEN)/m68kops.c $(GEN)/m68kopac.c $(GEN)/m68kopdm.c \
                          $(GEN)/m68kopnz.c
MUSASHIGENERATEDFILES = $(MUSASHIGENERATEDSOURCES) $(GEN)/m68kops.h

# The VDI, which comes from EmuTOS. These are built as they stand, out of the
# submodule, and everything that adapts them is in src/emuvdi/ - see
# src/emuvdi/README.
EMUTOS = 3rdparty/emutos
EMUTOSFILES = $(EMUTOS)/vdi/vdi_main.c $(EMUTOS)/vdi/vdi_control.c \
              $(EMUTOS)/vdi/vdi_line.c $(EMUTOS)/vdi/vdi_fill.c \
              $(EMUTOS)/vdi/vdi_col.c \
              $(EMUTOS)/vdi/vdi_bezier.c $(EMUTOS)/vdi/vdi_gdp.c \
              $(EMUTOS)/vdi/vdi_marker.c $(EMUTOS)/vdi/vdi_misc.c \
              $(EMUTOS)/vdi/vdi_text.c $(EMUTOS)/vdi/vdi_textblit.c \
              $(EMUTOS)/vdi/vdi_esc.c $(EMUTOS)/vdi/vdi_input.c \
              $(EMUTOS)/bios/fnt_st_6x6.c $(EMUTOS)/bios/fnt_st_8x8.c \
              $(EMUTOS)/bios/fnt_st_8x16.c \
              $(EMUTOS)/bios/fnt_off_6x6.c $(EMUTOS)/bios/fnt_off_8x8.c \
              $(EMUTOS)/util/intmath.c \
              $(EMUTOS)/util/miscutil.c $(EMUTOS)/util/rectfunc.c $(EMUTOS)/util/optimize.c \
              $(EMUTOS)/aes/gemgsxif.c \
              $(EMUTOS)/aes/gemobed.c $(EMUTOS)/aes/gemfslib.c $(EMUTOS)/aes/gemfmlib.c \
              $(EMUTOS)/aes/gemrslib.c \
              $(EMUTOS)/aes/gemgraf.c $(EMUTOS)/aes/gemgrlib.c \
              $(EMUTOS)/aes/gemwrect.c $(EMUTOS)/aes/gem_rsc.c \
              $(EMUTOS)/aes/mforms.c
# The Wayland side. The protocol code is generated rather than written, from
# the descriptions in wayland-protocols.
WAYLAND_PROTOCOLS = $(shell pkg-config --variable=pkgdatadir wayland-protocols)
WAYLAND_SCANNER = $(shell pkg-config --variable=wayland_scanner wayland-scanner)
# The header is generated too, but only the source becomes an object
WAYLANDGENERATED = $(GEN)/xdg-shell-protocol.c $(GEN)/xdg-dialog-protocol.c \
                   $(GEN)/xdg-decoration-protocol.c
WAYLANDHEADERS = $(GEN)/xdg-shell-client-protocol.h \
                 $(GEN)/xdg-dialog-v1-client-protocol.h \
                 $(GEN)/xdg-decoration-unstable-v1-client-protocol.h
WAYLANDFLAGS = $(shell pkg-config --cflags wayland-client xkbcommon)
WAYLANDLIBS = $(shell pkg-config --libs wayland-client xkbcommon)

# Talking to the compositor without showing anything, which is what asking how
# large the display is amounts to. The daemon wants this and not the rest: it
# has no windows and no keyboard, and a keymap library it never calls is a
# dependency it should not have.
WAYLANDONLYLIBS = $(shell pkg-config --libs wayland-client)

# Building on a machine that has no Wayland to build against.
#
# Wayland is a build dependency here rather than a run time one. The emulator
# already runs with nobody to show anything to - gfx_open answers that there is
# no window and the screen stays in memory, which is what happens on a machine
# where nobody is logged in and what the whole test suite does anyway - but the
# headers and the libraries still have to be there to compile it at all.
#
# NO_WAYLAND=1 leaves them out. What goes with them is the half of gfx.c that
# opens windows and the question screen.c asks about how large the display is;
# everything an application can see is the same, because all of that happens in
# the emulated screen and none of it happens on the desktop. It is meant for a
# build server, which is a machine with no desktop for the answer to differ on.
ifdef NO_WAYLAND
WAYLANDGENERATED =
WAYLANDHEADERS =
WAYLANDFLAGS = -DNO_WAYLAND
WAYLANDLIBS =
WAYLANDONLYLIBS =
endif

EMUVDIFILES = emuvdi/hostvars.c emuvdi/hostfs.c emuvdi/fonts.c emuvdi/textblit.c emuvdi/bridge.c \
              emuvdi/gsx2.c emuvdi/gemoblib.c emuvdi/gemobjop.c emuvdi/gemfmalt.c emuvdi/gemmnlib.c emuvdi/vdi_raster.c emuvdi/aeskernel.c \
              emuvdi/strings.c

# Compilation flags
CC = gcc
LD = gcc
# -fno-pie, and -no-pie below, are not about tosemu's own code: they are what
# keeps every address in the program below the four gigabyte line, because GEM
# keeps pointers in thirty two bit fields and the VDI and AES are linked in
# here. See EMUTOSLDFLAGS.
# The tray icon is the one thing here that wants a library the rest does not,
# and a machine without it should get a working session rather than a build
# failure - so it is looked for rather than required.
DBUSFLAGS = $(shell pkg-config --cflags dbus-1 2>/dev/null && echo -DHAVE_DBUS)
DBUSLIBS  = $(shell pkg-config --libs dbus-1 2>/dev/null)

# And libpng, for the picture half of the clipboard, looked for the same way
# and for the same reason: a machine without it should get a session with a
# working text clipboard rather than a build that will not finish. When it is
# missing the scrap goes on holding pictures for GEM applications only.
PNGFLAGS = $(shell pkg-config --cflags libpng 2>/dev/null && echo -DHAVE_PNG)
PNGLIBS  = $(shell pkg-config --libs libpng 2>/dev/null)

# $(GEN) is on the include path for the same reason $(SRC) is: the generated
# headers - Musashi's m68kops.h, the Wayland protocol headers and the tray icon
# - are included by name, and where they were written is the build's business
# rather than something every source has to know.
CFLAGS = -I$(GEN) -I$(SRC)/Musashi -I$(SRC) -Wall -pedantic -fno-pie $(WAYLANDFLAGS) $(DBUSFLAGS) $(PNGFLAGS)
LDFLAGS = -no-pie

# Libraries go after the objects that want them, which is where the linker
# looks for them
LIBS = -lc $(WAYLANDLIBS) $(PNGLIBS)

# EmuTOS has its own idea of what compiles cleanly, so it gets its own flags.
#
# src/emuvdi/ comes first on the include path, which is how asm.h, intmath.h
# and the two trap binding headers there are reached instead of EmuTOS's own.
#
# __mcoldfire__ is EmuTOS's switch for a target without m68k inline assembly,
# which is what the host is. It selects the C rotates, the C bit_blt and the C
# loops, and sets ASM_BLIT_IS_AVAILABLE to 0 so none of the .S files are
# wanted. It is not true of the host in any other sense, so everything else it
# would decide is pinned here rather than left to follow from it. All of it is
# hardware that is not there.
#
# CONF_WITH_VDI_TEXT_SPEEDUP is off for a different reason, and it is not
# hardware. It turns on direct_screen_blit, which draws a byte at a time and
# picks which byte of a word by testing bit 3 of the x coordinate. That is only
# the right byte on a big endian machine. Surfaces here are host endian, which
# is what lets the rest of the VDI go unedited, so the two halves of every word
# are the other way round and the glyph lands in the wrong one. Turning it off
# sends all text through normal_blit, which works in words and does not care.
EMUTOSFLAGS = -I$(SRC)/emuvdi -I$(EMUTOS)/include -I$(EMUTOS)/vdi -I$(EMUTOS)/bios \
              -I$(EMUTOS)/aes -I$(EMUTOS)/desk -D__mcoldfire__ \
              -DCONF_WITH_BLITTER=0 -DCONF_WITH_VIDEL=0 -DCONF_WITH_TT_SHIFTER=0 \
              -DCONF_WITH_VDI_16BIT=0 -DCONF_WITH_VDI_TEXT_SPEEDUP=0 \
              -DCONF_WITH_ADVANCED_CPU=0 -DCONF_WITH_APOLLO_68080=0 \
              -DCONF_WITH_68030_PMMU=0 -DCONF_WITH_CACHE_CONTROL=0 \
              -DDETECT_NATIVE_FEATURES=0 -DCONF_WITH_COLDFIRE_RS232=0 \
              -DCONF_COLDFIRE_TIMER_C=0 -DCONF_WITH_EXTENDED_MOUSE=0 \
              -DCONF_WITH_MENU_EXTENSION=0 \
              -fno-pie

# GEM keeps pointers in 32 bit fields. An OBJECT's ob_spec is a LONG that
# holds the address of a string, a USERBLK holds the address of a routine, and
# an MFDB holds the address of a bitmap. That is not a shape anything can be
# talked out of: it is the layout applications and resource files are built to.
#
# So every address the AES and the VDI put in one has to fit in thirty two
# bits. Linking without position independence is what does it: the program
# lands at 0x400000 and its static data with it, well below the four gigabyte
# line. Anything allocated at run time has to come from below it as well - see
# host_vdi_alloc.
#
# A position independent build puts static data above 0x550000000000, where
# truncating an address to a LONG leaves a wild pointer that usually still
# points at something mapped, so it draws rubbish rather than crashing.
EMUTOSLDFLAGS = -no-pie

all: $(BIN)/tosemu $(BIN)/tosaesd

.PHONY: all tests check devpac-tests devpac-check lattice-tests lattice-check \
        emuvdi-check screen-check settings-check scrap-check demos clean

# A checkout without --recurse-submodules leaves the submodule an empty
# directory, and "No rule to make target" says nothing about why. This catches
# whichever of its sources is asked for first, which with a parallel build is
# not necessarily the first one listed.
$(EMUTOS)/%.c:
	@echo "The EmuTOS submodule is not there, so there is no VDI to build."
	@echo "Run:"
	@echo "    git submodule update --init"
	@false

# The programs run under the emulator. Their sources are in tests/ and demos/
# and what is built from them lands under $(BUILD) - see tests/Makefile, which
# explains why the suite runs there rather than where it is written.
tests:
	$(MAKE) -C tests/

# Programs meant to be looked at rather than checked. They need a compositor
# to be worth running: with one there is a window, and without one the screen
# is only in memory.
demos: $(BIN)/tosemu
	$(MAKE) -C demos/

# Test cases assembled by tosemu itself, using Devpac's GEN.TTP. These need a
# Devpac 3.10 installation, see tests/devpac/Makefile.
devpac-tests: $(BIN)/tosemu
	$(MAKE) -C tests/devpac

# Test cases compiled by tosemu itself, using Lattice C's LC1, LC2 and CLink.
# These need a Lattice C 5.60 installation, see tests/lattice/Makefile.
lattice-tests: $(BIN)/tosemu
	$(MAKE) -C tests/lattice

# Every object mirrors the path of the source it was compiled from, under
# $(OBJ) rather than next to it. The generated sources have no place in src/ to
# mirror, so they get one of their own under $(OBJ)/gen.
OBJECTS = $(patsubst %.c,$(OBJ)/%.o,$(SOURCEFILES) $(MUSASHIFILES)) \
          $(patsubst $(GEN)/%.c,$(OBJ)/gen/%.o,$(MUSASHIGENERATEDSOURCES) $(WAYLANDGENERATED))
# Objects from the submodule are built outside it. It is a checkout of somebody
# else's tree, and leaving build output in it means git reports it as dirty for
# work nobody did.
EMUTOSOBJECTS = $(patsubst $(EMUTOS)/%.c,$(OBJ)/emutos/%.o,$(EMUTOSFILES)) \
                $(patsubst %.c,$(OBJ)/%.o,$(EMUVDIFILES))
# The daemon's own two, which are none of the emulator's business and so are
# not in the list above. It links screen.o and settings.o from there as well -
# see bin/tosaesd below for why those two and nothing else.
DAEMONOBJECTS = $(OBJ)/aesd.o $(OBJ)/aesdtray.o

# How each of the four kinds of source is compiled. There is no built-in rule
# to fall back on any more - the built-in one writes the object next to its
# source, which is the whole of what this file is arranged to stop - so each
# says so for itself.
#
# All of them are written with -MMD -MP, which records the headers each object
# was built from in a .d file beside it. Without that, editing a header relinks
# stale objects: make has no other way to know that aesproto.h or screen.h has
# anything to do with an object built from a .c that includes it.
#
# And all of them depend on this file. Half of what the flags here do is choose
# between paths through the code rather than merely how to compile it -
# NO_WAYLAND takes half of gfx.c out, and the EmuTOS switches below decide what
# the VDI does - so changing a flag changes the program, and make has no other
# way to see that the objects are stale.
$(OBJ)/%.o: $(SRC)/%.c Makefile
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -MMD -MP -c -o $@ $<

$(OBJ)/gen/%.o: $(GEN)/%.c Makefile
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -MMD -MP -c -o $@ $<

# The EmuTOS sources and the code adapting them are the only ones built with
# EMUTOSFLAGS, hence a pair of rules of their own. Which of the two applies is
# decided by the shorter stem, so an object under $(OBJ)/emuvdi comes from here
# rather than from the general rule above.
$(OBJ)/emutos/%.o: $(EMUTOS)/%.c Makefile
	@mkdir -p $(dir $@)
	$(CC) $(EMUTOSFLAGS) -MMD -MP -c -o $@ $<

$(OBJ)/emuvdi/%.o: $(SRC)/emuvdi/%.c Makefile
	@mkdir -p $(dir $@)
	$(CC) $(EMUTOSFLAGS) -MMD -MP -c -o $@ $<

# Which headers each object was built from, written by -MMD above. The shim
# headers in src/emuvdi/ shadow EmuTOS's, so editing one changes what the
# sources in the submodule compile to, and there is no other way for make to
# see that.
DEPFILES = $(OBJECTS:.o=.d) $(EMUTOSOBJECTS:.o=.d) $(DAEMONOBJECTS:.o=.d)
-include $(DEPFILES)

# And a .d that is not there yet is not something to go and make. It is written
# as a side effect of compiling the object, so on a first build there are none
# and there is nothing wrong with that.
#
# Saying so stops make from looking for a way to build one, which it does by
# way of the built-in rule that links a program from the object of the same
# name: build/obj/emutos/vdi/vdi_main.d becomes a hunt for vdi_main.d.o and
# then for vdi_main.d.c in the submodule, which is not there and never was, so
# the $(EMUTOS)/%.c rule announces a missing submodule that is present. Dozens
# of those, one for each object, before the build has begun. An empty recipe is
# what ends the hunt; make ignores the failure, so they were only ever noise,
# but noise on the first line of the log somebody reads.
$(DEPFILES): ;

# The parts of EmuTOS that are generated rather than carried.
#
# A resource is drawn in a resource editor and kept as a .rsc and a .def, and
# EmuTOS turns each one into C when it builds rather than keeping the C - its
# .gitignore says as much. So a fresh checkout of the submodule has the inputs
# and none of the outputs, and the first source that includes gem_rsc.h stops
# the build. Three of them are wanted here: the AES's own resource, the mouse
# forms, and the header the localisation settings come out as.
#
# This is what a build server saw and this machine did not. A tree that has
# ever had them made is a tree where they are simply there, so the dependency
# was invisible from here and fatal from anywhere else.
#
# They are made by EmuTOS's own Makefile rather than by a copy of its rules,
# which is the same principle as the rest of the port: how the submodule builds
# is its own business, and src/emuvdi/ adapts the result. They are also the one
# thing this build writes outside $(BUILD), because where they go is EmuTOS's
# decision and not ours - all three are ignored by its git, so making them
# leaves the submodule as clean as it was found.
EMUTOSGENERATED = $(EMUTOS)/aes/gem_rsc.c $(EMUTOS)/aes/gem_rsc.h \
                  $(EMUTOS)/aes/mforms.c $(EMUTOS)/aes/mforms.h \
                  $(EMUTOS)/include/i18nconf.h

# The '%' where a '.' belongs is EmuTOS's trick, and it is here for their
# reason: one run of the tool writes both the .c and the .h, and only a pattern
# rule tells make that one run produces both. Two ordinary rules would be two
# runs, and with -j two runs at once over the same pair of files.
$(EMUTOS)/aes/gem_rsc%c $(EMUTOS)/aes/gem_rsc%h: \
                $(EMUTOS)/aes/gem%rsc $(EMUTOS)/aes/gem%def
	$(MAKE) -C $(EMUTOS) aes/gem_rsc.c

$(EMUTOS)/aes/mforms%c $(EMUTOS)/aes/mforms%h: \
                $(EMUTOS)/aes/mform%rsc $(EMUTOS)/aes/mform%def
	$(MAKE) -C $(EMUTOS) aes/mforms.c

$(EMUTOS)/include/i18nconf.h: $(EMUTOS)/localise.ctl
	$(MAKE) -C $(EMUTOS) include/i18nconf.h

# Every object built out of the submodule wants all of them there first. The
# $(OBJ)/emutos/%.o rule names only the source it compiles, and on a first
# build there are no .d files yet to say which headers that source went on to
# read.
$(EMUTOSOBJECTS): $(EMUTOSGENERATED)

# Main emulator target
$(BIN)/tosemu: $(OBJECTS) $(EMUTOSOBJECTS)
	@mkdir -p $(BIN)
	$(LD) $(LDFLAGS) $^ $(LIBS) -o $@

# The daemon several emulators have in common. It links none of the emulator's
# own workings: nothing it does involves a 68000, and everything it says is in
# aesproto.h.
#
# screen.c is the exception and has to be. The daemon is what decides which
# screen the session has - it tells each application when it arrives, including
# the accessories it starts itself - so it needs the same answer the emulator
# would have worked out alone, which means the same code rather than a second
# copy of it. That is also what puts wayland in this link: one of the screens
# is as large as the display, and only the compositor knows how large that is.
# The tray icon, turned into pixels the panel can be handed.
#
# Done here rather than at runtime because a picture that cannot be found at
# runtime leaves the item showing nothing at all, which looks exactly like a
# session that failed to start. Compiled in, it cannot go missing.
#
# It wants a rasteriser - rsvg-convert, ImageMagick or Inkscape - and does
# without one by drawing a plain mark instead, rather than failing the build.
#
# The header lands under $(GEN) rather than next to the picture it was drawn
# from, and is reached the way every other generated header is: by name, off
# the include path. See src/rsc/README.
$(GEN)/rsc/tray-icon.h: $(SRC)/rsc/tray.svg $(SRC)/rsc/icon-to-c.py
	@mkdir -p $(dir $@)
	python3 $(SRC)/rsc/icon-to-c.py $< $@

$(OBJ)/aesdtray.o: $(GEN)/rsc/tray-icon.h

$(BIN)/tosaesd: $(DAEMONOBJECTS) $(OBJ)/screen.o $(OBJ)/settings.o
	@mkdir -p $(BIN)
	$(LD) $(LDFLAGS) $^ $(DBUSLIBS) $(WAYLANDONLYLIBS) -o $@

# Turning a display into a screen, checked without a display. Built for the
# host rather than for the emulated machine, for the same reason bin/vditest
# is: what is being checked is not something an application can reach. A
# compositor's answer cannot be arranged by a test, so the asking is checked by
# using it and the arithmetic is checked here.
$(BIN)/screentest: $(SRC)/screentest.c $(OBJ)/screen.o $(OBJ)/settings.o
	@mkdir -p $(BIN)
	$(CC) $(CFLAGS) $(LDFLAGS) $^ $(WAYLANDONLYLIBS) -o $@

screen-check: $(BIN)/screentest
	./$(BIN)/screentest

# Reading a settings file, checked without one of somebody's own. Host-built
# for the same reason as the two above: what is checked here - a remark
# understood as a remark, a value said twice, a name spelled wrongly being
# complained about - is not visible from inside the emulated machine.
$(BIN)/settingstest: $(SRC)/settingstest.c $(OBJ)/settings.o
	@mkdir -p $(BIN)
	$(CC) $(CFLAGS) $(LDFLAGS) $^ -o $@

settings-check: $(BIN)/settingstest
	./$(BIN)/settingstest

# The character set and the line endings, checked without an emulator. Host
# built for the same reason as the rest of these: an emulated program can say
# what it read back, but not whether the bytes in between were right, and it
# cannot be handed a malformed UTF-8 sequence to be unbothered by.
$(BIN)/scraptest: $(SRC)/scraptest.c $(OBJ)/scraptext.o $(OBJ)/scrapimg.o
	@mkdir -p $(BIN)
	$(CC) $(CFLAGS) $(LDFLAGS) $^ $(PNGLIBS) -o $@

scrap-check: $(BIN)/scraptest
	./$(BIN)/scraptest

# Draws with the ported VDI and compares against what it should have drawn.
# Built for the host rather than for the emulated machine: it is the port that
# is being checked, not anything an application can reach yet.
#
# settings.o comes along because hostfs.c asks whether it is to say which
# directories were read, and that is a setting rather than a variable now.
$(BIN)/vditest: $(SRC)/emuvdi/vditest.c $(EMUTOSOBJECTS) $(OBJ)/settings.o
	@mkdir -p $(BIN)
	$(CC) $(EMUTOSFLAGS) $(EMUTOSLDFLAGS) $^ -o $@

emuvdi-check: $(BIN)/vditest
	./$(BIN)/vditest | diff -u $(SRC)/emuvdi/vditest.expected -

# The Wayland protocol code. wayland-scanner turns the protocol description
# into the marshalling both sides of the socket agree on, so it is generated
# here rather than carried.
$(GEN)/xdg-shell-protocol.c:
	@mkdir -p $(GEN)
	$(WAYLAND_SCANNER) private-code $(WAYLAND_PROTOCOLS)/stable/xdg-shell/xdg-shell.xml $@

$(GEN)/xdg-shell-client-protocol.h:
	@mkdir -p $(GEN)
	$(WAYLAND_SCANNER) client-header $(WAYLAND_PROTOCOLS)/stable/xdg-shell/xdg-shell.xml $@

$(GEN)/xdg-dialog-protocol.c:
	@mkdir -p $(GEN)
	$(WAYLAND_SCANNER) private-code $(WAYLAND_PROTOCOLS)/staging/xdg-dialog/xdg-dialog-v1.xml $@

$(GEN)/xdg-dialog-v1-client-protocol.h:
	@mkdir -p $(GEN)
	$(WAYLAND_SCANNER) client-header $(WAYLAND_PROTOCOLS)/staging/xdg-dialog/xdg-dialog-v1.xml $@

$(GEN)/xdg-decoration-protocol.c:
	@mkdir -p $(GEN)
	$(WAYLAND_SCANNER) private-code $(WAYLAND_PROTOCOLS)/unstable/xdg-decoration/xdg-decoration-unstable-v1.xml $@

$(GEN)/xdg-decoration-unstable-v1-client-protocol.h:
	@mkdir -p $(GEN)
	$(WAYLAND_SCANNER) client-header $(WAYLAND_PROTOCOLS)/unstable/xdg-decoration/xdg-decoration-unstable-v1.xml $@

$(OBJ)/gfx.o: $(WAYLANDHEADERS)

# Every object needs the generated m68kops.h, so none of them may be compiled
# before m64kmake has run
$(OBJECTS): $(MUSASHIGENERATEDFILES)

# Files generated using m64kmake. One run produces all of them, so they hang
# off a single stamp target to keep parallel builds from running it more than
# once at a time.
$(MUSASHIGENERATEDFILES): $(GEN)/.stamp

$(GEN)/.stamp: $(BIN)/m64kmake $(SRC)/Musashi/m68k_in.c
	@mkdir -p $(GEN)
	$(BIN)/m64kmake $(GEN)/ $(SRC)/Musashi/m68k_in.c > /dev/null
	touch $@

# The m64kmake generator. It is a build tool rather than part of tosemu, but
# it is compiled with the same flags, so it needs the same link option to go
# with the -fno-pie in them.
$(BIN)/m64kmake: $(SRC)/Musashi/m68kmake.c
	@mkdir -p $(BIN)
	$(CC) $(CFLAGS) -no-pie $< -o $@

check: $(BIN)/tosemu $(BIN)/tosaesd screen-check settings-check scrap-check
	$(MAKE) -C tests check

devpac-check: $(BIN)/tosemu
	$(MAKE) -C tests/devpac check

lattice-check: $(BIN)/tosemu
	$(MAKE) -C tests/lattice check

# Clean up the source tree, which is now two directories rather than a hunt.
# The tests and the demos are in there too - each of them builds into a
# directory of its own under $(BUILD) - so there is nothing to recurse into.
clean:
	$(RM) -r $(BUILD)/ $(BIN)/
