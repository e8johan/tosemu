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
 * Objects.
 *
 * Every dialog, every menu and every button in GEM is an object tree, and
 * drawing one is what the AES is for as much as windows are. The drawing
 * itself is EmuTOS's, which is the reason its object library is carried at
 * all; what is here is getting the tree from the machine to where that library
 * can read it, and the answers back again.
 */

#include "aes_p.h"

#include "gem_p.h"
#include "emuvdi/emuvdi.h"
#include "tossystem.h"
#include "m68k.h"

/*
 * form_do - run a dialog until something ends it
 *
 * The tree goes across, the AES loops over the waiting an application would do
 * for itself, and what comes back is which object ended it. The tree comes
 * back too: the button that was pressed is SELECTED in it, and an edited field
 * holds what was typed.
 */
uint32_t AES_form_do()
{
    uint32_t address = aes_addrin(0);
    int16_t start = aes_intin(0);
    void *host;
    int16_t ended;

    FUNC_TRACE_ENTER_ARGS {
        printf("    tree: 0x%x, editing from %d\n", address, start);
    }

    if (!gem_start())
        return AES_ERROR;

    host = aes_tree_in(address);
    if (!host)
        return AES_ERROR;

    ended = emuvdi_form_do(host, start);

    aes_tree_out();
    aes_tree_done();

    return (uint32_t)ended;
}

uint32_t AES_objc_draw()
{
    uint32_t address = aes_addrin(0);
    int16_t start = aes_intin(0);
    int16_t depth = aes_intin(1);
    int16_t x = aes_intin(2);
    int16_t y = aes_intin(3);
    int16_t w = aes_intin(4);
    int16_t h = aes_intin(5);
    void *host;

    FUNC_TRACE_ENTER_ARGS {
        printf("    tree: 0x%x, from %d, %d deep, clip %d,%d %dx%d\n",
               address, start, depth, x, y, w, h);
    }

    if (!gem_start())
        return AES_ERROR;

    host = aes_tree_in(address);
    if (!host)
        return AES_ERROR;

    emuvdi_objc_draw(host, start, depth, x, y, w, h);

    /* Drawing does not change a tree, but it costs nothing to put back what
     * did not move, and it keeps every one of these the same shape */
    aes_tree_out();
    aes_tree_done();

    return AES_E_OK;
}

/*
 * What the alert in emuvdi/gemfmalt.c reaches when it puts its window up and
 * takes it away. The same two things form_dial does, so that an alert and a
 * dialog behave alike.
 */
void host_dialog_begin(int16_t x, int16_t y, int16_t width, int16_t height)
{
    gem_dialog_begin(x, y, width, height);
}

void host_dialog_end(void)
{
    gem_dialog_end();
}

/* form_alert **************************************************************/

/*
 * form_alert - the box with an icon, some text and up to three buttons
 *
 * The whole alert is one string, packed the way an application writes it:
 * "[1][Something went wrong][OK|Cancel]". The AES takes it apart, builds a
 * tree out of it, centres it and runs it, and answers with which button was
 * pressed counting from one.
 *
 * The string is in the machine, so it comes across first. Unlike form_dial
 * this does not reserve the screen: an alert saves what is under it and puts
 * it back afterwards, which is a raster copy in each direction.
 */
uint32_t AES_form_alert()
{
    int16_t defbut = aes_intin(0);
    uint32_t address = aes_addrin(0);
    char text[512];
    int i;

    FUNC_TRACE_ENTER_ARGS {
        printf("    default button %d, text at 0x%x\n", defbut, address);
    }

    if (!gem_start())
        return AES_ERROR;

    for (i = 0; i < (int)sizeof text - 1; i++)
    {
        text[i] = (char)m68k_read_memory_8(address + i);
        if (text[i] == 0)
            break;
    }
    text[i] = 0;

    return (uint32_t)emuvdi_form_alert(defbut, text);
}

/* form_dial ***************************************************************/

/*
 * form_dial - reserve or release the screen a dialog sits on
 *
 * On a real machine this is how an application asks the AES to remember what
 * is under a dialog so that it can be put back afterwards. FMD_START says a
 * rectangle is about to be covered, FMD_FINISH says it is free again, and the
 * two in between draw the box that grows and shrinks as a dialog appears.
 *
 * Here the rectangle becomes a window of the compositor's, which is what makes
 * a GEM dialog behave like a dialog: kept above the window it belongs to, the
 * parent out of reach while it is up, and movable with whatever the desktop
 * uses for moving windows even though the application inside it is blocked.
 *
 * Nothing needs remembering, because nothing is covered: the screen behind is
 * still there, in the other window, exactly as it was.
 *
 * http://toshyp.atari.org/en/007005.html
 */
