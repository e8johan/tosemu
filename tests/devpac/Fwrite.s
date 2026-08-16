; TOSEMU - an emulated environment for TOS applications
; Copyright (C) 2014 Johan Thelin <e8johan@gmail.com>
;
; This program is free software, licensed under the GNU General Public License
; version 2 or later. See the COPYING file in the top directory for details.
;
; Devpac/Gen syntax version of ../Fwrite.s

        text

start:  clr.w   -(sp)
        pea     name
        move.w  #60,-(sp)       ; call Fcreate
        trap    #1
        addq.l  #8,sp

        tst.w   d0              ; check success
        bmi.s   fail

        pea     string
        move.l  #11,-(sp)
        move.w  d0,-(sp)
        move.w  #64,-(sp)       ; call Fwrite
        trap    #1
        lea     12(sp),sp

        cmp.l   #11,d0          ; check success
        bne.s   fail

        clr.w   -(sp)           ; call Pterm0
        trap    #1

fail:   move.w  #1,-(sp)
        move.w  #$4c,-(sp)      ; call Pterm
        trap    #1

        data

name:   dc.b    "FNAME",0
string: dc.b    "hello world"
        even

        end
