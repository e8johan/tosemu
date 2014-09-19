# Each testname is build from a source file with the file name extension .s
TESTNAME=Pterm Pterm0 helloworld

all: $(TESTNAME)

%: %.s
	m68k-atari-mint-gcc $< -Wa,-S -nostdlib -o $@
	
clean:
	$(RM) $(TESTNAME)