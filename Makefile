# Source files for TOS emulator
SOURCEFILES = main.c gemdos.c gemdosmem.c gemdoscon.c gemdosfile.c xbios.c bios.c tossystem.c utils.c memory.c cpu.h

# Hand-written Musashi files
MUSASHIFILES = Musashi/m68kcpu.c Musashi/m68kdasm.c

# Generated Musashi files
MUSASHIGENERATEDFILES = gen/m68kops.c gen/m68kopac.c gen/m68kopdm.c gen/m68kopnz.c gen/m68kops.h

# Compilation flags
CC = gcc
LD = gcc
CFLAGS = -Igen -IMusashi -I. -Wall -pedantic
LDFLAGS = -lc

all: bin/tosemu

.PHONY: tests check

tests:
	$(MAKE) -C tests/

OBJECTS = $(addsuffix .o,$(basename $(SOURCEFILES) $(MUSASHIFILES) $(MUSASHIGENERATEDFILES)))

# Main emulator target
bin/tosemu: $(OBJECTS)
	$(LD) $(LDFLAGS) $^ -o $@

# Every object needs the generated m68kops.h, so none of them may be compiled
# before m64kmake has run
$(OBJECTS): $(MUSASHIGENERATEDFILES)

# Files generated using m64kmake. One run produces all of them, so they hang
# off a single stamp target to keep parallel builds from running it more than
# once at a time.
$(MUSASHIGENERATEDFILES): gen/.stamp

gen/.stamp: bin/m64kmake Musashi/m68k_in.c
	mkdir -p gen/
	bin/m64kmake gen/ Musashi/m68k_in.c > /dev/null
	touch $@

# The m64kmake generator
bin/m64kmake: Musashi/m68kmake.c
	mkdir -p bin/
	$(CC) $(CFLAGS) $< -o $@

check: bin/tosemu
	$(MAKE) -C tests check

# Clean up the source tree
clean:
	$(RM) *.o Musashi/*.o
	$(RM) gen/*
	$(RM) bin/*
	$(RM) -d gen/ bin/
	$(MAKE) -C tests/ clean