#define FMD_START  (0)
#define FMD_GROW   (1)
#define FMD_SHRINK (2)
#define FMD_FINISH (3)

uint32_t AES_form_dial()
{
    int16_t what = aes_intin(0);
    int16_t x = aes_intin(5);
    int16_t y = aes_intin(6);
    int16_t width = aes_intin(7);
    int16_t height = aes_intin(8);

    FUNC_TRACE_ENTER_ARGS {
        printf("    %d, %d,%d %dx%d\n", what, x, y, width, height);
    }

    if (!gem_start())
        return AES_ERROR;

    switch (what)
    {
        case FMD_START:
            gem_dialog_begin(x, y, width, height);
            break;

        case FMD_FINISH:
            gem_dialog_end();
            break;

        case FMD_GROW:
        case FMD_SHRINK:
            /*
             * The box that grows out of nothing and shrinks back into it. It
             * was there to show where a dialog came from on a screen that
             * could not move windows; a compositor has its own way of showing
             * that, and drawing this one over the top would fight it.
             */
            break;

        default:
            halt_execution();
            printf("AES form_dial was asked for %d, which is not one of the "
                   "four it has\n", what);
            return AES_ERROR;
    }

    return AES_E_OK;
}


/* form_center *************************************************************/

/* An OBJECT in the machine, and the fields this needs out of one */
#define OB_SIZE     (24)
#define OB_TYPE      (6)
#define OB_STATE    (10)
#define OB_SPEC     (12)
#define OB_X        (16)
#define OB_Y        (18)
#define OB_WIDTH    (20)
#define OB_HEIGHT   (22)

/* The two states that make a tree take up more room than it says, obdefs.h */
#define OUTLINED (0x0010)
#define SHADOWED (0x0020)

/* The box types, whose ob_spec holds a border thickness rather than a
 * pointer */
#define G_BOX       (20)
#define G_IBOX      (25)
#define G_BOXCHAR   (27)

/*
 * Putting a dialog in the middle of the screen.
 *
 * This is done in the machine's own memory rather than by bringing the tree
 * across, because it changes two fields of one object and reads three more.
 * Copying a whole tree to move it would be more work than the work, and the
 * marshaller deliberately does not write geometry back - what it puts back is
 * what the AES is expected to have changed, and this call is the exception
 * that would have to become a rule.
 *
 * The rectangle answered is not the tree's. It is what has to be reserved on
 * screen, which is larger when the dialog is drawn with an outline or a shadow
 * round it - and reserving too little is how a dialog leaves a piece of itself
 * behind when it goes.
 */
uint32_t AES_form_center()
{
    uint32_t tree = aes_addrin(0);
    int16_t handle, wchar, hchar, wbox, hbox;
    int16_t w, h, x, y, state, type;

    FUNC_TRACE_ENTER_ARGS {
        printf("    tree: 0x%x\n", tree);
    }

    if (!gem_start() || !tree)
        return AES_ERROR;

    emuvdi_graf_handle(&handle, &wchar, &hchar, &wbox, &hbox);

    w = (int16_t)m68k_read_memory_16(tree + OB_WIDTH);
    h = (int16_t)m68k_read_memory_16(tree + OB_HEIGHT);
    state = (int16_t)m68k_read_memory_16(tree + OB_STATE);
    type = (int16_t)m68k_read_memory_16(tree + OB_TYPE) & 0xff;

    /* Across the screen, and down what is left of it under the menu bar - a
     * dialog centred on the whole screen sits too high */
    x = (int16_t)((emuvdi_screen_width() - w) / 2);
    y = (int16_t)(hbox + ((emuvdi_screen_height() - hbox - h) / 2));

    m68k_write_memory_16(tree + OB_X, (uint16_t)x);
    m68k_write_memory_16(tree + OB_Y, (uint16_t)y);

    if (state & OUTLINED)
    {
        /* Three pixels of outline on every side, and never off the screen */
        x -= 3;
        if (x < 0)
            x = 0;
        y -= 3;
        if (y < 0)
            y = 0;
        w += 6;
        h += 6;
    }

    if (state & SHADOWED)
    {
        int16_t thick = 0;

        if (type == G_BOX || type == G_IBOX || type == G_BOXCHAR)
        {
            /* The second byte of the spec, which is signed: a negative
             * thickness is a border drawn inside the edge rather than out */
            thick = (int16_t)((m68k_read_memory_32(tree + OB_SPEC) >> 16)
                              & 0xff);
            if (thick > 128)
                thick -= 256;
            if (thick < 0)
                thick = -thick;
        }

        w += thick + thick;
        h += thick + thick;
    }

    aes_set_intout(1, x);
    aes_set_intout(2, y);
    aes_set_intout(3, w);
    aes_set_intout(4, h);

    FUNC_TRACE_ARGS {
        printf("    centred at %d,%d %dx%d\n", x, y, w, h);
    }

    return AES_E_OK;
}


