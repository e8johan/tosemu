MIDI
====

There is a MIDI port, and it goes wherever the setting says. An application
sends the way it always did - `Bconout` on device 3 a byte at a time, or
`Midiws` for a whole message - and the bytes reach whatever is plugged into the
host.

Nothing is set up by default. Without a word said there is no port, every byte
written is discarded and nothing ever arrives, which is what an ST with nothing
in the socket did.

    [midi]
    device = hw:1,0,0

| in the file            | in the environment  |
| ---------------------- | ------------------- |
| `[midi] device`        | `TOSEMU_MIDI`       |
| `[machine] interrupts` | `TOSEMU_INTERRUPTS` |

How it is spelled picks what kind of port it is, and the prefix is not
optional:

- `hw:1,0,0` is an ALSA raw device - the interface itself. `aplaymidi -l` lists
  them. This is the faithful one: an ST's MIDI port was a serial line and so is
  this, so running status, active sensing and a system exclusive dump of any
  length all pass through untouched, nothing along the way trying to understand
  them. A raw device is one program at a time.
- `seq:20:0`, or `seq:` and a port's name, is the ALSA sequencer. `aconnect -l`
  lists those. It reaches software synthesisers as readily as hardware and it
  appears in a patchbay by name. `seq:` on its own makes the port and connects
  it to nothing, for something else to connect to afterwards.
- `file:incoming.bin,sent.bin` is neither, and it is not a lesser port. It is
  how the traffic is looked at without a synthesiser to look at it on - by a
  test, on a machine with no sound hardware, or by somebody who wants to see
  what a program actually sent. Either half may be left out.

**The prefix has to be there**, because a sequencer port named rather than
numbered has nothing in its spelling to mark it as one. Without the rule a
mistyped `hw:` would quietly become a sequencer port connected to nothing,
which from the outside is indistinguishable from a working port with a silent
synthesiser on the end of it.

Built without ALSA - `make NO_ALSA=1`, or on a machine that has no
`libasound2-dev` - `file:` still works and the other two are refused with a
line saying why. A build server has no MIDI interface and should not need one.

# Development of MIDI for tosemu: an ST MIDI program driving a host USB interface

> **Status: stages 1 to 7 done, 2026-09-14.** A program can send MIDI and receive
> it, the machine has a 68901 MFP and two 6850 ACIAs in its memory map, the
> timers run against the host clock, and a handler an application installs is
> called - between instructions and while the application is asleep in GEM,
> which is where a sequencer spends its time. Watched end to end through ALSA's
> sequencer - note on, a complete six-byte sysex, note off - though against its
> own loopback rather than an interface with a cable in it, there being no MIDI
> hardware on this machine.
>
> What is left is stage 7's remaining polish and stage 10, and the thing none of
> it proves: **no period sequencer has been run.** Cubase and Notator are the
> point of the exercise and they have not been tried.
>
> Three departures from the plan as written, each decided while building and
> each noted where it applies below: the MIDI setting requires its prefix; the
> XBIOS timer calls became real in the dispatch commit rather than the BIOS one,
> because without them nothing could install a handler to dispatch to; and a
> channel tosemu answers for itself has to be *finished* as well as
> acknowledged, which the plan did not mention and which cost an afternoon -
> the first byte of MIDI arrived and the second never did.

## Context

