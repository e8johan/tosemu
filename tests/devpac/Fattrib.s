; TOSEMU - an emulated environment for TOS applications
; Copyright (C) 2014 Johan Thelin <e8johan@gmail.com>
;
; This program is free software, licensed under the GNU General Public License
; version 2 or later. See the COPYING file in the top directory for details.
;
; Devpac/Gen syntax version of ../Fattrib.s

        text

start:  clr.l   -(sp)
        pea     fname1
        move.w  #67,-(sp)       ; call Fattrib
        trap    #1
        lea     10(sp),sp

        tst.w   d0              ; check success
        bmi.s   fail
        and.w   #$10,d0         ; not a directory
        tst.w   d0
        bne.s   fail

        clr.l   -(sp)
        pea     fname2
        move.w  #67,-(sp)       ; call Fattrib
        trap    #1
        lea     10(sp),sp

        tst.w   d0              ; check success
        bmi.s   fail
        and.w   #$10,d0         ; a directory
        tst.w   d0
        beq.s   fail

        clr.w   -(sp)           ; call Pterm0
        trap    #1

fail:   move.w  #1,-(sp)
        move.w  #$4c,-(sp)      ; call Pterm
        trap    #1

        data

fname1: dc.b    "Makefile",0
fname2: dc.b    ".",0
        even

        end
