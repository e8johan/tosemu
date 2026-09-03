# Test cases assembled by tosemu itself.
#
# These are Devpac/Gen syntax versions of the m68k-atari-mint sources in the
# parent directory. Instead of cross assembling them on the host, they are
# built by running HiSoft Devpac 3.10's GEN.TTP inside tosemu, which makes the
# test suite exercise a real, non-trivial TOS application as well as the
# resulting binaries.
#
# Gen only recognises CR and CR/LF as line terminators, so the sources in this
# directory are stored with CR/LF endings. See .gitattributes.
#
# Point TOS_ROOT at the directory holding the devpac31 installation, e.g.
#
#   make TOS_ROOT=/path/to/tos_root check
#
# Everything this makes lands in a directory of its own under build/, and make
# runs there rather than here - see tests/Makefile, which explains why. Gen
# writes its output next to the source it read, so the sources are copied over
# as well: what somebody wrote stays here, and what a run produces goes there.

TESTNAMES = Pterm0 Pterm Cconout Cconws Bconout Fopen Fclose Fread \
            Fcreate Fwrite Fdelete Dcreate Supexec cmdline Fattrib \
            Fstraversal

# Where the sources are, taken from the makefile that was read rather than from
# the directory make is standing in, since it is standing in the build
# directory half the time
srcdir := $(patsubst %/,%,$(dir $(realpath $(firstword $(MAKEFILE_LIST)))))
BUILDDIR := $(abspath $(srcdir)/../../build/tests/devpac)

# A TOS_ROOT given by hand should be an absolute path. The default is not one,
# so it is anchored to where the sources are and not to wherever make is
# running from.
TOS_ROOT ?= $(abspath $(srcdir)/../../../tos_root)
GEN       = $(TOS_ROOT)/devpac31/bin/gen.ttp
TOSEMU    = ../../../bin/tosemu

PROGRAMS = $(addsuffix .PRG,$(TESTNAMES))

ifneq ($(CURDIR),$(BUILDDIR))

all check:
	@mkdir -p $(BUILDDIR)
	$(MAKE) -C $(BUILDDIR) -f $(srcdir)/Makefile TOS_ROOT=$(TOS_ROOT) $@

clean:
	$(RM) -r $(BUILDDIR)

else

all: $(PROGRAMS)

# Gen is handed a name and appends .S to it, so it reads the source from the
# directory it is run in. That is this one, which is why the source is copied
# here first - and it is what Gen writes the .PRG next to as well.
%.s: $(srcdir)/%.s
	cp -f $< $@

# The copies are a step on the way to a program, which make deletes again
# unless told not to, making every run reassemble
.SECONDARY: $(addsuffix .s,$(TESTNAMES))

# Gen exits with 20 when it found errors, so make notices a failed assembly.
%.PRG: %.s
	$(TOSEMU) $(GEN) $*

check: all
	# test-Fread reads the first hundred bytes of a file called Makefile,
	# for the reason given in tests/Makefile: there was one next to it when
	# it was written, and there is not one next to it here.
	ln -sfn $(srcdir)/Makefile Makefile
	$(TOSEMU) Pterm0.PRG
	$(TOSEMU) Pterm.PRG; test "$$?" = 42
	$(TOSEMU) Bconout.PRG > out && test "`cat out`" = 'Hello World!'
	$(TOSEMU) Cconout.PRG > out && test "`cat out`" = 'Hello World!'
	$(TOSEMU) Cconws.PRG > out && test "`cat out`" = 'Hello World!'
	$(TOSEMU) Fstraversal.PRG
	$(TOSEMU) Fopen.PRG
	$(TOSEMU) Fclose.PRG
	echo x | $(TOSEMU) Fread.PRG > out
	head -c100 Makefile > out2
	echo -n x >> out2
	cmp out out2
	$(TOSEMU) Supexec.PRG
	$(TOSEMU) Dcreate.PRG && rmdir DNAME
	$(TOSEMU) Fcreate.PRG && rm FNAME
	$(TOSEMU) Fwrite.PRG
	echo -n "hello world" > out
	cmp FNAME out
	$(TOSEMU) Fdelete.PRG
	$(TOSEMU) Fattrib.PRG
	$(TOSEMU) cmdline.PRG 12 345 6789 > out
	echo -n "12 345 6789" > out2
	cmp out out2
	$(RM) out out2

# Reached only by a make standing in the build directory, which is the one
# place where deleting the directory itself is not an option
clean:
	$(RM) $(PROGRAMS) $(addsuffix .s,$(TESTNAMES)) Makefile out out2 FNAME
	$(RM) -d DNAME

endif

.PHONY: all check clean