/* The rest of the object calls ********************************************/

/*
 * These change a tree rather than draw it, and every one of them is EmuTOS's
 * doing the changing. What is here is the marshalling either side: the tree
 * comes across, the library walks it, and what it changed goes back.
 *
 * The ones that add, delete and reorder change more than a state, so the
 * marshaller's usual rule - put back what the AES is expected to have changed,
 * which is a state and an edited string - is not enough for them. They are
 * done in the machine's own memory instead, where changing a tree is changing
 * three words and there is nothing to put back.
 */

/* An OBJECT in the machine, and the fields these need */
#define OB_NEXT      (0)
#define OB_HEAD      (2)
#define OB_TAIL      (4)

/* The end of a list of children, and of the tree */
#define NIL         (-1)

static int16_t word_at(uint32_t tree, int16_t obj, int field)
{
    return (int16_t)m68k_read_memory_16(tree + obj*OB_SIZE + field);
}

static void set_word(uint32_t tree, int16_t obj, int field, int16_t value)
{
    m68k_write_memory_16(tree + obj*OB_SIZE + field, (uint16_t)value);
}

/*
 * Whose child an object is.
 *
 * A tree records children but not parents, so finding one means walking. The
 * list of children ends by pointing back at the parent rather than at nothing,
 * so following next from any child arrives there - it is the first object
 * along that claims this one.
 */
static int16_t parent_of(uint32_t tree, int16_t obj)
{
    int16_t walk = obj;
    int steps;

    for (steps = 0; steps < 512; steps++)
    {
        int16_t next = word_at(tree, walk, OB_NEXT);

        if (next == NIL)
            return NIL;             /* the root, which has no parent */

        if (word_at(tree, next, OB_HEAD) != NIL)
        {
            /* Somebody whose children this could be. It is the parent if the
             * walk arrived here from inside its list rather than past it. */
            int16_t child;

            for (child = word_at(tree, next, OB_HEAD);
                 child != NIL && child != next;
                 child = word_at(tree, child, OB_NEXT))
                if (child == obj)
                    return next;
        }

        walk = next;
    }

    return NIL;
}

/* Takes an object out of its parent's list, and says whose it was */
static int16_t unlink_from_parent(uint32_t tree, int16_t obj)
{
    int16_t parent = parent_of(tree, obj);
    int16_t before, last, walk;

    if (parent == NIL)
        return NIL;

    if (word_at(tree, parent, OB_HEAD) == obj)
        set_word(tree, parent, OB_HEAD, word_at(tree, obj, OB_NEXT));
    else
    {
        for (before = word_at(tree, parent, OB_HEAD);
             before != NIL && word_at(tree, before, OB_NEXT) != obj;
             before = word_at(tree, before, OB_NEXT))
            ;

        if (before == NIL)
            return NIL;

        set_word(tree, before, OB_NEXT, word_at(tree, obj, OB_NEXT));
    }

    /* The last child again, which may have been the one taken out */
    last = NIL;
    for (walk = word_at(tree, parent, OB_HEAD);
         walk != NIL && walk != parent;
         walk = word_at(tree, walk, OB_NEXT))
        last = walk;

    set_word(tree, parent, OB_TAIL, last);

    if (last == NIL)
        set_word(tree, parent, OB_HEAD, NIL);

    set_word(tree, obj, OB_NEXT, NIL);

    return parent;
}

/*
 * objc_add - make one object the last child of another
 *
 * A child list runs from the parent's head through each child's next and ends
 * by pointing back at the parent, which is what makes walking one terminate
 * without a count.
 */
uint32_t AES_objc_add()
{
    uint32_t tree = aes_addrin(0);
    int16_t parent = aes_intin(0);
    int16_t child = aes_intin(1);

    FUNC_TRACE_ENTER_ARGS {
        printf("    %d becomes a child of %d\n", child, parent);
    }

    if (!tree || parent < 0 || child < 0)
        return AES_ERROR;

    set_word(tree, child, OB_NEXT, parent);
    set_word(tree, child, OB_HEAD, NIL);
    set_word(tree, child, OB_TAIL, NIL);

    if (word_at(tree, parent, OB_HEAD) == NIL)
        set_word(tree, parent, OB_HEAD, child);
    else
        set_word(tree, word_at(tree, parent, OB_TAIL), OB_NEXT, child);

    set_word(tree, parent, OB_TAIL, child);

    return AES_E_OK;
}

