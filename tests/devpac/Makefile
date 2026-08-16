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

TESTNAMES = Pterm0 Pterm Cconout Cconws Bconout Fopen Fclose Fread \
            Fcreate Fwrite Fdelete Dcreate Supexec cmdline Fattrib \
            Fstraversal

TOS_ROOT ?= ../../../tos_root
GEN       = $(TOS_ROOT)/devpac31/bin/gen.ttp
TOSEMU    = ../../bin/tosemu

PROGRAMS = $(addsuffix .PRG,$(TESTNAMES))

all: $(PROGRAMS)

# Gen appends .S to the name it is given and writes <name>.PRG next to it. It
# exits with 20 when it found errors, so make notices a failed assembly.
%.PRG: %.s
	$(TOSEMU) $(GEN) $*

check: all
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

clean:
	$(RM) $(PROGRAMS) out out2 FNAME
	$(RM) -d DNAME

.PHONY: all check clean
