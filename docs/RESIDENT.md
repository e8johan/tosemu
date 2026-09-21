# Programs that stay: Ptermres, and running something on top of one

> **Status: planned, 2026-09-14.** Branch `pexec-resident`, based on `main`.
>
> The one prerequisite is done: the exception table is filled on `main` as of
> *"cpu: every exception vector pointed at address nought"*, so MROS gets past
> its resident check and Cubase now stops at `Ptermres`, which is where this
> plan starts. Nothing else here is implemented.
>
> Settled with the person who asked for it: `--resident` is a command line
> option and nothing else - not a setting, not a file, not anything the emulated
> machine can ask for.
>
> **Stages 1 to 4 are done.** `Ptermres`, `--resident`, the memory that is kept
> and the pair of tests that prove it. Cubase gets past MROS, and past a VDI
> form with no width in it - see *"VDI: a form that did not say how wide it was
> was refused"* - and now runs into MROS's own code and stays there.
>
> What it does there is an infinite crash loop, and it is worth writing down
> because it looks like a hang:
>
> ```
> 846ec: jmp (A1)     ; A1 is nought, so this jumps to address nought
> 000..014            ; and runs the exception vector table as instructions
>                     ; until an ILLEGAL
> 1c5aa               ; which MROS's own exception handler catches
> 1c600: rte -> 846e2 ; restoring a register set from 0x1c688
> 846ec: jmp (A1)     ; where A1 is nought again
> ```
>
> The register set it keeps restoring is inside the part of MROS that stayed
> resident, so nothing has overwritten it - it was never filled in. **A
> hypothesis worth testing before anything else**: MROS is a multitasking
> kernel and this looks like it dispatching a task that was never started, and
> what starts one may well be the timer interrupt it expects. This branch is off
> `main` and has no interrupts at all; the `midi` branch has the MFP and the
> timers. If that is the answer, Cubase needs both branches and neither alone.
> It has not been tried, and it should be before anybody debugs MROS itself.

## Context

A TSR is a program that loads, installs itself into the machine, and stays
there so that the next program can use it. It was an ordinary shape for system
software of the period: the AUTO folder was nothing but a list of them, and
anything that added a capability to the machine - a RAM disk, a printer
spooler, a MIDI kernel - arrived that way.

