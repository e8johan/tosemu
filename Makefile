# Source files for TOS emulator
SOURCEFILES = main.c gemdos.c gemdosmem.c gemdoscon.c gemdosfile.c gemdosdrive.c gemdosproc.c \
              xbios.c xbiosscreen.c xbiossys.c xbiosdev.c bios.c \
              gem.c aesclient.c aes.c aesappl.c aesevnt.c aesgraf.c aeswind.c aesmenu.c aesframe.c aesfsel.c aesobjc.c aesrsrc.c aesscrp.c aesshel.c aestree.c vdi.c surface.c \
              gfx.c screen.c settings.c \
              tossystem.c utils.c memory.c cpu.c

# Hand-written Musashi files
MUSASHIFILES = Musashi/m68kcpu.c Musashi/m68kdasm.c

# Generated Musashi files
MUSASHIGENERATEDFILES = gen/m68kops.c gen/m68kopac.c gen/m68kopdm.c gen/m68kopnz.c gen/m68kops.h

# The VDI, which comes from EmuTOS. These are built as they stand, out of the
# submodule, and everything that adapts them is in emuvdi/ - see emuvdi/README.
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
WAYLANDGENERATED = gen/xdg-shell-protocol.c gen/xdg-dialog-protocol.c \
                   gen/xdg-decoration-protocol.c
WAYLANDHEADERS = gen/xdg-shell-client-protocol.h \
                 gen/xdg-dialog-v1-client-protocol.h \
                 gen/xdg-decoration-unstable-v1-client-protocol.h
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

CFLAGS = -Igen -IMusashi -I. -Wall -pedantic -fno-pie $(WAYLANDFLAGS) $(DBUSFLAGS)
LDFLAGS = -no-pie

# Libraries go after the objects that want them, which is where the linker
# looks for them
LIBS = -lc $(WAYLANDLIBS)

# EmuTOS has its own idea of what compiles cleanly, so it gets its own flags.
#
# emuvdi/ comes first on the include path, which is how asm.h, intmath.h and
# the two trap binding headers there are reached instead of EmuTOS's own.
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
EMUTOSFLAGS = -Iemuvdi -I$(EMUTOS)/include -I$(EMUTOS)/vdi -I$(EMUTOS)/bios \
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

all: bin/tosemu bin/tosaesd

.PHONY: tests check devpac-tests devpac-check lattice-tests lattice-check \
        emuvdi-check screen-check settings-check demos

# A checkout without --recurse-submodules leaves the submodule an empty
# directory, and "No rule to make target" says nothing about why. This catches
# whichever of its sources is asked for first, which with a parallel build is
# not necessarily the first one listed.
$(EMUTOS)/%.c:
	@echo "The EmuTOS submodule is not there, so there is no VDI to build."
	@echo "Run:"
	@echo "    git submodule update --init"
	@false

tests:
	$(MAKE) -C tests/

# Programs meant to be looked at rather than checked. They need a compositor
# to be worth running: with one there is a window, and without one the screen
# is only in memory.
demos: bin/tosemu
	$(MAKE) -C demos/

# Test cases assembled by tosemu itself, using Devpac's GEN.TTP. These need a
# Devpac 3.10 installation, see tests/devpac/Makefile.
devpac-tests: bin/tosemu
	$(MAKE) -C tests/devpac

# Test cases compiled by tosemu itself, using Lattice C's LC1, LC2 and CLink.
# These need a Lattice C 5.60 installation, see tests/lattice/Makefile.
lattice-tests: bin/tosemu
	$(MAKE) -C tests/lattice

OBJECTS = $(addsuffix .o,$(basename $(SOURCEFILES) $(MUSASHIFILES) $(MUSASHIGENERATEDFILES) $(WAYLANDGENERATED)))
# Objects from the submodule are built outside it. It is a checkout of somebody
# else's tree, and leaving build output in it means git reports it as dirty for
# work nobody did.
EMUTOSOBJECTS = $(patsubst $(EMUTOS)/%.c,emuvdi/obj/%.o,$(EMUTOSFILES)) \
                $(addsuffix .o,$(basename $(EMUVDIFILES)))

