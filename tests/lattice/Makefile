# Test cases compiled by tosemu itself.
#
# These are Lattice C 5.60 versions of the m68k-atari-mint sources in the
# parent directory. Instead of cross compiling them on the host, they are built
# by running Lattice's own LC1.TTP, LC2.TTP and CLINK.TTP inside tosemu, which
# makes the test suite exercise a real, non-trivial C tool chain as well as the
# binaries it produces.
#
# Point TOS_ROOT at the directory holding the lattice installation, e.g.
#
#   make TOS_ROOT=/path/to/tos_root check

TESTNAMES = c-helloworld c-drives c-bios

TOS_ROOT ?= ../../../tos_root
LATTICE   = $(TOS_ROOT)/lattice
TOSEMU    = ../../bin/tosemu

LC1   = $(LATTICE)/BIN/LC1.TTP
LC2   = $(LATTICE)/BIN/LC2.TTP
CLINK = $(LATTICE)/BIN/CLINK.TTP

# LC1 reads the standard headers from the directory in INCLUDE, and looks for
# its message file lc1.lc in the current directory and then along PATH. Setting
# PATH here only reaches LC1, the recipes name every host program by path.
LC1ENV = INCLUDE=$(LATTICE)/H/ PATH=$(LATTICE)/BIN/

PROGRAMS = $(addsuffix .PRG,$(TESTNAMES))
OBJECTS  = $(addsuffix .o,$(TESTNAMES))

all: $(PROGRAMS)

# The object files are a step on the way to a program, which make deletes again
# unless told not to, making every run recompile
.SECONDARY: $(OBJECTS)

# LC1 parses the source into a quad file, which LC2 turns into an object file
# and then deletes. Both exit non-zero when they found errors, so make notices
# a failed compilation.
%.o: %.c
	$(LC1ENV) $(TOSEMU) $(LC1) $<
	$(TOSEMU) $(LC2) $*

# A TOS command line holds 126 characters, which the startup module and the
# library do not fit into next to a TOS_ROOT of any length. CLink reads them
# from a control file instead.
%.PRG: %.o
	printf '%s\n' 'FROM $(LATTICE)/LIB/C.O $<' 'TO $@' \
	              'LIB $(LATTICE)/LIB/LC.LIB' > $*.lnk
	$(TOSEMU) $(CLINK) WITH $*.lnk

check: all
	# The C library writes text mode line endings, i.e. CRLF, to files
	$(TOSEMU) c-helloworld.PRG > out
	printf 'Hello World!\r\n' > out2
	cmp out out2
	$(TOSEMU) c-drives.PRG > out; ! grep -q '^not ok' out
	$(TOSEMU) c-bios.PRG > out; ! grep -q '^not ok' out
	$(RM) out out2

clean:
	$(RM) $(PROGRAMS) $(OBJECTS) $(addsuffix .q,$(TESTNAMES))
	$(RM) $(addsuffix .lnk,$(TESTNAMES)) out out2

.PHONY: all check clean
