# Each testname in STESTNAME is built from a source file with the file name
# extension .s, each one in CTESTNAME from a source file with the extension .c
STESTNAME=Pterm Pterm0 Cconout Cconws Bconout Fstraversal \
          Fopen Fclose Fread Supexec Dcreate Fcreate Fwrite Fdelete Fattrib \
          cmdline Malloc
CTESTNAME=c-helloworld c-drives c-bios c-xbios c-mxalloc c-pexec c-pexchild c-overlay c-gem c-vdi c-aesd c-acc c-userdef c-icon c-boxchar c-fsel c-watchbox c-mkstate c-screen c-menu c-frame c-resize

CC=m68k-atari-mint-gcc

# A socket that is not there, so that a session somebody is using is not what
# these run against. A daemon decides things a test has an opinion about -
# which application is which, and which screen the machine has - and it decides
# them for every emulator that finds it, so one running in the background turns
# the results into a matter of what else is on the computer. The tests that
# want a daemon start one of their own and say where it is.
NO_DAEMON=TOSEMU_AESD=$(CURDIR)/nosuch.socket

# And a compositor that is not there, for the screens whose size is as large as
# the display. What a compositor answers is not something a test can arrange -
# one machine has two displays and a build server has none - so the screens
# that ask are checked for what they come to when there is nothing to ask, and
# the arithmetic for when there is gets bin/screentest, which is handed the
# numbers rather than told to go and find them.
NO_DISPLAY=WAYLAND_DISPLAY=nosuch.wayland

# The GEM tests would each open a window on whatever desktop is logged in,
# which is no way for a test suite to behave. The screen is in memory either
# way, and that is the part being tested.
TOSEMU=TOSEMU_NO_WINDOW=1 $(NO_DAEMON) ../bin/tosemu --no-config

# The same, with a limit on how long it may take. The tests that need a daemon
# leave a process waiting for a message, and one that never comes would
# otherwise wait for ever rather than fail. Which daemon, if any, is left to
# the caller: most of these are the tests that want a real one.
TOSEMU_FOR_A_WHILE=timeout 20 env TOSEMU_NO_WINDOW=1 ../bin/tosemu --no-config

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

test-c-acc: c-acc.c
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

test-c-screen: c-screen.c
	$(CC) $< -lgem -o $@

test-c-menu: c-menu.c
	$(CC) $< -lgem -o $@

test-c-frame: c-frame.c
	$(CC) $< -lgem -o $@

