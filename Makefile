# Source files for TOS emulator
SOURCEFILES = main.c gemdos.c gemdosmem.c gemdoscon.c gemdosfile.c gemdosdrive.c gemdosproc.c \
              xbios.c xbiosscreen.c xbiossys.c xbiosdev.c bios.c \
              gem.c aes.c aesappl.c vdi.c \
              tossystem.c utils.c memory.c cpu.c

# Hand-written Musashi files
MUSASHIFILES = Musashi/m68kcpu.c Musashi/m68kdasm.c

# Generated Musashi files
MUSASHIGENERATEDFILES = gen/m68kops.c gen/m68kopac.c gen/m68kopdm.c gen/m68kopnz.c gen/m68kops.h

# The VDI, which comes from EmuTOS. These are built as they stand, out of the
# submodule, and everything that adapts them is in emuvdi/ - see emuvdi/README.
EMUTOS = 3rdparty/emutos
#
# vdi_text.c and vdi_textblit.c are not here yet. They call normal_blit, which
# is the inner loop that puts a character on the screen, and EmuTOS has it only
# in assembly: vdi_tblit.S and a ColdFire variant, with no C behind either. It
# has to be written before text can be drawn, and it needs the fonts alongside
# it, so both arrive together.
EMUTOSFILES = $(EMUTOS)/vdi/vdi_line.c $(EMUTOS)/vdi/vdi_fill.c \
              $(EMUTOS)/vdi/vdi_raster.c $(EMUTOS)/vdi/vdi_col.c \
              $(EMUTOS)/vdi/vdi_bezier.c $(EMUTOS)/vdi/vdi_gdp.c \
              $(EMUTOS)/vdi/vdi_marker.c $(EMUTOS)/vdi/vdi_misc.c \
              $(EMUTOS)/util/intmath.c
EMUVDIFILES = emuvdi/hostvars.c

# Compilation flags
CC = gcc
LD = gcc
CFLAGS = -Igen -IMusashi -I. -Wall -pedantic
LDFLAGS = -lc

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
EMUTOSFLAGS = -Iemuvdi -I$(EMUTOS)/include -I$(EMUTOS)/vdi -I$(EMUTOS)/bios \
              -I$(EMUTOS)/aes -D__mcoldfire__ \
              -DCONF_WITH_BLITTER=0 -DCONF_WITH_VIDEL=0 -DCONF_WITH_TT_SHIFTER=0 \
              -DCONF_WITH_VDI_16BIT=0 -DCONF_WITH_VDI_TEXT_SPEEDUP=0 \
              -DCONF_WITH_ADVANCED_CPU=0 -DCONF_WITH_APOLLO_68080=0 \
              -DCONF_WITH_68030_PMMU=0 -DCONF_WITH_CACHE_CONTROL=0 \
              -DDETECT_NATIVE_FEATURES=0 -DCONF_WITH_COLDFIRE_RS232=0 \
              -DCONF_COLDFIRE_TIMER_C=0

all: bin/tosemu

.PHONY: tests check devpac-tests devpac-check lattice-tests lattice-check emuvdi-check

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

# Test cases assembled by tosemu itself, using Devpac's GEN.TTP. These need a
# Devpac 3.10 installation, see tests/devpac/Makefile.
devpac-tests: bin/tosemu
	$(MAKE) -C tests/devpac

# Test cases compiled by tosemu itself, using Lattice C's LC1, LC2 and CLink.
# These need a Lattice C 5.60 installation, see tests/lattice/Makefile.
lattice-tests: bin/tosemu
	$(MAKE) -C tests/lattice

OBJECTS = $(addsuffix .o,$(basename $(SOURCEFILES) $(MUSASHIFILES) $(MUSASHIGENERATEDFILES)))
# Objects from the submodule are built outside it. It is a checkout of somebody
# else's tree, and leaving build output in it means git reports it as dirty for
# work nobody did.
EMUTOSOBJECTS = $(patsubst $(EMUTOS)/%.c,emuvdi/obj/%.o,$(EMUTOSFILES)) \
                $(addsuffix .o,$(basename $(EMUVDIFILES)))

# The EmuTOS sources and the code adapting them are the only ones built with
# EMUTOSFLAGS, so they need rules of their own rather than the built-in one
emuvdi/obj/%.o: $(EMUTOS)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(EMUTOSFLAGS) -c -o $@ $<

emuvdi/%.o: emuvdi/%.c
	$(CC) $(EMUTOSFLAGS) -c -o $@ $<

# Main emulator target
bin/tosemu: $(OBJECTS)
	$(LD) $(LDFLAGS) $^ -o $@

# Draws with the ported VDI and compares against what it should have drawn.
# Built for the host rather than for the emulated machine: it is the port that
# is being checked, not anything an application can reach yet.
bin/vditest: emuvdi/vditest.c $(EMUTOSOBJECTS)
	@mkdir -p bin/
	$(CC) $(EMUTOSFLAGS) $^ -o $@

emuvdi-check: bin/vditest
	./bin/vditest | diff -u emuvdi/vditest.expected -

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

# The m64kmake generator
bin/m64kmake: Musashi/m68kmake.c
	mkdir -p bin/
	$(CC) $(CFLAGS) $< -o $@

check: bin/tosemu
	$(MAKE) -C tests check

devpac-check: bin/tosemu
	$(MAKE) -C tests/devpac check

lattice-check: bin/tosemu
	$(MAKE) -C tests/lattice check

# Clean up the source tree
clean:
	$(RM) *.o Musashi/*.o emuvdi/*.o
	$(RM) -r emuvdi/obj/
	$(RM) gen/*
	$(RM) bin/*
	$(RM) -d gen/ bin/
	$(MAKE) -C tests/ clean
	$(MAKE) -C tests/devpac clean
	$(MAKE) -C tests/lattice clean
