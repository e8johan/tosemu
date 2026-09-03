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
 * Everything tosemu can be told, and the two places it can be told it.
 *
 * An environment variable says something for one run. A file says it for every
 * run, which is what most of these are: which screen the machine has and where
 * the drive is rooted do not change between one program and the next, and
 * having to remember them on every command line is how they come to differ by
 * accident.
 *
 * The environment wins. Saying something on a command line is saying it about
 * this run in particular, and a file that overrode that would leave no way to
 * try anything without editing it first.
 */

#include "settings.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/*
 * Every setting there is.
 *
 * One line each, and the line is the whole of what relates the two names: the
 * environment variable a call site asks for, and the section and key a person
 * writes in a file. Adding a setting means adding it here, and nothing that
 * reads one needs to know this table exists.
 *
 * `window` is the one that is not a rename. TOSEMU_NO_WINDOW is a negative
 * because an environment variable is set or it is not, and there is no room in
 * that for saying no; a file has room, and "no-window = no" is not a sentence
 * anybody should have to write. So the file says whether there is to be a
 * window and the flag it answers is the opposite - see setting_flag.
 */
static const struct {
    const char *env;
    const char *section;
    const char *key;
    int inverted;
} settings[] = {
    { "TOSEMU_SCREEN",      "screen",  "mode",        0 },
    { "TOSEMU_SCALE",       "screen",  "scale",       0 },
    { "TOSEMU_OUTPUT",      "screen",  "output",      0 },
    { "TOSEMU_NO_WINDOW",   "screen",  "window",      1 },
    { "TOSEMU_DECORATIONS", "screen",  "decorations", 0 },

    { "TOSEMU_KEYS",        "input",   "keys",        0 },
    { "TOSEMU_CLICKS",      "input",   "clicks",      0 },

    { "TOS_BASE_PATH",      "files",   "base",        0 },

    { "TOSEMU_AESD",        "session", "socket",      0 },

    { "TOSEMU_SCREENSHOT",  "debug",   "screenshot",  0 },
    { "TOSEMU_TRACE_INPUT", "debug",   "trace-input", 0 },
    { "TOSEMU_TRACE_PATHS", "debug",   "trace-paths", 0 },
};

#define SETTINGS (int)(sizeof settings / sizeof settings[0])

/* What the file said, one slot per line of the table above */
static char *from_file[SETTINGS];

/* And whether to look at a file at all */
static int ignoring;

/* Which file was read, for saying so. A file that was looked for and not
 * found leaves this empty, which is not the same as not having looked. */
static char came_from[512];

static int index_of(const char *env)
{
    int i;

    for (i = 0; i < SETTINGS; i++)
        if (strcmp(settings[i].env, env) == 0)
            return i;

    /* A name nothing in the table answers to, which is a call site asking for
     * a setting that was never added here. It will still see the environment,
     * so it works and only the file half is missing - which is exactly the
     * sort of half-working that is worth saying out loud. */
    return -1;
}

const char *setting(const char *name)
{
    const char *said = getenv(name);
    int i;

    if (said)
        return said;

    i = index_of(name);

    return i < 0 ? 0 : from_file[i];
}

int setting_flag(const char *name)
{
    const char *said = getenv(name);
    int inverted = 0;
    int i = index_of(name);
    int yes;

    if (i >= 0)
        inverted = settings[i].inverted;

    /*
     * The environment first, and a variable that is there at all is a yes -
     * that is what setting one has always meant here, and TOSEMU_NO_WINDOW=1
     * is how every test run says it. The words that plainly mean no are still
     * taken at their word, because somebody writing =0 does not mean yes.
     *
     * It is read the same way whether the setting is inverted or not: the
     * environment's name is the negative one, so a yes there is a yes to the
     * flag. Only the file speaks in the positive.
     */
    if (said)
        return !(strcasecmp(said, "0") == 0 || strcasecmp(said, "no") == 0
                 || strcasecmp(said, "false") == 0
                 || strcasecmp(said, "off") == 0);

    if (i < 0 || !from_file[i])
        return 0;

    said = from_file[i];
    yes = !(strcasecmp(said, "0") == 0 || strcasecmp(said, "no") == 0
            || strcasecmp(said, "false") == 0 || strcasecmp(said, "off") == 0);

    return inverted ? !yes : yes;
}

const char *settings_default_path(void)
{
    static char path[512];
    const char *home;

    if (path[0])
        return path;

    home = getenv("HOME");
    if (!home || !*home)
        return 0;

    snprintf(path, sizeof path, "%s/.tosemu", home);

    return path;
}

void settings_ignore_file(void)
{
    ignoring = 1;
}

/* Whitespace off both ends, in place */
static char *trim(char *s)
{
    char *end;

    while (*s && isspace((unsigned char)*s))
        s++;

    end = s + strlen(s);
    while (end > s && isspace((unsigned char)end[-1]))
        end--;
    *end = '\0';

    return s;
}

/*
 * Where in the file the reading has got to.
 *
 * Which section the lines now belong to, and whether the last thing that tried
 * to be one succeeded. The two are not the same: a file that has said nothing
 * yet and a file whose last heading named a section nobody has heard of both
 * have no section to put a setting in, but only the first is worth mentioning
 * again. The other has been complained about once already, and saying it again
 * for every line inside buries the complaint that matters.
 */
