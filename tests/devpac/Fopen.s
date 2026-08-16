; TOSEMU - an emulated environment for TOS applications
; Copyright (C) 2014 Johan Thelin <e8johan@gmail.com>
;
; This program is free software, licensed under the GNU General Public License
; version 2 or later. See the COPYING file in the top directory for details.
;
; Devpac/Gen syntax version of ../Fopen.s

        text

start:  move.w  #0,-(sp)        ; read-only
        pea     fname1
        move.w  #61,-(sp)       ; call Fopen
        trap    #1
        addq.l  #8,sp

        tst.w   d0              ; check success
        bmi.s   fail

        move.w  #0,-(sp)        ; read-only
        pea     fname2
        move.w  #61,-(sp)       ; call Fopen
        trap    #1
        addq.l  #8,sp

        tst.w   d0              ; check failure
        bpl.s   fail

        clr.w   -(sp)           ; call Pterm0
        trap    #1

fail:   move.w  #1,-(sp)
        move.w  #$4c,-(sp)      ; call Pterm
        trap    #1

        data

fname1: dc.b    "Makefile",0
fname2: dc.b    "not found",0
        even

        end
