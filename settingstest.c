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
 * Reading a settings file.
 *
 * Built for the host, because what is being checked is not something an
 * application can reach: an emulated program can be told which screen it ended
 * up on, and that says a file was read, but it cannot see whether a remark was
 * understood as a remark or whether a value said twice took the second one.
 *
 * The end of a file has as much to do with this as the middle. A setting
 * spelled wrongly has to say so - a settings file that is quietly half ignored
 * is worse than one that will not load - so the complaints are part of what is
 * checked here, by counting the lines the parser objected to.
 */

#include "settings.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

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
        printf("not ok %d - %s (got %ld, want %ld)\n", n, name, got, want);
    }
}

static void check_str(const char *got, const char *want, const char *name)
{
    n++;
    if (got && want && strcmp(got, want) == 0)
        printf("ok %d - %s\n", n, name);
    else
    {
        fails++;
        printf("not ok %d - %s (got [%s], want [%s])\n", n, name,
               got ? got : "nothing", want ? want : "nothing");
    }
}

static char written[] = "/tmp/tosemu-settings-testXXXXXX";

/* Puts the text in a file and reads it as the settings, answering what
 * settings_load answered */
static int load(const char *text)
{
    FILE *f = fopen(written, "w");

    if (!f)
    {
        printf("Bail out! - cannot write %s\n", written);
        exit(1);
    }

    fputs(text, f);
    fclose(f);

    return settings_load(written);
}

/*
 * The same, counting what it complained about on the way.
 *
 * A complaint is a line on stderr, so this catches stderr in a file and counts
 * them. Reading a settings file is one of the few things here that is supposed
 * to talk: what it says when a line cannot be read is the whole of how anybody
 * finds out, so how much it says is worth checking as well as what it took.
 */
static int complaints(const char *text)
{
    char noise[] = "/tmp/tosemu-settings-noiseXXXXXX";
    int fd = mkstemp(noise);
    int saved, lines = 0, c;
    FILE *f;

    if (fd < 0)
    {
        printf("Bail out! - nowhere to catch what it says\n");
        exit(1);
    }

    fflush(stderr);
    saved = dup(fileno(stderr));
    dup2(fd, fileno(stderr));

    load(text);

    fflush(stderr);
    dup2(saved, fileno(stderr));
    close(saved);
    close(fd);

    f = fopen(noise, "r");
    if (f)
    {
        while ((c = fgetc(f)) != EOF)
            if (c == '\n')
                lines++;
        fclose(f);
    }

    unlink(noise);

    return lines;
}

