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
 * The MIDI port, on the host side of it. See midi.h for what it is for.
 *
 * Two rings and a backend, and the split between them is the whole design.
 * The rings hold raw bytes, because that is what the emulated machine has to
 * offer and what it expects back; the backend moves raw bytes to and from
 * whatever the host calls a MIDI port. Nothing here knows what a note is.
 *
 * The rings exist because neither side may be made to wait for the other. A
 * program writing to the port is the emulated machine, and stopping it until
 * ALSA is ready would stop a sequencer mid bar; a synthesiser sending a dump
 * is the host, and it will not wait for an application that is busy redrawing.
 * So both sides put bytes down and walk away, and midi_pump is where the two
 * meet.
 *
 * A note on why no backend parses anything. The sequencer one has to assemble
 * events, because the ALSA sequencer is event shaped, and it does that with
 * ALSA's own parser rather than with one written here - snd_midi_event_* is
 * exactly this problem and it already knows about running status, about the
 * real time messages that may appear in the middle of another message, and
 * about system exclusive longer than any buffer. Writing a second parser
 * beside it would be writing a worse one.
 */

#include "midi.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

#ifdef HAVE_ALSA
#include <alsa/asoundlib.h>
#endif

#include "settings.h"

/* Rings ********************************************************************/

/*
 * Big enough that nothing an application does in one go can fill one. A
 * sysex dump is the large case and a few kilobytes covers the ones a patch
 * editor sends; past that the far end has stopped reading, which is a
 * different problem and one that dropping is the right answer to.
 */
#define RING_SIZE (8192)

struct ring {
    uint8_t bytes[RING_SIZE];
    int head;
    int tail;
};

/*
 * One slot is always left empty, which is what makes head equal to tail mean
 * empty rather than full. The alternative is a count beside the two indices
 * and one more thing to keep in step.
 */
static int ring_push(struct ring *r, uint8_t byte)
{
    int next = (r->tail + 1) % RING_SIZE;

    if (next == r->head)
        return 0;

    r->bytes[r->tail] = byte;
    r->tail = next;

    return 1;
}

static int ring_take(struct ring *r, uint8_t *byte)
{
    if (r->head == r->tail)
        return 0;

    *byte = r->bytes[r->head];
    r->head = (r->head + 1) % RING_SIZE;

    return 1;
}

static void ring_empty(struct ring *r)
{
    r->head = 0;
    r->tail = 0;
}

static struct ring inbound;
static struct ring outbound;

/* Said once each, however often the thing that prompted it happens */
static void said(const char *what)
{
    static const char *before[8];
    static int count;
    int i;

    for (i = 0; i < count; i++)
        if (before[i] == what)
            return;

    if (count < (int)(sizeof before / sizeof before[0]))
        before[count++] = what;

    printf("tosemu: MIDI, %s\n", what);
    fflush(stdout);
}

/* Backends *****************************************************************/

/*
 * A backend is opened with the whole of what the setting said, prefix and all,
 * and strips its own. That is a line of code either way and it keeps the
 * spelling in one place: hw:1,0,0 is what ALSA itself wants to be handed, so
 * taking the prefix off and putting it back would be work done twice.
 *
 * forget is not close. A child of fork has a copy of whatever the parent
 * opened, and tidying it up properly would tell the other end that the parent
 * had gone - so the child lets go of the descriptors and says nothing to
 * anybody. See midi_forget.
 */
struct midi_backend {
    const char *prefix;
    int  (*open)(const char *spelling);
    void (*close)(void);
    void (*forget)(void);
    int  (*fd)(void);
    int  (*read)(uint8_t *into, int room);     /* taken, 0 none, -1 gone */
    int  (*write)(const uint8_t *from, int n); /* accepted, 0 would block */
};

/* The file backend *********************************************************/

/*
 * file:<in>[,<out>] - a file of bytes to be read as though they had arrived,
 * and a file to write everything sent into.
 *
 * The input is read whole when the port is opened and the file is closed
 * again, which is why midi_fd has nothing to offer for this backend. A test
 * wants the bytes to be there, not to arrive at some particular moment, and a
 * regular file in a poll is a wait that never waits.
 */
static int file_out = -1;

