# Each testname is build from a source file with the file name extension .s
STESTNAME=Pterm Pterm0 Cconout Cconws Bconout c-helloworld

CC=m68k-atari-mint-gcc

all: $(addprefix test-,$(basename $(STESTNAME) $(CTESTNAME)))

test-%: %.s
	m68k-atari-mint-gcc $< -Wa,-S -nostdlib -o $@

test-%: %.c
	m68k-atari-mint-gcc $< -o $@

clean:
	$(RM) $(addprefix test-,$(basename $(STESTNAME) $(CTESTNAME)))