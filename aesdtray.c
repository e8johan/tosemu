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
 * The session's front door, in the panel.
 *
 * One item, not one per accessory. A person does not want six mystery icons
 * appearing in their panel because a GEM program is running; they want to see
 * that there is a GEM session and be able to get at what is in it. So there is
 * a single mark, and the accessories are a menu hanging off it.
 *
 * It is also the only way to reach an accessory when no GEM application is
 * running, which is the case a Desk menu cannot cover: the Desk menu belongs
 * to an application, and if none is running there is no menu to put anything
 * in. An accessory that could only be reached that way would be unreachable
 * exactly when a person most wants a clock or a calculator.
 *
 * None of this is standard. StatusNotifierItem is a de facto protocol that KDE
 * wrote and others adopted, GNOME needs an extension for it, and some desktops
 * have nothing of the sort. So every failure here is quiet and none of them
 * stops anything: no bus, no watcher, no panel - the session still runs, the
 * accessories still appear in the Desk menu of whatever is running, and the
 * only thing lost is the icon. Saying so once is worth more than failing.
 */

#include "aesdtray.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifdef HAVE_DBUS

#include <dbus/dbus.h>

#include "aesproto.h"

/* Where we are on the bus, and where the panel expects to find these */
#define ITEM_PATH   "/StatusNotifierItem"
#define MENU_PATH   "/MenuBar"

#define ITEM_FACE   "org.kde.StatusNotifierItem"
#define MENU_FACE   "com.canonical.dbusmenu"

#define WATCHER     "org.kde.StatusNotifierWatcher"
#define WATCHER_OBJ "/StatusNotifierWatcher"

static DBusConnection *bus;
static int said_why_not;

/* Who the accessories are, as the panel last saw them. The menu is rebuilt
 * from this, and the numbers are what come back when one is clicked. */
static struct {
    char name[AESD_NAME_LEN + 1];
    int16_t ap_id;
} shown[AESD_MAX_ACCS];

static int shown_count;
static unsigned menu_revision = 1;

/* What to do when somebody picks one, which is the daemon's business rather
 * than this file's */
static void (*picked)(int16_t ap_id);
static void (*quit)(void);

/*
 * Which entry is which.
 *
 * The accessories are numbered from one so that nought can be the root, which
 * makes the number the index into the list. Quit is given a number well past
 * them so that it stays itself however many accessories there are - a menu
 * where the last entry means something different depending on how many are
 * above it is a menu somebody will eventually click wrong.
 */
#define ENTRY_QUIT      (100)
#define ENTRY_SEPARATOR (101)

static void say_once(const char *why)
{
    if (said_why_not)
        return;

    said_why_not = 1;

    printf("tosaesd: no icon in the panel: %s\n", why);
    printf("tosaesd: the session runs without one - the accessories are still "
           "in the Desk menu of whatever is running\n");
    fflush(stdout);
}

/* The mark itself *********************************************************/

/*
 * The picture, compiled in rather than found.
 *
 * A panel is handed pixels rather than a file, so the picture has to be in the
 * program somewhere. Putting it in at build time means it cannot go missing: an
 * icon looked for at runtime and not found leaves the item rendering as
 * nothing, which looks exactly like a session that failed to start, and there
 * is no way to tell the two apart from outside.
 *
 * rsc/tray.svg is the picture and rsc/tray-icon.h is what comes out of it. The
 * generated file is committed, so building tosemu needs no rasteriser; only
 * changing the picture does.
 */
#include "rsc/tray-icon.h"

#define HOW_MANY_PICTURES \
    ((int)(sizeof tray_pictures / sizeof tray_pictures[0]))

/* Answering for a property ************************************************/

static void put_string(DBusMessageIter *at, const char *what)
{
    DBusMessageIter variant;

    dbus_message_iter_open_container(at, DBUS_TYPE_VARIANT, "s", &variant);
    dbus_message_iter_append_basic(&variant, DBUS_TYPE_STRING, &what);
    dbus_message_iter_close_container(at, &variant);
}

static void put_object(DBusMessageIter *at, const char *what)
{
    DBusMessageIter variant;

    dbus_message_iter_open_container(at, DBUS_TYPE_VARIANT, "o", &variant);
    dbus_message_iter_append_basic(&variant, DBUS_TYPE_OBJECT_PATH, &what);
    dbus_message_iter_close_container(at, &variant);
}

