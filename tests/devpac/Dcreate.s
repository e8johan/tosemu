; TOSEMU - an emulated environment for TOS applications
; Copyright (C) 2014 Johan Thelin <e8johan@gmail.com>
;
; This program is free software, licensed under the GNU General Public License
; version 2 or later. See the COPYING file in the top directory for details.
;
; Devpac/Gen syntax version of ../Dcreate.s

        text

start:  pea     name
        move.w  #57,-(sp)       ; call Dcreate
        trap    #1
        addq.l  #6,sp

        tst.w   d0              ; check success
        bmi.s   fail

        pea     name
        move.w  #57,-(sp)       ; call Dcreate
        trap    #1
        addq.l  #6,sp

        tst.w   d0              ; check failure
        beq.s   fail

        clr.w   -(sp)           ; call Pterm0
        trap    #1

fail:   move.w  #1,-(sp)
        move.w  #$4c,-(sp)      ; call Pterm
        trap    #1

        data

name:   dc.b    "DNAME",0
        even

        end