static int file_open(const char *spelling)
{
    const char *spec = spelling + strlen("file:");
    const char *comma = strchr(spec, ',');
    char *in_name;
    size_t in_len;
    int in;

    in_len = comma ? (size_t)(comma - spec) : strlen(spec);

    if (comma)
    {
        file_out = open(comma + 1, O_WRONLY | O_CREAT | O_TRUNC, 0644);

        if (file_out < 0)
        {
            printf("tosemu: MIDI, %s could not be written to (%s)\n",
                   comma + 1, strerror(errno));
            return 0;
        }
    }

    /* Only somewhere to send to, which is the common way to use this */
    if (in_len == 0)
        return 1;

    in_name = malloc(in_len + 1);
    if (!in_name)
        return 0;

    memcpy(in_name, spec, in_len);
    in_name[in_len] = 0;

    in = open(in_name, O_RDONLY);

    if (in < 0)
    {
        printf("tosemu: MIDI, %s could not be read (%s)\n",
               in_name, strerror(errno));
        free(in_name);
        return 0;
    }

    free(in_name);

    for (;;)
    {
        uint8_t buffer[512];
        ssize_t got = read(in, buffer, sizeof buffer);
        int i;

        if (got <= 0)
            break;

        for (i = 0; i < got; i++)
            if (!ring_push(&inbound, buffer[i]))
            {
                said("more was waiting to be read than the port can hold");
                break;
            }
    }

    close(in);

    return 1;
}

static void file_close(void)
{
    if (file_out >= 0)
        close(file_out);

    file_out = -1;
}

static void file_forget(void)
{
    /* The parent is still writing its own copy of this, and a child that
     * closed the descriptor would be closing only its own - but one that went
     * on writing would interleave with the parent. So let go without tidying */
    file_out = -1;
}

static int file_fd(void)
{
    return -1;
}

static int file_read(uint8_t *into, int room)
{
    (void)into;
    (void)room;

    /* Everything there was to say was said when the port was opened */
    return 0;
}

static int file_write(const uint8_t *from, int n)
{
    ssize_t put;

    if (file_out < 0)
        return n; /* Asked for input only, so sending is a thing that works */

    put = write(file_out, from, (size_t)n);

    return put < 0 ? 0 : (int)put;
}

static const struct midi_backend file_backend = {
    "file:", file_open, file_close, file_forget, file_fd, file_read, file_write
};

#ifdef HAVE_ALSA

/* The raw device ***********************************************************/

/*
 * hw:1,0,0 - the interface itself, as a stream of bytes in each direction.
 *
 * This is the faithful one. An ST's MIDI port was a serial line and so is
 * this, so running status arrives as the program sent it, real time bytes
 * interleave where they were put, and a system exclusive dump of any length
 * goes through because nothing along the way is trying to understand it.
 */
static snd_rawmidi_t *raw_in;
static snd_rawmidi_t *raw_out;
static int raw_fd = -1;

static int raw_open(const char *spelling)
{
    const char *spec = spelling;
    struct pollfd fds[8];
    int count;
    int err;
    int i;

    if (strncmp(spec, "rawmidi:", strlen("rawmidi:")) == 0)
        spec += strlen("rawmidi:");

    err = snd_rawmidi_open(&raw_in, &raw_out, spec, SND_RAWMIDI_NONBLOCK);

    if (err < 0)
    {
        printf("tosemu: MIDI, %s could not be opened (%s)\n",
               spec, snd_strerror(err));

        if (err == -EBUSY)
            printf("tosemu: something else has it - a raw device is one "
                   "program at a time\n");

        raw_in = 0;
        raw_out = 0;
        return 0;
    }

    /* The descriptor to sleep on, which is the input side's. ALSA may offer
     * several and only the readable one ends a wait for a byte. */
    count = snd_rawmidi_poll_descriptors_count(raw_in);

    if (count > (int)(sizeof fds / sizeof fds[0]))
        count = (int)(sizeof fds / sizeof fds[0]);

    if (count > 0 && snd_rawmidi_poll_descriptors(raw_in, fds, count) > 0)
        for (i = 0; i < count; i++)
            if (fds[i].events & POLLIN)
            {
                raw_fd = fds[i].fd;
                break;
            }

    return 1;
}

static void raw_close(void)
{
    if (raw_in)
        snd_rawmidi_close(raw_in);
    if (raw_out)
    {
        snd_rawmidi_drain(raw_out);
        snd_rawmidi_close(raw_out);
    }

    raw_in = 0;
    raw_out = 0;
    raw_fd = -1;
}