static void put_int(DBusMessageIter *at, int what)
{
    DBusMessageIter variant;
    dbus_int32_t value = what;

    dbus_message_iter_open_container(at, DBUS_TYPE_VARIANT, "i", &variant);
    dbus_message_iter_append_basic(&variant, DBUS_TYPE_INT32, &value);
    dbus_message_iter_close_container(at, &variant);
}

static void put_bool(DBusMessageIter *at, int what)
{
    DBusMessageIter variant;
    dbus_bool_t value = what ? TRUE : FALSE;

    dbus_message_iter_open_container(at, DBUS_TYPE_VARIANT, "b", &variant);
    dbus_message_iter_append_basic(&variant, DBUS_TYPE_BOOLEAN, &value);
    dbus_message_iter_close_container(at, &variant);
}

/*
 * The icon, as the protocol wants it: a(iiay), which is a list of sizes each
 * with its pixels. Every size is offered and the panel takes whichever suits
 * it, which is what makes one look right on a doubled display as well.
 */
static void put_the_mark(DBusMessageIter *at)
{
    DBusMessageIter variant, array;
    int i;

    dbus_message_iter_open_container(at, DBUS_TYPE_VARIANT, "a(iiay)", &variant);
    dbus_message_iter_open_container(&variant, DBUS_TYPE_ARRAY, "(iiay)",
                                     &array);

    for (i = 0; i < HOW_MANY_PICTURES; i++)
    {
        DBusMessageIter one, pixels;
        dbus_int32_t size = tray_pictures[i].size;
        const unsigned char *p = tray_pictures[i].argb;
        int n = size * size * 4;

        dbus_message_iter_open_container(&array, DBUS_TYPE_STRUCT, 0, &one);

        dbus_message_iter_append_basic(&one, DBUS_TYPE_INT32, &size);
        dbus_message_iter_append_basic(&one, DBUS_TYPE_INT32, &size);

        dbus_message_iter_open_container(&one, DBUS_TYPE_ARRAY, "y", &pixels);
        dbus_message_iter_append_fixed_array(&pixels, DBUS_TYPE_BYTE, &p, n);
        dbus_message_iter_close_container(&one, &pixels);

        dbus_message_iter_close_container(&array, &one);
    }

    dbus_message_iter_close_container(&variant, &array);
    dbus_message_iter_close_container(at, &variant);
}

/*
 * The tooltip, which is a structure rather than a line of text: an icon name,
 * an icon, a heading and a body. Saying it is a string is the sort of mistake
 * that costs nothing until something reads it strictly.
 */
static void put_the_tooltip(DBusMessageIter *at)
{
    DBusMessageIter variant, tip, pixmaps;
    const char *nothing = "";
    const char *heading = "Atari";
    const char *body = "GEM applications";

    dbus_message_iter_open_container(at, DBUS_TYPE_VARIANT, "(sa(iiay)ss)",
                                     &variant);
    dbus_message_iter_open_container(&variant, DBUS_TYPE_STRUCT, 0, &tip);

    dbus_message_iter_append_basic(&tip, DBUS_TYPE_STRING, &nothing);

    dbus_message_iter_open_container(&tip, DBUS_TYPE_ARRAY, "(iiay)",
                                     &pixmaps);
    dbus_message_iter_close_container(&tip, &pixmaps);

    dbus_message_iter_append_basic(&tip, DBUS_TYPE_STRING, &heading);
    dbus_message_iter_append_basic(&tip, DBUS_TYPE_STRING, &body);

    dbus_message_iter_close_container(&variant, &tip);
    dbus_message_iter_close_container(at, &variant);
}

/* Everything a panel asks about, in the order it is worth asking */
static const char *const every_property[] = {
    "Category", "Id", "Title", "Status", "IconName", "IconPixmap",
    "AttentionIconName", "OverlayIconName", "IconThemePath",
    "ToolTip", "Menu", "ItemIsMenu", "WindowId", 0
};

