- Mode 7 of Pexec takes the program flags a program header carries, and tosemu
  acts on none of them, so it is mode 5 with an argument it ignores. Same for
  the flags in a program header itself.
- A child of Pexec mode 0 gets the whole of memory rather than a TPA carved
  out of what its parent left free, so it is handed more than TOS would give
  it. An application that Mallocs before it Mshrinks gets nothing, where on
  TOS it would get what the parent released. The modes that load a program
  into the caller's memory do carve it out, and do not have this.
- A child of Pexec has a copy of the machine's memory rather than the same
  memory, because it is a forked host process, so nothing it writes is ever
  seen by its parent. TOS had one address space and no fork, and the modes
  that run a program the caller has already loaded - 4, 6 and their
  asynchronous twins - are there so that the two can pass structures back and
  forth through it. Reading works, the child having inherited everything as it
  stood; it is the answer coming back that is lost.

  Devpac 3 is the case that shows it. Its editor loads GEN.TTP resident with
  mode 3, asks mode 5 for a basepage, points that basepage's text segment at
  the resident copy and runs it with mode 4, handing it the address of a
  structure of its own in an environment variable. The assembler reads the
  source out of the editor's buffer and writes back what it found rather than
  touching a file at all - and the editor gets back a buffer nothing wrote to.
  It reports no errors and shows an empty document.

  Closing it means the machine's memory being shared rather than copied -
  MAP_SHARED rather than calloc for te->appmem and the areas beside it - for
  the modes where the child keeps the machine it inherited. The modes that
  load a program build one of their own and want no part of it.
- Only sixteen children of the asynchronous Pexec modes can be waited for at
  once, and Pwait3 ignores the resource usage it is handed, which tosemu has
  nothing to fill in.
- The BIOS device handles, -1 CON:, -2 AUX: and -3 PRN:, which GEMDOS accepts
  everywhere a handle is taken. Lattice's startup code calls Fforce(4, -2).
  tosemu has a console but no serial port or printer, so -1 is the one with an
  answer to give, and the other two keep reporting EIHNDL.
- Console output goes to the console rather than through handle 1, so Cconout
  and Cconws ignore an Fforce that redirected it. Everything a program writes
  now passes through one place - console.c - which is what makes this doable at
  all; before it there was no single point to redirect.
- Ptermres, which has nowhere to stay resident in. Cubase is the case that
  wants it: MROS, the MIDI kernel it loads before it will start, is a TSR, and
  Pexec of it ends on this call. Implementing it alone would not be enough -
  see the entry above about a child of Pexec having a copy of the machine's
  memory rather than the same memory. A program that stayed resident would stay
  resident in the forked child, and whoever Pexec'd it would find nothing
  there. See RESIDENT.md on the pexec-resident branch, which proposes loading
  the resident and the program that uses it into one machine from one command
  line rather than making Pexec share memory.
- remove_memory_area never advances its pointer, so it only ever finds the
  first area. reset_memory happens to always ask for that one.
- The "superram" area at 0x600 is mapped, is five hundred and twelve bytes
  long, and nothing has ever used it. It was put there for the supervisor
  stack, which never reached it: the stack pointer started at 0x600 and a
  stack grows downwards, so what it actually used was the five hundred and
  twelve bytes below that address rather than the ones above it - which is the
  system variable area, and which is why the stack has been moved out to RAM
  the system owns. The area is left mapped because a program reading 0x600 is
  cheaper to allow than to explain, but it is five hundred and twelve bytes of
  nothing and the machine would be no different without it.
- No period sequencer has been made to play anything, which is the whole point
  of the MIDI work rather than a loose end from it.

  One now runs. Cubase 3.01 starts - with MROS loaded by --resident and the key
  in the cartridge port - reaches its event loop with a window and a menu bar,
  and MROS puts its own handler on Timer B at a kilohertz and is called there:
  997.4 delivered against the 999.0 it asked for, over a minute, with the
  shortfall all in the first seconds rather than accumulating. That answers the
  question this entry used to ask, which was whether a program that programs
  the chip itself and clocks everything off its own handler would run at all.

  What it does not answer is whether it plays. Nothing has been loaded into it,
  nothing has been started, and no note has been asked for - so the path from
  its handler through Midiws and the ACIA to a port has never been exercised by
  the program it was built for. That is the next thing to do, and it wants a
  person driving the GUI rather than a test.

  Nor has any of it been tried against real hardware. The sequencer backend was
  watched through ALSA's own loopback, which is not an interface with a cable
  in it, and the raw backend - the one meant for the USB device somebody
  actually plugs in - has never been opened against a device that exists.
