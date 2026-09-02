# Demonstration programs, cross compiled for the emulated machine with the
# m68k-atari-mint tools. Unlike the tests these are meant to be looked at
# rather than checked, so run them with a compositor to hand:
#
#   make demos
#   ./bin/tosemu demos/dialog
#
DEMONAME=dialog window menu fsel userdef

# An accessory is named for what it is rather than after its source: the daemon
# looks for .ACC and nothing else
ACCNAME=DEMO.ACC

CC=m68k-atari-mint-gcc

all: $(DEMONAME) $(ACCNAME)

# The AES and VDI bindings live in their own library rather than in the C one
%: %.c
	$(CC) $< -lgem -o $@

DEMO.ACC: acc.c
	$(CC) $< -lgem -o $@

clean:
	$(RM) $(DEMONAME) $(ACCNAME)
