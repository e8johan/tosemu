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

# c-drives and c-bios are built by running the passes one at a time, c-hello
# by LC.TTP, the driver that looks them up along PATH and Pexecs each of them
# itself. A failure then says whether the passes or the driver broke.
TESTNAMES   = c-drives c-bios
LCTESTNAMES = c-hello

TOS_ROOT ?= ../../../tos_root

# LC.TTP builds the path to each pass out of PATH and hands it to Pexec, and a
# TOS program expects the entries there to name a drive rather than to be
# relative to wherever it happens to have been started
LATTICE   = $(abspath $(TOS_ROOT)/lattice)
TOSEMU    = ../../bin/tosemu

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

clean:
	$(RM) $(PROGRAMS) $(OBJECTS)
	$(RM) $(addsuffix .q,$(TESTNAMES) $(LCTESTNAMES))
	$(RM) $(addsuffix .lnk,$(TESTNAMES) $(LCTESTNAMES)) out out2

.PHONY: all check clean