- There is no way to say no to interrupts once something has implied them. A
  MIDI port turns them on and so does the key in the cartridge port, and each
  of those is an or against the setting rather than a default the setting can
  overrule - so interrupts = no on a command line that also names a port is
  simply not heard. Nobody has wanted to yet, and the suite depends on the
  present behaviour: c-midi sends bytes on a line that says no to interrupts,
  and they only go out because the port overrode it. What would close it is
  three states rather than two - asked for, refused, and not mentioned - with
  the implications filling in only the third.
- The vertical blank keeps its counters and interrupts nobody. _vbclock and
  _frclock advance fifty times a second, so a program that measures time by
  them is right, but the vector at 0x70 is never taken and neither is anything
  on the vertical blank queue at 0x456. A program that hangs a routine there -
  which is how an ST animated anything - is not called. It wants the same
  dispatch the MFP channels have and a queue to walk; what stopped it being
  done was that nothing needing it had come up.
- A byte at a time moves from the host into the ACIA, once per look at the
  clock, which is once per few thousand instructions. That is the right shape -
  the chip holds one byte and the machine has to read it before the next may go
  in - but it ties how fast MIDI arrives to how fast the program is executing
  rather than to the clock. A system exclusive dump of any size arrives slowly,
  and a program that stops executing stops receiving. What it should be is a
  byte every thirty two microseconds, which is what 31250 baud comes to.
- Timer B counting display lines is refused rather than emulated. It is the one
  MFP mode that counts something the machine does not have: there is no video
  running, so there are no horizontal blanks to count. A program using it for a
  split screen or a raster effect is told out loud and gets no timer, which is
  better than silently never firing but is not the same as working.
- Bconmap is remembered and never consulted, so a program that maps a device
  onto the serial port still reaches nothing. It was already true before MIDI
  and is worth writing down beside it: the machine now has one device that
  really goes somewhere, which makes the mapping mean something it did not
  mean before.
- Cauxin, Cauxout, Cauxis and Cauxos still halt the emulator, and Fmidipipe
  with them. The serial port has nowhere to go, which is why they were never
  written; Fmidipipe is the odd one out now, being about MIDI, and what it asks
  for - redirecting one process's MIDI into another's - has an answer here that
  it did not have before.
- A program that saves the ACIA's interrupt vector and chains to what was there
  gets a routine that returns and does nothing, rather than one that takes the
  byte out of the chip the way TOS's did. Replacing midivec - which is the
  documented way to watch MIDI, and what the vectors in _KBDVECS are for - does
  work properly, so this is the less travelled of the two roads. Closing it
  means the ACIA's vector pointing at magic memory of its own that does what
  acia_arrived does, the way midivec's already does.
- The magic memory that midivec points at is read by the disassembler as well
  as by the processor, so running with -vvv puts a byte in the input buffer for
  every time the trace walks past it. The same is true of Supexec's, and for
  the same reason: nothing distinguishes a read that is about to execute from
  one that is only looking.
- A routine that draws an object for the application may only call the VDI,
  and one that calls the AES is refused rather than obeyed. The AES is
  halfway through drawing a tree when it calls the routine, and the tree it
  is drawing is the copy aestree.c made, which the next AES call would free
  and build again. Making that safe means a stack of trees rather than one,
  and there is nothing yet that wants it: the routine is documented as VDI
  only, and both of WTEST's keep to it.
- A routine that draws an object runs with whatever writing mode the AES left
  set on its workstation, because it draws through the AES's handle. That is
  what an ST did as well - WTEST leans on it, and a button held down comes
  out inverted rather than erased - but it is the sort of thing that will
  differ from real GEM somewhere, and it will look like the routine being
  wrong rather than the mode.
- A colour icon is drawn in black and white. A G_CICON points at a CICONBLK,
  which is an ICONBLK with a chain of colour versions of the same icon after
  it - one for each resolution the icon was drawn for - and only the ICONBLK
  at the front of it is brought across, so what is drawn is the mono version
  underneath. Which is what a CICONBLK carries one for, and what the icon
  looked like in high resolution, so it is a fallback rather than a hole; but
  a machine with four planes ought to be showing the four plane icon.

  The chain is the work: each link is a plane count and up to four forms - the
  image and its mask, and the same again for the selected state - where a mono
  icon is two. Picking which link is the other half, and it is not aestree.c's
  to decide from what the application handed over: rsrc_load fixes the chain
  up for the screen it finds, so what arrives here should already be the right
  one, and that wants checking against a resource file that has more than one.
