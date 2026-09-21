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
 * A picture drawn without the VDI, and shown.
 *
 * A program that takes the machine over draws into memory of its own and
 * points the video base at it; what the video hardware shows is that memory,
 * in the shifter's colours. So this draws a few pixels whose places test the
 * layout - one in the middle of a word, which is where a word read the wrong
 * way round puts it at the other end, one that needs a second plane, and the
 * very last pixel of the screen - points the shifter at them, says a frame is
 * done, and reads back the screenshot the emulator was told to take.
 *
 * The run says where the screenshot goes and the screen it runs on, and this
 * reads the file through GEMDOS like any other.
 */

#include <stdio.h>
#include <string.h>
#include <mint/osbind.h>

#define SHOT "video.ppm"

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

static short width, height, planes;
static unsigned char shot[640 * 400 * 3];

/* The screenshot, which is a binary PPM: a header of three numbers and then
 * three bytes a pixel */
static int read_shot(void)
{
    FILE *f = fopen(SHOT, "rb");
    int w, h, most;

    if (!f)
        return 0;

    if (fscanf(f, "P6 %d %d %d", &w, &h, &most) != 3 || w != width
        || h != height)
    {
        fclose(f);
        return 0;
    }

    fgetc(f);
    fread(shot, 3, (size_t)w * h, f);
    fclose(f);

    return 1;
}

/* A pixel of it as 0xRRGGBB */
static long pixel(int x, int y)
{
    unsigned char *p = shot + 3 * ((long)y * width + x);

    return ((long)p[0] << 16) | ((long)p[1] << 8) | p[2];
}

/* What a colour register looks like on the glass: four bits a gun, each
 * spread over eight */
static long colour_of(int pen)
{
    unsigned short c = (unsigned short)Setcolor(pen, -1);

    return ((long)((c >> 8) & 0xf) * 0x11 << 16)
         | ((long)((c >> 4) & 0xf) * 0x11 << 8)
         | ((c & 0xf) * 0x11);
}

static unsigned short buffer[16000 + 128];

int main(void)
{
    unsigned short *screen;
    unsigned short old_colour = 0;
    long phys = (long)Physbase();
    long words, row;
    int pen_8;

    switch (Getrez())
    {
    case 0:
        width = 320, height = 200, planes = 4;
        break;
    case 1:
        width = 640, height = 200, planes = 2;
        break;
    default:
        width = 640, height = 400, planes = 1;
        break;
    }

    words = (long)width / 16 * planes * height;
    row = (long)width / 16 * planes;

    /* Nothing is shown of a machine whose screen nobody has moved: its
     * pictures are the VDI's, and this program has drawn none */
    Vsync();
    check(read_shot(), 0, "nothing is shown before the video base moves");

    /* On the boundary an ST needed */
    screen = (unsigned short *)(((long)buffer + 255) & ~255L);
    memset(screen, 0, words * 2);

    /*
     * The ninth pixel, bit seven of the first word of plane nought. A word
     * brought across the wrong way round has it at the first pixel instead.
     * On sixteen colours it is in plane three as well, so it is pen nine,
     * which only four planes interleaved properly can make.
     */
    screen[0] = 0x0080;
    if (planes == 4)
        screen[3] = 0x0080;
    pen_8 = (planes == 4) ? 9 : 1;

    /* The second pixel in plane one, on the screens that have one */
    if (planes >= 2)
        screen[1] = 0x4000;

    /* And the last pixel of the screen, which is the last word of plane
     * nought on the last row */
    screen[words - planes] = 0x0001;

    /* A colour nobody would pick, so that it is certain the picture is shown
     * in the registers' colours. The one screen with no colours is left be. */
    if (planes > 1)
        old_colour = (unsigned short)Setcolor(pen_8, 0x0123);

    Setscreen(-1L, (void *)screen, -1);
    Vsync();

    check(read_shot(), 1, "moving the video base shows what it points at");
    check(pixel(0, 0), colour_of(0), "the first pixel is the background");
    check(pixel(8, 0), colour_of(pen_8),
          "the ninth is where its word says, in its register's colour");
    if (planes > 1)
        check(pixel(8, 0), 0x112233L, "which is the colour it was given");
    if (planes >= 2)
        check(pixel(1, 0), colour_of(2), "the second is in the next plane");
    check(pixel(width - 1, height - 1), colour_of(1),
          "and the last pixel of the screen is shown too");

    /* A colour changing changes the picture with nothing written to it */
    Setcolor(0, 0x0f00);
    Vsync();
    read_shot();
    check(pixel(0, 0), 0xff0000L, "changing a colour shows it at once");
    Setcolor(0, 0x0fff);

    /* And the picture follows the base back, rather than going away */
    Setscreen(-1L, (void *)phys, -1);
    Vsync();
    check(read_shot(), 1, "moving the base back goes on showing it");
    check(pixel(8, 0), colour_of(0),
          "and what it shows is where the base is now");

    if (planes > 1)
        Setcolor(pen_8, old_colour);

    printf("1..%d\n", n);

    return fails;
}