static void raw_forget(void)
{
    /* Not snd_rawmidi_close: the handle is a copy of the parent's and the
     * device behind it is the parent's too. Dropping it is all a child may do */
    raw_in = 0;
    raw_out = 0;
    raw_fd = -1;
}

static int raw_fd_of(void)
{
    return raw_fd;
}

static int raw_read(uint8_t *into, int room)
{
    ssize_t got;

    if (!raw_in)
        return -1;

    got = snd_rawmidi_read(raw_in, into, (size_t)room);

    if (got == -EAGAIN)
        return 0;

    if (got < 0)
    {
        printf("tosemu: MIDI, the port stopped answering (%s)\n",
               snd_strerror((int)got));
        return -1;
    }

    return (int)got;
}

static int raw_write(const uint8_t *from, int n)
{
    ssize_t put;

    if (!raw_out)
        return 0;

    put = snd_rawmidi_write(raw_out, from, (size_t)n);

    if (put == -EAGAIN)
        return 0;

    if (put < 0)
    {
        said("the port stopped accepting what was sent to it");
        return 0;
    }

    return (int)put;
}

static const struct midi_backend raw_backend = {
    "hw:", raw_open, raw_close, raw_forget, raw_fd_of, raw_read, raw_write
};

static const struct midi_backend rawmidi_backend = {
    "rawmidi:", raw_open, raw_close, raw_forget, raw_fd_of, raw_read, raw_write
};

/* The sequencer ************************************************************/

/*
 * seq:20:0, or a port named rather than numbered, or nothing at all for a port
 * that somebody else connects with aconnect.
 *
 * The sequencer deals in events where the machine deals in bytes, so both
 * directions go through one of ALSA's own parsers. Keeping the same parser for
 * the life of the port is what makes a message split across two writes work:
 * the half finished state lives in the parser rather than in anything here.
 */
static snd_seq_t *seq;
static int seq_port = -1;
static snd_midi_event_t *encoder;
static snd_midi_event_t *decoder;
static int seq_fd = -1;

/* Long enough for the system exclusive a patch editor sends in one go. Past
 * it ALSA splits the message and puts it back together at the far end. */
#define SEQ_CHUNK (1024)

static int seq_open(const char *spelling)
{
    const char *spec = spelling;
    struct pollfd fds[8];
    int count;
    int err;
    int i;

    if (strncmp(spec, "seq:", strlen("seq:")) == 0)
        spec += strlen("seq:");

    err = snd_seq_open(&seq, "default", SND_SEQ_OPEN_DUPLEX, SND_SEQ_NONBLOCK);

    if (err < 0)
    {
        printf("tosemu: MIDI, the sequencer could not be reached (%s)\n",
               snd_strerror(err));
        seq = 0;
        return 0;
    }

    snd_seq_set_client_name(seq, "tosemu");

    seq_port = snd_seq_create_simple_port(seq, "MIDI",
                                          SND_SEQ_PORT_CAP_READ |
                                          SND_SEQ_PORT_CAP_WRITE |
                                          SND_SEQ_PORT_CAP_SUBS_READ |
                                          SND_SEQ_PORT_CAP_SUBS_WRITE,
                                          SND_SEQ_PORT_TYPE_MIDI_GENERIC |
                                          SND_SEQ_PORT_TYPE_APPLICATION);

    if (seq_port < 0)
    {
        printf("tosemu: MIDI, no port could be made (%s)\n",
               snd_strerror(seq_port));
        snd_seq_close(seq);
        seq = 0;
        return 0;
    }

    if (snd_midi_event_new(SEQ_CHUNK, &encoder) < 0
        || snd_midi_event_new(SEQ_CHUNK, &decoder) < 0)
    {
        printf("tosemu: MIDI, there was no room for the parsers\n");
        return 0;
    }

    /*
     * The decoder must not put running status back in. A program that resets
     * its own idea of running status - which one does around a system
     * exclusive - would read the next event's data bytes as a continuation of
     * whatever came before it, and the first note after a dump would be wrong.
     */
    snd_midi_event_no_status(decoder, 1);

    /* A spelling connects; an empty one leaves the port for somebody to patch */
    if (*spec)
    {
        snd_seq_addr_t addr;

        /*
         * A port that was named and is not there is a mistake rather than
         * something to carry on past. Leaving an unconnected port behind would
         * look exactly like a working one - the program runs, the notes go
         * nowhere - and the person would have no way to tell which of the two
         * they had. An unconnected port is what the empty spelling is for.
         */
        if (snd_seq_parse_address(seq, &addr, spec) < 0)
        {
            printf("tosemu: MIDI, there is no port called %s - aconnect -l "
                   "lists the ones there are, and seq: on its own makes one "
                   "for something else to connect to\n", spec);
            return 0;
        }

        if (snd_seq_connect_to(seq, seq_port, addr.client, addr.port) < 0)
            printf("tosemu: MIDI, nothing could be sent to %s\n", spec);
        if (snd_seq_connect_from(seq, seq_port, addr.client, addr.port) < 0)
            printf("tosemu: MIDI, nothing can arrive from %s\n", spec);
    }

    count = snd_seq_poll_descriptors_count(seq, POLLIN);

    if (count > (int)(sizeof fds / sizeof fds[0]))
        count = (int)(sizeof fds / sizeof fds[0]);

    if (count > 0 && snd_seq_poll_descriptors(seq, fds, count, POLLIN) > 0)
        for (i = 0; i < count; i++)
            if (fds[i].events & POLLIN)
            {
                seq_fd = fds[i].fd;
                break;
            }

    return 1;
}

