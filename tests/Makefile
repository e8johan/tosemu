# The tests, and where they are built and run.
#
# Nothing built belongs next to what it was built from, and for these that is
# more than tidiness: a run leaves programs, files the programs made, sockets,
# process id files and whole directories behind, and every one of them used to
# land among the sources. So the suite is built and run in a directory of its
# own under build/, and this directory holds only what somebody wrote.
#
# Which means make itself has to run there, rather than merely writing there.
# Half of what these tests check is what a program does with a relative path -
# Pexec of a sibling program, Fcreate of a name with no directory in it,
# Dgetpath answering for where it stands - and all of that is answered from the
# working directory of the process. So make hands the work to a copy of itself
# running in the build directory, and every recipe below names what it wants
# without a path, the way it did when the two were the same place.
#
# Each testname in STESTNAME is built from a source file with the file name
# extension .s, each one in CTESTNAME from a source file with the extension .c
STESTNAME=Pterm Pterm0 Cconout Cconws Bconout Fstraversal \
          Fopen Fclose Fread Supexec Dcreate Fcreate Fwrite Fdelete Fattrib \
          cmdline Malloc
CTESTNAME=c-helloworld c-drives c-bios c-xbios c-mxalloc c-memory c-pexec c-pexchild c-overlay c-gem c-vdi c-aesd c-acc c-userdef c-icon c-boxchar c-fsel c-watchbox c-mkstate c-screen c-menu c-frame c-resize c-scrap

# The ones that call the AES or the VDI, whose bindings live in their own
# library rather than in the C one
GEMTESTNAME=c-gem c-vdi c-aesd c-scrap c-acc c-userdef c-icon c-boxchar c-fsel \
            c-watchbox c-mkstate c-screen c-menu c-frame c-resize

# Where the sources are, found through the makefile that was read rather than
# from the directory make happens to be standing in, since it is standing in
# the build directory half the time. $(realpath) rather than $(abspath), so
# that this still holds when the file was reached through a link.
srcdir := $(patsubst %/,%,$(dir $(realpath $(firstword $(MAKEFILE_LIST)))))
BUILDDIR := $(abspath $(srcdir)/../build/tests)

CC=m68k-atari-mint-gcc

ifneq ($(CURDIR),$(BUILDDIR))

# Run from the source directory, which is not where any of this belongs: make
# the build directory and hand the whole job to a copy of make standing in it.
all check:
	@mkdir -p $(BUILDDIR)
	$(MAKE) -C $(BUILDDIR) -f $(srcdir)/Makefile $@

clean:
	$(RM) -r $(BUILDDIR)

else

# Only the sources are looked for over there. A blanket VPATH would also find
# programs left behind by an older build in the source directory and take them
# for something it had made itself.
vpath %.c $(srcdir)
vpath %.s $(srcdir)

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
TOSEMU=TOSEMU_NO_WINDOW=1 $(NO_DAEMON) ../../bin/tosemu --no-config

# The same, with a limit on how long it may take. The tests that need a daemon
# leave a process waiting for a message, and one that never comes would
# otherwise wait for ever rather than fail. Which daemon, if any, is left to
# the caller: most of these are the tests that want a real one.
TOSEMU_FOR_A_WHILE=timeout 20 env TOSEMU_NO_WINDOW=1 ../../bin/tosemu --no-config

all: $(addprefix test-,$(STESTNAME) $(CTESTNAME))

test-%: %.s
	$(CC) $< -Wa,-S -nostdlib -o $@

test-%: %.c
	$(CC) $< -o $@

# The AES and VDI bindings live in their own library rather than in the C one
$(addprefix test-,$(GEMTESTNAME)): test-%: %.c
	$(CC) $< -lgem -o $@