static int item_property(DBusMessage *reply, const char *name)
{
    DBusMessageIter at;

    dbus_message_iter_init_append(reply, &at);

    if (!strcmp(name, "Category"))
        put_string(&at, "ApplicationStatus");
    else if (!strcmp(name, "Id"))
        put_string(&at, "tosemu");
    else if (!strcmp(name, "Title"))
        put_string(&at, "Atari");
    else if (!strcmp(name, "Status"))
        put_string(&at, "Active");
    else if (!strcmp(name, "IconName"))
        put_string(&at, "");
    else if (!strcmp(name, "IconPixmap"))
        put_the_mark(&at);
    else if (!strcmp(name, "ToolTip"))
        put_the_tooltip(&at);
    else if (!strcmp(name, "Menu"))
        put_object(&at, MENU_PATH);
    else if (!strcmp(name, "AttentionIconName")
             || !strcmp(name, "OverlayIconName")
             || !strcmp(name, "IconThemePath"))
        /* Nothing, but said rather than refused: a panel that asks for all of
         * them and is given an error for one may make nothing of the lot */
        put_string(&at, "");
    else if (!strcmp(name, "WindowId"))
        put_int(&at, 0);
    else if (!strcmp(name, "ItemIsMenu"))
        /*
         * Yes: clicking it opens the menu rather than doing something. There
         * is nothing sensible for a click on its own to do - the session has
         * no single main window to raise - so the menu is the whole of it.
         */
        put_bool(&at, 1);
    else
        return 0;

    return 1;
}

/* The menu ****************************************************************/

/* One entry's properties, as a{sv} */
static void put_entry_properties(DBusMessageIter *at, const char *label,
                                 int is_separator)
{
    DBusMessageIter properties, pair, value;
    const char *key;
    const char *text;

    dbus_message_iter_open_container(at, DBUS_TYPE_ARRAY, "{sv}", &properties);

    if (is_separator)
    {
        key = "type";
        text = "separator";

        dbus_message_iter_open_container(&properties, DBUS_TYPE_DICT_ENTRY, 0,
                                         &pair);
        dbus_message_iter_append_basic(&pair, DBUS_TYPE_STRING, &key);
        dbus_message_iter_open_container(&pair, DBUS_TYPE_VARIANT, "s", &value);
        dbus_message_iter_append_basic(&value, DBUS_TYPE_STRING, &text);
        dbus_message_iter_close_container(&pair, &value);
        dbus_message_iter_close_container(&properties, &pair);
    }
    else
    {
        key = "label";

        dbus_message_iter_open_container(&properties, DBUS_TYPE_DICT_ENTRY, 0,
                                         &pair);
        dbus_message_iter_append_basic(&pair, DBUS_TYPE_STRING, &key);
        dbus_message_iter_open_container(&pair, DBUS_TYPE_VARIANT, "s", &value);
        dbus_message_iter_append_basic(&value, DBUS_TYPE_STRING, &label);
        dbus_message_iter_close_container(&pair, &value);
        dbus_message_iter_close_container(&properties, &pair);
    }

    dbus_message_iter_close_container(at, &properties);
}

/* One entry, as (ia{sv}av) with no children of its own */
static void put_entry(DBusMessageIter *at, int id, const char *label,
                      int is_separator)
{
    DBusMessageIter wrapper, entry, children;
    dbus_int32_t number = id;

    dbus_message_iter_open_container(at, DBUS_TYPE_VARIANT, "(ia{sv}av)",
                                     &wrapper);
    dbus_message_iter_open_container(&wrapper, DBUS_TYPE_STRUCT, 0, &entry);

    dbus_message_iter_append_basic(&entry, DBUS_TYPE_INT32, &number);
    put_entry_properties(&entry, label, is_separator);

    dbus_message_iter_open_container(&entry, DBUS_TYPE_ARRAY, "v", &children);
    dbus_message_iter_close_container(&entry, &children);

    dbus_message_iter_close_container(&wrapper, &entry);
    dbus_message_iter_close_container(at, &wrapper);
}

/*
 * The whole menu.
 *
 * Entry nought is the root and is never shown; the accessories are one to six,
 * numbered so that the number is the index into the list. Nothing else is in
 * it yet - Settings and About want a GEM application of our own to own those
 * dialogs, and there is not one.
 */
