/*
 * TOSEMU - an emulated environment for TOS applications
 * Copyright (C) 2026 Johan Toverland Thelin <e8johan@gmail.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 *
 */

/* See iorec.h for why this is TOS's ring and not merely a ring. */

#include "iorec.h"

/* The next slot round, which is where the byte goes rather than where it is */
static int after(const struct iorec_ring *r, int index)
{
    int next = index + 1;

    /* Compared against the size rather than reduced by it, because that is
     * what _midivec does and because a size that is not a power of two - which
     * an application is entitled to set, the buffer being its own - would come
     * out differently from a mask */
    if (next >= r->size)
        next = 0;

    return next;
}

int iorec_push(struct iorec_ring *r, uint8_t byte)
{
    int next;

    if (!r->buf || r->size <= 0)
        return 0;

    next = after(r, r->tail);

    /* Catching up with the reader is a full ring, and a full ring drops. The
     * slot this would have used is the one that is always left empty. */
    if (next == r->head)
        return 0;

    r->buf[next] = byte;
    r->tail = next;

    return 1;
}

int iorec_take(struct iorec_ring *r, uint8_t *byte)
{
    if (!r->buf || r->size <= 0)
        return 0;

    if (r->head == r->tail)
        return 0;

    r->head = after(r, r->head);
    *byte = r->buf[r->head];

    return 1;
}

int iorec_count(const struct iorec_ring *r)
{
    int waiting;

    if (!r->buf || r->size <= 0)
        return 0;

    waiting = r->tail - r->head;

    if (waiting < 0)
        waiting += r->size;

    return waiting;
}

int iorec_capacity(const struct iorec_ring *r)
{
    if (!r->buf || r->size <= 0)
        return 0;

    return r->size - 1;
}
