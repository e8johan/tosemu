/*      GEMOBJOP.C      03/15/84 - 05/27/85     Gregg Morris            */
/*      merge High C vers. w. 2.2               8/21/87         mdf     */
/*      fix get_par                             11/12/87        mdf     */

/*
*       Copyright 1999, Caldera Thin Clients, Inc.
*                 2002-2022 The EmuTOS development team
*
*       This software is licenced under the GNU Public License.
*       Please see LICENSE.TXT for further information.
*
*                  Historical Copyright
*       -------------------------------------------------------------
*       GEM Application Environment Services              Version 2.3
*       Serial No.  XXXX-0000-654321              All Rights Reserved
*       Copyright (C) 1987                      Digital Research Inc.
*       -------------------------------------------------------------
*/

#include "emutos.h"
#include "obdefs.h"
#include "gemobjop.h"


char ob_sst(OBJECT *tree, WORD obj, LONG *pspec, WORD *pstate, WORD *ptype,
            WORD *pflags, GRECT *pt, WORD *pth)
{
    WORD    th;
    OBJECT  *objptr = tree + obj;
    TEDINFO *ted;

    pt->g_w = objptr->ob_width;
    pt->g_h = objptr->ob_height;
    *pflags = objptr->ob_flags;
    *pspec = objptr->ob_spec;
    if (objptr->ob_flags & INDIRECT)
        *pspec = *(LONG *)objptr->ob_spec;

    *pstate = objptr->ob_state;
    *ptype = objptr->ob_type & 0x00ff;
    th = 0;
    switch(*ptype)
    {
    case G_TITLE:
        th = 1;
        break;
    case G_TEXT:
    case G_BOXTEXT:
    case G_FTEXT:
    case G_FBOXTEXT:
        ted = (TEDINFO *)*pspec;
        th = ted->te_thickness;
        break;
    case G_BOX:
    case G_BOXCHAR:
    case G_IBOX:
        /*
         * TOSEMU: one of the two things changed in this file - see the return
         * at the end of this function for the other, which is the same
         * mistake made about a different byte.
         *
         * The border thickness is the second byte of the spec, and EmuTOS
         * takes it by pointing a char at the LONG and stepping one along. That
         * finds it on a 68000, where a LONG is four bytes and the high one
         * comes first. Here it is eight bytes and the low one comes first, so
         * the same step lands in the middle of the colour word and a two pixel
         * border is drawn seventeen pixels thick.
         *
         * Shifting says which byte is wanted rather than where it sits, and is
         * right on either machine.
         *
         * Everything else in this file is EmuTOS's, unchanged, from
         * VERSION_1_4.
         */
        th = (*pspec >> 16) & 0xff;
        break;
    case G_BUTTON:
        th--;
        if (objptr->ob_flags & EXIT)
            th--;
        if (objptr->ob_flags & DEFAULT)
            th--;
        break;
    }

    if (th > 128)
        th -= 256;
    *pth = th;

    /*
     * TOSEMU: the character a G_BOXCHAR shows, which is the top byte of the
     * spec and is what EmuTOS reaches for by pointing a char at the LONG.
     *
     * The same step off the same wrong end as the thickness above, and the
     * more visible of the two: on a machine whose LONG is eight bytes and
     * whose low one comes first, this reads the last byte of the spec instead
     * of the first. That byte is nought in everything the AES draws itself -
     * the drive letters, the close box and the arrows in the file selector are
     * 0x41ff1100 and its like - so every one of them drew nothing at all, and
     * the four arrows of a window's scroll bars, whose specs end in 0x01, all
     * drew the same arrow.
     *
     * Shifting asks for the byte rather than for the end it happens to be at.
     */
    return (char)((*pspec >> 24) & 0xff);  /* only useful for G_BOXCHAR */
}


void everyobj(OBJECT *tree, WORD this, WORD last, EVERYOBJ_CALLBACK routine,
              WORD startx, WORD starty, WORD maxdep)
{
    WORD    tmp1;
    WORD    depth;
    WORD    x[MAX_DEPTH+2], y[MAX_DEPTH+2];
    OBJECT  *obj;

    x[0] = startx;
    y[0] = starty;
    depth = 1;

    /*
     * non-recursive tree traversal
     */
child:
    /* see if we need to stop */
    if (this == last)
        return;

    /* do this object */
    obj = tree + this;
    x[depth] = x[depth-1] + obj->ob_x;
    y[depth] = y[depth-1] + obj->ob_y;
    (*routine)(tree, this, x[depth], y[depth]);

    /* if this guy has kids then do them */
    tmp1 = obj->ob_head;
    if (tmp1 != NIL)
    {
        if (!(obj->ob_flags & HIDETREE) && (depth <= maxdep))
        {
            depth++;
            this = tmp1;
            goto child;
        }
    }

sibling:
    /*
     * if this is the root (which has no parent),
     *  or it is the last then stop else...
     */
    obj = tree + this;
    tmp1 = obj->ob_next;
    if ((tmp1 == last) || (this == ROOT))
        return;
    /*
     * if this object has a sibling that is not his parent,
     * then move to him and do him and his kids
     */
    obj = tree + tmp1;
    if (obj->ob_tail != this)
    {
        this = tmp1;
        goto child;
    }
    /* else move up to the parent and finish off his siblings */
    depth--;
    this = tmp1;
    goto sibling;
}


/*
 * Routine that will find the parent of a given object.  The
 * idea is to walk to the end of our siblings and return
 * our parent.  If object is the root then return NIL as parent.
 */
WORD get_par(OBJECT *tree, WORD obj)
{
    WORD next;

    if (obj == ROOT)
        return NIL;

    while(1)
    {
        next = tree[obj].ob_next;
        /* if the next object's tail points to us, it must be our parent */
        if (tree[next].ob_tail == obj)
            break;
        obj = next;
    }

    return next;
}
