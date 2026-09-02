; TOSEMU - an emulated environment for TOS applications
; Copyright (C) 2014 Johan Thelin <e8johan@gmail.com>
;
; This program is free software, licensed under the GNU General Public License
; version 2 or later. See the COPYING file in the top directory for details.
;
; Devpac/Gen syntax version of ../Fread.s

bytes   equ     100

        text

start:  move.w  #0,-(sp)        ; read-only
        pea     fname
        move.w  #61,-(sp)       ; call Fopen
        trap    #1
        addq.l  #8,sp

        tst.w   d0              ; check success
        bmi     fail

        pea     buf
        move.l  #bytes,-(sp)
        move.w  d0,-(sp)
        move.w  #63,-(sp)       ; call Fread
        trap    #1
        lea     12(sp),sp

        cmp.l   #bytes,d0       ; check success
        bne     fail

        clr.b   buf+bytes
        pea     buf
        move.w  #9,-(sp)        ; call Cconws
        trap    #1
        addq.l  #6,sp

        pea     buf
        move.l  #1,-(sp)
        move.w  #0,-(sp)        ; keyboard/stdin
        move.w  #63,-(sp)       ; call Fread
        trap    #1
        lea     12(sp),sp

        cmp.l   #1,d0           ; check success
        bne     fail

        clr.w   d0
        move.b  buf,d0
        move.w  d0,-(sp)
        move.w  #2,-(sp)        ; call Cconout
        trap    #1
        addq.l  #4,sp

        clr.w   -(sp)           ; call Pterm0
        trap    #1

fail:   move.w  #1,-(sp)
        move.w  #$4c,-(sp)      ; call Pterm
        trap    #1

        data

fname:  dc.b    "Makefile",0
        even

        bss

buf:    ds.b    bytes+1

        end