tosemu cannot run one. `Ptermres` is not implemented and halts the emulator,
and underneath that is a deeper problem: `Pexec` starts a child by forking the
host process ([gemdosproc.c:256](src/gemdosproc.c#L256)), so a child has a
*copy* of the machine's memory. A program that stayed resident would stay
resident in the copy, and the parent would never see it.

Cubase is the case that raises it. Before it will start it loads MROS - the
MIDI kernel it does all its timing and routing through - and MROS is a TSR. It
gets as far as `Ptermres` and dies, and Cubase reports that MROS is missing.
The same fork behaviour is what Devpac 3 is waiting on, which is already in
`TODO`.

**What this branch is for is the first half only**: making a program able to
stay resident, and making another program able to run on top of it. Whether
`Pexec` should share memory rather than copy it is a separate question, and one
of the findings below is that Cubase may not need it.

---

## What a TSR needs, and what it does not

`Ptermres(keepcnt, retcode)` says: I have finished running, but keep `keepcnt`
bytes from my basepage allocated, and do not give them to anybody else. The
program's code stays where it is, whatever it installed goes on pointing at it,
and the memory above it is free for the next program.

That is all it is. The part that makes it work is not the call - it is that on
an ST there was **one address space**, and the program that ran next was loaded
above the resident one and could see it.

So the question for tosemu is not really "how do we implement `Ptermres`". It
is "what does *next* mean", and that is where the design decision is.

---

## The design: residency is a command line, not a process relationship

```
tosemu --resident MROS/MROS3_31 CUBASE.PRG
```

One tosemu process, one machine, one address space. The resident programs are
loaded and run first, in order; each one stays where it is; then the real
program is loaded above them and run. No fork happens anywhere in that
sequence.

This is what an ST did. The AUTO folder ran before the desktop, in the machine
the desktop would then be using, and nothing about it was a parent-child
relationship - the programs simply went in one after another and the last one
to start was the one the person wanted.

It satisfies the three things asked for:

**No global state.** A tosemu process still owns exactly one machine and shares
nothing with any other tosemu process. Two sessions with different residents do
not know about each other. The process separation that exists now is untouched;
what changes is only how many programs one machine loads before it settles on
the one that matters.

**No launcher on the TOS side.** Nothing in the emulated machine arranges any of
this. There is no AUTO-folder program to write, no shell, no `.INF` file - the
emulator loads them because the command line said to.

**One command line.** The whole arrangement is visible in the line somebody
typed, which also means it is reproducible, scriptable and easy to say in a
settings file later.

### Spelling

`--resident PATH` (short `-r`), repeatable, in the order they should load:

```
tosemu -r RAMDISK.PRG -r SPOOLER.PRG WORD.PRG
```

Everything after the first non-option argument belongs to the program, exactly
as now - so the residents have to be named before it, which they would be
anyway.

An `--auto DIR` that loads every `.PRG` in a directory in sorted order is the
obvious follow-on and is deliberately *not* in the first cut. It is a loop over
`--resident` once `--resident` works, and the Cubase installation has an AUTO
folder to try it on.

---

## What happens inside

Most of the machinery exists. The two pieces that matter are already used by
`Pexec`'s load-and-go-separately modes:

- `place_program(base, len, binary, size, cmdlin, env, parent)`
  ([tossystem.c](src/tossystem.c)) loads a program at *any* base and builds its
  basepage there.
- `exec_tos_basepage(basepage)` hands the run loop a program that is already in
  memory, **in the same machine and without forking**.

So the sequence is a loop the run loop can already almost express:

```
floor = 0x800
for each resident:
    place_program(floor, top - floor, ...)   /* load it where the floor is */
    run it
    on Ptermres(keepcnt):  floor = round_up_even(floor + keepcnt)
    on Pterm:              floor unchanged - it did not stay
place_program(floor, top - floor, ...)       /* and then the real program */
run it
```

### `Ptermres` itself

```c
uint32_t GEMDOS_Ptermres(void);   /* keepcnt at 4, retcode at 8 */
```

Three things, and the third is the only new one:

1. Shrink this program's block to `keepcnt` bytes, which is what `Mshrink`
   already does - `find_mem_area` on the basepage and set `len`.
2. Mark the block so nothing ever frees it. The memory manager has no notion of
   that yet; a flag on `struct mem_area` is enough, and `mem_free` refusing it
   is one line.
3. Stop this program **without stopping the host process**, and tell the run
   loop where the floor now is.

(3) is the part that differs from `Pterm`. `terminate()` in
[gemdosproc.c:178](src/gemdosproc.c#L178) calls `exit()`, which is right for a
program that is finished and wrong for one that is staying. `Ptermres` wants
the `halt_execution()` and "hand the loop something else" shape that
`exec_tos_basepage` already uses, not the `exit()` one.

### What the second program sees

Its basepage is at the floor, its TPA runs from there to the top of RAM, and
the resident block below it is allocated and stays allocated. `Malloc` will not
hand out the resident's memory because it is in the list, which is the whole
reason the list exists.

That is also the honest answer to "how much memory does my program get" - less
than before, by exactly as much as the residents kept, which is what an ST with
a full AUTO folder did to you as well.

---

## `Ptermres` from inside `Pexec`, which is the case that cannot work

A program that `Pexec`s a TSR is a different matter. The child is a forked host
process; anything it makes resident is resident in its own copy of the machine,
and when it exits that copy goes. There is no arrangement of `Ptermres` that
fixes this - it is the fork.

So `Ptermres` in a child should:

- keep the memory and terminate the child with the given code, so that `Pexec`
  returns success and the parent behaves as it would have;
- **say so, once and plainly**, naming `--resident` as the way to do it.

That message matters more than it looks. Without it the parent is told the TSR
loaded, believes it, and crashes later when it calls into something that is not
there - which is a far worse failure than the one being fixed. A line saying "a
program tried to stay resident from inside Pexec, which this cannot do; start
it with --resident instead" turns a mystery into an instruction.

---

## Why Cubase may not need shared-memory `Pexec` at all

This is the finding that makes the branch worth doing on its own, and it was
read out of the MROS binary rather than guessed.

MROS begins by asking whether it is already installed:

```
1b5e:  moveal 0xa0,%a0          ; the TRAP #8 vector
1b62:  movel  %a0@(-4),%d0      ; four bytes before whatever it points at
1b66:  cmpl   #0xb0aeb31f,%d0   ; its own signature
1b6c:  bne    install
       st 0x1316                ; already there
       rts
```

and the caller acts on it:

```
1b4c:  tstw 0x1316
1b52:  bne  0x20fc              ; -> moveq #-1,%d0 ; rts
1b56:  bsr  install
```

**The already-installed path returns -1 and never reaches `Ptermres`.**

That was the hypothesis, and the conclusion turned out to be right for a
different reason than the one predicted. It was tried, and what happens is:

> `tosemu -r MROS/MROS3_31 CUBASE.PRG` gets Cubase past MROS entirely. No "MROS
> not found" alert, and six times as many OS calls before it stops - it goes on
> into loading its resources and drawing, where it now fails in the VDI on
> something unrelated.

But the forked child does *not* find the signature. tosemu's `Pexec` rebuilds
the machine in the child - two machines are built in that run - so the child
gets a fresh address space rather than a readable copy of the parent's, does
its full install into that, and calls `Ptermres`. What makes it work anyway is
that the child's `Ptermres` now terminates cleanly instead of halting, so
`Pexec` returns success and Cubase believes it; and the MROS that Cubase
actually calls through TRAP #8 is the one `--resident` put in *its own* machine
before it started.

So the conclusion stands - shared-memory `Pexec` is not on Cubase's critical
path - but not because the child reads the parent's memory. It is because the
child's answer does not matter once the parent already has what it needs.

And what MROS installs is reachable. The install path writes its handlers into
the TRAP #8, #9 and #10 vectors:

```
1bde:  lea %pc@(0x1c9c),%a0 ; movel %a0,0xa0     ; trap #8
1bf0:  lea %pc@(0x1e08),%a0 ; movel %a0,0xa8     ; trap #10
1c02:  lea %pc@(0x1f74),%a0 ; movel %a0,0xa4     ; trap #9
```

Musashi is patched here to intercept traps 1, 2, 13 and 14 only
([m68kcpu.h](src/Musashi/m68kcpu.h)), so a `trap #8` goes through the ordinary
exception path and lands on whatever the vector holds - which is MROS. Cubase
calling into MROS should work with no further emulator support.

Shared-memory `Pexec` therefore stays a `TODO` entry for Devpac 3 rather than
being on Cubase's critical path.

### The prerequisite, which is done

MROS's resident check reads the TRAP #8 vector and dereferences it, and the
exception table used to be all noughts - so that read was of address `0xFFFFFC`
and the machine stopped before any of the above was reached.

The table is now filled, on `main`, so this branch has it. Cubase reaches
`Ptermres` and stops there, which is exactly where the work below begins.
`tos_default_vector()` is what the table was filled with, for anything that
later needs to tell a vector nobody claimed from one a program installed.

---

## Stages

0. ~~**`cpu`: the exception table**~~ - done, on `main`.
1. **`GEMDOS`: a program had no way to stay in memory** - `Ptermres`, the
   never-free flag on a memory area, and the child case with its message.
   Testable on its own: a TSR that keeps memory, and `Malloc` afterwards not
   handing that memory out.
2. **`tosemu`: nothing could be loaded on top of a resident program** -
   `--resident`, the floor, and the load-and-run loop over several programs.
3. **`tests`: a resident program and one that uses it** - the pair described
   below.
4. **`TODO`/`README`** - what residency does and does not do, and the `Pexec`
   child limitation written down where somebody will find it.
5. **Cubase**, by hand, which is the point of all of it.

`--auto DIR` is a sixth if it is wanted, and wants nothing from the first five
that they do not already do.

---

## Testing

The suite can cover this properly, which is worth saying because the MIDI work
could not cover its own most important case.

**`tests/c-tsr.c`** - a resident program. Writes a signature somewhere the next
program can find it, installs a handler on a spare TRAP vector that returns a
known value, and calls `Ptermres` keeping itself.

**`tests/c-usetsr.c`** - the program that runs on top of it. Checks that the
signature is there, calls the trap and checks the answer, checks its own
basepage is above the resident block, and checks `Malloc` never returns memory
inside it.

One check line runs the pair:

```
$(TOSEMU) -r test-c-tsr test-c-usetsr > out
```

That is a real end-to-end test of the whole arrangement, needs no hardware, and
fails loudly if any of the three parts - staying resident, being found, being
called - stops working.

Worth adding beside it: `c-usetsr` run *without* `-r`, checking it reports the
resident as absent rather than crashing. A program that tests for a TSR and
finds none is the ordinary case and must be survivable.

---

## Open questions, which are for the person and not the code

- **Should residents take arguments?** `-r 'PROG.PRG -x'` is easy enough, but
  no TSR obviously wants any, and the quoting is a cost paid by everybody who
  does not need it. Suggest: no arguments in the first cut.
- **Should a resident be able to fail the run?** If a TSR exits with a non-zero
  code, or terminates with `Pterm` instead of staying, should tosemu carry on
  to the main program or stop? Suggest: carry on, and say what happened - a
  TSR that declines to install is often a TSR saying "already there" or "not
  needed on this machine", which is not a reason to refuse to run anything.
- ~~**Should `--resident` be sayable in a settings file?**~~ Settled: no. It is
  a command line option and nothing else. What a machine has resident in it is
  a property of the run rather than of the machine, and a settings file that
  quietly loaded a TSR would make two runs of the same command mean different
  things.