/* objc_delete - take an object out of its parent's list of children */
uint32_t AES_objc_delete()
{
    uint32_t tree = aes_addrin(0);
    int16_t obj = aes_intin(0);

    FUNC_TRACE_ENTER_ARGS {
        printf("    object %d\n", obj);
    }

    if (!tree || obj <= 0)
        return AES_ERROR;

    return (unlink_from_parent(tree, obj) == NIL) ? AES_ERROR : AES_E_OK;
}

/*
 * objc_offset - where an object is on the screen
 *
 * An object's own coordinates are relative to its parent, so finding where it
 * actually is means adding up every parent above it. Applications use this
 * constantly, to draw into a box or to work out what a click landed on.
 */
uint32_t AES_objc_offset()
{
    uint32_t address = aes_addrin(0);
    int16_t obj = aes_intin(0);
    int16_t x = 0, y = 0;
    void *host;

    FUNC_TRACE_ENTER_ARGS {
        printf("    object %d\n", obj);
    }

    host = aes_tree_in(address);
    if (!host)
        return AES_ERROR;

    emuvdi_objc_offset(host, obj, &x, &y);

    aes_tree_done();

    aes_set_intout(1, x);
    aes_set_intout(2, y);

    FUNC_TRACE_ARGS {
        printf("    at %d,%d\n", x, y);
    }

    return AES_E_OK;
}

/* objc_find - which object a point is in, looking no deeper than asked */
uint32_t AES_objc_find()
{
    uint32_t address = aes_addrin(0);
    int16_t start = aes_intin(0);
    int16_t depth = aes_intin(1);
    int16_t x = aes_intin(2);
    int16_t y = aes_intin(3);
    void *host;
    int16_t found;

    FUNC_TRACE_ENTER_ARGS {
        printf("    from %d, %d deep, at %d,%d\n", start, depth, x, y);
    }

    host = aes_tree_in(address);
    if (!host)
        return AES_ERROR;

    found = emuvdi_objc_find(host, start, depth, x, y);

    aes_tree_done();

    FUNC_TRACE_ARGS {
        printf("    object %d\n", found);
    }

    return (uint32_t)(uint16_t)found;
}

/*
 * objc_order - move an object up or down among its brothers and sisters
 *
 * Which matters because the order is the drawing order: the last child is
 * drawn last and so appears on top. Nought puts it first and -1 last, which is
 * what an application uses to bring something to the front.
 *
 * Done here rather than by the library for the same reason as adding and
 * deleting: it changes which object points at which, and putting that back
 * through the marshaller would mean writing back the shape of a tree, which is
 * the one thing the copy is not allowed to decide.
 */
uint32_t AES_objc_order()
{
    uint32_t tree = aes_addrin(0);
    int16_t obj = aes_intin(0);
    int16_t position = aes_intin(1);
    int16_t parent, before, i;

    FUNC_TRACE_ENTER_ARGS {
        printf("    object %d to position %d\n", obj, position);
    }

    if (!tree || obj <= 0)
        return AES_ERROR;

    parent = unlink_from_parent(tree, obj);
    if (parent == NIL)
        return AES_ERROR;

    /* Last, which is what -1 means and what an empty list gives anyway */
    if (position < 0 || word_at(tree, parent, OB_HEAD) == NIL)
    {
        set_word(tree, obj, OB_NEXT, parent);

        if (word_at(tree, parent, OB_HEAD) == NIL)
            set_word(tree, parent, OB_HEAD, obj);
        else
            set_word(tree, word_at(tree, parent, OB_TAIL), OB_NEXT, obj);

        set_word(tree, parent, OB_TAIL, obj);

        return AES_E_OK;
    }

    if (position == 0)
    {
        set_word(tree, obj, OB_NEXT, word_at(tree, parent, OB_HEAD));
        set_word(tree, parent, OB_HEAD, obj);

        return AES_E_OK;
    }

    /* After that many of them, or at the end if there are fewer */
    before = word_at(tree, parent, OB_HEAD);
    for (i = 1; i < position; i++)
    {
        int16_t next = word_at(tree, before, OB_NEXT);

        if (next == NIL || next == parent)
            break;

        before = next;
    }

    set_word(tree, obj, OB_NEXT, word_at(tree, before, OB_NEXT));
    set_word(tree, before, OB_NEXT, obj);

    if (word_at(tree, parent, OB_TAIL) == before)
        set_word(tree, parent, OB_TAIL, obj);

    return AES_E_OK;
}