- Investigate how to support non-planar modes (up to 16bpp currently) and 
  planar modes without having to rewrite the entire VDI stack.
- The system variables that say where memory begins and ends - phystop at
  0x42e, _membot and _memtop, and v_bas_ad at 0x44e - are all nought, because
  nothing has ever written them. A program that asks GEMDOS how much memory
  there is gets the right answer, and one that reads the variables the way a
  Supexec'd routine of the period does is told the machine has none. It is
  worth doing now that how much there is can be chosen, since that is the
  answer they would be carrying.
- Which window a piece of drawing belongs to is decided by aes_wind_owner, and
  where it says nothing certain it is a guess. GEM never says: an application
  sets a clipping rectangle and draws, and where two windows overlap that
  rectangle is inside both. What is listened to is the conversation around the
  drawing - a message about a window, a wind_get of one - and where there is
  none, the drawing is placed by which window holds it and which of those is in
  front.

  The guessing is good enough for everything tried so far and it is still
  guessing. An application that draws in a window it has said nothing about,
  inside the part another window is standing on, lands in the wrong one; so
  does one that answers a message about one window by drawing in another
  without asking anything first. Microsoft Write does neither, and nor does
  anything else here, which is why the fallbacks are what they are rather than
  something cleverer.

  What would end it is the application saying, and GEM has no way for it to.
  wind_update brackets a redraw without naming a window, which is the shape the
  answer would take if there were one.

- wind_update takes no
  lock, and with a process per application there is
  nothing yet for it to protect: each has a screen of its own and what reaches
  the desktop are its windows, so two applications cannot reach each other's
  pixels. It was a lock because every application drew into one screen. It
  becomes one again when windows belonging to different applications overlap
  and have to be composed from a single surface rather than shown side by
  side - which is the same piece of work as the surface router, and neither is
  needed until then.
- A keyboard table an application installs through Keytbl changes what the next
  application to ask is told and nothing else, because nothing in tosemu reads
  the tables: what a key types is what the desktop says it types. Handing out a
  table is what applications actually want it for - a menu shortcut on
  Alternate and a letter arrives as a key with no character, and the table is
  how the letter is got back - so the half that matters works. What does not is
  a program that installs a layout of its own and expects to type with it,
  which was how a person changed keyboard on an ST. Closing it means keyboard.c
  reading the installed table rather than the codepoint, and then the layout on
  the desktop stops deciding what gets typed, which is a decision rather than a
  detail.
- The keypad an ST had is unreachable, and the cursor keys are the reason. An
  ST puts its cursor block where a PC keyboard puts its keypad, and it is the
  cursor keys a GEM application waits for, so a PC's keypad is read as the
  cursor block and the ST's own keypad - scan codes 0x67 to 0x72, which some
  applications use for numbers - has nothing pointing at it. What would close
  it is a setting: one keyboard cannot be both, and which of the two somebody
  wants depends on the application in front of them.
- The alternate translation tables in a KEYTAB are not handed out, only the
  three TOS 1.0 had. They arrived with the _AKP cookie, which tosemu has no
  cookie jar to declare, so an application has no way to look for them and no
  reason to read past the three - but the structure it is given is three
  pointers long, and an application that reads a fourth reads whatever is
  reserved after it.
- A GEM application is told what the desktop has on its clipboard only while
  one of its windows has the keyboard, which is Wayland's rule rather than
  anything tosemu chooses: a client with no focused surface is sent no
  selection at all. So an application that has opened no window cannot paste
  from the desktop, and one whose window is not on top learns nothing new until
  it is. Nothing is wrong and nothing is missing - it is worth writing down
  because it looks exactly like the bridge being broken, and because it is the
  first thing to check when a paste comes up empty.

  What would close it is the daemon holding the last offer for the session and
  handing it to whichever application asks, rather than each one hearing it
  separately. That is a real piece of work: the daemon cannot take a selection
  itself, having no seat, but it can be told what the current one is by
  whichever emulator does have focus, and pass the bytes on. Nothing needs it
  yet, an application being pasted into normally being the one in front.

