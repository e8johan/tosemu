# Each testname in STESTNAME is built from a source file with the file name
# extension .s, each one in CTESTNAME from a source file with the extension .c
STESTNAME=Pterm Pterm0 Cconout Cconws Bconout Fstraversal \
          Fopen Fclose Fread Supexec Dcreate Fcreate Fwrite Fdelete Fattrib \
          cmdline Malloc
CTESTNAME=c-helloworld c-drives c-bios c-xbios c-pexec c-pexchild c-overlay c-gem c-vdi c-aesd c-userdef c-icon c-boxchar c-fsel c-watchbox c-mkstate

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

test-c-userdef: c-userdef.c
	$(CC) $< -lgem -o $@

test-c-icon: c-icon.c
	$(CC) $< -lgem -o $@

test-c-boxchar: c-boxchar.c
	$(CC) $< -lgem -o $@

test-c-fsel: c-fsel.c
	$(CC) $< -lgem -o $@

test-c-watchbox: c-watchbox.c
	$(CC) $< -lgem -o $@

test-c-mkstate: c-mkstate.c
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
	# How much of the machine each of the two kinds of program is given, and
	# how each is entered - the same binary run twice, the extension being
	# the only difference, which is all the AES had to go on either. A
	# program owns all of it and has nothing to Malloc from until it
	# Mshrinks; an accessory owns room for itself, is handed its basepage in
	# a0, and Mallocs a stack out of what is left.
	$(TOSEMU) test-Malloc; test "$$?" = 1
	cp test-Malloc MALLOC.ACC
	$(TOSEMU) MALLOC.ACC; test "$$?" = 2
	rm -f MALLOC.ACC
	# The C library writes text mode line endings, i.e. CRLF, to files
	$(TOSEMU) test-c-helloworld > out
	printf 'Hello World!\r\n' > out2
	cmp out out2
	$(TOSEMU) test-c-drives > out; ! grep -q '^not ok' out
	# And again with the root of the drive moved, which is a different set
	# of paths through the same calls: everything relative stops meaning
	# what it did, and what Dgetpath hands out has to be a path on the
	# drive rather than one on the host
	TOS_BASE_PATH=$(CURDIR) $(TOSEMU) test-c-drives BASED > out; ! grep -q '^not ok' out
	grep -q '^1\.\.19' out
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
	# An object the application draws itself, which is the AES calling back
	# into the machine in the middle of a call out of it
	$(TOSEMU) test-c-userdef > out; ! grep -q '^not ok' out
	grep -q '^1\.\.' out
	# The objects that are pictures, which are the only ones whose ob_spec
	# leads to a bitmap rather than to a word or a string
	$(TOSEMU) test-c-icon > out; ! grep -q '^not ok' out
	grep -q '^1\.\.' out
	# One character packed into the object itself rather than pointed at,
	# which is every drive letter and every scroll bar arrow the AES draws
	$(TOSEMU) test-c-boxchar > out; ! grep -q '^not ok' out
	grep -q '^1\.\.' out
	# Opening the file selector more than once, which has to be the same
	# dialog every time. Three clicks to a pass: one the dialog throws
	# away, then the close box, then Cancel - see the test for why.
	TOSEMU_CLICKS='5,18 33,73 167,181 5,18 33,73 167,181 5,18 33,73 167,181' \
	  $(TOSEMU_FOR_A_WHILE) test-c-fsel > out; ! grep -q '^not ok' out
	grep -q '^1\.\.' out
	# Watching a box, which waits for the button to come up and so needs
	# one to have been pressed - twice, because it is watched twice
	TOSEMU_CLICKS='72,64 72,64' $(TOSEMU) test-c-watchbox > out; \
	  ! grep -q '^not ok' out
	grep -q '^1\.\.' out
	# Asking where the mouse is rather than waiting for it, which is what an
	# application tracking a slider does. The pointer is moved twice and
	# then pressed and not let go of, and none of it is waited for.
	TOSEMU_CLICKS='@100,60 @150,80 !200,100' $(TOSEMU) test-c-mkstate > out; \
	  ! grep -q '^not ok' out
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