int main(void)
{
    int fd = mkstemp(written);

    if (fd < 0)
    {
        printf("Bail out! - nowhere to write a settings file\n");
        return 1;
    }
    close(fd);

    /* Nothing from the environment for any of this, so that what comes back
     * is what the file said and not what this machine happens to be set to */
    unsetenv("TOSEMU_SCREEN");
    unsetenv("TOSEMU_SCALE");
    unsetenv("TOSEMU_NO_WINDOW");
    unsetenv("TOSEMU_CLICKS");
    unsetenv("TOS_BASE_PATH");

    /* A file as somebody would write one */
    check(load("[screen]\n"
               "mode = tt-high\n"
               "scale = 2\n"
               "\n"
               "[files]\n"
               "base = /srv/tos\n"), 1, "a settings file is read");
    check_str(setting("TOSEMU_SCREEN"), "tt-high", "a setting comes back");
    check_str(setting("TOSEMU_SCALE"), "2", "and so does the one after it");
    check_str(setting("TOS_BASE_PATH"), "/srv/tos",
              "and one in another section");
    check(setting("TOSEMU_CLICKS") == 0, 1, "one nothing said is nothing");

    /* Spaces are around the parts rather than part of them */
    load("[screen]\n"
         "   mode   =   high   \n"
         "scale=1\n");
    check_str(setting("TOSEMU_SCREEN"), "high", "the spaces are not the value");
    check_str(setting("TOSEMU_SCALE"), "1", "and neither is their absence");

    /* A remark is a whole line, either way of starting one */
    load("# [screen]\n"
         "; mode = low\n"
         "[screen]\n"
         "mode = medium\n");
    check_str(setting("TOSEMU_SCREEN"), "medium", "a remark is not a setting");

    /*
     * And only a whole line. A hash inside a value is part of it, because the
     * alternative is a path or a list of clicks quietly losing its tail.
     */
    load("[input]\nclicks = 10,20 # not a remark\n");
    check_str(setting("TOSEMU_CLICKS"), "10,20 # not a remark",
              "a hash inside a value is part of the value");

    /* Quotes are how a value keeps its spaces */
    load("[files]\nbase = \"  /srv/with spaces  \"\n");
    check_str(setting("TOS_BASE_PATH"), "  /srv/with spaces  ",
              "quotes come off and what was inside them stays");

    /* Said twice is the second one meaning it */
    load("[screen]\nmode = low\nmode = tt-medium\n");
    check_str(setting("TOSEMU_SCREEN"), "tt-medium", "the later line wins");

    /* And a second file is not the first one with more on top */
    load("[screen]\nmode = low\nscale = 4\n");
    load("[screen]\nmode = high\n");
    check_str(setting("TOSEMU_SCREEN"), "high", "a second file replaces");
    check(setting("TOSEMU_SCALE") == 0, 1,
          "and what it does not mention is not still there");

    /*
     * The environment beats the file. Saying something on a command line is
     * saying it about this run, and a file that overrode it would leave no way
     * to try anything without editing the file first.
     */
    load("[screen]\nmode = low\n");
    setenv("TOSEMU_SCREEN", "tt-high", 1);
    check_str(setting("TOSEMU_SCREEN"), "tt-high", "the environment wins");
    unsetenv("TOSEMU_SCREEN");
    check_str(setting("TOSEMU_SCREEN"), "low", "and the file is still there");

    /* The ones that are a yes or a no */
    load("[debug]\ntrace-paths = yes\ntrace-input = no\n");
    check(setting_flag("TOSEMU_TRACE_PATHS"), 1, "yes is a yes");
    check(setting_flag("TOSEMU_TRACE_INPUT"), 0, "and no is a no");

    load("[debug]\ntrace-paths = 1\ntrace-input = 0\n");
    check(setting_flag("TOSEMU_TRACE_PATHS"), 1, "and so is 1");
    check(setting_flag("TOSEMU_TRACE_INPUT"), 0, "and so is 0");

    load("[debug]\ntrace-paths = on\ntrace-input = off\n");
    check(setting_flag("TOSEMU_TRACE_PATHS"), 1, "and on");
    check(setting_flag("TOSEMU_TRACE_INPUT"), 0, "and off");

    load("[screen]\nmode = high\n");
    check(setting_flag("TOSEMU_TRACE_PATHS"), 0, "and nothing said is a no");

    /*
     * The window, which is the one setting the file says the other way round
     * from the environment: an environment variable is set or it is not and
     * has no room to say no, so it is called NO_WINDOW; a file has room, and
     * nobody should have to write "no-window = no".
     */
    load("[screen]\nwindow = no\n");
    check(setting_flag("TOSEMU_NO_WINDOW"), 1, "no window means no window");
    load("[screen]\nwindow = yes\n");
    check(setting_flag("TOSEMU_NO_WINDOW"), 0, "and a window means a window");

    setenv("TOSEMU_NO_WINDOW", "1", 1);
    check(setting_flag("TOSEMU_NO_WINDOW"), 1,
          "and the environment still means what it always did");
    unsetenv("TOSEMU_NO_WINDOW");

    /*
     * What is said and not understood, which has to be said out loud: a
     * settings file quietly half read is worse than one refused outright.
     * Each of these takes nothing from the line it could not read, leaves the
     * rest of the file alone, and complains exactly once - once being the
     * point. A section nobody has heard of has no settings in it either, and
     * saying so again for every line inside it would bury the one complaint
     * that matters under a list of its consequences.
     */
    check(complaints("[screen]\nmode = low\n"), 0, "a file that reads has nothing to say");

    check(complaints("[nosuchsection]\nmode = low\nscale = 2\n"), 1,
          "a section that is not one is mentioned once, not once a line");
    check(setting("TOSEMU_SCREEN") == 0, 1, "and nothing inside it is taken");

    check(complaints("[screen]\nnosuchkey = low\nmode = high\n"), 1,
          "a key that is not one is mentioned once");
    check_str(setting("TOSEMU_SCREEN"), "high",
              "and the rest of the file is read as usual");

    check(complaints("mode = low\n"), 1, "a setting before any section is one complaint");
    check(setting("TOSEMU_SCREEN") == 0, 1, "and is not taken either");

    check(complaints("[screen]\nthis line has no equals sign\nmode = low\n"), 1,
          "and so is a line that is neither a section nor a setting");
    check_str(setting("TOSEMU_SCREEN"), "low", "with the rest of it still read");

    /* A file that was named and is not there is an error; the usual place not
     * being there is not */
    check(settings_load("/nonexistent/tosemu/settings"), 0,
          "a file that was named and cannot be read fails");
    check(settings_load(0) != 0, 1, "and the usual place may simply be absent");

    /* Told to look at no file at all, which is what a test run wants: what is
     * in somebody's home directory is not its business */
    settings_ignore_file();
    load("[screen]\nmode = tt-high\n");
    check(setting("TOSEMU_SCREEN") == 0, 1, "and a file can be ignored outright");

    unlink(written);

    printf("1..%d\n", n);

    return fails ? 1 : 0;
}