- A picture coming in is quantised to the screen's palette, which is the
  machine's, and the machine's is whatever the application last set it to. A
  screenshot pasted into a program that has loaded a palette of its own comes
  out in that program's colours. That is what an ST would have done as well,
  and it is not obviously the wrong answer, but it means the same picture
  pasted into two applications arrives looking different.

- Dragging a window's elevator follows the pointer only when the button goes
  down: a press says where to put it and the slider goes there, and moving the
  pointer while holding it does nothing until the next press. GEM tracked the
  drag, which is what makes scrolling feel continuous. The tracking loop is
  gr_slidebox, which is bridged already and wants the frame as an object tree
  it can be given - aesframe.c builds one and throws it away, so keeping it is
  what this needs.

- A window resized by the desktop whose application does not answer WM_SIZED
  stays the size it was, and the compositor goes on thinking it is the size it
  asked for. That is GEM's own arrangement - the application decides, and one
  that ignores the message keeps its window - but on a desktop it shows,
  because what is left over is drawn by the compositor rather than by nobody.
  Only a window created with a size box can get into it, so an application that
  asked to be resizable and then declined to resize is the whole of the
  exposure.

- Minimising a window has no gadget of its own, because GEM has no such thing:
  a window went away or it did not, there being nowhere for one to go. It lives
  on the desktop's window menu, which the other mouse button on the title bar
  brings up. The gadget it would want is AES 4.1's iconifier, W_SMALLER, and
  nothing written for the AES this reports being would ever ask for one - so
  drawing it would be drawing a button no application knows about.

- The full box makes a window as large as the screen the AES lays windows out
  on and does not put the desktop's window into its maximised state, so on a
  640x400 screen the full box and the desktop's maximise mean different sizes.
  Asking the compositor to maximise instead would mean the two deciding the
  size in turn - it configures, the application answers, which configures again
  - and the window that came out of that would be neither of the two things
  somebody asked for. A screen the size of the display, which is what
  TOSEMU_SCREEN=native-mono is for, makes them the same thing.

- What a surface owes is one rectangle round everything drawn in it, so two
  changes at opposite corners of a screen come out as the screen. A list of
  them would convert what actually changed; one rectangle is what fits in the
  surface without anything to allocate or free, and what suits the way GEM
  draws, which is a rectangle at a time with the clipping set to it. Nothing
  has wanted better yet. Whoever does it wants to carry a handful of
  rectangles, merge the two nearest when they run out, and hand the list to
  wl_surface_damage_buffer as it stands rather than one box round the lot.

- The compositor will take a buffer of the machine's own pixels if it is asked
  in a format it knows, and then there would be no converting to do at all.
  Four planes is not one of them, so that means finding out what it does know
  and whether anything can hold pens without turning them into colours first -
  and a palette that the machine changes under it would still have to go
  somewhere. It is a larger piece of work than the two that came before it and
  it is the one that would finish the job.

- m68kmake.c, Musashi's code generator, warns in five places about writing a
  path into a buffer that a long enough argument would overflow - and above
  them is the strcpy of argv[1] that would do the overflowing. It cannot happen
  from the makefile, which hands it build/gen/, and it only shows in a build
  with OPT set to something without -O, gcc needing the optimiser to see far
  enough at -O2 to stop worrying. So it is a real hole in a program nobody runs
  by hand, which is why it is written down rather than fixed.

- Nothing in make check reaches the parts of a window's frame that ask the
  desktop for something: dragging it by the title bar, pulling the size box,
  and the window menu on the other button. All three want the serial of an
  input event to prove they are answering something a person did, and injected
  input has none - a compositor is not something a test can arrange, and this
  is the far end of that. They were watched working against kwin instead, the
  configures driven through its scripting interface rather than by hand.

- The boxes an application drags about follow the mouse correctly but can only
  be seen where a window is showing the screen they are drawn into. A rubber
  band pulled out inside a window works; one pulled across the desktop is
  followed correctly and seen by nobody, and the same goes for graf_movebox,
  growbox and shrinkbox, which animate between two places that are usually not
  both inside a window. This is the same gap dialogs had before form_dial gave
  them windows of their own, and it closes the same way: a surface for the
  drag that lives as long as the drag does, marked so the desktop leaves it
  alone. It is the overlay surface in the plan.
