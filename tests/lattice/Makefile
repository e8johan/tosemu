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
#
# Everything this makes lands in a directory of its own under build/, and make
# runs there rather than here - see tests/Makefile, which explains why. The
# passes write their quad files and objects beside the source they were given,
# so the sources are copied over as well: what somebody wrote stays here, and
# what a run produces goes there.

# c-drives and c-bios are built by running the passes one at a time, c-hello
# by LC.TTP, the driver that looks them up along PATH and Pexecs each of them
# itself. A failure then says whether the passes or the driver broke.
TESTNAMES   = c-drives c-bios
LCTESTNAMES = c-hello

# Where the sources are, taken from the makefile that was read rather than from
# the directory make is standing in, since it is standing in the build
# directory half the time
srcdir := $(patsubst %/,%,$(dir $(realpath $(firstword $(MAKEFILE_LIST)))))
BUILDDIR := $(abspath $(srcdir)/../../build/tests/lattice)

# A TOS_ROOT given by hand should be an absolute path. The default is not one,
# so it is anchored to where the sources are and not to wherever make is
# running from.
TOS_ROOT ?= $(abspath $(srcdir)/../../../tos_root)

# LC.TTP builds the path to each pass out of PATH and hands it to Pexec, and a
# TOS program expects the entries there to name a drive rather than to be
# relative to wherever it happens to have been started
LATTICE   = $(abspath $(TOS_ROOT)/lattice)
TOSEMU    = ../../../bin/tosemu

LC    = $(LATTICE)/BIN/LC.TTP
LC1   = $(LATTICE)/BIN/LC1.TTP
LC2   = $(LATTICE)/BIN/LC2.TTP
CLINK = $(LATTICE)/BIN/CLINK.TTP

# LC1 reads the standard headers from the directory in INCLUDE, and looks for
# its message file lc1.lc in the current directory and then along PATH. Setting
# PATH here only reaches LC1, the recipes name every host program by path.
LC1ENV = INCLUDE=$(LATTICE)/H/ PATH=$(LATTICE)/BIN/

PROGRAMS = $(addsuffix .PRG,$(TESTNAMES)) $(addsuffix .ttp,$(LCTESTNAMES))
OBJECTS  = $(addsuffix .o,$(TESTNAMES) $(LCTESTNAMES))
SOURCES  = $(addsuffix .c,$(TESTNAMES) $(LCTESTNAMES))

ifneq ($(CURDIR),$(BUILDDIR))

all check:
	@mkdir -p $(BUILDDIR)
	$(MAKE) -C $(BUILDDIR) -f $(srcdir)/Makefile TOS_ROOT=$(TOS_ROOT) $@

clean:
	$(RM) -r $(BUILDDIR)

else

all: $(PROGRAMS)

# A source file has to fit the 8.3 of a TOS file system and be reachable in a
# command line of 126 characters, so each is copied here and handed to the
# compiler by its bare name rather than by a path from somewhere else
%.c: $(srcdir)/%.c
	cp -f $< $@

# The copies and the object files are steps on the way to a program, which make
# deletes again unless told not to, making every run recompile
.SECONDARY: $(SOURCES) $(OBJECTS)

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

# The driver compiles and links in one go, writing its own control file for
# CLink and naming the result .ttp. A source file has to fit the 8.3 of a TOS
# file system: the command line the driver hands a pass is sized for one, and
# a longer name reaches the compiler cut short.
%.ttp: %.c
	$(LC1ENV) LIB=$(LATTICE)/LIB/ $(TOSEMU) $(LC) -L $<

check: all
	# The C library writes text mode line endings, i.e. CRLF, to files
	$(TOSEMU) c-hello.ttp > out
	printf 'Hello World!\r\n' > out2
	cmp out out2
	$(TOSEMU) c-drives.PRG > out; ! grep -q '^not ok' out
	$(TOSEMU) c-bios.PRG > out; ! grep -q '^not ok' out
	grep -q '^1\.\.' out
	$(RM) out out2

# Reached only by a make standing in the build directory, which is the one
# place where deleting the directory itself is not an option
clean:
	$(RM) $(PROGRAMS) $(OBJECTS) $(SOURCES)
	$(RM) $(addsuffix .q,$(TESTNAMES) $(LCTESTNAMES))
	$(RM) $(addsuffix .lnk,$(TESTNAMES) $(LCTESTNAMES)) out out2

endif

.PHONY: all check clean