static void seq_close(void)
{
    if (encoder)
        snd_midi_event_free(encoder);
    if (decoder)
        snd_midi_event_free(decoder);
    if (seq)
        snd_seq_close(seq);

    encoder = 0;
    decoder = 0;
    seq = 0;
    seq_port = -1;
    seq_fd = -1;
}

static void seq_forget(void)
{
    /*
     * Emphatically not snd_seq_close. The client and the port belong to the
     * parent, and closing them from a child would take the parent's port away
     * from under it - everything connected to it would be disconnected by a
     * program the person never started.
     */
    encoder = 0;
    decoder = 0;
    seq = 0;
    seq_port = -1;
    seq_fd = -1;
}

static int seq_fd_of(void)
{
    return seq_fd;
}

static int seq_read(uint8_t *into, int room)
{
    int taken = 0;

    if (!seq || !decoder)
        return -1;

    while (taken < room)
    {
        snd_seq_event_t *ev;
        long got;
        int err = snd_seq_event_input(seq, &ev);

        if (err == -EAGAIN || err == -ENOSPC)
        {
            if (err == -ENOSPC)
            {
                /* More arrived than ALSA had room to keep. What is in the
                 * parser is half of a message nobody will finish. */
                snd_midi_event_reset_decode(decoder);
                said("bytes arrived faster than they could be read, and some "
                     "were lost");
            }
            break;
        }

        if (err < 0)
            break;

        got = snd_midi_event_decode(decoder, into + taken,
                                    (long)(room - taken), ev);

        /* Not everything the sequencer carries is bytes on a wire - a port
         * being connected is an event too, and decoding one is no bytes */
        if (got > 0)
            taken += (int)got;
    }

    return taken;
}

static int seq_write(const uint8_t *from, int n)
{
    int i;

    if (!seq || !encoder)
        return 0;

    for (i = 0; i < n; i++)
    {
        snd_seq_event_t ev;

        snd_seq_ev_clear(&ev);

        /*
         * A byte at a time, and the encoder says when it has a whole event.
         * Zero back is a message that is not finished yet rather than a
         * failure, which is the state this would otherwise have to keep.
         */
        if (snd_midi_event_encode_byte(encoder, from[i], &ev) != 1)
            continue;

        snd_seq_ev_set_source(&ev, seq_port);
        snd_seq_ev_set_subs(&ev);

        /*
         * Now rather than at a time of the sequencer's choosing. There is no
         * queue here to schedule against: the emulated machine's sense of when
         * a note happens is the host's clock, so the moment it says to send
         * one is the moment it should go.
         */
        snd_seq_ev_set_direct(&ev);

        if (snd_seq_event_output(seq, &ev) < 0)
            said("the sequencer would not take what was sent");
    }

    if (snd_seq_drain_output(seq) < 0)
        said("the sequencer would not take what was sent");

    return n;
}

static const struct midi_backend seq_backend = {
    "seq:", seq_open, seq_close, seq_forget, seq_fd_of, seq_read, seq_write
};

#endif /* HAVE_ALSA */

/*
 * Which kind of port it is has to be said, and a spelling that says none is
 * refused rather than guessed at.
 *
 * Guessing was tried and it is worse. A sequencer port named rather than
 * numbered has nothing in its spelling to mark it as one, so anything not
 * recognised would have to be treated as a port name - and then a mistyped
 * hw: becomes a sequencer port that connects to nothing, which looks from the
 * outside exactly like a working port with a silent synthesiser on the end of
 * it. Four more characters buys an error message that says what is wrong.
 */