- The file selector lists a directory and answers with what was chosen, and
  what it has not been watched doing is somebody picking a file out of the list
  with the mouse. Listing, walking into a folder and the drive buttons are all
  EmuTOS's and are as tested as the rest of the reused library; the part that
  is new is underneath, in src/emuvdi/hostfs.c, and that is exercised by the
  listing appearing at all.

- src/emuvdi/hostfs.c answers four of GEMDOS's calls against the host filesystem
  rather than against tosemu's own GEMDOS, because every entrance to that
  reads its arguments off the 68000's stack. It is a second implementation of
  Fsfirst and Fsnext and should not become a third of anything: what it shares
  with the original is tos_path_to_host rather than a copy of the translation.
  If more of GEMDOS is wanted from the host side - shel_find will want it - the
  answer is to give gemdosfile.c host callable entry points and have both use
  them, not to keep adding here.
- The accessory scenario is not in make check. An accessory registering, its
  name appearing in another application's Desk menu, and AC_OPEN reaching it
  when the entry is picked all work and were watched doing it, but automating
  it wants three processes with a daemon between them and the recipe for that
  was not reliable - the run either hung or left nothing in the file it was
  meant to write. The two process message test next to it is the shape to
  follow; what it does not have to deal with is a third process that has to be
  clicked in the right place before anything happens.
- An accessory started by the daemon inherits the daemon's environment, so it
  talks to a compositor whether or not the application that will show it in its
  menu does. That is right for a session and wrong for a test, and it made one
  look as though messages were arriving late when they were not. Whether an
  accessory should be told where to draw - or simply left to inherit, as now -
  is a question for when there is a desktop of our own to start them from.
- The block Physbase hands out is reserved when the machine is laid out and
  sized from the screen this process's own settings ask for, so a daemon
  deciding on a larger one leaves it short. The daemon is not asked until GEM
  starts, which is after the memory map is fixed, and an application then
  hears about a screen bigger than the block it has been told to paint in - so
  a program writing a screen's worth into it writes past the end, the way one
  did before the block was sized from the screen at all. It reaches only a
  program that draws without the VDI: a raster operation naming the screen,
  by zero or by that address, is drawn on the surface the VDI keeps and never
  touches the block.

  Closing it means settling the screen before the machine is built, and that
  means asking the daemon before there is an application to ask on behalf of.
  A program that never opens a window would be joining a session it has no
  business in, which is worse than the gap. The other way is to reserve at the
  first Physbase instead, and that wants somewhere to take the memory from
  that is not where the program is standing: the top of RAM is its own stack
  until it Mshrinks.
- GDOS is there now - ASSIGN.SYS, the fonts on the disk, and typefaces by
  name on FreeType behind the SpeedoGDOS calls - and these are what is left
  of it.

  vqt_fontheader, v_getbitmap_info and v_getoutline are refused rather than
  answered. Each hands something back through a buffer the application
  supplied, whose address arrives in two words of the control array, and
  filling one in means crossing the seam in the direction nothing else in the
  VDI goes: the buffer is in the machine's memory and the answer is on this
  side. vst_error says so to an application that asks. v_getoutline is the
  one somebody might actually want, a character as a polygon being how a
  drawing program turns a letter into something it can edit, and it is
  FT_Outline_Decompose into ptsout once there is somewhere to put the answer.

  Kerning answers that there are no pairs. The faces have them - every
  outline font does - and reading them is vqt_pairkern and vqt_trackkern
  filling in what FreeType already knows. What stops it being free is that
  kerning changes where every character after the first one goes, so v_ftext
  and vqt_f_extent have to agree about it or a line comes out laid out one
  way and drawn another.

  v_ftext_offset draws the string as one run rather than reading the
  positions it was given. A program that has justified a line itself hands
  over a position for each character, and what it gets is the same line set
  without the justification. Reading them is a loop rather than a design
  question; nothing that has been run here asks for it yet.

  Nothing maps the Atari character set above 127. A document with an umlaut
  in it draws whatever Unicode has at that code point, which is not what an
  Atari would have drawn - the top half of the set is Atari's own, and
  vst_charmap is where an application says which set it means.

  There is no cookie jar, so a program that detects SpeedoGDOS by looking for
  the FSMC cookie rather than by calling vq_gdos finds nothing and goes on
  believing there is none. That is the same hole as the system variables
  above: the pointer at 0x5a0 is nought like the rest of them.

  And the metrics warning stands. An application lays itself out from what
  vqt_extent tells it, so a substituted face moves every line break on the
  page. That happened on real SpeedoGDOS too and is not a fault; it is the
  reason the fonts from files are the ones worth getting exactly right, those
  being what a document of the period was actually set in.