static void put_the_menu(DBusMessage *reply)
{
    DBusMessageIter at, root, properties, children;
    dbus_uint32_t revision = menu_revision;
    dbus_int32_t nought = 0;
    int i;

    dbus_message_iter_init_append(reply, &at);
    dbus_message_iter_append_basic(&at, DBUS_TYPE_UINT32, &revision);

    dbus_message_iter_open_container(&at, DBUS_TYPE_STRUCT, 0, &root);
    dbus_message_iter_append_basic(&root, DBUS_TYPE_INT32, &nought);

    dbus_message_iter_open_container(&root, DBUS_TYPE_ARRAY, "{sv}",
                                     &properties);
    dbus_message_iter_close_container(&root, &properties);

    dbus_message_iter_open_container(&root, DBUS_TYPE_ARRAY, "v", &children);

    if (shown_count == 0)
    {
        /*
         * Something rather than an empty menu, because an empty menu looks
         * broken and this is not: it is a session with nothing in it yet.
         */
        put_entry(&children, 1, "No accessories", 0);
    }
    else
    {
        for (i = 0; i < shown_count; i++)
            put_entry(&children, i + 1, shown[i].name, 0);
    }

    /* And the way out, which is the one thing in here that is about the
     * session rather than about what is in it */
    put_entry(&children, ENTRY_SEPARATOR, "", 1);
    put_entry(&children, ENTRY_QUIT, "Quit", 0);

    dbus_message_iter_close_container(&root, &children);
    dbus_message_iter_close_container(&at, &root);
}

/* Answering the bus *******************************************************/

/*
 * Copies one value across, whatever shape it is.
 *
 * A variant can hold anything, and the things here range from a string to an
 * array of structures with a byte array inside, so this walks rather than
 * knows: containers are opened and their contents copied, and everything else
 * is a basic value and is appended.
 */
static void put_variant_from(DBusMessageIter *to, DBusMessageIter *from)
{
    int type = dbus_message_iter_get_arg_type(from);

    if (dbus_type_is_basic(type))
    {
        DBusBasicValue value;

        dbus_message_iter_get_basic(from, &value);
        dbus_message_iter_append_basic(to, type, &value);

        return;
    }

    if (type == DBUS_TYPE_INVALID)
        return;

    {
        DBusMessageIter inside, into;
        char *what = 0;
        const char *holds = 0;

        dbus_message_iter_recurse(from, &inside);

        /*
         * What a container has to be told it holds, which is different for
         * each kind. A struct or a dict entry carries its shape in itself and
         * is told nothing. An array is named for what is in it, so its own
         * signature less the leading a. A variant is named "v" whatever is
         * inside it, so the answer has to come from the thing inside instead -
         * which is what made this abort the first time.
         */
        if (type == DBUS_TYPE_VARIANT)
        {
            what = dbus_message_iter_get_signature(&inside);
            holds = what;
        }
        else if (type != DBUS_TYPE_STRUCT && type != DBUS_TYPE_DICT_ENTRY)
        {
            what = dbus_message_iter_get_signature(from);
            holds = what ? what + 1 : 0;
        }

        dbus_message_iter_open_container(to, type, holds, &into);

        while (dbus_message_iter_get_arg_type(&inside) != DBUS_TYPE_INVALID)
        {
            put_variant_from(&into, &inside);
            dbus_message_iter_next(&inside);
        }

        dbus_message_iter_close_container(to, &into);

        if (what)
            dbus_free(what);
    }
}