# The EmuTOS sources and the code adapting them are the only ones built with
# EMUTOSFLAGS, so they need rules of their own rather than the built-in one.
#
# They depend on this file as well as on their source. Half of what EMUTOSFLAGS
# does is choose between paths inside EmuTOS rather than merely how to compile
# them, so changing a flag changes the program, and make has no other way to
# know that the objects are stale.
emuvdi/obj/%.o: $(EMUTOS)/%.c Makefile
	@mkdir -p $(dir $@)
	$(CC) $(EMUTOSFLAGS) -MMD -MP -c -o $@ $<

emuvdi/%.o: emuvdi/%.c Makefile
	$(CC) $(EMUTOSFLAGS) -MMD -MP -c -o $@ $<

# Which headers each object was built from, written by -MMD above. The shim
# headers in emuvdi/ shadow EmuTOS's, so editing one changes what the sources
# in the submodule compile to, and there is no other way for make to see that.
-include $(EMUTOSOBJECTS:.o=.d)

# And a .d that is not there yet is not something to go and make. It is written
# as a side effect of compiling the object, so on a first build there are none
# and there is nothing wrong with that.
#
# Saying so stops make from looking for a way to build one, which it does by
# way of the built-in rule that links a program from the object of the same
# name: emuvdi/obj/vdi/vdi_main.d becomes a hunt for vdi_main.d.o and then for
# vdi_main.d.c in the submodule, which is not there and never was, so the
# $(EMUTOS)/%.c rule announces a missing submodule that is present. Thirty-two
# of those, one for each object, before the build has begun. An empty recipe is
# what ends the hunt; make ignores the failure, so they were only ever noise,
# but noise on the first line of the log somebody reads.
$(EMUTOSOBJECTS:.o=.d): ;

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
# is its own business, and emuvdi/ adapts the result. None of the three needs a
# cross compiler - the tools that write them are built for the host - and all
# of them are ignored by EmuTOS's git, so making them leaves the submodule as
# clean as it was found.
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
# emuvdi/obj/%.o rule names only the source it compiles, and on a first build
# there are no .d files yet to say which headers that source went on to read.
$(EMUTOSOBJECTS): $(EMUTOSGENERATED)

# Main emulator target
bin/tosemu: $(OBJECTS) $(EMUTOSOBJECTS)
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
rsc/tray-icon.h: rsc/tray.svg rsc/icon-to-c.py
	python3 rsc/icon-to-c.py $< $@

aesdtray.o: rsc/tray-icon.h

bin/tosaesd: aesd.o aesdtray.o screen.o settings.o
	$(LD) $(LDFLAGS) $^ $(DBUSLIBS) $(WAYLANDONLYLIBS) -o $@

# Turning a display into a screen, checked without a display. Built for the
# host rather than for the emulated machine, for the same reason bin/vditest
# is: what is being checked is not something an application can reach. A
# compositor's answer cannot be arranged by a test, so the asking is checked by
# using it and the arithmetic is checked here.
bin/screentest: screentest.c screen.o settings.o
	@mkdir -p bin/
	$(CC) $(CFLAGS) $(LDFLAGS) $^ $(WAYLANDONLYLIBS) -o $@

screen-check: bin/screentest
	./bin/screentest

# Reading a settings file, checked without one of somebody's own. Host-built
# for the same reason as the two above: what is checked here - a remark
# understood as a remark, a value said twice, a name spelled wrongly being
# complained about - is not visible from inside the emulated machine.
bin/settingstest: settingstest.c settings.o
	@mkdir -p bin/
	$(CC) $(CFLAGS) $(LDFLAGS) $^ -o $@

settings-check: bin/settingstest
	./bin/settingstest