check: all
	# Four of these open a file to see what a file that is there does, and
	# the file they open is called Makefile - because when they were written
	# there was one next to them and there is not one next to them now. So
	# put one where they run. It has to be a real file with real contents:
	# test-Fread reads the first hundred bytes of it and the check below
	# compares them against the same hundred bytes.
	ln -sfn $(srcdir)/Makefile Makefile
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
	# And what Dbmsg did with the message it was handed, which is on the
	# emulator's own output rather than in anything the program can ask
	# about: an application says something to whoever is debugging it, and
	# tosemu is what there is.
	grep -q 'Dbmsg: a message for the debugger' out
	# Asking for memory and saying which kind it has to be, which is the
	# call every compiler of the period makes before anything else
	$(TOSEMU) test-c-mxalloc > out; ! grep -q '^not ok' out
	grep -q '^1\.\.' out
	# How much memory the machine has, which is the sizes the Ataris were
	# sold in and then as much as the memory map has room for. Every one of
	# them, because what each has to reach is both halves of the machine:
	# the map, where the screen sits at the top of the RAM there is, and
	# GEMDOS, which is the half an application sees when it asks for memory.
	# The first without asking for anything, that being the default.
	$(TOSEMU) test-c-memory max > out; ! grep -q '^not ok' out
	grep -q '^1\.\.' out
	for m in 512k 1m 2m 4m 14m max; do \
	  TOSEMU_MEMORY=$$m $(TOSEMU) test-c-memory $$m > out || exit 1; \
	  ! grep -q '^not ok' out || exit 1; \
	  grep -q '^1\.\.' out || exit 1; \
	done
	# A size that is a number rather than a machine, which is what somebody
	# trying a program at the size it runs out of memory at asks for. 14336k
	# is the Falcon's fourteen megabytes said the other way, so what it comes
	# to is a machine the test already knows the shape of.
	TOSEMU_MEMORY=14336k $(TOSEMU) test-c-memory 14m > out; \
	  ! grep -q '^not ok' out
	grep -q '^1\.\.' out
	# And a size that is neither is complained about rather than taken, the
	# machine falling back to the one nobody asked for. A misspelt size that
	# quietly became something else would be a machine of some other size,
	# and how much memory there is is not something an application can be
	# told twice.
	TOSEMU_MEMORY=frog $(TOSEMU) test-c-memory max > out 2> said; \
	  ! grep -q '^not ok' out
	grep -q '^1\.\.' out
	grep -q 'TOSEMU_MEMORY' said
	# The same said in a settings file, which is where a machine that is not
	# to change between one program and the next belongs
	printf '[machine]\nmemory = 2m\n' > settings.tosemu
	TOSEMU_NO_WINDOW=1 $(NO_DAEMON) ../../bin/tosemu -c settings.tosemu \
	  test-c-memory 2m > out; ! grep -q '^not ok' out
	grep -q '^1\.\.' out
	$(RM) settings.tosemu said
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
	TOSEMU_NO_WINDOW=1 $(NO_DAEMON) ../../bin/tosemu -c settings.tosemu \
	  test-c-screen tt-medium > out; ! grep -q '^not ok' out
	grep -q '^1\.\.' out
	TOSEMU_SCREEN=high TOSEMU_NO_WINDOW=1 $(NO_DAEMON) \
	  ../../bin/tosemu -c settings.tosemu test-c-screen high > out; \
	  ! grep -q '^not ok' out
	grep -q '^1\.\.' out
	# And a file named and not there is refused rather than ignored, since
	# carrying on would be running on a machine nobody asked for
	TOSEMU_NO_WINDOW=1 $(NO_DAEMON) ../../bin/tosemu -c nosuch.tosemu \
	  test-c-screen high > out 2>/dev/null; test "$$?" != 0
	$(RM) settings.tosemu
	TOSEMU_AESD=$(CURDIR)/screen.socket TOSEMU_SCREEN=medium \
	  ../../bin/tosaesd --no-config & \
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
	TOSEMU_AESD=$(CURDIR)/aesd.socket ../../bin/tosaesd --no-config & \
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
	  ../../bin/tosaesd --no-config accs > accsaid 2>&1 & \
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
	#
	# And one started by hand rather than out of a directory, which the
	# daemon never forked and so has nothing to wait for. It is seen off all
	# the same, because what makes an accessory worth stopping is not who
	# started it: it has no window and is reached only by being told to open
	# one, so a daemon going away leaves it with nothing that could ever
	# ask. Started from a subshell that then goes, so that what is looked
	# for afterwards is nobody's child - one that is would still answer
	# while it waited to be collected, and read as alive.
	TOSEMU_NO_WINDOW=1 TOSEMU_AESD=$(CURDIR)/acc.socket \
	  ../../bin/tosaesd --no-config > accsaid 2>&1 & \
	  echo $$! > accd.pid; \
	  sleep 1; \
	  ( TOSEMU_NO_WINDOW=1 TOSEMU_AESD=$(CURDIR)/acc.socket \
	      ../../bin/tosemu --no-config test-c-acc & \
	    echo $$! > accwas ); \
	  sleep 3; \
	  kill -INT `cat accd.pid`; \
	  wait `cat accd.pid` || true; \
	  { if kill -0 `cat accwas` 2>/dev/null; then \
	      echo "not ok 1 - one started by hand went too (got still running, want gone)"; \
	    else \
	      echo "ok 1 - one started by hand went too"; \
	    fi; \
	    if grep -q '^Asking ' accsaid; then \
	      echo "ok 2 - and the daemon reached past its own children to do it"; \
	    else \
	      echo "not ok 2 - and the daemon reached past its own children to do it (got silence, want a line naming it)"; \
	    fi; \
	    echo "1..2"; } > out; \
	  kill `cat accwas` 2>/dev/null; true
	! grep -q '^not ok' out
	grep -q '^1\.\.' out
	$(RM) out accsaid accwas accd.pid acc.socket
	#
	# Pasting what the desktop is offering, with a file standing in for a
	# desktop. There is no compositor in a test and there cannot be, so
	# TOSEMU_SCRAP_IN is how the offer is made - see settings.c. The text in
	# it has two letters an ST spells its own way and a line ending it
	# spells with two bytes, which is the whole of the conversion in one
	# short paragraph.
	$(RM) -r scrapdir
	printf '\303\204rade\nv\303\244nner\n' > offer.txt
	TOSEMU_SCRAP=C:$(CURDIR)/scrapdir/ TOSEMU_SCRAP_IN=$(CURDIR)/offer.txt \
	  $(TOSEMU) test-c-scrap > out
	! grep -q '^not ok' out
	grep -q '^1\.\.' out
	$(RM) -r out scrapdir
	#
	# And two GEM applications passing a scrap between themselves while the
	# desktop goes on offering something else. That worked before any of
	# this existed and is what the bringing across is most likely to break:
	# the sender's cut is newer than the offer, so the reader has to be
	# given the sender's. A daemon is needed for the two to find each other
	# at all, and gets a socket of its own so a session somebody is using is
	# left alone.
	$(RM) -r scrapdir
	TOSEMU_AESD=$(CURDIR)/scrap.socket ../../bin/tosaesd --no-config & \
	  echo $$! > scrapd.pid; \
	  sleep 1; \
	  TOSEMU_AESD=$(CURDIR)/scrap.socket \
	    TOSEMU_SCRAP=C:$(CURDIR)/scrapdir/ \
	    TOSEMU_SCRAP_IN=$(CURDIR)/offer.txt \
	    $(TOSEMU_FOR_A_WHILE) test-c-scrap wait > out & \
	  echo $$! > waiter.pid; \
	  sleep 1; \
	  TOSEMU_AESD=$(CURDIR)/scrap.socket \
	    TOSEMU_SCRAP=C:$(CURDIR)/scrapdir/ \
	    TOSEMU_SCRAP_IN=$(CURDIR)/offer.txt \
	    $(TOSEMU_FOR_A_WHILE) test-c-scrap send 'TEST-C-S' > out2; \
	  wait `cat waiter.pid` || true; \
	  kill `cat scrapd.pid`
	! grep -q '^not ok' out
	! grep -q '^not ok' out2
	grep -q '^1\.\.' out
	grep -q '^1\.\.' out2
	$(RM) -r out out2 scrapd.pid waiter.pid scrap.socket scrapdir offer.txt
	#
	# And the other direction, which nothing announces: a GEM application
	# writes SCRAP.TXT and says nothing at all, so the file appearing is the
	# only notice there is and a watch on the directory is the only way to
	# take it. What the desktop would have been offered goes to
	# TOSEMU_SCRAP_OUT, and it is the file rather than the test that says
	# whether this worked - the application cannot see its own clipboard.
	$(RM) -r scrapdir offered.txt
	TOSEMU_SCRAP=C:$(CURDIR)/scrapdir/ TOSEMU_SCRAP_OUT=$(CURDIR)/offered.txt \
	  $(TOSEMU) test-c-scrap copy > out
	! grep -q '^not ok' out
	grep -q '^1\.\.' out
	printf '\303\204rade\n' > wanted.txt
	cmp offered.txt wanted.txt
	$(RM) -r out scrapdir offered.txt wanted.txt
	#
	# And the two of them at once, which is where they can chase each other.
	# Bringing the desktop's clipboard in means writing into the directory
	# being watched, and the watch cannot tell that write from an
	# application's - so without something to stop it, what the desktop
	# offered is read straight back out and offered to the desktop it came
	# from. There is nowhere inside GEM that shows: what says it happened is
	# a file appearing here that nothing should have written.
	$(RM) -r scrapdir offered.txt
	printf '\303\204rade\nv\303\244nner\n' > offer.txt
	TOSEMU_SCRAP=C:$(CURDIR)/scrapdir/ TOSEMU_SCRAP_IN=$(CURDIR)/offer.txt \
	  TOSEMU_SCRAP_OUT=$(CURDIR)/offered.txt $(TOSEMU) test-c-scrap > out
	! grep -q '^not ok' out
	grep -q '^1\.\.' out
	test ! -e offered.txt
	$(RM) -r out scrapdir offered.txt offer.txt
	#
	# And the same chase across two emulators, which is the case a guard
	# kept inside one process cannot cover. One sits watching the scrap
	# while the other brings the desktop's clipboard in; the write reaches
	# both, and the watcher has to recognise it as the desktop's rather than
	# as a GEM application cutting something out. It can, because the file
	# carries the time the offer arrived rather than the time it was
	# written, and that is a fact on the disk rather than a count in
	# somebody's memory.
	$(RM) -r scrapdir offered.txt
	mkdir -p scrapdir
	printf '\303\204rade\nv\303\244nner\n' > offer.txt
	TOSEMU_SCRAP=C:$(CURDIR)/scrapdir/ TOSEMU_SCRAP_IN=$(CURDIR)/offer.txt \
	  TOSEMU_SCRAP_OUT=$(CURDIR)/offered.txt $(TOSEMU) \
	  test-c-scrap watch 'C:$(CURDIR)/scrapdir/' > out & \
	  echo $$! > watcher.pid; \
	  sleep 2; \
	  TOSEMU_SCRAP=C:$(CURDIR)/scrapdir/ TOSEMU_SCRAP_IN=$(CURDIR)/offer.txt \
	    $(TOSEMU) test-c-scrap > out2; \
	  wait `cat watcher.pid` || true
	! grep -q '^not ok' out
	! grep -q '^not ok' out2
	grep -q '^1\.\.' out
	grep -q '^1\.\.' out2
	test -e scrapdir/SCRAP.TXT
	test ! -e offered.txt
	$(RM) -r out out2 watcher.pid scrapdir offered.txt offer.txt
	#
	# A picture, and nothing but a picture, which is what a clipboard holds
	# every time somebody uses a screenshot tool. The stand-in says which
	# of the two it is offering by what is in it - a PNG says so in its
	# first eight bytes - so the same setting covers both without a second
	# one to keep in step. bin/scraptest writes the file, because asking
	# every machine that builds this for something that can write a PNG
	# when the thing being tested already can would be silly.
	#
	# Which it can only do where there is a libpng to do it with. That is
	# looked for rather than required, so a build without one is a build
	# that works - the picture half is compiled out and the text half goes
	# on - and there is then no picture for a desktop to offer and nothing
	# here to check. So ask first and say so, the way bin/scraptest says it
	# about the same checks of its own.
	$(RM) -r scrapdir offer.png
	if ../../bin/scraptest --png-available; then \
	  ../../bin/scraptest --write-png offer.png || exit 1; \
	  TOSEMU_SCRAP=C:$(CURDIR)/scrapdir/ TOSEMU_SCRAP_IN=$(CURDIR)/offer.png \
	    $(TOSEMU) test-c-scrap picture > out || exit 1; \
	  ! grep -q '^not ok' out || exit 1; \
	  grep -q '^1\.\.' out || exit 1; \
	  test ! -e scrapdir/SCRAP.TXT || exit 1; \
	else \
	  echo "# built without libpng, so the picture a desktop offered is not checked"; \
	fi
	$(RM) -r out scrapdir offer.png
	#
	# A wait nothing could ever answer, with a scrap being watched. The
	# emulator has to say so and stop rather than sit there, and the watch
	# is what makes that hard: it puts a descriptor in the wait that can
	# never end one, because it serves the desktop and not the program. A
	# loop that counts descriptors instead of asking which could answer is
	# fooled by exactly that, and the honest message becomes a silent hang -
	# which is what the timeout below would catch.
	$(RM) -r scrapdir
	timeout 10 env TOSEMU_SCRAP=C:$(CURDIR)/scrapdir/ TOSEMU_NO_WINDOW=1 \
	  $(NO_DAEMON) ../../bin/tosemu --no-config test-c-scrap hang > out 2>&1; \
	  echo "$$?" > code
	test "`cat code`" != 124
	grep -q 'no window for either to arrive at' out
	! grep -q '^not ok' out
	grep -q '^1\.\.' out
	$(RM) -r out code scrapdir

# Reached only by a make standing in the build directory, which is the one
# place where deleting the directory itself is not an option
clean:
	$(RM) $(addprefix test-,$(STESTNAME) $(CTESTNAME))
	$(RM) Makefile out out2 code said settings.tosemu

endif

.PHONY: all check clean
