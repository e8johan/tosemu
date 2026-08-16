; TOSEMU - an emulated environment for TOS applications
; Copyright (C) 2014 Johan Thelin <e8johan@gmail.com>
;
; This program is free software, licensed under the GNU General Public License
; version 2 or later. See the COPYING file in the top directory for details.
;
; Devpac/Gen syntax version of ../Supexec.s

        text

start:  move.l  sp,a3

        pea     func
        move.w  #38,-(sp)       ; call Supexec
        trap    #14
        addq.l  #6,sp

        cmp.l   #42,d0          ; check return value
        bne.s   fail

        cmp.l   a3,sp           ; check a3 not clobbered
        bne.s   fail

        clr.w   -(sp)           ; call Pterm0
        trap    #1

fail:   move.w  #1,-(sp)
        move.w  #$4c,-(sp)      ; call Pterm
        trap    #1

func:   move.l  $8.w,a3         ; read from protected RAM
        moveq   #42,d0          ; return a value
        rts

        end
