# Each testname in STESTNAME is built from a source file with the file name
# extension .s, each one in CTESTNAME from a source file with the extension .c
STESTNAME=Pterm Pterm0 Cconout Cconws Bconout Fstraversal \
          Fopen Fclose Fread Supexec Dcreate Fcreate Fwrite Fdelete Fattrib \
          cmdline
CTESTNAME=c-helloworld c-drives c-bios c-xbios c-pexec c-pexchild

CC=m68k-atari-mint-gcc
TOSEMU=../bin/tosemu

all: $(addprefix test-,$(STESTNAME) $(CTESTNAME))

test-%: %.s
	$(CC) $< -Wa,-S -nostdlib -o $@

test-%: %.c
	$(CC) $< -o $@

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
	$(RM) out out2

clean:
	$(RM) $(addprefix test-,$(STESTNAME) $(CTESTNAME))