static DBusHandlerResult handle(DBusConnection *connection, DBusMessage *msg,
                                void *user)
{
    DBusMessage *reply = 0;
    const char *face = dbus_message_get_interface(msg);
    const char *member = dbus_message_get_member(msg);

    (void)user;

    if (!face || !member)
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;

    /*
     * Everything at once, which is how a panel usually asks. Getting this
     * wrong is invisible from the outside: the item registers, the watcher
     * lists it, every property answers when asked for by name - and nothing
     * appears, because nothing ever asked for one by name.
     */
    if (!strcmp(face, "org.freedesktop.DBus.Properties")
        && !strcmp(member, "GetAll"))
    {
        const char *asked_face = 0;

        dbus_message_get_args(msg, 0, DBUS_TYPE_STRING, &asked_face,
                              DBUS_TYPE_INVALID);

        if (asked_face && !strcmp(asked_face, ITEM_FACE))
        {
            DBusMessageIter at, all, pair;
            int i;

            reply = dbus_message_new_method_return(msg);

            dbus_message_iter_init_append(reply, &at);
            dbus_message_iter_open_container(&at, DBUS_TYPE_ARRAY, "{sv}",
                                             &all);

            for (i = 0; every_property[i]; i++)
            {
                DBusMessage *one = dbus_message_new_method_return(msg);
                DBusMessageIter from;

                if (!one)
                    continue;

                /*
                 * Each is written the same way it would be written on its own,
                 * and then moved across. Two ways of saying what a property is
                 * would be two things to keep alike.
                 */
                if (!item_property(one, every_property[i])
                    || !dbus_message_iter_init(one, &from))
                {
                    dbus_message_unref(one);
                    continue;
                }

                dbus_message_iter_open_container(&all, DBUS_TYPE_DICT_ENTRY, 0,
                                                 &pair);
                dbus_message_iter_append_basic(&pair, DBUS_TYPE_STRING,
                                               &every_property[i]);
                put_variant_from(&pair, &from);
                dbus_message_iter_close_container(&all, &pair);

                dbus_message_unref(one);
            }

            dbus_message_iter_close_container(&at, &all);
        }
    }
    else if (!strcmp(face, "org.freedesktop.DBus.Properties")
        && !strcmp(member, "Get"))
    {
        const char *asked_face = 0, *asked = 0;

        dbus_message_get_args(msg, 0, DBUS_TYPE_STRING, &asked_face,
                              DBUS_TYPE_STRING, &asked, DBUS_TYPE_INVALID);

        if (asked && !strcmp(asked_face, ITEM_FACE))
        {
            reply = dbus_message_new_method_return(msg);

            if (!item_property(reply, asked))
            {
                dbus_message_unref(reply);
                reply = dbus_message_new_error(msg,
                            "org.freedesktop.DBus.Error.UnknownProperty",
                            "no such property");
            }
        }
    }
    else if (!strcmp(face, ITEM_FACE))
    {
        /*
         * Activate, ContextMenu and the rest. The panel opens the menu itself
         * because ItemIsMenu says to, so there is nothing to do here beyond
         * not failing - an error would have the panel think the item is broken.
         */
        reply = dbus_message_new_method_return(msg);
    }
    else if (!strcmp(face, MENU_FACE))
    {
        if (!strcmp(member, "GetLayout"))
        {
            reply = dbus_message_new_method_return(msg);
            put_the_menu(reply);
        }
        else if (!strcmp(member, "Event"))
        {
            dbus_int32_t which = 0;
            const char *what = 0;

            dbus_message_get_args(msg, 0, DBUS_TYPE_INT32, &which,
                                  DBUS_TYPE_STRING, &what, DBUS_TYPE_INVALID);

            if (what && !strcmp(what, "clicked"))
            {
                if (which == ENTRY_QUIT && quit)
                    quit();
                else if (which >= 1 && which <= shown_count && picked)
                    /* Numbered from one so that nought can be the root, so
                     * this is the index into the list of accessories */
                    picked(shown[which - 1].ap_id);
            }

            reply = dbus_message_new_method_return(msg);
        }
        else if (!strcmp(member, "AboutToShow"))
        {
            dbus_bool_t changed = FALSE;

            reply = dbus_message_new_method_return(msg);
            dbus_message_append_args(reply, DBUS_TYPE_BOOLEAN, &changed,
                                     DBUS_TYPE_INVALID);
        }
        else
            reply = dbus_message_new_method_return(msg);
    }

    if (!reply)
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;

    dbus_connection_send(connection, reply, 0);
    dbus_message_unref(reply);

    return DBUS_HANDLER_RESULT_HANDLED;
}

static const DBusObjectPathVTable answering = { 0, handle, 0, 0, 0, 0 };

/* What the daemon calls ***************************************************/

