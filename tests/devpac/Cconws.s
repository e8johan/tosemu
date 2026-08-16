; TOSEMU - an emulated environment for TOS applications
; Copyright (C) 2014 Johan Thelin <e8johan@gmail.com>
;
; This program is free software, licensed under the GNU General Public License
; version 2 or later. See the COPYING file in the top directory for details.
;
; Devpac/Gen syntax version of ../Cconws.s

        text

start:  pea     msg
        move.w  #9,-(sp)        ; call Cconws
        trap    #1
        addq.l  #6,sp

        move.w  #0,-(sp)        ; call Pterm0
        trap    #1

        data

msg:    dc.b    "Hello World!",10,0
        even

        end