tosemu translates TOS calls rather than emulating a machine, and MIDI is one of
the places where that stops short. Today device 3 falls through every one of
`Bconin`/`Bconout`/`Bconstat` in [bios.c:85-144](tosemu/src/bios.c#L85-L144),
`Midiws` discards what it is given
([xbiosdev.c:147-158](tosemu/src/xbiosdev.c#L147-L158)), and `Iorec(2)` hands out
a ring buffer that is permanently empty
([xbiosdev.c:200-226](tosemu/src/xbiosdev.c#L200-L226)). A MIDI program runs and
plays nothing.

Byte plumbing alone would fix the polled half of the world — patch editors,
librarians, monitors, dump utilities. It would not fix the half people actually
mean by "ST MIDI software". Cubase, Notator and Dr. T's clock themselves off MFP
timer interrupts and install their own handler on the ACIA vector; tosemu has no
interrupts, no timers, and no hardware registers at all. The header of
[xbiosdev.c:32-36](tosemu/src/xbiosdev.c#L32-L36) says as much and marks itself
as where an interrupt subsystem would attach.

So this is two pieces of work that only make sense together: a host MIDI port,
and enough of a 68901 MFP and 6850 ACIA — behind real 68000 interrupts — for a
sequencer to keep time against. The intended outcome is that a period sequencer
runs under tosemu and drives a synthesiser plugged into the host's USB MIDI
interface.

Two decisions are already made: **full scope** (registers and real IRQs, not just
plumbing), and **both ALSA backends** (rawmidi and sequencer) chosen by how the
setting is spelled, plus a file backend for the suite.

---

## Hardware facts, all verified against EmuTOS and Musashi in this tree

These were checked rather than recalled; several contradict the obvious reading.

| Fact | Where it was confirmed |
|---|---|
| **Musashi does *not* check interrupts per instruction.** `m68k_execute`'s loop has no `m68ki_check_interrupts()` — `/* ASG: removed per-instruction interrupt checks */`. Only `m68k_set_irq()` and `m68ki_set_sr()` (RTE, MOVE to SR) check. So `m68k_set_irq()` **dispatches synchronously**: it builds the frame and sets PC before returning. | [m68kcpu.c:639-680](tosemu/src/Musashi/m68kcpu.c#L639-L680), [:732-739](tosemu/src/Musashi/m68kcpu.c#L732-L739) |
| `m68ki_instr_hook()` runs **before** `REG_IR = m68ki_read_imm_16()`, so in `cpu_instr_callback` the PC is the not-yet-executed instruction — the correct safe point to raise an IRQ. | [m68kcpu.c:665-671](tosemu/src/Musashi/m68kcpu.c#L665-L671) |
| With `M68K_EMULATE_INT_ACK` **on**, `CPU_INT_LEVEL` is no longer auto-cleared. A line left asserted retakes the interrupt for ever. We must call `m68k_set_irq(0)` ourselves. | [m68kcpu.h:1972-1975](tosemu/src/Musashi/m68kcpu.h#L1972-L1975) |
| A vector of **zero** sends the machine to the uninitialised-interrupt vector, which is also zero here. "Did the program install a handler?" is a safety check, not a nicety. | [m68kcpu.h:1949-1952](tosemu/src/Musashi/m68kcpu.h#L1949-L1952) |
| **MFP channels 8-15 live in the A registers, 0-7 in the B registers.** Timer A (13) is IERA bit 5; the ACIA (6) is IERB bit 6; Timer C (5) is IERB bit 5. | `disable_mfp_interrupt`, 3rdparty/emutos/bios/mfp.c:38-55 |
| **IPRA/IPRB/ISRA/ISRB are written with AND semantics** — a written 0 clears, a written 1 leaves alone. Implement as `*reg &= value`. | mfp.c:47 `/* note: IPRA/ISRA ignore '1' bits */`; `move.b #0xbf,0x11(a1)` in aciavecs.S |
| **GPIP bit 4 is the ACIA IRQ, active low, and TOS spins on it**: `btst.b #4,0x1(a1)` / `jeq int_acia_loop` loops *while the bit is zero*. Without it TOS's handler never exits. | `_int_acia`, bios/aciavecs.S |
| `mfp->vr = 0x48` — vector base 0x40 **with bit 3 (software EOI) set**. Channel n → vector 0x40+n → address 0x100+4n. Timer A → 0x134, B → 0x120, C → 0x114, D → 0x110, ACIA → **0x118**. | mfp.c:104, mfp.c:110-113 |
| `Midiws(cnt, ptr)` sends **cnt+1** bytes. | bios/midi.c:97-100, "number of bytes to send less one" |
| The VBL is **not** an MFP interrupt: IPL 4, autovectored, vector 28 → 0x70. Turning int-ack on routes every level through the callback, so level 4 must be answered `M68K_INT_ACK_AUTOVECTOR` explicitly. | Musashi `EXCEPTION_INTERRUPT_AUTOVECTOR` |
| Prescaler 0 = **stopped** (not "÷nothing"); 1-7 = ÷4/10/16/50/64/100/200; 8-15 = event-count/pulse-width; data 0 counts as 256. EmuTOS's own Timer C is `xbtimer(2, 0x50, 192, ...)` → 2457600/64/192 = **200 Hz exactly**. | mfp.c:207 |
| `midivec` is at offset **0** of `_KBDVECS`. | aciavecs.S:131 |
| Registers land at 0xFFFA00/0xFFFC00, not 0xFFFFFA00: `ADDRESS_68K` masks with `0x00ffffff` for a 68000. | [m68kcpu.h:259](tosemu/src/Musashi/m68kcpu.h#L259) |
| `pkg-config --cflags alsa` prints nothing and exits 0 — the `&& echo -DHAVE_ALSA` idiom keys off exit status, so it works exactly like dbus. ALSA is present on this machine. | checked directly |

---

## Shape of the change

Five new source files, split on one line: **anything that calls `m68k_*` goes in
`interrupt.c`; everything else is pure host C.** That split is what lets
`bin/miditest` link the model and check the timer arithmetic, the MFP priority
resolution and the ring wraparound in CI with no ALSA, no hardware and no
emulator.

```
src/midi.c  midi.h        the host's port: three backends, two rings
src/mfp.c   mfp.h         the 68901 as registers and rules, no machine
src/acia.c  acia.h        the two 6850s, the same way
src/iorec.c iorec.h       the ring an IOREC describes, as arithmetic
src/interrupt.c .h        the only file that knows there is a 68000
src/miditest.c            host-side checks for the four above
```

---

## Stage 1 — `src/midi.c`: the host's port

No `m68k.h`, no `memory.h`, no `tossystem.h`.

```c
int         midi_open(void);          /* what the setting asked for; 0 when nothing did */
void        midi_close(void);
int         midi_wanted(void);
int         midi_fd(void);            /* -1 when there is nothing to sleep on */
void        midi_pump(void);          /* both directions, never blocks */
int         midi_take(uint8_t *byte); /* one byte from the host, 0 when none */
int         midi_give(uint8_t byte);  /* one byte to the host, 0 when it would block */
void        midi_reset(void);         /* forget a half-finished message, drop what is queued */
const char *midi_named(void);
```

`midi_pump` fills an inbound ring and drains an outbound one; `midi_take`/
`midi_give` are all `interrupt.c` ever calls, so the emulator never touches ALSA.

**Backend table**, matched longest-prefix-first:

```c
struct midi_backend {
    const char *prefix;
    int  (*open)(const char *spec);   /* spec is what follows the prefix */
    void (*close)(void);
    int  (*fd)(void);
    int  (*read)(uint8_t *into, int room);      /* bytes taken, 0 none, -1 gone */
    int  (*write)(const uint8_t *from, int n);  /* accepted, 0 would block */
    void (*flush)(void);
};
```

`hw:`/`rawmidi:` → rawmidi, `seq:` → sequencer, `file:` → the stand-in.

**Departure from the plan, decided while building it.** The plan had a bare
spelling fall through to the sequencer, because `20:0` and `"FLUID Synth"` are
how a person names a port. That was tried and refused: a mistyped `hw:` then
becomes a sequencer port connected to nothing, which from the outside is
indistinguishable from a working port with a silent synthesiser on the end —
and it makes `make check` behave differently on a machine that has an ALSA
sequencer and one that does not. The prefix is now required and an unrecognised
spelling is an error naming the three forms. Relatedly, `seq:<name>` where the
name does not resolve is now a failure rather than an unconnected port; `seq:`
with no name is how you ask for one of those on purpose. The ALSA rows sit inside `#ifdef HAVE_ALSA`; asking
for one on a build without it prints a line and leaves MIDI off, never halts.
`midi_open` is the one place that reads `setting("TOSEMU_MIDI")` — never `getenv`.

**rawmidi**: `snd_rawmidi_open(&in, &out, spec, SND_RAWMIDI_NONBLOCK)`; poll fd
from `snd_rawmidi_poll_descriptors()` on the input handle. `-EAGAIN` is the empty
case, not an error. Sysex needs nothing special — this backend is a byte pipe and
so is the ACIA, which is its whole virtue. `-EBUSY` on a second open (two
emulators in one session) gets one honest line and no MIDI, not a refusal to start.

**sequencer**: `snd_seq_open(..., DUPLEX, NONBLOCK)`, one
`snd_seq_create_simple_port` with `READ|WRITE|SUBS_READ|SUBS_WRITE`. An empty spec
creates the port and connects nothing, so it can be patched with `aconnect`.
Partial messages and long sysex are handled by keeping one parser alive for the
life of the port:

- *out*: `snd_midi_event_encode_byte(encoder, byte, &ev)` returns 0 for "need
  more" — the partial-message state lives in the encoder rather than in code we
  write — and 1 when an event is whole, then `snd_seq_ev_set_direct` and
  `snd_seq_event_output`/`drain`. Direct rather than scheduled: tosemu has no
  queue and the machine's sense of time is the host's. Output every event the
  moment `encode_byte` says there is one and never wait for a "whole" sysex; a
  long one comes out as several events with ALSA's own continuation semantics.
- *in*: `snd_seq_event_input` until `-EAGAIN`, then `snd_midi_event_decode`.
  Call **`snd_midi_event_no_status(decoder, 1)`** at open — without it the decoder
  emits running status and a program that resets its own across a sysex reads the
  next event's data bytes as a continuation.

**file**: `file:<in>[,<out>]`. The input is read whole at open and the fd closed;
`fd()` always returns -1, deliberately — a regular file is always readable, so
returning its fd would spin `wait_for`'s poll at full speed. This is the
`TOSEMU_SCRAP_IN`/`TOSEMU_SCRAP_OUT` idea ([settings.c:86-87](tosemu/src/settings.c#L86-L87))
in one setting.

**Risks**: a `seq:` name matching two ports; a device vanishing mid-run
(`-ENODEV` must close and go quiet, not be retried per instruction); a full
output ring dropping bytes, which becomes stuck notes — drop, but say so once.

---

## Stage 2 — `src/mfp.c`, `src/acia.c`, `src/iorec.c`: the model, with no machine in it

### mfp.h

```c
#define MFP_BASE_ADDRESS (0xFFFA00)
#define MFP_LENGTH       (0x30)
#define MFP_TIMER_D (4)
#define MFP_TIMER_C (5)
#define MFP_ACIA    (6)
#define MFP_TIMER_B (8)
#define MFP_TIMER_A (13)

void    mfp_reset(void);
uint8_t mfp_read(uint32_t offset);
void    mfp_write(uint32_t offset, uint8_t value);
void    mfp_raise(int channel);
void    mfp_gpip(int bit, int high);          /* bit 4 is the ACIAs, active low */
int     mfp_pending_channel(void);            /* 15..0, or -1 */
int     mfp_acknowledge(void);                /* the vector, or -1 */
void    mfp_enable(int channel);
void    mfp_disable(int channel);
void    mfp_setup_timer(int timer, uint8_t control, uint8_t data);  /* 0..3 = A..D */
int     mfp_timer_channel(int timer);
long    mfp_period_from(uint8_t control, uint8_t data);   /* ns, 0 = stopped */
long    mfp_timer_period(int timer);
```

Decode by odd offset (0x01 gpip … 0x25 tddr); even offsets read 0 and ignore
writes, because the MFP sits on the low byte of the bus. Write side effects, in
the order a real program breaks without them:

| register | a write does |
|---|---|
| IPRA/IPRB/ISRA/ISRB | `*reg &= value` |
| IERA/IERB | `*reg = value; ipr &= value;` — clearing an enable clears the pending bit |
| VR | bit 3 is software-EOI; clearing it clears ISRA and ISRB |
| TACR/TBCR/TCDCR | the prescaler changed → `interrupt_timers_changed()` |

Priority is channel 15 down to 0: scan IPRA bits 7..0 (channels 15..8), then IPRB
(7..0). Two helpers make it readable and testable: `highest_pending()` and
`blocked_by_service(channel)`. `mfp_acknowledge()` is then four lines — take the
highest pending, clear its IPR bit, set its ISR bit **only if VR bit 3 is set**,
return `(vr & 0xf0) | channel`.

`mfp_period_from`: prescale table `{0,4,10,16,50,64,100,200}`; `control & 0x0f ==
0` is stopped; `control & 0x08` is event-count/pulse-width, not delay mode; data 0
means 256; `period_ns = 1e9 * prescale * data / 2457600`. Do it in `long long` and
round — 2457600 does not divide evenly and the wrong rounding drifts a sequencer's
tempo by a bar an hour.

### acia.h

```c
#define ACIA_BASE_ADDRESS (0xFFFC00)
#define ACIA_LENGTH       (8)
#define ACIA_IKBD (0)
#define ACIA_MIDI (1)

void    acia_reset(void);
uint8_t acia_read(int which, int reg);        /* reg 0 status, 1 data */
void    acia_write(int which, int reg, uint8_t value);
int     acia_interrupting(void);              /* either, for GPIP4 */
int     acia_can_receive(int which);
void    acia_receive(int which, uint8_t byte);
int     acia_take_transmitted(int which, uint8_t *byte);
```

`which = (offset >> 2) & 1`, `reg = (offset >> 1) & 1`. Status: RDRF while a byte
is held; TDRE always set (the host will drain, so there is no reason to make an
application wait); OVRN on a second byte before the first was read; IRQ =
RX-enabled AND (RDRF OR OVRN). Reading data clears RDRF, OVRN and IRQ. Control
with `(value & 3) == 3` is a master reset.

**The keyboard ACIA must be modelled too, and this is not optional**: TOS's
`_int_acia` calls `ikbdsys` unconditionally, which reads 0xFFFC00, and an
unmapped address there calls `halt_execution()`
([memory.c:146-150](tosemu/src/memory.c#L146-L150)). Model it permanently idle —
TDRE set, RDRF clear, IRQ clear — so `_ikbdsys` falls through.

### iorec.h

```c
struct iorec_ring { uint8_t *buf; int size, head, tail; };
int iorec_push(struct iorec_ring *r, uint8_t byte);   /* 0 when dropped */
int iorec_take(struct iorec_ring *r, uint8_t *byte);  /* 0 when empty */
int iorec_count(const struct iorec_ring *r);
```

`iorec_push` is EmuTOS's `_midivec`: advance tail, wrap, **drop if the advanced
tail equals head**. One slot is always empty, which is what makes `head == tail`
mean empty — exactly what [tests/c-xbios.c:230](tosemu/tests/c-xbios.c#L230)
asserts. `xbiosdev.c` reads and writes the emulated fields around these so the
arithmetic has one home.

**Risks**: getting the A/B split backwards makes Timer A behave like Timer C;
assigning to IPR instead of ANDing makes every `bclr` clear the whole register;
forgetting that reading the ACIA data register lowers GPIP4 leaves TOS's handler
spinning.

---

## Stage 3 — `src/interrupt.c`: the only file that knows there is a 68000

```c
void interrupt_init(void);
void interrupt_reset(void);
int  interrupt_wanted(void);
int  interrupt_fd(void);              /* midi_fd(), for wait_for */
long interrupt_next_due_ms(void);     /* -1 when nothing is due */
void interrupt_tick(void);            /* throttled, from cpu_instr_callback */
void interrupt_service(void);         /* the same work, from inside a trap */
void interrupt_timers_changed(void);
int  interrupt_running(void);         /* re-entry guard, like aes_userdef_running */
```

### Memory areas

Registered in `interrupt_init`, and **only when something asked for them** —
that is the whole of the "default behaviour is unchanged" guarantee.

```c
add_fnct_memory_area("mfp",  MEMORY_READWRITE | MEMORY_SUPERREAD | MEMORY_SUPERWRITE,
                     MFP_BASE_ADDRESS,  MFP_LENGTH,  0, mfp_area_read,  mfp_area_write);
add_fnct_memory_area("acia", MEMORY_READWRITE | MEMORY_SUPERREAD | MEMORY_SUPERWRITE,
                     ACIA_BASE_ADDRESS, ACIA_LENGTH, 0, acia_area_read, acia_area_write);
```

Readable in **both** modes deliberately. An ST bus-errors these in user mode, but
`tos_read` does not raise a bus error — it halts
([memory.c:141-190](tosemu/src/memory.c#L141-L190)) — so refusing a user-mode poke
turns a program that touches the MFP outside Supexec into a dead emulator, which
is worse than the departure. Say that in the comment.

Both area handlers end by pushing ACIA state into the MFP:
`mfp_gpip(4, !acia_interrupting())`, and `mfp_raise(MFP_ACIA)` on the falling
edge. Getting that call site wrong in either direction is what makes TOS's
`int_acia_loop` spin.

Note `remove_memory_area` never advances its pointer
([memory.c:73-95](tosemu/src/memory.c#L73-L95), already in `TODO`): register these
once and never unregister; only `reset_memory()` is safe.

### The int-ack

[m68kconf.h:89-90](tosemu/src/m68kconf.h#L89-L90) becomes
`OPT_SPECIFY_HANDLER` / `tos_int_ack(A)`, matching the shape already used for
`cpu_instr_callback` at `:131-135`.

```c
int tos_int_ack(int level)
{
    int vector = M68K_INT_ACK_AUTOVECTOR;   /* the VBL at 4, the HBL at 2 */

    if (level == 6)
    {
        vector = mfp_acknowledge();
        if (vector < 0)
            vector = M68K_INT_ACK_SPURIOUS;
    }

    /* The line stays where it was put. Musashi only clears it for itself when
     * nothing acknowledges, so a line left asserted takes the same interrupt
     * again the moment the handler returns, for ever. */
    m68k_set_irq(0);

    return vector;
}
```

`m68k_set_irq(0)` from inside the ack is safe: it sets `CPU_INT_LEVEL = 0` and
`m68ki_check_interrupts()` then finds nothing above the mask, so there is no
recursion. The driver re-asserts on the next tick if a channel is still pending —
which it must, because while `FLAG_INT_MASK` is 6 inside a handler,
`m68k_set_irq(6)` is a silent no-op.

### The clock and the tick

One `clock_gettime(CLOCK_MONOTONIC)` behind `static long now_ns(void)` — a vDSO
call, and `aesevnt.c` already has `now_ms()` in the same shape. Sources are a
small array of `{ int channel; long period_ns; long due_ns; }`: the four MFP
timers plus the VBL at 20 ms (which agrees with `BIOS_Tickcal`
[bios.c:148-154](tosemu/src/bios.c#L148-L154) by construction).

```c
void interrupt_tick(void)
{
    static int countdown;

    if (!wanted)         return;
    if (--countdown > 0) return;

    countdown = TICK_INSTRUCTIONS;
    interrupt_service();
}
```

`TICK_INSTRUCTIONS` around 4096: tens of microseconds at the speed the host runs
Musashi, well inside the shortest interval a sequencer asks Timer A for, and one
vDSO read per few thousand instructions does not show in a profile. Much smaller
costs the whole emulator; much larger makes a fast Timer A jitter.

`interrupt_service()`, in order: `midi_pump()`; drain the MIDI ACIA transmit side
into `midi_give`; if `acia_can_receive` and `midi_take` has a byte,
`acia_receive` and update GPIP4; fire each timer whose `due_ns` has passed,
advancing `due_ns += period_ns` **no more than `CATCHUP_MOST` times** (coming back
from a suspended laptop with a 5 kHz Timer A would otherwise fire ten thousand
interrupts back to back and the machine would never execute another instruction —
past that, jump to now and say once what was dropped); bump the system variables;
dispatch.

### Writing `_hz_200`, and why not the obvious way

`m68k_write_memory_32(0x4BA, ...)` goes through `tos_write`, which checks
`is_supervisor_mode_enabled()` against the *emulated* CPU, and staticmem1 is
`SUPERREAD|SUPERWRITE` ([tossystem.c:793](tosemu/src/tossystem.c#L793)). Bumping
the counter while the application happens to be in user mode is refused — and the
refusal calls `halt_execution()`. The emulator would die at a random instruction
with a message about non-writeable memory.

Use `tos_mem_to_host_mem(0x4BA)`
([memory.c:118-136](tosemu/src/memory.c#L118-L136)), which returns a direct host
pointer and bypasses the mode check, and store four bytes big-endian by hand.
One helper, `static void poke_system_long(uint32_t address, uint32_t value)`, with
the whole reasoning in its comment — same for `_frclock` (0x466) and `_vbclock`
(0x462). **`_hz_200` is written whenever Timer C runs**, whether or not anybody
installed a handler, because it is a system variable programs read directly.

This is the single easiest thing here to get wrong in a way that only shows up on
somebody else's machine.

### Dispatch, and the hybrid

```c
static void dispatch(void)
{
    int channel = mfp_pending_channel();

    if (channel < 0)
        return;

    if (m68k_read_disassembler_32(0x100 + 4*channel) == 0)
    {
        /* Nobody claimed this one, so tosemu answers for it - which is what
         * TOS's own handler would have been doing. */
        handled_here(channel);
        return;
    }

    m68k_set_irq(6);
}
```

The zero test is a requirement, not a policy — see the uninitialised-vector fact
above. `handled_here` clears the pending bit and, for `MFP_ACIA`, runs the midivec
path; for the timers there is nothing to tell anybody, the counters having moved.

### Running 68000 code from the host

From `cpu_instr_callback` nothing more is needed: `m68k_set_irq(6)` builds the
frame and sets PC, and the loop fetches from the handler. From inside `wait_for`
or a blocking `Bconin(3)` we are inside a trap with no `m68k_execute` running, so
the handler must be run to completion. `interrupt_run_handler()` copies
`host_userdef_draw` ([aestree.c:751-910](tosemu/src/aestree.c#L751-L910)) and its
hard-won register discipline:

```
save d0-d7, a0-a7, pc, sr, isp;  isp_before = isp
m68k_set_irq(6)                          /* builds the frame and sets PC */
for (steps = 0; steps < INTERRUPT_STEPS; steps++)
{
    if (m68k_get_reg(0, M68K_REG_ISP) >= isp_before) break;   /* the RTE popped it */
    if (execution_halted()) break;
    m68k_execute(1);
}
restore SR first, then ISP, then the registers, then PC
```

SR before ISP before A7, for the reason
[aestree.c:890-899](tosemu/src/aestree.c#L890-L899) gives: SR decides which of the
two stack pointers a7 is, so putting the mode back afterwards files the restored
a7 under the wrong one. The step cap catches a handler that switches stacks under
us, and its message should name the vector the way `aestree.c:884-887` names the
object. `interrupt_running()` guards re-entry.

**Fallback if the ISP test proves flaky**: build the frame by hand —
`push_u32(INTERRUPT_RETURN); push_u16(sr | 0x2000);` with PC set to the handler —
and stop when PC reaches the magic address, exactly as `USERDEF_RETURN` works.
More robust, and it does not need `M68K_EMULATE_INT_ACK` for this path at all.

### midivec, and why it is a magic address

`XBIOS_Kbdvbase` stops handing out a zeroed block and fills it: `midivec`
(offset 0) with `MIDIVEC_MAGIC`, the eight others with `JUST_RTS_MAGIC`.
`MIDIVEC_MAGIC` is a two-byte magic memory area registered exactly like Supexec's
at 0x200 ([xbiossys.c:102-124](tosemu/src/xbiossys.c#L102-L124)): the first byte
reads 0x4e, the second does the work and reads 0x75, together `rts`. The work is
`iorec_push` of `(uint8_t)m68k_get_reg(0, M68K_REG_D0)` into the MIDI record —
precisely EmuTOS's `_midivec`.

A magic address rather than a host-side "is it still the default" branch because
the documented way to hook MIDI input is to read midivec, keep it, install your
own, and jump to the one you kept; a host-side branch serves the first case and
leaves the second jumping to zero. Known wart, shared with Supexec and worth a
`TODO` line: with `-vvv` the disassembler reads the same bytes and fires the side
effect.

**Risks**: a handler that never clears its ISR bit wedges every lower channel
(honouring VR bit 3 is the escape); Timer B in event-count mode counts HBLs that
do not exist and must be refused out loud rather than silently never firing;
reading TADR/TCDR gives the reload value rather than a counting-down one; a
handler that calls the AES re-enters `wait_for`, which the running guard catches.

---

## Stage 4 — the two landmines

### 4a. The supervisor stack grows into the system variables

[tossystem.c:883](tosemu/src/tossystem.c#L883) sets ISP to 0x600 and it grows
*down* into staticmem1 (0x380-0x5FF) — the TOS system variable area, with
`_hz_200` at 0x4BA. A frame is 6 bytes; EmuTOS's own handlers add 32
(`movem.l d0-d3/a0-a3`) and an application's typically 60. Three deep reaches the
timer's own counter. Nothing noticed because Supexec routines are short and every
system variable was zero (see the `TODO` entry about phystop/_membot/_memtop).

- Reserve it in `load_tos_environment` right after `biosram_free = BIOSRAMBASE`
  ([tossystem.c:631](tosemu/src/tossystem.c#L631)):
  `te->superstack = bios_static_alloc(SUPERSTACK_SIZE)`, 8 KB.
  `bios_static_alloc` only bumps a counter, so it is safe there.
- `start_cpu` sets ISP to `te->superstack + SUPERSTACK_SIZE`.
- Answer the existing `/* TODO is this really correct, or should it be the MSP? */`
  rather than moving it: on a 68000 there is no master stack pointer, ISP *is* the
  supervisor stack pointer, and the question only arises from the 68020 up.
- Guard in `interrupt_run_handler` and `interrupt_tick`: ISP below
  `te->superstack` means say so and halt. The BIOS RAM below the block holds the
  IOREC buffers and `_KBDVECS`, so a runaway handler eats exactly the structures
  MIDI depends on — silent corruption that looks like a MIDI bug.
- `host_userdef_draw`'s comment at
  [aestree.c:815-817](tosemu/src/aestree.c#L815-L817) — "a few hundred bytes for
  the short routines Supexec runs" — becomes untrue and must be reworded. The
  behaviour stays: GEM really did call a userdef on the application's stack.
- `superram` at 0x600-0x7FF is now genuinely unused; worth a `TODO` line.

A commit of its own, with `make check`, `make devpac-check` and
`make lattice-check` all run — it touches the stack every program stands on.

### 4b. `wait_for` sleeps through whatever comes due while it sleeps

In [aesevnt.c](tosemu/src/aesevnt.c):

- `struct pollfd fds[4]` at `:214` becomes `fds[5]`. Getting that wrong is a stack
  overwrite that shows only when wayland, daemon, scrap, giving and MIDI are all
  live at once.
- Register the fd the way scrap's is at `:451-458`.
- **Ordering that matters more than anything else here** — clamp *after* the
  "nothing could ever answer this wait" test at `:496`, never before:

```c
        /* ... the test at :496, exactly as it is ... */

        wait = (left < 0) ? -1 : (int)left;

        /*
         * And no longer than it is until something interrupts. No 68000
         * instruction runs while this sleeps, so a timer whose moment passes in
         * here would not go off until whatever else woke the poll.
         */
        due = interrupt_next_due_ms();
        if (due >= 0 && (wait < 0 || due < wait))
            wait = (int)due;

        poll(fds, nfds, wait);

        interrupt_service();
```

Clamping first would give a positive timeout whenever a timer runs, and the honest
message about waiting for a keyboard that is not there would become exactly the
silent hang the comment at `:483-495` was written to prevent.

- The MIDI fd does **not** count towards that test, for the same reason the scrap
  directory does not: what arrives serves the machine, not the application's
  question. Extend the comment at `:483-495` to say so about both.
- `midi_slot`'s revents are never read — the fd is there only to end the sleep.
  Calling `interrupt_service()` before the other dispatches keeps a timer that came
  due alongside a compositor event from lagging a whole poll cycle.
- The other blocking point, `gfx_selection_flush`'s poll, is bounded and rare.
  Note it; leave it.

---

## Stage 5 — existing call sites

### `src/bios.c` — MIDI as its own branch above the fall-through, not by widening `is_console`

| call | with `[midi]` configured | with nothing configured |
|---|---|---|
| `Bconstat(3)` | -1 when the MIDI IOREC has a byte, else 0 | **0** — [c-bios.c:85](tosemu/tests/c-bios.c#L85) |
| `Bconin(3)` | take from the IOREC; if empty, idle — service interrupts and poll with the next-due clamp — until a byte arrives or the machine halts, which is what `bconin3` spins for | **0 at once** — [c-bios.c:90](tosemu/tests/c-bios.c#L90) |
| `Bconout(3, c)` | hand the byte to the ACIA transmit side | discard, as now |
| `Bcostat(3)` | **-1** | **-1**, unchanged — [c-bios.c:79](tosemu/tests/c-bios.c#L79) |

`Bcostat` stays -1 both ways and the comment at
[bios.c:140-143](tosemu/src/bios.c#L140-L143) still holds. The blocking
`Bconin(3)` needs the same honesty `wait_for` has: no timer running and no port
open means nothing is coming, so say so and give up rather than hang.

### `src/xbiosdev.c`

- `XBIOS_Midiws`: send **cnt+1** bytes.
- `XBIOS_Iorec`: unchanged in shape; device 2's record is now filled by the
  midivec path, device 0's — which the test asks about — is not.
- `XBIOS_Kbdvbase`: fills the block as in Stage 3.
- `XBIOS_Mfpint(interno, vector)`: `mfp_disable`, write `0x100 + 4*(interno & 15)`,
  `mfp_enable` — EmuTOS's `mfpint`.
- `XBIOS_Jenabint`/`XBIOS_Jdisint`: `mfp_enable`/`mfp_disable`. Disabling clears
  pending *and* in-service as well as enable and mask; getting that wrong leaves a
  channel that never fires again.
- `XBIOS_Xbtimer(timer, control, data, vector)`: `mfp_setup_timer`, then the
  `Mfpint` above with the channel from `{A,B,C,D}` in that order, then
  `interrupt_timers_changed()`.
- `xbios_dev_reset` also calls `interrupt_reset()` and `midi_reset()` — the
  vectors, pending bits, running timers and a half-finished sysex all belonged to
  the application that has gone, and a sysex cut in half by a Pexec would
  otherwise be finished by the next program's first byte.

**These four become real unconditionally and the tests still pass — put the reason
in the commit message so nobody rediscovers it.**
[c-xbios.c:246-253](tosemu/tests/c-xbios.c#L246-L253) calls `Mfpint(0, 0L)` (zero
over the zero already at 0x100), `Jenabint(0)`/`Jdisint(0)` (channel 0 is
Centronics busy, which nothing raises), and `Xbtimer(0, 0, 0, 0L)` — Timer A with
**control 0, which is stopped**. Every argument is inert. A non-zero control there
would start a timer and the reasoning would no longer hold.

### `src/main.c`

`interrupt_tick()` at the top of `cpu_instr_callback`
([main.c:38](tosemu/src/main.c#L38)), **before** the verbose block, so an
instruction trace shows the machine at the instruction it is about to run rather
than at the handler it was pushed into.

### Where it starts

`init_tos_environment` has `/* TODO initialization other sub-systems here as
well */` ([tossystem.c:945-957](tosemu/src/tossystem.c#L945-L957)) and is the
honest place for `midi_open()` and `interrupt_init()`.

---

## Stage 6 — settings, build, README

[settings.c](tosemu/src/settings.c), after the scrap rows:

```c
    { "TOSEMU_MIDI",       "midi",    "device",     0 },
    { "TOSEMU_INTERRUPTS", "machine", "interrupts", 0 },
```

`TOSEMU_INTERRUPTS` earns its row: it turns the MFP, timers and 200 Hz clock on
**without** a MIDI port, which is the only way the suite can check that a real
68000 interrupt was taken with no ALSA and no hardware. Configuring MIDI implies it.

Makefile, beside dbus and libpng at
[Makefile:119-127](tosemu/Makefile#L119-L127):

```make
ALSAFLAGS = $(shell pkg-config --cflags alsa 2>/dev/null && echo -DHAVE_ALSA)
ALSALIBS  = $(shell pkg-config --libs alsa 2>/dev/null)
```

into `CFLAGS` at `:133` and `LIBS` at `:138`; `SOURCEFILES` at `:19-23` gains the
five files. Add `libasound2-dev` to `install-deps.sh`, and a `NO_ALSA=1` mirroring
`NO_WAYLAND`. **`make NO_WAYLAND=1 check` must pass either way** — that is what
"ALSA is optional" has to mean in practice, not just at link time.

README: two rows in the table at `:277-292`, a `[midi]` stanza in the example, and
a short paragraph on the three spellings — `hw:1,0,0`, `seq:20:0` or a port name,
`file:in.bin,out.bin`.

---

## Stage 7 — testing

### `bin/miditest` (`make midi-check`, added to `check:` at Makefile:510)

Links `mfp.o acia.o iorec.o midi.o settings.o` — which works only because none of
them call `m68k_*`. That is the reason for the split and the line to hold if
something later wants to reach into the machine from `mfp.c`. Same shape as
`bin/scraptest` ([Makefile:432-437](tosemu/Makefile#L432-L437)). Named by what
each check establishes, not what it calls:

1. **Timer arithmetic** — `mfp_period_from(0x50, 192)` is 5,000,000 ns, EmuTOS's
   own Timer C and 200 Hz exactly; the prescaler table end to end; data 0 counting
   as 256; control 0 stopped; control ≥ 8 refused rather than silently never firing.
2. **Priority** — 13 beats 6; IMR clear does not fire though IPR is set; IER clear
   never sets IPR; a higher ISR bit blocks a lower channel; VR bit 3 clear means
   the ack never sets ISR; channel 6 under VR 0x48 is vector 0x46.
3. **Register semantics** — `mfp_write(IPRB, 0xbf)` clears bit 6 and leaves the
   other seven, the write TOS's ACIA handler actually makes; clearing IERB clears
   the matching IPRB bit; GPIP bit 4 reads 0 while an ACIA interrupts.
4. **The ACIA** — master reset drops a waiting byte; reading data clears RDRF and
   IRQ; status bit 7 only with RX interrupts enabled; OVRN on a second byte.
5. **The ring** — 256 bytes holds 255; a full ring drops rather than overwrites;
   head and tail wrap; a take from empty says so.
6. **The file backend** — bytes out come back out of the file in order, bytes in
   come back through `midi_take` in order, an unknown spelling is refused with one
   line rather than ignored.

### `tests/c-midi.c` (`CTESTNAME`, no `-lgem`), run three ways

1. **Nothing configured** — the case the whole suite runs in. Restates
   `c-bios.c:79-90` and `c-xbios.c:229-253` in one file whose *name* is about MIDI,
   so a change breaking the default breaks a test that says what it broke.
2. **`TOSEMU_MIDI=file:in.bin,out.bin`** — send with `Bconout(3)` and with
   `Midiws(2, "abc")`, which is three bytes and fails loudly if somebody writes
   `cnt`. The Makefile compares `out.bin` against a file made with `printf`, the
   shape of the `TOSEMU_SCRAP_OUT` check at
   [tests/Makefile:495-500](tosemu/tests/Makefile#L495-L500). Read a known sequence
   back and check bytes and order.
3. **`TOSEMU_INTERRUPTS=yes`** — install a Timer C handler with
   `Xbtimer(2, 0x50, 192, handler)`, wait, and check both that the handler's
   counter moved and that `_hz_200` moved with it (read through Supexec). **This is
   the one check that a real 68000 interrupt was taken**, and it needs no ALSA, no
   port and no hardware.

Each asserts both `! grep -q '^not ok' out` and `grep -q '^1\.\.' out`, and uses
`$(TOSEMU)` for `$(NO_DAEMON)` and `--no-config`.

### What only a person with a USB interface can check — into `TODO`, not pretended

That a note sent really sounds; that a sysex dump of tens of kilobytes arrives
whole and in order; that a sequencer's Timer A tempo is steady against a
metronome; that `hw:` and `seq:` naming the same port behave the same; that jitter
under a busy desktop stays inside a few milliseconds. One program each: a patch
editor for the polled path, and Cubase, Notator or Dr. T's for the interrupt path
— the whole point of the MFP model being that the third kind of program works at all.

---

## Order — ten commits, each `make check`-clean on its own

1. `MIDI: bytes written to the port went nowhere` — `midi.c/.h`, file backend, settings row, Makefile flags.
2. `MIDI: there was no model of the MFP or the ACIAs to work against` — `mfp.c`, `acia.c`, `iorec.c`, `miditest.c`, `make midi-check`.
3. `cpu: the supervisor stack grew into the system variables` — Stage 4a, alone, with `devpac-check` and `lattice-check`.
4. `cpu: nothing in the machine could raise an interrupt` — `interrupt.c`: memory areas, int-ack, clock, host-side half only; `TOSEMU_INTERRUPTS`.
5. `AES: a wait slept through whatever came due while it slept` — Stage 4b.
6. `cpu: a handler installed by the application was never reached` — `m68k_set_irq`, `interrupt_run_handler`, the vector-is-zero test.
7. `MIDI: a byte that arrived reached no IOREC and no midivec` — the magic vector, `Kbdvbase`, the IOREC fill.
8. `MIDI: the BIOS answered for a port that was not there` — the Bcon* and XBIOS call sites.
9. `MIDI: nothing in make check reached any of it` — `tests/c-midi.c` and the three check lines.
10. `TODO: what MIDI still cannot do` — README, TODO, `install-deps.sh`, `NO_ALSA`.

Areas seen in the log so far are `AES`, `GEM`, `GEMDOS`, `cpu`, `tosaesd`, `tray`,
`rsc`, `TODO`; this adds `MIDI`.

---

## Verification

```sh
make clean && make                 # with ALSA present
make check                         # includes the new midi-check and c-midi lines
make NO_WAYLAND=1 NO_ALSA=1 check  # the CI shape, and the no-ALSA shape
make emuvdi-check
make devpac-check && make lattice-check   # after the supervisor-stack commit
```

Per `AGENTS.md`, prove each fix by putting the bug back faithfully, rebuilding,
watching the specific checks fail, then restoring and rebuilding again.

By hand, with a USB interface plugged in:

```sh
aconnect -l                                    # find the port
TOSEMU_MIDI=hw:1,0,0 bin/tosemu SOMEPROG.PRG   # rawmidi
TOSEMU_MIDI=seq:20:0 bin/tosemu SOMEPROG.PRG   # sequencer
aseqdump -p tosemu                             # watch what the emulator sends
```

Then the two that matter: a patch editor doing a sysex dump both ways, and a
sequencer playing against a metronome for several minutes to see whether the
tempo holds.