- The line-A answers $a000 and refuses the other fifteen. Refusing stops the
  emulator and names the call, which is what an unimplemented GEMDOS call
  does, and it is a great deal better than the nought vector that was there
  before - a program executing one of these used to jump to address nought
  and walk through the whole of memory. But a program that really draws
  through the line-A still does not work, and there were plenty: it was the
  fast way to put a pixel on the screen, and every demo and half the games
  used it in preference to the VDI.

  What each of them draws is in 3rdparty/emutos/vdi/, since the line-A and
  the VDI are the same routines reached two ways, and EmuTOS's linea.S is
  the table of which is which. The work is not the drawing, it is the
  arguments: a line-A routine reads them out of the variable block in the
  machine's memory and the VDI here takes them as C arguments on the host,
  so each one wants the block unpacked into a call across the seam.

  The three system fonts are the same seam and are the reason $a000 hands
  back a table saying there are none. TOS answered with the machine's own
  bitmap fonts, for a program that draws its own text; the copies here are
  EmuTOS's, compiled for the host and sitting at host addresses that a 68000
  cannot reach. Making them real means the header, the offset table and the
  raster copied into the machine's memory the right way round - which is
  bounded work, and nothing that has been run here has asked for it yet.

- Microsoft Write runs now. It was three things in a row, and none of them
  was the fonts: the line-A, which its loader calls once and which nothing
  answered; Super, which took the caller's stack away; and Dfree, which was
  not implemented and stopped the emulator the moment a document was open.
  It starts, puts up its menu bar, opens Untitled and sets typed text. What
  it has not been watched doing is printing, or loading a document off the
  disk. It is the one thing here that ships a complete ASSIGN.SYS and thirty
  six .FNT files, so it is the program to watch the fonts from files in.

  Atari Works is the one that does run, and it is where the font work was
  actually shaken out: it starts, opens its word processor, enumerates the
  faces and sets text in them. Three faults came out of watching it. What it
  has not been watched doing is loading a document written in a face it then
  has to substitute, which is where the metrics warning above stops being
  theoretical.

- Splitting a line in Microsoft Write leaves the tail of the old line on the
  screen. Put the caret in the middle of a line and press Return: the two new
  lines are drawn correctly, and whatever the old line had beyond the first
  character of the split is still sitting there underneath.

  What Write does is not in doubt, and it is the same at every resolution,
  with GDOS and without, and with every window on a surface of its own or all
  of them sharing the screen. It redraws each changed line as v_gtext of the
  line's new text plus one trailing space, in replace mode with clipping
  turned off, and scrolls the lines below the two it redrew with a screen to
  screen vro_cpyfm. Three lines of a document with the caret at the start of
  the second come out as v_gtext " " on line two, v_gtext "BBB " on line
  three, the copy, and v_gtext "CCC " on line four. One trailing space clears
  one character cell, which is all that ever gets cleared, and nothing else
  Write asks for would clear the rest.

  So Write is relying on something to have emptied that line, and what it is
  has not been found. Ruled out so far: it is not the per window surfaces and
  not the compositing, the same pixels come out with every window sharing the
  screen; it is not the screen size, it happens at 320x200; it is not GDOS,
  it happens with no ASSIGN.SYS; it is not Write scrolling the screen itself,
  the block Logbase hands out is never written to in a whole session; and it
  is not the resolution Getrez reports, because 8, which is no machine's, and
  2 take Write down different paths and leave the same line behind.

  The next thing to try is the one that cannot be tried from here: what a real
  VDI does with v_gtext in replace mode, since everything above says Write
  expects more of the line to be cleared than a character cell. Worth checking
  against Hatari with a TOS ROM before looking anywhere else in tosemu.

- Drawing outside a window is not promoted to a window of its own. form_dial
  says a rectangle is being reserved and that gets one, which covers dialogs;
  an application that draws on the desktop beside its window without saying so
  - Calamus and its tool palette are the case the plan was written around -
  draws into the screen the AES keeps and is seen by nobody.

  The plan's answer is a scratch surface the size of the screen with the
  regions that were drawn into tracked, and a rectangle that stays put for a
  moment promoted to a window. The hysteresis is what stops a mouse drag
  spawning windows. It also wants a per application file to say what the
  heuristics got wrong, which is the honest part of the design rather than an
  admission: no rule about where an application meant to draw will be right
  for every application.

