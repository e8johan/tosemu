# Each testname is build from a source file with the file name extension .s
STESTNAME=Pterm Pterm0 Cconout Cconws Bconout Fstraversal c-helloworld \
          Fopen Fclose

CC=m68k-atari-mint-gcc
TOSEMU=../bin/tosemu

all: $(addprefix test-,$(basename $(STESTNAME) $(CTESTNAME)))

test-%: %.s
	m68k-atari-mint-gcc $< -Wa,-S -nostdlib -o $@

test-%: %.c
	m68k-atari-mint-gcc $< -o $@

check: all
	$(TOSEMU) test-Pterm0
	$(TOSEMU) test-Pterm; test "$$?" = 42
	$(TOSEMU) test-Bconout > out && test "`cat out`" = 'Hello World!'
	$(TOSEMU) test-Cconout > out && test "`cat out`" = 'Hello World!'
	$(TOSEMU) test-Cconws > out && test "`cat out`" = 'Hello World!'
	$(TOSEMU) test-Fstraversal
	$(TOSEMU) test-Fopen
	$(TOSEMU) test-Fclose
	$(TOSEMU) test-c-helloworld

clean:
	$(RM) $(addprefix test-,$(basename $(STESTNAME) $(CTESTNAME)))
