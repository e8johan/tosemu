; TOSEMU - an emulated environment for TOS applications
; Copyright (C) 2014 Johan Thelin <e8johan@gmail.com>
;
; This program is free software, licensed under the GNU General Public License
; version 2 or later. See the COPYING file in the top directory for details.
;
; Devpac/Gen syntax version of ../cmdline.s

        text

start:  move.l  4(sp),a0        ; basepage
        lea     128(a0),a1      ; p_cmdlin

        moveq   #0,d3
        move.b  (a1)+,d3        ; length byte
        subq.w  #1,d3

loop:   moveq   #0,d1
        move.b  (a1)+,d1
        move.w  d1,-(sp)
        move.w  #2,-(sp)        ; call Cconout
        trap    #1
        addq.l  #4,sp
        dbra    d3,loop

done:   move.w  #0,-(sp)        ; call Pterm0
        trap    #1

        end
