/*
 * FILE:    ihdtv.c
 * AUTHORS: Colin Perkins    <csp@csperkins.org>
 *          Ladan Gharai     <ladan@isi.edu>
 *          Martin Benes     <martinbenesh@gmail.com>
 *          Lukas Hejtmanek  <xhejtman@ics.muni.cz>
 *          Petr Holub       <hopet@ics.muni.cz>
 *          Milos Liska      <xliska@fi.muni.cz>
 *          Jiri Matela      <matela@ics.muni.cz>
 *          Dalibor Matura   <255899@mail.muni.cz>
 *          Ian Wesley-Smith <iwsmith@cct.lsu.edu>
 *
 * Copyright (c) 2005-2010 CESNET z.s.p.o.
 * Copyright (c) 2001-2004 University of Southern California
 * Copyright (c) 2003-2004 University of Glasgow
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, is permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *
 *      This product includes software developed by the University of Southern
 *      California Information Sciences Institute. This product also includes
 *      software developed by CESNET z.s.p.o.
 *
 * 4. Neither the name of the University nor of the Institute may be used
 *    to endorse or promote products derived from this software without
 *    specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESSED OR IMPLIED WARRANTIES, INCLUDING,
 * BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO
 * EVENT SHALL THE AUTHORS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#include "config_unix.h"
#include "config_win32.h"
#endif // HAVE_CONFIG_H

#include "debug.h"
#include "host.h"
#include "ihdtv.h"
#include "ihdtv/ihdtv.h"
#include "sender.h"
#include "video_display.h"
#include "video_capture.h"

struct ihdtv_state {
#ifdef HAVE_IHDTV
        ihdtv_connection tx_connection, rx_connection;
        struct display *display_device;
#endif
};

static void ihdtv_done(void *state);
static void ihdtv_send_frame(void *state, struct video_frame *tx_frame);
static void *ihdtv_receiver_thread(void *arg);

struct rx_tx ihdtv_rxtx = {
        IHDTV,
        "iHDTV",
        ihdtv_send_frame,
        ihdtv_done,
        ihdtv_receiver_thread
};


static void ihdtv_send_frame(void *state, struct video_frame *tx_frame)
{
#ifdef HAVE_IHDTV
        struct ihdtv_state *s = state;
        ihdtv_send(&s->tx_connection, tx_frame, 9000000);      // FIXME: fix the use of frame size!!
#else // IHDTV
        UNUSED(state);
        UNUSED(tx_frame);
#endif // IHDTV
}

static void *ihdtv_receiver_thread(void *arg)
{
#ifdef HAVE_IHDTV
        struct ihdtv_state *s = (struct ihdtv_state *) arg;
        ihdtv_connection *connection = &s->rx_connection;
        struct display *display_device = s->display_device;

        struct video_frame *frame_buffer = NULL;

        while (!should_exit_receiver) {
                frame_buffer = display_get_frame(display_device);
                if (ihdtv_receive
                    (connection, frame_buffer->tiles[0].data, frame_buffer->tiles[0].data_len))
                        return 0;       // we've got some error. probably empty buffer
                display_put_frame(display_device, frame_buffer, PUTF_BLOCKING);
        }
        return NULL;
#else // IHDTV
        UNUSED(arg);
        return NULL;
#endif // IHDTV
}

#if 0
static void *ihdtv_sender_thread(void *arg)
{
        ihdtv_connection *connection = (ihdtv_connection *) ((void **)arg)[0];
        struct vidcap *capture_device = (struct vidcap *)((void **)arg)[1];
        struct video_frame *tx_frame;
        struct audio_frame *audio;

        while (1) {
                if ((tx_frame = vidcap_grab(capture_device, &audio)) != NULL) {
                        ihdtv_send(connection, tx_frame, 9000000);      // FIXME: fix the use of frame size!!
                        free(tx_frame);
                } else {
                        fprintf(stderr,
                                "Error receiving frame from capture device\n");
                        return 0;
                }
        }

        return 0;
}
#endif

struct ihdtv_state *initialize_ihdtv(struct vidcap *capture_device, struct display *display_device,
                int requested_mtu, int argc, char **argv)
{
#ifdef HAVE_IHDTV
        if ((argc != 0) && (argc != 1) && (argc != 2)) {
                return NULL;
        }

        struct ihdtv_state *s = (struct ihdtv_state *)
                malloc(sizeof(struct ihdtv_state));

        printf("Initializing ihdtv protocol\n");

        // we cannot act as both together, because parameter parsing would have to be revamped
        if (capture_device && display_device) {
                printf
                        ("Error: cannot act as both sender and receiver together in ihdtv mode\n");
                return NULL;
        }

        s->display_device = display_device;

        if (requested_mtu == 0)     // mtu not specified on command line
        {
                requested_mtu = 8112;       // not really a mtu, but a video-data payload per packet
        }

        if (display_device) {
                if (ihdtv_init_rx_session
                                (&s->rx_connection, (argc == 0) ? NULL : argv[0],
                                 (argc ==
                                  0) ? NULL : ((argc == 1) ? argv[0] : argv[1]),
                                 3000, 3001, requested_mtu) != 0) {
                        fprintf(stderr,
                                        "Error initializing receiver session\n");
                        return NULL;
                }
        }

        if (capture_device != 0) {
                if (argc == 0) {
                        fprintf(stderr,
                                        "Error: specify the destination address\n");
                        return NULL;
                }

                if (ihdtv_init_tx_session
                                (&s->tx_connection, argv[0],
                                 (argc == 2) ? argv[1] : argv[0],
                                 requested_mtu) != 0) {
                        fprintf(stderr,
                                        "Error initializing sender session\n");
                        return NULL;
                }
        }

        return s;
#else
        UNUSED(capture_device);
        UNUSED(display_device);
        UNUSED(requested_mtu);
        UNUSED(argc);
        UNUSED(argv);
        return NULL;
#endif // HAVE_IHDTV
}

static void ihdtv_done(void *state)
{
        struct ihdtv_state *s = state;
        if(!s)
                return;
        free(s);
}

