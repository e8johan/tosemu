| TOSEMU - an emulated environment for TOS applications
| Copyright (C) 2026 Johan Toverland Thelin <e8johan@gmail.com>
|
| This program is free software; you can redistribute it and/or
| modify it under the terms of the GNU General Public License
| as published by the Free Software Foundation; either version 2
| of the License, or (at your option) any later version.
|
| This program is distributed in the hope that it will be useful,
| but WITHOUT ANY WARRANTY; without even the implied warranty of
| MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
| GNU General Public License for more details.
|
| You should have received a copy of the GNU General Public License
| along with this program; if not, write to the Free Software
| Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.

| How much of the machine a program is given, and how much an accessory is.
|
| A program is given all of it, so a Malloc before any Mshrink finds nothing -
| there is nothing left to find until it hands some back, which is what Mshrink
| is for and what the startup code of every compiler does first thing. An
| accessory is given room for itself and no more, because the AES sized the
| block when it loaded it, and the rest of the machine stays free. So an
| accessory Mallocs a stack for itself without Mshrinking, having nothing to
| give back, and that is the call this makes.
|
| The other half of being an accessory is how it is entered: the AES jumps to
| the text segment with the basepage in a0 and nothing on the stack, where a
| program finds the basepage at 4(sp) and a0 holding nothing. That is what a
| startup reads to tell which it is, so it is checked here too.
|
| The same binary is run both ways and the extension is the only difference
| between the two runs, which is also all the AES had to go on.
|
| Written in assembly rather than C because a C startup calls Mshrink before
| main, and there is no way to be before that from inside main.

XDEF _start

.text
_start:
        move.l  a0, d0          | which kind this is, before anything else can
        bne     accessory       | reach a0

| A program. Every byte of the machine is already its own, so there is none to
| be had, and being given some means the TPA did not cover what it should
        bsr     ask
        tst.l   d0
        bne     bad_program

        move.w  #1, -(sp)
        move.w  #0x4c, -(sp)    | call Pterm
        trap    #1

| An accessory, whose basepage is the one the machine was built around
accessory:
        cmpi.l  #0x800, d0
        bne     bad_basepage

        bsr     ask
        tst.l   d0
        beq     bad_accessory

        move.w  #2, -(sp)
        move.w  #0x4c, -(sp)    | call Pterm
        trap    #1

| A stack of the size an accessory asks for one, which is the call the whole
| of this is about
ask:
        move.l  #8192, -(sp)
        move.w  #0x48, -(sp)    | call Malloc
        trap    #1
        addq.l  #6, sp
        rts

| Every answer says which thing went wrong, and none of them is nought: that
| is what the emulator itself exits with when it stops early, and a test that
| cannot tell that apart passes when nothing ran
bad_basepage:
        move.w  #3, -(sp)
        move.w  #0x4c, -(sp)
        trap    #1

bad_program:
        move.w  #4, -(sp)
        move.w  #0x4c, -(sp)
        trap    #1

bad_accessory:
        move.w  #5, -(sp)
        move.w  #0x4c, -(sp)
        trap    #1
