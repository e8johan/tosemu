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
 * Built for the host rather than for the emulated machine, because a modifier
 * is not something a test can hold down. TOSEMU_KEYS hands over characters and
 * has nowhere to say that Alternate was down while one arrived, and a
 * compositor - which is the only other thing that reports a keypress - is not
 * something a test can arrange either. So the rules are a function of their
 * own and the words are handed to it here.
 *
 * What is checked is that the word an application is given is the word TOS
 * would have given it, which is the whole of why menu shortcuts work: an
 * application looks for the key rather than for the letter, and a word with a
 * letter in it is somebody typing.
 */

#include "keyboard.h"

#include <stdio.h>

static int n;
static int fails;

static void check(long got, long want, const char *name)
{
    n++;
    if (got == want)
        printf("ok %d - %s\n", n, name);
    else
    {
        fails++;
        printf("not ok %d - %s (got 0x%04lx, want 0x%04lx)\n",
               n, name, got, want);
    }
}

/* The scan codes this asks about, which are places on an ST keyboard */
#define KEY_1       (0x02)
#define KEY_L       (0x26)
#define KEY_MINUS   (0x0c)
#define KEY_A       (0x1e)
#define KEY_F1      (0x3b)
#define KEY_F4      (0x3e)
#define KEY_F10     (0x44)
#define KEY_LEFT    (0x4b)
#define KEY_RIGHT   (0x4d)
#define KEY_HOME    (0x47)
#define KEY_UP      (0x48)
#define KEY_TAB     (0x0f)
#define KEY_RETURN  (0x1c)

int main(int argc, char **argv)
{
    (void)argc; (void)argv;

    /* Nothing held: the key and what it typed, side by side */
    check(keyboard_word(KEY_L, 'l', 0), 0x266c,
          "a letter is the key and the letter");
    check(keyboard_word(KEY_L, 'L', KEYBOARD_LSHIFT), 0x264c,
          "and shifted it is the same key and a capital");

    /*
     * Alternate and a letter, which is what a menu shortcut is. The character
     * has to go: an application looks the key up in the keyboard table to get
     * the letter back, and one that arrives with a letter in it is a letter
     * somebody typed.
     */
    check(keyboard_word(KEY_L, 'l', KEYBOARD_ALT), 0x2600,
          "Alternate and a letter is the key alone");
    check(keyboard_word(KEY_L, 'L', KEYBOARD_ALT|KEYBOARD_LSHIFT), 0x2600,
          "and so is Alternate, Shift and a letter");

    /* Alternate and one of the digits is a key of its own, well past the ones
     * an ST keyboard has, and it types nothing either */
    check(keyboard_word(KEY_1, '1', KEYBOARD_ALT), 0x7800,
          "Alternate and a digit is a key of its own");

    /*
     * Shift and a function key is the second row of ten an ST keyboard has no
     * caps for. Both ends of the row, because an off-by-one here is an
     * application acting on the wrong command rather than on none.
     */
    check(keyboard_word(KEY_F1, 0, KEYBOARD_LSHIFT), 0x5400,
          "Shift and F1 is F11");
    check(keyboard_word(KEY_F4, 0, KEYBOARD_RSHIFT), 0x5700,
          "Shift and F4 is F14");
    check(keyboard_word(KEY_F10, 0, KEYBOARD_LSHIFT), 0x5d00,
          "Shift and F10 is F20");
    check(keyboard_word(KEY_F1, 0, 0), 0x3b00,
          "and F1 on its own is still F1");

    /* Alternate wins over Shift, the ST reading the two in that order */
    check(keyboard_word(KEY_F1, 0, KEYBOARD_ALT|KEYBOARD_LSHIFT), 0x3b00,
          "Alternate and Shift and a function key is not the second row");

    /* Control and the three cursor keys that become keys of their own */
    check(keyboard_word(KEY_LEFT, 0, KEYBOARD_CTRL), 0x7300,
          "Control and left is a key of its own");
    check(keyboard_word(KEY_RIGHT, 0, KEYBOARD_CTRL), 0x7400,
          "and so is Control and right");
    check(keyboard_word(KEY_HOME, 0, KEYBOARD_CTRL), 0x7700,
          "and Control and home");
    check(keyboard_word(KEY_UP, 0, KEYBOARD_CTRL), 0x4800,
          "while the cursor keys that do not are left where they are");

    /*
     * Control and a character, which is the character with its top three bits
     * taken off. The letters arrive folded already, because the desktop folds
     * them the same way; the digits do not.
     */
    check(keyboard_word(KEY_A, 0x01, KEYBOARD_CTRL), 0x1e01,
          "Control and a letter is a control character");
    check(keyboard_word(KEY_1, '1', KEYBOARD_CTRL), 0x0211,
          "Control and a digit is folded the same way");
    check(keyboard_word(KEY_MINUS, '-', KEYBOARD_CTRL), 0x0c1f,
          "Control and minus is the one the fold gets wrong");

    /*
     * A character the ST has no byte for types nothing and the key still goes,
     * so a shortcut on such a key keeps working. U+20AC is the Euro sign,
     * which an ST predates.
     */
    check(keyboard_word(KEY_L, 0x20ac, 0), 0x2600,
          "a character the ST has no byte for is the key alone");

    /* An accented letter is the ST's byte for it rather than the desktop's */
    check(keyboard_word(KEY_L, 0xe5, 0), 0x2686,
          "and one it does have is spelled the ST's way");

    /*
     * The keys that type the same thing however they are held. Shift and Tab
     * is the one the desktop disagrees about: it has a keysym of its own there
     * and types nothing, where on an ST it is a tab with a shift key held -
     * which is what a dialog reads to go back a field.
     */
    check(keyboard_word(KEY_TAB, 0, KEYBOARD_LSHIFT), 0x0f09,
          "Shift and Tab is still a tab");
    check(keyboard_word(KEY_RETURN, 0x0d, 0), 0x1c0d,
          "Return is a Return");
    check(keyboard_word(KEY_RETURN, 0x0d, KEYBOARD_CTRL), 0x1c0a,
          "and Control and Return is a line feed");

    /* Neither a key nor a character is not a press anything can be told
     * about */
    check(keyboard_word(0, 0, 0), 0,
          "a press with no key and no character is nothing at all");

    printf("1..%d\n", n);

    return fails ? 1 : 0;
}