test-c-resize: c-resize.c
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
	# And the same root spelled with a separator on the end, which is the
	# natural way to write a directory in a settings file. It has to name
	# the same drive: the base is what the front of every host path is
	# compared against, and one ending in two separators is a base no path
	# resolves to, so the whole drive reads as empty.
	TOS_BASE_PATH=$(CURDIR)/ $(TOSEMU) test-c-drives BASED > out; \
	  ! grep -q '^not ok' out
	grep -q '^1\.\.19' out
	$(TOSEMU) test-c-bios > out; ! grep -q '^not ok' out
	grep -q '^1\.\.' out
	$(TOSEMU) test-c-xbios > out; ! grep -q '^not ok' out
	# Asking for memory and saying which kind it has to be, which is the
	# call every compiler of the period makes before anything else
	$(TOSEMU) test-c-mxalloc > out; ! grep -q '^not ok' out
	grep -q '^1\.\.' out
	$(TOSEMU) test-c-pexec > out; ! grep -q '^not ok' out
	# A dialog needs something to press a button in it, and a test has
	# nobody to do it, so the Return that picks the default button is given
	# on the command line
	TOSEMU_KEYS='\r\r\r' TOSEMU_CLICKS='60,116' $(TOSEMU) test-c-gem > out; ! grep -q '^not ok' out
	grep -q '^1\.\.' out
	# On a screen with colours in it, because half of what the VDI does is
	# decide which planes to light and there is nothing to decide with one.
	# Twice, on the two shapes of screen that have the same four planes: the
	# drawing is the same, and what differs is how many bytes a row is,
	# which is the arithmetic every one of these calls does first.
	TOSEMU_SCREEN=low $(TOSEMU) test-c-vdi low > out; ! grep -q '^not ok' out
	# A GEM call that is not implemented stops the emulator, so the count at
	# the end of the output is what says every check in the file ran
	grep -q '^1\.\.' out
	TOSEMU_SCREEN=tt-medium $(TOSEMU) test-c-vdi tt-medium > out; \
	  ! grep -q '^not ok' out
	grep -q '^1\.\.' out
	# Which screen the machine turned out to have, which an application is
	# told once and lays everything out for. Every one of them, the first
	# without asking because that is the default, and then a run where a
	# daemon was asked for one screen and the emulator for another: the
	# daemon is what decides, because the screen has to be one screen for
	# everything sharing it.
	$(TOSEMU) test-c-screen high > out; ! grep -q '^not ok' out
	grep -q '^1\.\.' out
	for s in low medium high tt-medium tt-high; do \
	  TOSEMU_SCREEN=$$s $(TOSEMU) test-c-screen $$s > out || exit 1; \
	  ! grep -q '^not ok' out || exit 1; \
	  grep -q '^1\.\.' out || exit 1; \
	done
	# The two whose size is a rule rather than a number, with nothing to
	# ask. A compositor is not something a test can arrange - this machine
	# may have two displays and a build server none - so what is checked
	# here is what they come to when there is none, and the arithmetic for
	# when there is one is bin/screentest's to check. The planes are the
	# half that holds either way, and they are what differs below.
	for s in native-mono native-color; do \
	  TOSEMU_SCREEN=$$s $(NO_DISPLAY) $(TOSEMU) test-c-screen $$s > out \
	    || exit 1; \
	  ! grep -q '^not ok' out || exit 1; \
	  grep -q '^1\.\.' out || exit 1; \
	done
	# And the same thing said in a file rather than in the environment,
	# which is what a settings file is for. bin/settingstest checks the
	# reading of one; this checks that what it read reaches the machine,
	# and that the environment still wins over it.
	printf '[screen]\nmode = tt-medium\n' > settings.tosemu
	TOSEMU_NO_WINDOW=1 $(NO_DAEMON) ../bin/tosemu -c settings.tosemu \
	  test-c-screen tt-medium > out; ! grep -q '^not ok' out
	grep -q '^1\.\.' out
	TOSEMU_SCREEN=high TOSEMU_NO_WINDOW=1 $(NO_DAEMON) \
	  ../bin/tosemu -c settings.tosemu test-c-screen high > out; \
	  ! grep -q '^not ok' out
	grep -q '^1\.\.' out
	# And a file named and not there is refused rather than ignored, since
	# carrying on would be running on a machine nobody asked for
	TOSEMU_NO_WINDOW=1 $(NO_DAEMON) ../bin/tosemu -c nosuch.tosemu \
	  test-c-screen high > out 2>/dev/null; test "$$?" != 0
	$(RM) settings.tosemu
	TOSEMU_AESD=$(CURDIR)/screen.socket TOSEMU_SCREEN=medium \
	  ../bin/tosaesd --no-config & \
	  echo $$! > screen.pid; \
	  sleep 1; \
	  TOSEMU_AESD=$(CURDIR)/screen.socket TOSEMU_SCREEN=low \
	    $(TOSEMU_FOR_A_WHILE) test-c-screen medium > out; \
	  kill `cat screen.pid`
	! grep -q '^not ok' out
	grep -q '^1\.\.' out
	$(RM) screen.pid screen.socket
	# An object the application draws itself, which is the AES calling back
	# into the machine in the middle of a call out of it
	$(TOSEMU) test-c-userdef > out; ! grep -q '^not ok' out
	grep -q '^1\.\.' out
	# The objects that are pictures, which are the only ones whose ob_spec
	# leads to a bitmap rather than to a word or a string. On the screen
	# with the colours, because an icon is a mask drawn in one and an image
	# drawn over it in another, and telling them apart needs three.
	TOSEMU_SCREEN=low $(TOSEMU) test-c-icon > out; ! grep -q '^not ok' out
	grep -q '^1\.\.' out
	# One character packed into the object itself rather than pointed at,
	# which is every drive letter and every scroll bar arrow the AES draws
	$(TOSEMU) test-c-boxchar > out; ! grep -q '^not ok' out
	grep -q '^1\.\.' out
	# Opening the file selector more than once, which has to be the same
	# dialog every time. Three clicks to a pass: one the dialog throws
	# away, then the close box, then Cancel - see the test for why.
	$(NO_DAEMON) \
	TOSEMU_CLICKS='460,40 193,146 327,360 460,40 193,146 327,360 460,40 193,146 327,360' \
	  $(TOSEMU_FOR_A_WHILE) test-c-fsel > out; ! grep -q '^not ok' out
	grep -q '^1\.\.' out
	# Watching a box, which waits for the button to come up and so needs
	# one to have been pressed - twice, because it is watched twice
	TOSEMU_CLICKS='72,64 72,64' $(TOSEMU) test-c-watchbox > out; \
	  ! grep -q '^not ok' out
	grep -q '^1\.\.' out
	# The two strips along the top of a window, and which of them is drawn
	# where. Twice, because the title bar is GEM's when the decorations are
	# and the desktop's when they are not, and the information line is
	# drawn either way.
	$(TOSEMU) test-c-frame > out; ! grep -q '^not ok' out
	grep -q '^1\.\.' out
	TOSEMU_DECORATIONS=desktop $(TOSEMU) test-c-frame desktop > out; \
	  ! grep -q '^not ok' out
	grep -q '^1\.\.' out
	# A window given a new rectangle, which is what an application does with
	# the four words WM_SIZED carries. The frame has to move with it and the
	# application has to be told to paint what is now inside - the drag that
	# produced the rectangle is the compositor's and is not something a test
	# can arrange, but everything after it is.
	$(TOSEMU) test-c-resize > out; ! grep -q '^not ok' out
	grep -q '^1\.\.' out
	# Where a menu that has dropped down is drawn, which is not on the
	# screen: a window under it goes on showing what it was showing. The
	# pointer goes into the Desk title, on to the File title and down on to
	# the first entry, which is three moves because a GEM menu opens on the
	# pointer arriving and the title left behind is half of what this
	# checks. The coordinates are the high resolution screen's, which is
	# what these run on.
	TOSEMU_CLICKS='@32,8 @96,8 @100,27 100,27' $(TOSEMU) test-c-menu > out; \
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
	TOSEMU_AESD=$(CURDIR)/aesd.socket ../bin/tosaesd --no-config & \
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
	#
	# An accessory outliving the daemon that started it, which is the one
	# thing it must not do: nothing can reach it afterwards, because being
	# reached is something an accessory only ever does by being told to.
	# Quit in the panel closed the session properly and a signal did not, so
	# this asks the daemon to stop the way a person at a terminal does and
	# then looks for what it left. The accessory is copied into a directory
	# of its own rather than pointed at, since the daemon starts everything
	# in the one it is given.
	mkdir -p accs
	cp test-c-acc accs/TEST.ACC
	TOSEMU_NO_WINDOW=1 TOSEMU_AESD=$(CURDIR)/acc.socket \
	  ../bin/tosaesd --no-config accs > accsaid 2>&1 & \
	  echo $$! > accd.pid; \
	  sleep 3; \
	  pgrep -P `cat accd.pid` > accwas || true; \
	  kill -INT `cat accd.pid`; \
	  wait `cat accd.pid` || true; \
	  { if ! test -s accwas; then \
	      echo "not ok 1 - the daemon started the accessory (got no process, want one)"; \
	    elif kill -0 `cat accwas` 2>/dev/null; then \
	      echo "not ok 1 - the accessory went with the daemon (got still running, want gone)"; \
	    else \
	      echo "ok 1 - the accessory went with the daemon"; \
	    fi; \
	    if grep -q '^Asking ' accsaid; then \
	      echo "ok 2 - and the daemon said what it did with it"; \
	    else \
	      echo "not ok 2 - and the daemon said what it did with it (got silence, want a line naming it)"; \
	    fi; \
	    echo "1..2"; } > out; \
	  test -s accwas && kill `cat accwas` 2>/dev/null; true
	! grep -q '^not ok' out
	grep -q '^1\.\.' out
	$(RM) -r accs out accsaid accwas accd.pid acc.socket

clean:
	$(RM) $(addprefix test-,$(STESTNAME) $(CTESTNAME))
