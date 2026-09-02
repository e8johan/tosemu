; TOSEMU - an emulated environment for TOS applications
; Copyright (C) 2014 Johan Thelin <e8johan@gmail.com>
;
; This program is free software, licensed under the GNU General Public License
; version 2 or later. See the COPYING file in the top directory for details.
;
; Devpac/Gen syntax version of ../Bconout.s

        text

start:  lea     msg,a1

l0:     clr.l   d0
        move.b  (a1)+,d0
        beq.s   l1

        move.w  d0,-(sp)
        move.w  #2,-(sp)        ; console
        move.w  #3,-(sp)        ; call Bconout
        trap    #$d
        addq.l  #6,sp
        bra.s   l0

l1:     move.w  #0,-(sp)        ; call Pterm0
        trap    #1

        data

msg:    dc.b    "Hello World!",10,0
        even

        end
