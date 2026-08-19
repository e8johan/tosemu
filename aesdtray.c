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
 * Drawn rather than loaded, because a file would have to be found at runtime
 * and an icon that is missing is worse than one that is plain. It is a small
 * ARGB square, which is what the panel takes when no icon theme has a name it
 * recognises - and no icon theme has a name for this.
 */
#define ICON_SIZE (22)

static void draw_the_mark(unsigned char *argb)
{
    int x, y;

    for (y = 0; y < ICON_SIZE; y++)
    {
        for (x = 0; x < ICON_SIZE; x++)
        {
            unsigned char *p = argb + (y * ICON_SIZE + x) * 4;
            int mid = ICON_SIZE / 2;
            int from_mid = (x < mid) ? (mid - x) : (x - mid);
            int on = 0;

            /*
             * Three uprights on a base: the middle one full height, the outer
             * two shorter and leaning away, which is the shape of the mark
             * everybody who had one of these machines recognises.
             */
            if (y > ICON_SIZE - 4)
                on = x > 1 && x < ICON_SIZE - 2;        /* the base */
            else if (from_mid < 2)
                on = y > 2;                             /* the middle */
            else if (from_mid >= 4 && from_mid <= 6)
                on = y > 2 + (from_mid - 4) * 3;        /* the outer two */

            /* Bytes are blue, green, red, alpha the other way about: the wire
             * wants them most significant first, which is A R G B */
            p[0] = on ? 0xff : 0x00;
            p[1] = on ? 0xf0 : 0x00;
            p[2] = on ? 0xf0 : 0x00;
            p[3] = on ? 0xf0 : 0x00;
        }
    }
}

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

static void put_bool(DBusMessageIter *at, int what)
{
    DBusMessageIter variant;
    dbus_bool_t value = what ? TRUE : FALSE;

    dbus_message_iter_open_container(at, DBUS_TYPE_VARIANT, "b", &variant);
    dbus_message_iter_append_basic(&variant, DBUS_TYPE_BOOLEAN, &value);
    dbus_message_iter_close_container(at, &variant);
}

/* The icon, as the one thing the protocol calls a(iiay): a list of sizes, each
 * with its pixels */
static void put_the_mark(DBusMessageIter *at)
{
    unsigned char argb[ICON_SIZE * ICON_SIZE * 4];
    DBusMessageIter variant, array, one, pixels;
    dbus_int32_t size = ICON_SIZE;
    const unsigned char *p = argb;
    int n = (int)sizeof argb;

    draw_the_mark(argb);

    dbus_message_iter_open_container(at, DBUS_TYPE_VARIANT, "a(iiay)", &variant);
    dbus_message_iter_open_container(&variant, DBUS_TYPE_ARRAY, "(iiay)",
                                     &array);
    dbus_message_iter_open_container(&array, DBUS_TYPE_STRUCT, 0, &one);

    dbus_message_iter_append_basic(&one, DBUS_TYPE_INT32, &size);
    dbus_message_iter_append_basic(&one, DBUS_TYPE_INT32, &size);

    dbus_message_iter_open_container(&one, DBUS_TYPE_ARRAY, "y", &pixels);
    dbus_message_iter_append_fixed_array(&pixels, DBUS_TYPE_BYTE, &p, n);
    dbus_message_iter_close_container(&one, &pixels);

    dbus_message_iter_close_container(&array, &one);
    dbus_message_iter_close_container(&variant, &array);
    dbus_message_iter_close_container(at, &variant);
}

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
        put_string(&at, "GEM");
    else if (!strcmp(name, "Menu"))
        put_object(&at, MENU_PATH);
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

    dbus_message_iter_close_container(&root, &children);
    dbus_message_iter_close_container(&at, &root);
}

/* Answering the bus *******************************************************/

static DBusHandlerResult handle(DBusConnection *connection, DBusMessage *msg,
                                void *user)
{
    DBusMessage *reply = 0;
    const char *face = dbus_message_get_interface(msg);
    const char *member = dbus_message_get_member(msg);

    (void)user;

    if (!face || !member)
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;

    /* What everything on a bus has to answer */
    if (!strcmp(face, "org.freedesktop.DBus.Properties")
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

            /* Numbered from one so that nought can be the root, so this is
             * the index into the list of accessories */
            if (what && !strcmp(what, "clicked")
                && which >= 1 && which <= shown_count && picked)
                picked(shown[which - 1].ap_id);

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

int tray_open(void (*when_picked)(int16_t ap_id))
{
    DBusError trouble;
    DBusMessage *ask, *said;
    char name[64];

    picked = when_picked;

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
int tray_open(void (*when_picked)(int16_t ap_id))
{
    (void)when_picked;

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
