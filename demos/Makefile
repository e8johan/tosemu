# Demonstration programs, cross compiled for the emulated machine with the
# m68k-atari-mint tools. Unlike the tests these are meant to be looked at
# rather than checked, so run them with a compositor to hand:
#
#   make demos
#   ./bin/tosemu build/demos/dialog
#
DEMONAME=dialog window menu fsel userdef

# An accessory is named for what it is rather than after its source: the daemon
# looks for .ACC and nothing else
ACCNAME=DEMO.ACC

# Where the programs go. Nothing built belongs next to the sources it was built
# from, and a demo is a program like any other - see the top level Makefile.
BUILDDIR = ../build/demos

CC=m68k-atari-mint-gcc

all: $(addprefix $(BUILDDIR)/,$(DEMONAME) $(ACCNAME))

$(BUILDDIR):
	mkdir -p $@

# The AES and VDI bindings live in their own library rather than in the C one
$(BUILDDIR)/%: %.c | $(BUILDDIR)
	$(CC) $< -lgem -o $@

$(BUILDDIR)/DEMO.ACC: acc.c | $(BUILDDIR)
	$(CC) $< -lgem -o $@

clean:
	$(RM) -r $(BUILDDIR)

.PHONY: all clean