/* objc_change - set an object's state, and draw it that way if asked */
uint32_t AES_objc_change()
{
    uint32_t address = aes_addrin(0);
    int16_t obj = aes_intin(0);
    int16_t x = aes_intin(2);
    int16_t y = aes_intin(3);
    int16_t w = aes_intin(4);
    int16_t h = aes_intin(5);
    int16_t state = aes_intin(6);
    int16_t draw = aes_intin(7);
    void *host;

    FUNC_TRACE_ENTER_ARGS {
        printf("    object %d to state 0x%x, %s\n", obj, state,
               draw ? "drawn" : "not drawn");
    }

    if (!gem_start())
        return AES_ERROR;

    host = aes_tree_in(address);
    if (!host)
        return AES_ERROR;

    /* The clipping the caller asked for, which ob_change draws inside */
    emuvdi_set_clip(x, y, w, h);

    emuvdi_objc_change(host, obj, (uint16_t)state, draw);

    aes_tree_out();
    aes_tree_done();

    if (draw)
        gem_present();

    return AES_E_OK;
}


/* The rest of the form calls **********************************************/

/*
 * form_do is the whole of running a dialog and these are the pieces of it, for
 * an application that wants the loop to be its own - one that has something
 * else to watch while the dialog is up, or wants a keystroke to mean something
 * form_do does not know about.
 */

uint32_t AES_form_keybd()
{
    uint32_t address = aes_addrin(0);
    int16_t obj = aes_intin(0);
    int16_t next = aes_intin(1);
    int16_t key = aes_intin(2);
    void *host;
    int16_t carry;

    FUNC_TRACE_ENTER_ARGS {
        printf("    object %d, key 0x%x\n", obj, key);
    }

    if (!gem_start())
        return AES_ERROR;

    host = aes_tree_in(address);
    if (!host)
        return AES_ERROR;

    carry = emuvdi_form_keybd(host, obj, &key, &next);

    aes_tree_out();
    aes_tree_done();

    gem_present();

    /* Whether the dialog carries on, and what to do next: which object the
     * keystroke moved to, and what is left of the keystroke itself */
    aes_set_intout(1, next);
    aes_set_intout(2, key);

    return (uint32_t)(uint16_t)carry;
}

uint32_t AES_form_button()
{
    uint32_t address = aes_addrin(0);
    int16_t obj = aes_intin(0);
    int16_t clicks = aes_intin(1);
    int16_t next = 0;
    void *host;
    int16_t carry;

    FUNC_TRACE_ENTER_ARGS {
        printf("    object %d, %d clicks\n", obj, clicks);
    }

    if (!gem_start())
        return AES_ERROR;

    host = aes_tree_in(address);
    if (!host)
        return AES_ERROR;

    carry = emuvdi_form_button(host, obj, clicks, &next);

    aes_tree_out();
    aes_tree_done();

    gem_present();

    aes_set_intout(1, next);

    return (uint32_t)(uint16_t)carry;
}

/*
 * form_error - the alert the AES puts up for a GEMDOS error
 *
 * An application hands over a number and gets the words for it, which is why
 * every GEM program of the period said the same thing when a disk was full.
 */
uint32_t AES_form_error()
{
    int16_t which = aes_intin(0);
    int16_t button;

    FUNC_TRACE_ENTER_ARGS {
        printf("    error %d\n", which);
    }

    if (!gem_start())
        return AES_ERROR;

    button = emuvdi_form_error(which);

    gem_present();

    return (uint32_t)(uint16_t)button;
}

/*
 * objc_edit - a character typed into an editable field
 *
 * The application does the waiting and this does the editing: where the cursor
 * is, what a backspace takes out, and whether the character fits the field's
 * template. An application that runs its own dialog loop needs it, and one
 * that uses form_do never sees it.
 */
uint32_t AES_objc_edit()
{
    uint32_t address = aes_addrin(0);
    int16_t obj = aes_intin(0);
    int16_t key = aes_intin(1);
    int16_t index = aes_intin(2);
    int16_t what = aes_intin(3);
    void *host;
    int16_t answer;

    FUNC_TRACE_ENTER_ARGS {
        printf("    object %d, key 0x%x, at %d, %d\n", obj, key, index, what);
    }

    if (!gem_start())
        return AES_ERROR;

    host = aes_tree_in(address);
    if (!host)
        return AES_ERROR;

    answer = emuvdi_objc_edit(host, obj, key, &index, what);

    aes_tree_out();
    aes_tree_done();

    gem_present();

    aes_set_intout(1, index);

    return (uint32_t)(uint16_t)answer;
}