struct parse {
    char section[64];
    int lost;
};

/* One line, already trimmed and known not to be blank or a comment */
static void take_line(const char *where, int line, char *text,
                      struct parse *at)
{
    char *equals, *key, *value;
    int i;

    if (text[0] == '[')
    {
        char *close = strchr(text, ']');
        char *named;

        at->section[0] = '\0';
        at->lost = 1;

        if (!close || close[1] != '\0')
        {
            fprintf(stderr, "%s:%d: a section is a name in square brackets, "
                            "and this is '%s'\n", where, line, text);
            return;
        }

        *close = '\0';
        named = trim(text + 1);

        for (i = 0; i < SETTINGS; i++)
            if (strcmp(settings[i].section, named) == 0)
            {
                snprintf(at->section, sizeof at->section, "%s", named);
                at->lost = 0;
                return;
            }

        fprintf(stderr, "%s:%d: there is no section called '%s'. There is "
                        "screen, input, files, session and debug\n",
                where, line, named);
        return;
    }

    equals = strchr(text, '=');
    if (!equals)
    {
        fprintf(stderr, "%s:%d: '%s' is neither a section nor a setting, "
                        "which is a name, an equals sign and a value\n",
                where, line, text);
        return;
    }

    *equals = '\0';
    key = trim(text);
    value = trim(equals + 1);

    /* Quotes are not part of a value, and are how one keeps its spaces */
    if (strlen(value) >= 2 && value[0] == '"'
        && value[strlen(value) - 1] == '"')
    {
        value[strlen(value) - 1] = '\0';
        value++;
    }

    if (!at->section[0])
    {
        if (!at->lost)
            fprintf(stderr, "%s:%d: '%s' is before any section, and every "
                            "setting is in one\n", where, line, key);
        return;
    }

    for (i = 0; i < SETTINGS; i++)
    {
        if (strcmp(settings[i].section, at->section) != 0)
            continue;
        if (strcmp(settings[i].key, key) != 0)
            continue;

        /* Said twice, which is the later one meaning it */
        free(from_file[i]);
        from_file[i] = strdup(value);

        return;
    }

    fprintf(stderr, "%s:%d: [%s] has no setting called '%s'. It has",
            where, line, at->section, key);
    {
        int said = 0;

        for (i = 0; i < SETTINGS; i++)
            if (strcmp(settings[i].section, at->section) == 0)
                fprintf(stderr, "%s %s", said++ ? "," : "", settings[i].key);
    }
    fprintf(stderr, "\n");
}

int settings_load(const char *path)
{
    struct parse at;
    char line[1024];
    FILE *f;
    int number = 0;
    int named = path != 0;
    int i;

    if (ignoring)
        return 1;

    /* What a file says is what it says, rather than what it says on top of
     * whatever a previous one did */
    for (i = 0; i < SETTINGS; i++)
    {
        free(from_file[i]);
        from_file[i] = 0;
    }

    came_from[0] = 0;

    if (!path)
        path = settings_default_path();

    if (!path)
        return 1;   /* Nowhere to look, which is not a complaint */

    f = fopen(path, "r");
    if (!f)
    {
        /*
         * Not being there is only a failure when somebody said where it was.
         * The usual place is a place a file may be, not one it has to be.
         */
        if (named)
            fprintf(stderr, "tosemu: cannot read the settings in %s\n", path);

        return !named;
    }

    snprintf(came_from, sizeof came_from, "%s", path);

    memset(&at, 0, sizeof at);

    while (fgets(line, sizeof line, f))
    {
        char *text;

        number++;

        text = trim(line);

        /*
         * A comment is a whole line. It is tempting to let one start
         * anywhere, and then a path or a list of clicks with a hash in it
         * quietly loses its tail - which is worse than having to put the
         * remark on a line of its own.
         */
        if (!*text || *text == '#' || *text == ';')
            continue;

        take_line(path, number, text, &at);
    }

    fclose(f);

    return 1;
}

void settings_say(const char *who)
{
    int said = 0;
    int i;

    if (ignoring)
        printf("%s: no settings file, because none was to be read\n", who);
    else if (came_from[0])
        printf("%s: settings from %s\n", who, came_from);
    else
        printf("%s: no settings file to read\n", who);

    for (i = 0; i < SETTINGS; i++)
    {
        const char *from_env = getenv(settings[i].env);

        /*
         * Said in the terms it was said in, rather than translated into one
         * of the two spellings. They are not the same sentence: the
         * environment's name for the window is a negative and the file's is
         * not, so a line reporting one as the other would be reporting the
         * opposite of what was asked for.
         */
        if (from_env && from_file[i])
            printf("%s: %s=%s, over the file's %s\n", who, settings[i].env,
                   from_env, from_file[i]);
        else if (from_env)
            printf("%s: %s=%s\n", who, settings[i].env, from_env);
        else if (from_file[i])
            printf("%s: [%s] %s = %s\n", who, settings[i].section,
                   settings[i].key, from_file[i]);
        else
            continue;

        said = 1;
    }

    if (!said)
        printf("%s: and nothing was asked for, so all of it is the usual\n",
               who);

    fflush(stdout);
}
