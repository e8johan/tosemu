; TOSEMU - an emulated environment for TOS applications
; Copyright (C) 2014 Johan Thelin <e8johan@gmail.com>
;
; This program is free software, licensed under the GNU General Public License
; version 2 or later. See the COPYING file in the top directory for details.
;
; Devpac/Gen syntax version of ../Fstraversal.s

        text

start:  move.w  #47,-(sp)       ; call Fgetdta
        trap    #1
        addq.l  #2,sp

        lea     olddta,a1       ; store the old DTA in olddta
        move.l  d0,(a1)

        pea     dtabuf
        move.w  #26,-(sp)       ; call Fsetdta
        trap    #1
        addq.l  #6,sp

        move.w  #$ff,-(sp)
        pea     wildcard
        move.w  #78,-(sp)       ; call Fsfirst
        trap    #1
        addq.l  #8,sp

loop:   tst.w   d0
        bne.s   done

        pea     dtabuf+30       ; offset to the filename
        move.w  #9,-(sp)        ; call Cconws
        trap    #1
        addq.l  #6,sp

        move.w  #10,-(sp)       ; line break
        move.w  #2,-(sp)        ; call Cconout
        trap    #1
        addq.l  #4,sp

        move.w  #79,-(sp)       ; call Fsnext
        trap    #1
        addq.l  #2,sp

        bra.s   loop

done:   lea     olddta,a1       ; restore the old DTA
        move.l  (a1),-(sp)
        move.w  #26,-(sp)       ; call Fsetdta
        trap    #1
        addq.l  #6,sp

        move.w  #0,-(sp)        ; call Pterm0
        trap    #1

        data

wildcard:
        dc.b    "*",0
        even

        bss

olddta: ds.l    1
dtabuf: ds.b    44

        end