- Settings and About, the tosemu dialogs of its own, are not written, and the
  reason is a decision rather than an oversight. The plan has the daemon link
  the AES, the VDI and the graphics and run a built in desktop application to
  own them. That reverses the one thing kept true of the daemon throughout -
  it links no part of the emulator, and everything it knows is in aesproto.h -
  and would make the cross compiler a dependency of an ordinary build.

  The cheaper way that keeps both: make About an ordinary GEM program the
  daemon starts when the entry is picked, the way it starts an accessory. It
  already knows how to start programs. That wants deciding rather than
  assuming, because the plan says otherwise.

- No .desktop files are written for GEM programs, so one has no name of its own
  in a launcher or a task bar, and no picture of its own either. The windows
  already carry an app_id and EmuTOS's generic application icon, which are the
  halves that need no files: a task bar shows what the desktop the program came
  from would have shown for it, which is the right answer for a program that
  never said what its own icon was. What a file would add is a name and a
  picture belonging to the program rather than to the emulator running it.

- The printer prints, and five things about it are known to be short of what a
  driver of the period did.

  Bitmap fonts are the screen's. `ASSIGN.SYS` has a section per device and the
  printer sections are the ones holding the printer fonts, which is exactly
  what Atari sold them for: a ten point .FNT is thirteen scan lines tall
  because a screen is about seventy two dots to the inch, and the same file on
  a three hundred dot page is type a person cannot read. gdos_assign_init takes
  the device number already and is called once, for the screen; a printer
  wants its own list, its own font chain and its own scratch buffer, and
  vst_load_fonts wants to answer for whichever device the handle names. The
  outline faces need none of this - a size in points becomes a size in pixels
  through the device's resolution, which prndev.c already says - so a document
  set in one of those prints correctly today and one set in a .FNT does not.

  v_bit_image and vq_scan are not answered. Both belong to a driver that could
  only hold a band of a page at a time: vq_scan says how tall a band is so that
  an application can draw the page in strips, and v_bit_image prints a picture
  straight out of a file. This driver holds the page, so the first has nothing
  interesting to say and the second is a file format nobody has written the
  reader for. An application that calls vq_scan gets no answer rather than a
  wrong one, which is a call it has to be watched making before it is worth
  guessing at.

  v_output_window prints the sheet rather than the rectangle it was given, for
  the same reason: the rectangle was how a program handed a band driver one
  band, and there are no bands here.

  The unprintable margins are reported as nothing, INQ_TAB 40 to 43 being
  nought. Every printer has them and none of them has the same ones, and there
  is no way to ask a queue what its are through lp - which is the price of not
  linking libcups. What it costs is a program that lays a page out inside the
  margins it was told about: told none, it uses the whole sheet and the printer
  clips what it cannot reach. Asking properly means libcups and
  cupsGetDestMediaDefault, which is a build dependency for one number.

  And a paper size is a setting rather than what is in the tray, for the same
  reason and with the same answer.

- Drawing outside the device is only stopped on the printer. A workstation
  opens with clipping off, and with it off the VDI does not check where it is
  drawing: a row below the last one is written past the end of the memory the
  device has, which is the screen surface or the page. The printer's
  workstation is opened with clipping on because a page is as large as a
  setting on this machine says and no program can have been written for it -
  see prndev.c. The screen has the same hole and nothing has fallen down it,
  every program having been told the size of the screen it is drawing on. The
  honest fix is for the surfaces to have a row of slack at the end so that an
  overrun lands somewhere harmless, rather than for the VDI to be made to check
  on every call what GEM said it would not.

- There is no PRN:. A program that prints by writing characters to the printer
  port rather than by drawing on a workstation reaches nothing - the BIOS
  device handle -3 above, and Cprnout and Bconout(0) with it. That is a
  different kind of printing from the one that works now: it is a byte stream
  of somebody's escape codes, so making it print means either deciding which
  printer it is pretending to be and rendering that, or handing the bytes to
  CUPS as text and letting the queue make what it can of them. The second is
  cheap and is probably what a program that does this wants.