# Draws with the ported VDI and compares against what it should have drawn.
# Built for the host rather than for the emulated machine: it is the port that
# is being checked, not anything an application can reach yet.
#
# settings.o comes along because hostfs.c asks whether it is to say which
# directories were read, and that is a setting rather than a variable now.
bin/vditest: emuvdi/vditest.c $(EMUTOSOBJECTS) settings.o
	@mkdir -p bin/
	$(CC) $(EMUTOSFLAGS) $(EMUTOSLDFLAGS) $^ -o $@

emuvdi-check: bin/vditest
	./bin/vditest | diff -u emuvdi/vditest.expected -

# The Wayland protocol code. wayland-scanner turns the protocol description
# into the marshalling both sides of the socket agree on, so it is generated
# here rather than carried.
gen/xdg-shell-protocol.c:
	@mkdir -p gen/
	$(WAYLAND_SCANNER) private-code $(WAYLAND_PROTOCOLS)/stable/xdg-shell/xdg-shell.xml $@

gen/xdg-shell-client-protocol.h:
	@mkdir -p gen/
	$(WAYLAND_SCANNER) client-header $(WAYLAND_PROTOCOLS)/stable/xdg-shell/xdg-shell.xml $@

gen/xdg-dialog-protocol.c:
	@mkdir -p gen/
	$(WAYLAND_SCANNER) private-code $(WAYLAND_PROTOCOLS)/staging/xdg-dialog/xdg-dialog-v1.xml $@

gen/xdg-dialog-v1-client-protocol.h:
	@mkdir -p gen/
	$(WAYLAND_SCANNER) client-header $(WAYLAND_PROTOCOLS)/staging/xdg-dialog/xdg-dialog-v1.xml $@

gen/xdg-decoration-protocol.c:
	@mkdir -p gen/
	$(WAYLAND_SCANNER) private-code $(WAYLAND_PROTOCOLS)/unstable/xdg-decoration/xdg-decoration-unstable-v1.xml $@

gen/xdg-decoration-unstable-v1-client-protocol.h:
	@mkdir -p gen/
	$(WAYLAND_SCANNER) client-header $(WAYLAND_PROTOCOLS)/unstable/xdg-decoration/xdg-decoration-unstable-v1.xml $@

gfx.o: $(WAYLANDHEADERS)

# The two files NO_WAYLAND changes, which depend on this one as well as on
# their source. What the flag decides is what those objects contain rather than
# merely how they were compiled, and make has no other way to see that turning
# it on or off has made them stale - the same reason the EmuTOS objects above
# depend on this file.
gfx.o screen.o: Makefile

# Every object needs the generated m68kops.h, so none of them may be compiled
# before m64kmake has run
$(OBJECTS): $(MUSASHIGENERATEDFILES)

# Files generated using m64kmake. One run produces all of them, so they hang
# off a single stamp target to keep parallel builds from running it more than
# once at a time.
$(MUSASHIGENERATEDFILES): gen/.stamp

gen/.stamp: bin/m64kmake Musashi/m68k_in.c
	mkdir -p gen/
	bin/m64kmake gen/ Musashi/m68k_in.c > /dev/null
	touch $@

# The m64kmake generator. It is a build tool rather than part of tosemu, but
# it is compiled with the same flags, so it needs the same link option to go
# with the -fno-pie in them.
bin/m64kmake: Musashi/m68kmake.c
	mkdir -p bin/
	$(CC) $(CFLAGS) -no-pie $< -o $@

check: bin/tosemu bin/tosaesd screen-check settings-check
	$(MAKE) -C tests check

devpac-check: bin/tosemu
	$(MAKE) -C tests/devpac check

lattice-check: bin/tosemu
	$(MAKE) -C tests/lattice check

# Clean up the source tree
clean:
	$(RM) *.o Musashi/*.o emuvdi/*.o emuvdi/*.d
	$(RM) -r emuvdi/obj/
	$(RM) gen/*
	$(RM) bin/*
	$(RM) -d gen/ bin/
	$(MAKE) -C tests/ clean
	$(MAKE) -C demos/ clean
	$(MAKE) -C tests/devpac clean
	$(MAKE) -C tests/lattice clean