static const struct midi_backend *const backends[] = {
#ifdef HAVE_ALSA
    &rawmidi_backend,
    &raw_backend,
    &seq_backend,
#endif
    &file_backend,
};

#define BACKENDS (int)(sizeof backends / sizeof backends[0])

/* The port *****************************************************************/

static const struct midi_backend *port;
static const char *opened_as;

int midi_open(void)
{
    const char *said_so = setting("TOSEMU_MIDI");
    int i;

    if (!said_so || !*said_so)
        return 0;

    for (i = 0; i < BACKENDS; i++)
    {
        const struct midi_backend *b = backends[i];

        if (strncmp(said_so, b->prefix, strlen(b->prefix)) != 0)
            continue;

        opened_as = said_so;

        if (!b->open(said_so))
        {
            /* Whatever it managed to open before it gave up */
            b->close();
            opened_as = 0;
            return 0;
        }

        port = b;
        return 1;
    }

    /*
     * Nothing matched, which on a build without ALSA is the ordinary case
     * rather than a mistake: hw: and seq: are spellings this tosemu has no way
     * to honour. Say which it is, because "no MIDI" looks the same either way.
     */
#ifdef HAVE_ALSA
    printf("tosemu: MIDI, %s is not a port this understands - try hw:1,0,0, "
           "seq:20:0 or file:sent.bin\n", said_so);
#else
    printf("tosemu: MIDI, this tosemu was built without ALSA, so %s cannot be "
           "opened - file:sent.bin still works\n", said_so);
#endif

    return 0;
}

int midi_wanted(void)
{
    return port != 0;
}

int midi_asked_for(void)
{
    const char *said_so = setting("TOSEMU_MIDI");

    return said_so && *said_so;
}

int midi_fd(void)
{
    return port ? port->fd() : -1;
}

void midi_pump(void)
{
    uint8_t buffer[256];
    int n;

    if (!port)
        return;

    /* Out first. A program that wrote something and then waited should have it
     * on its way before the wait, rather than after whatever wakes it. */
    n = 0;
    while (n < (int)sizeof buffer && ring_take(&outbound, &buffer[n]))
        n++;

    if (n > 0)
    {
        int put = port->write(buffer, n);

        /*
         * What the host would not take goes back where it came from, at the
         * front, so that the bytes stay in the order the program wrote them.
         * Pushing to the tail would put the rest of a message in front of its
         * own beginning.
         */
        while (put < n)
        {
            n--;
            if (!ring_push(&outbound, buffer[n]))
                break;
        }
    }

    for (;;)
    {
        int got = port->read(buffer, (int)sizeof buffer);
        int i;

        if (got < 0)
        {
            /* The port has gone. Saying so once beats saying so per byte for
             * the rest of the run. */
            said("the port is no longer there");
            port->forget();
            port = 0;
            return;
        }

        if (got == 0)
            break;

        for (i = 0; i < got; i++)
            if (!ring_push(&inbound, buffer[i]))
            {
                said("more arrived than the machine was reading");
                break;
            }

        if (got < (int)sizeof buffer)
            break;
    }
}

int midi_take(uint8_t *byte)
{
    return ring_take(&inbound, byte);
}

int midi_give(uint8_t byte)
{
    if (!port)
        return 1; /* Nowhere to go, which is not the same as having failed */

    if (!ring_push(&outbound, byte))
    {
        said("more was sent than the port could carry, and some was dropped");
        return 0;
    }

    return 1;
}

void midi_reset(void)
{
    ring_empty(&inbound);
    ring_empty(&outbound);

#ifdef HAVE_ALSA
    if (encoder)
        snd_midi_event_reset_encode(encoder);
    if (decoder)
        snd_midi_event_reset_decode(decoder);
#endif
}

void midi_forget(void)
{
    ring_empty(&inbound);
    ring_empty(&outbound);

    if (port)
        port->forget();

    port = 0;
    opened_as = 0;
}

void midi_close(void)
{
    if (port)
    {
        midi_pump();
        port->close();
    }

    port = 0;
    opened_as = 0;

    ring_empty(&inbound);
    ring_empty(&outbound);
}

const char *midi_named(void)
{
    return opened_as ? opened_as : "";
}
