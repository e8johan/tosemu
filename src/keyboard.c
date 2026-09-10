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

/*
 * What a modified key press becomes.
 *
 * A keypress reaches an application as one word: which key, and what it typed.
 * Putting the two together is not a matter of writing them side by side,
 * because a modifier changes the word rather than being reported beside it -
 * which is what makes this worth a file of its own.
 *
 * Alternate and a letter is the case that matters most. It types nothing, and
 * TOS says so by leaving the character empty and sending the key alone: that is
 * how a menu shortcut is told from somebody typing. An application gets the
 * letter back by looking the key up in the keyboard table - see XBIOS Keytbl -
 * so Alternate-L arrives as the key where L is with nothing typed, and an
 * application matching it against the L on its own menu finds it whatever
 * alphabet the keyboard is arranged in.
 *
 * Shift and a function key is a different key rather than a modified one: F1 to
 * F10 become F11 to F20, which is a second row of ten an ST keyboard has no
 * caps for and every editor of the period used. Control and a cursor key is the
 * same idea - Control-left is a key of its own, and an application waiting for
 * it never sees Control at all.
 *
 * All of it is EmuTOS's, in convert_scancode in bios/ikbd.c, which is the
 * authority: these are the rules the keyboard interrupt applied before anything
 * else saw the key, so an application was written knowing them and cannot be
 * told otherwise. What is not here is what that interrupt did with the keyboard
 * table, because there is no table on this side of the seam: the host says what
 * a key typed, in Unicode, and scrap_text_key knows the ST's spelling of it.
 */

#include "keyboard.h"

#include "scraptext.h"

/* The keys a modifier does not change, and what each of them types. Every ST
 * keyboard table spells these the same way, whatever country it was sold in,
 * which is why they can be answered here rather than looked up. */
#define KEY_ESCAPE     (0x01)
#define KEY_BACKSPACE  (0x0e)
#define KEY_TAB        (0x0f)
#define KEY_RETURN     (0x1c)
#define KEY_UNDO       (0x61)
#define KEY_ENTER      (0x72)   /* the one on the keypad */

/* The keys the rules below move */
#define TOPROW_FIRST   (0x02)   /* the digits, minus and equals */
#define TOPROW_LAST    (0x0d)
#define ALT_TOPROW     (0x76)   /* what Alternate adds to one of those */

#define KEY_F1         (0x3b)
#define KEY_F10        (0x44)
#define SHIFT_FKEY     (0x19)   /* what Shift adds, making F11 to F20 */

#define KEY_HOME       (0x47)
#define KEY_LEFT       (0x4b)
#define KEY_RIGHT      (0x4d)
#define KEY_CTRL_HOME  (0x77)
#define KEY_CTRL_LEFT  (0x73)
#define KEY_CTRL_RIGHT (0x74)

static uint16_t word_of(uint16_t scancode, uint16_t ch)
{
    return (uint16_t)((scancode << 8) | ch);
}

uint16_t keyboard_word(uint16_t scancode, uint32_t codepoint, uint16_t held)
{
    uint16_t ch = 0;

    /*
     * The keys that type the same thing however they are held.
     *
     * They are answered first and without asking the desktop, because the
     * desktop does not always agree: Shift and Tab is a keysym of its own
     * there and types nothing, where on an ST it is a tab with a shift key
     * held - which is what a dialog reads to go back a field rather than on to
     * the next one.
     */
    switch (scancode)
    {
        case KEY_RETURN:
        case KEY_ENTER:
            /* Control turning a Return into a line feed is the one exception,
             * and it is TOS's rather than a convenience */
            return word_of(scancode, (held & KEYBOARD_CTRL) ? 0x0a : 0x0d);

        case KEY_ESCAPE:
            return word_of(scancode, 0x1b);
        case KEY_BACKSPACE:
            return word_of(scancode, 0x08);
        case KEY_TAB:
            return word_of(scancode, 0x09);
        case KEY_UNDO:
            return word_of(scancode, 0);

        default:
            break;
    }

    if (codepoint)
    {
        int typed = scrap_text_key(codepoint);

        if (typed >= 0)
            ch = (uint16_t)typed;
    }

    /*
     * Alternate, or Shift, and never both: an ST reads the two in that order
     * and stops at the first, so Alternate-Shift-F1 is Alternate-F1 with a
     * shift key held rather than Alternate-F11.
     */
    if (held & KEYBOARD_ALT)
    {
        /* Alternate and one of the digits is a key of its own, well past the
         * ones an ST keyboard has, and it types nothing */
        if (scancode >= TOPROW_FIRST && scancode <= TOPROW_LAST)
            return word_of((uint16_t)(scancode + ALT_TOPROW), 0);

        /* And Alternate and a letter is the key alone, which is the whole of
         * how a shortcut is told from a letter somebody meant */
        if ((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z'))
            ch = 0;
    }
    else if (held & KEYBOARD_SHIFT)
    {
        if (scancode >= KEY_F1 && scancode <= KEY_F10)
            return word_of((uint16_t)(scancode + SHIFT_FKEY), 0);
    }

    if (held & KEYBOARD_CTRL)
    {
        /*
         * The three cursor keys Control turns into keys of their own. Their
         * character is left as it is, there being none: what an application
         * waits for is the key.
         */
        switch (scancode)
        {
            case KEY_HOME:
                scancode = KEY_CTRL_HOME;
                break;
            case KEY_LEFT:
                scancode = KEY_CTRL_LEFT;
                break;
            case KEY_RIGHT:
                scancode = KEY_CTRL_RIGHT;
                break;

            /*
             * Everything else is the character with its top three bits taken
             * off, which is what makes Control-A a one and Control-1 an eleven.
             * The desktop has already done that much for the letters, and for
             * the two an ST answers oddly - Control-2 is a nought and Control-6
             * a thirty - because it folds those the same way. Folding again
             * leaves all of them as they are and settles the keys the desktop
             * left alone, so there is one rule here rather than a list of which
             * keys arrived folded already.
             *
             * Minus is the one the fold gets wrong. An ST answers it with 0x1f
             * and the fold makes it a Return, which is not a curiosity: it is a
             * real key beside the digits, and an editor watching for
             * Control-minus would act on Return instead.
             */
            default:
                if (ch == '-')
                    ch = 0x1f;

                ch &= 0x1f;
                break;
        }
    }

    if (scancode == 0 && ch == 0)
        return 0;

    return word_of(scancode, ch);
}