int tray_open(void (*when_picked)(int16_t ap_id), void (*when_quit)(void))
{
    DBusError trouble;
    DBusMessage *ask, *said;
    char name[64];

    picked = when_picked;
    quit = when_quit;

    dbus_error_init(&trouble);

    bus = dbus_bus_get(DBUS_BUS_SESSION, &trouble);
    if (!bus)
    {
        say_once(dbus_error_is_set(&trouble) ? trouble.message
                                             : "there is no session bus");
        dbus_error_free(&trouble);
        return 0;
    }

    dbus_connection_set_exit_on_disconnect(bus, FALSE);

    /* The name has to carry the process number, because a person is allowed
     * more than one session and two of them must not collide */
    snprintf(name, sizeof name, "org.kde.StatusNotifierItem-%d-1", (int)getpid());

    if (dbus_bus_request_name(bus, name, DBUS_NAME_FLAG_DO_NOT_QUEUE, &trouble)
        != DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER)
    {
        say_once(dbus_error_is_set(&trouble) ? trouble.message
                                             : "the name was taken");
        dbus_error_free(&trouble);
        tray_close();
        return 0;
    }

    dbus_connection_register_object_path(bus, ITEM_PATH, &answering, 0);
    dbus_connection_register_object_path(bus, MENU_PATH, &answering, 0);

    /*
     * And ask the panel to show it. There may be no panel - no watcher on the
     * bus at all - which is not a failure of anything: it is a desktop that
     * does not do this, and the session is no worse off than before.
     */
    ask = dbus_message_new_method_call(WATCHER, WATCHER_OBJ, WATCHER,
                                       "RegisterStatusNotifierItem");
    if (ask)
    {
        const char *me = name;

        dbus_message_append_args(ask, DBUS_TYPE_STRING, &me, DBUS_TYPE_INVALID);

        said = dbus_connection_send_with_reply_and_block(bus, ask, 1000,
                                                         &trouble);
        dbus_message_unref(ask);

        if (said)
            dbus_message_unref(said);
        else
        {
            say_once(dbus_error_is_set(&trouble)
                     ? "nothing on this desktop shows tray icons"
                     : "the panel would not have it");
            dbus_error_free(&trouble);
        }
    }

    return 1;
}

void tray_close(void)
{
    if (!bus)
        return;

    dbus_connection_unref(bus);
    bus = 0;
}

/*
 * The accessories changed, so the menu did.
 *
 * The panel is told the layout has a new number rather than being told what is
 * in it: it asks for the layout when it needs one, and telling it everything
 * now would be describing a menu nobody has opened.
 */
void tray_accessories(const char *const *names, const int16_t *ap_ids, int n)
{
    DBusMessage *news;
    dbus_uint32_t revision;
    dbus_int32_t root = 0;
    int i;

    if (n > AESD_MAX_ACCS)
        n = AESD_MAX_ACCS;

    shown_count = n;

    for (i = 0; i < n; i++)
    {
        snprintf(shown[i].name, sizeof shown[i].name, "%s", names[i]);
        shown[i].ap_id = ap_ids[i];
    }

    if (!bus)
        return;

    menu_revision++;
    revision = menu_revision;

    news = dbus_message_new_signal(MENU_PATH, MENU_FACE, "LayoutUpdated");
    if (!news)
        return;

    dbus_message_append_args(news, DBUS_TYPE_UINT32, &revision,
                             DBUS_TYPE_INT32, &root, DBUS_TYPE_INVALID);

    dbus_connection_send(bus, news, 0);
    dbus_message_unref(news);
}

/* The connection, for the daemon to wait on beside its own socket */
int tray_fd(void)
{
    int fd = -1;

    if (!bus)
        return -1;

    if (!dbus_connection_get_unix_fd(bus, &fd))
        return -1;

    return fd;
}

void tray_pump(void)
{
    if (!bus)
        return;

    dbus_connection_read_write(bus, 0);

    while (dbus_connection_dispatch(bus) == DBUS_DISPATCH_DATA_REMAINS)
        ;

    dbus_connection_flush(bus);
}

#else /* not HAVE_DBUS */

/*
 * Built without D-Bus, which is a thing to be able to do: the tray is the one
 * part of this that depends on a library the rest does not, and a machine
 * without it should still get a working session rather than a build failure.
 */
int tray_open(void (*when_picked)(int16_t ap_id), void (*when_quit)(void))
{
    (void)when_picked;
    (void)when_quit;

    printf("tosaesd: built without D-Bus, so there is no icon in the panel - "
           "the accessories are still in the Desk menu of whatever is "
           "running\n");
    fflush(stdout);

    return 0;
}

void tray_close(void) { }
void tray_accessories(const char *const *names, const int16_t *ap_ids, int n)
{
    (void)names; (void)ap_ids; (void)n;
}
int tray_fd(void) { return -1; }
void tray_pump(void) { }

#endif
