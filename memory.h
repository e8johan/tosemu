/*
 * TOSEMU - and emulated environment for TOS applications
 * Copyright (C) 2014 Johan Thelin <e8johan@gmail.com>
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

#ifndef MEMORY_H
#define MEMORY_H

#include <stdint.h>

/* Memory area access flags */
#define MEMORY_READ       (0x01)
#define MEMORY_WRITE      (0x02)
#define MEMORY_READWRITE  (0x03)
#define MEMORY_SUPERREAD  (0x04)
#define MEMORY_SUPERWRITE (0x08)

/* Memory area item */
struct _memarea;
struct _memarea {
    uint32_t base;
    uint32_t len;

    uint8_t (*read)(struct _memarea *, uint32_t);
    void (*write)(struct _memarea *, uint32_t, uint8_t);
    
    void *ptr;
    uint8_t flags;
    
    struct _memarea *next;
};

/* Add memory areas, return 0 on success */
int add_ptr_memory_area(char *name, uint8_t flags, uint32_t base, uint32_t len, void *ptr);
int add_fkt_memory_area(char *name, uint8_t flags, uint32_t base, uint32_t len, void *ptr, uint8_t (*read)(struct _memarea *, uint32_t), void (*write)(struct _memarea *, uint32_t, uint8_t));

/* Remove memory areas, return 0 on success */
int remove_memory_area(uint32_t base);

/* Remove all memory areas */
void reset_memory();

#endif /* MEMORY_H */
