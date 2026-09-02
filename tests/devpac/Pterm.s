; TOSEMU - an emulated environment for TOS applications
; Copyright (C) 2014 Johan Thelin <e8johan@gmail.com>
;
; This program is free software, licensed under the GNU General Public License
; version 2 or later. See the COPYING file in the top directory for details.
;
; Devpac/Gen syntax version of ../Pterm.s

        text

start:  move.w  #42,-(sp)
        move.w  #$4c,-(sp)      ; call Pterm
        trap    #1

        end
