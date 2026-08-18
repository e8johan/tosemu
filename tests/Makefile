# Each testname in STESTNAME is built from a source file with the file name
# extension .s, each one in CTESTNAME from a source file with the extension .c
STESTNAME=Pterm Pterm0 Cconout Cconws Bconout Fstraversal \
          Fopen Fclose Fread Supexec Dcreate Fcreate Fwrite Fdelete Fattrib \
          cmdline
CTESTNAME=c-helloworld c-drives c-bios c-xbios c-pexec c-pexchild c-overlay c-gem c-vdi

CC=m68k-atari-mint-gcc

# The GEM tests would each open a window on whatever desktop is logged in,
# which is no way for a test suite to behave. The screen is in memory either
# way, and that is the part being tested.
TOSEMU=TOSEMU_NO_WINDOW=1 ../bin/tosemu

all: $(addprefix test-,$(STESTNAME) $(CTESTNAME))

test-%: %.s
	$(CC) $< -Wa,-S -nostdlib -o $@

test-%: %.c
	$(CC) $< -o $@

# The AES and VDI bindings live in their own library rather than in the C one
test-c-gem: c-gem.c
	$(CC) $< -lgem -o $@

test-c-vdi: c-vdi.c
	$(CC) $< -lgem -o $@

check: all
	$(TOSEMU) test-Pterm0
	$(TOSEMU) test-Pterm; test "$$?" = 42
	$(TOSEMU) test-Bconout > out && test "`cat out`" = 'Hello World!'
	$(TOSEMU) test-Cconout > out && test "`cat out`" = 'Hello World!'
	$(TOSEMU) test-Cconws > out && test "`cat out`" = 'Hello World!'
	$(TOSEMU) test-Fstraversal
	$(TOSEMU) test-Fopen
	$(TOSEMU) test-Fclose
	echo x | $(TOSEMU) test-Fread > out
	head -c100 Makefile >out2
	echo -n x >> out2
	cmp out out2
	$(TOSEMU) test-Supexec
	$(TOSEMU) test-Dcreate && rmdir DNAME
	$(TOSEMU) test-Fcreate && rm FNAME
	$(TOSEMU) test-Fwrite
	echo -n "hello world" > out
	cmp FNAME out
	$(TOSEMU) test-Fdelete
	$(TOSEMU) test-Fattrib
	$(TOSEMU) test-cmdline 12 345 6789 > out
	echo -n "12 345 6789" > out2
	cmp out out2
	# The C library writes text mode line endings, i.e. CRLF, to files
	$(TOSEMU) test-c-helloworld > out
	printf 'Hello World!\r\n' > out2
	cmp out out2
	$(TOSEMU) test-c-drives > out; ! grep -q '^not ok' out
	$(TOSEMU) test-c-bios > out; ! grep -q '^not ok' out
	$(TOSEMU) test-c-xbios > out; ! grep -q '^not ok' out
	$(TOSEMU) test-c-pexec > out; ! grep -q '^not ok' out
	$(TOSEMU) test-c-gem > out; ! grep -q '^not ok' out
	grep -q '^1\.\.' out
	$(TOSEMU) test-c-vdi > out; ! grep -q '^not ok' out
	# A GEM call that is not implemented stops the emulator, so the count at
	# the end of the output is what says every check in the file ran
	grep -q '^1\.\.' out
	# Pexec 200 leaves test-Pterm running in its place, which returns 42
	$(TOSEMU) test-c-overlay; test "$$?" = 42
	$(RM) out out2

clean:
	$(RM) $(addprefix test-,$(STESTNAME) $(CTESTNAME))