- The console on a terminal passes the VT52 through rather than interpreting
  it. A program that clears the screen sends ESC E and the terminal, which
  speaks ANSI, prints an E; GenST sets the wrap mode with ESC v and a stray v
  is what starts its output. The Atari character set goes out raw as well, so
  an accented letter is a byte no UTF-8 terminal has a glyph for.

  Neither is worth fixing there, which is why this is written down rather than
  done: the console on a screen has the VT52 and the Atari font and gets both
  right, and the terminal one exists so that a program's output can be piped
  into a file or read by a Makefile - which is a use that wants the bytes as
  they were written. What would close it is translating the escapes a terminal
  can express and dropping the rest, the way EmuTOS's CONF_SERIAL_CONSOLE_ANSI
  does, and it would cost the pipe its transparency.

- A console window is taken away when the application next waits for a GEM
  event, which is where an ST redrew over the text. A program that writes a
  line, goes round its event loop and writes another therefore gets a window
  that flashes. Nothing seen here does it - a program that drops to the console
  stays there until it is finished - and the alternative is a rule about how
  long a console stays up, which is the same kind of guess the drawing
  heuristics above are.

- The console reads standard input and so does Fread on handle 0, and neither
  knows about the other. Asking whether a key is waiting reads one and keeps
  it, so a program that mixes the two can have a byte taken by the console that
  it meant to read as a file. It is the same seam as the handle 1 item above
  and closes with it.

- Nothing in make check reaches the pointer half of selecting text on the
  console: the drag that makes a selection, the release that copies it and the
  middle button that pastes. The suite runs with no window, so there is no
  console window for a pointer to be in, and injected input goes to the queue
  the AES reads rather than to the console - which is right, a console window
  being a window GEM knows nothing about.

  What is checked is everything underneath, in bin/vditest: the copy of the
  characters conout.c keeps beside the pixels, that it scrolls when they do,
  what comes back for a range and for one named backwards, and the blanks a
  line was padded out with being dropped. What is not is the forty lines that
  turn a place the pointer was into two cells, and those have not been watched
  working either - unlike the window frame's gadgets, which are in the same
  position and were at least driven by hand against kwin. This wants somebody
  dragging across a console window and pasting into one before it is believed.

- The whole of the cartridge port clocks the key, where on the machine only
  half of it does. The port is two banks of sixty four kilobytes with a chip
  select each, and the key is wired to one of them, so a read of the other half
  answers but does not move the key on. Here both halves do. Nothing has been
  seen to read the half Cubase does not, and a program looking for the key
  finds it either way, which is why this is written down rather than done -
  what would close it is knowing which of ROM3 and ROM4 is the upper bank
  firmly enough to refuse the other.
- The cartridge port is read by the disassembler as well as by the processor,
  so running with -vvv moves the key on for every time the trace walks past a
  read of it, and the stream the program gets is not the one it asked for. The
  same is true of the magic memory midivec and Supexec point at, and for the
  same reason: nothing distinguishes a read that is about to execute from one
  that is only looking. It means a program using a key cannot be traced at the
  instruction level, which is the level at which one would want to trace it.
- The operating system header answers three of its fields and leaves the rest
  at nought. os_run is the one worth filling next: it points at the pointer to
  the running basepage, which is how a resident program finds out whose memory
  it is standing in, and tosemu knows the answer - current_basepage - but has
  no longword in the machine's own memory kept in step with it. os_kbshift is
  the same shape of problem, the shift state being a host variable in bios.c
  rather than a byte the machine can point at. The reset handler, the GEM
  memory usage block and the GEMDOS pool are not oversights: there are none,
  and an address for them would be a pointer somebody follows.
- Which TOS the machine claims to be is fixed at 3.06 rather than being a
  setting, and it is a number programs branch on rather than only display. It
  is 3.06 because MROS takes an entirely different path below 3.00 - looking
  for TOS's own code in the ROM to copy and patch, which this machine cannot
  satisfy - and nothing else has yet wanted a different answer. A program
  written for an older TOS that refuses to run on a newer one would want one,
  and then this becomes a setting.
- The three places tosemu says what version it is do not agree, deliberately:
  the header says TOS 3.06, Sversion answers the GEMDOS it always answered,
  and the AES reports 1.4. Each says what the layer under it implements, which
  is what a program asking one of them is asking. Making them agree upwards is
  not a matter of changing the numbers - appl_getinfo, appl_search and
  menu_popup are named and not written, and an application told it was talking
  to an AES 3.4 is entitled to call one, which stops the emulator. What would
  close it is writing them, and then the AES's number goes up because it has
  earned it.
