# Each testname in STESTNAME is built from a source file with the file name
# extension .s, each one in CTESTNAME from a source file with the extension .c
STESTNAME=Pterm Pterm0 Cconout Cconws Bconout Fstraversal \
          Fopen Fclose Fread Supexec Dcreate Fcreate Fwrite Fdelete Fattrib \
          cmdline
CTESTNAME=c-helloworld c-drives c-bios c-xbios c-pexec c-pexchild c-overlay c-gem c-vdi c-aesd

CC=m68k-atari-mint-gcc

# The GEM tests would each open a window on whatever desktop is logged in,
# which is no way for a test suite to behave. The screen is in memory either
# way, and that is the part being tested.
TOSEMU=TOSEMU_NO_WINDOW=1 ../bin/tosemu

# The same, with a limit on how long it may take. The tests that need a daemon
# leave a process waiting for a message, and one that never comes would
# otherwise wait for ever rather than fail.
TOSEMU_FOR_A_WHILE=timeout 20 env TOSEMU_NO_WINDOW=1 ../bin/tosemu

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

test-c-aesd: c-aesd.c
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
	# A dialog needs something to press a button in it, and a test has
	# nobody to do it, so the Return that picks the default button is given
	# on the command line
	TOSEMU_KEYS='\r\r\r' TOSEMU_CLICKS='60,116' $(TOSEMU) test-c-gem > out; ! grep -q '^not ok' out
	grep -q '^1\.\.' out
	$(TOSEMU) test-c-vdi > out; ! grep -q '^not ok' out
	# A GEM call that is not implemented stops the emulator, so the count at
	# the end of the output is what says every check in the file ran
	grep -q '^1\.\.' out
	# Pexec 200 leaves test-Pterm running in its place, which returns 42
	$(TOSEMU) test-c-overlay; test "$$?" = 42
	#
	# Two applications and the daemon between them, which is the part that
	# cannot be tested with one process: with one, the identifier is always
	# nought, appl_find always answers -1, and a message posted comes back
	# to the sender. The daemon gets a socket of its own so that a test run
	# does not disturb a session somebody is using.
	TOSEMU_AESD=$(CURDIR)/aesd.socket ../bin/tosaesd & \
	  echo $$! > aesd.pid; \
	  sleep 1; \
	  TOSEMU_AESD=$(CURDIR)/aesd.socket $(TOSEMU_FOR_A_WHILE) test-c-aesd > out & \
	  echo $$! > waiter.pid; \
	  sleep 1; \
	  TOSEMU_AESD=$(CURDIR)/aesd.socket $(TOSEMU_FOR_A_WHILE) test-c-aesd 'TEST-C-A' > out2; \
	  wait `cat waiter.pid` || true; \
	  kill `cat aesd.pid`
	! grep -q '^not ok' out
	! grep -q '^not ok' out2
	grep -q '^1\.\.' out
	grep -q '^1\.\.' out2
	$(RM) out out2 aesd.pid waiter.pid aesd.socket

clean:
	$(RM) $(addprefix test-,$(STESTNAME) $(CTESTNAME))
