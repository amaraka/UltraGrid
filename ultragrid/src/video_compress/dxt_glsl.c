/*
 * FILE:    dxt_glsl_compress.c
 * AUTHORS: Martin Benes     <martinbenesh@gmail.com>
 *          Lukas Hejtmanek  <xhejtman@ics.muni.cz>
 *          Petr Holub       <hopet@ics.muni.cz>
 *          Milos Liska      <xliska@fi.muni.cz>
 *          Jiri Matela      <matela@ics.muni.cz>
 *          Dalibor Matura   <255899@mail.muni.cz>
 *          Ian Wesley-Smith <iwsmith@cct.lsu.edu>
 *
 * Copyright (c) 2005-2011 CESNET z.s.p.o.
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
 *      This product includes software developed by CESNET z.s.p.o.
 * 
 * 4. Neither the name of the CESNET nor the names of its contributors may be
 *    used to endorse or promote products derived from this software without
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

#include "config.h"
#include "config_unix.h"
#include "debug.h"
#include "x11_common.h"
#include "video_compress/dxt_glsl.h"
#include "dxt_compress/dxt_encoder.h"
#include "compat/platform_semaphore.h"
#include "video_codec.h"
#include <pthread.h>
#include <stdlib.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <GL/glew.h>
#include <GL/gl.h>
#include <GL/glx.h>
#include "x11_common.h"

struct video_compress {
        struct dxt_encoder *encoder;

        struct video_frame *out;
        decoder_t decoder;
        char *decoded;
        unsigned int configured:1;
        unsigned int interlaced_input:1;
        codec_t color_spec;
};

static void configure_with(struct video_compress *s, struct video_frame *frame);

static void configure_with(struct video_compress *s, struct video_frame *frame)
{
        int i;
        int x, y;
	int h_align = 0;
        enum dxt_format format;
        
        assert(tile_get(frame, 0, 0)->width % 4 == 0 && tile_get(frame, 0, 0)->height % 4 == 0);
        s->out = vf_alloc(frame->grid_width, frame->grid_height);
        
        for (x = 0; x < frame->grid_width; ++x) {
                for (y = 0; y < frame->grid_height; ++y) {
                        if (tile_get(frame, x, y)->width != tile_get(frame, 0, 0)->width ||
                                        tile_get(frame, x, y)->width != tile_get(frame, 0, 0)->width)
                                error_with_code_msg(128,"[RTDXT] Requested to compress tiles of different size!");
                                
                        tile_get(s->out, x, y)->width = tile_get(frame, 0, 0)->width;
                        tile_get(s->out, x, y)->height = tile_get(frame, 0, 0)->height;
                }
        }
        
        glx_init();
        
        s->out->aux = frame->aux;
        s->out->fps = frame->fps;
        s->out->color_spec = s->color_spec;

        switch (frame->color_spec) {
                case RGB:
                        s->decoder = (decoder_t) memcpy;
                        format = DXT_FORMAT_RGB;
                        break;
                case RGBA:
                        s->decoder = (decoder_t) memcpy;
                        format = DXT_FORMAT_RGBA;
                        break;
                case R10k:
                        s->decoder = (decoder_t) vc_copyliner10k;
                        format = DXT_FORMAT_RGBA;
                        break;
                case UYVY:
                case Vuy2:
                case DVS8:
                        s->decoder = (decoder_t) memcpy;
                        format = DXT_FORMAT_YUV422;
                        break;
                case v210:
                        s->decoder = (decoder_t) vc_copylinev210;
                        format = DXT_FORMAT_YUV422;
                        break;
                case DVS10:
                        s->decoder = (decoder_t) vc_copylineDVS10;
                        format = DXT_FORMAT_YUV422;
                        break;
                case DXT1:
                case DXT5:
                        fprintf(stderr, "Input frame is already comperssed!");
                        exit(128);
                default:
                        fprintf(stderr, "Unknown codec: %d\n", frame->color_spec);
                        exit(128);
        }
        
        

        /* We will deinterlace the output frame */
        if(s->out->aux & AUX_INTERLACED)
                s->interlaced_input = TRUE;
        else
                s->interlaced_input = FALSE;
        s->out->aux &= ~AUX_INTERLACED;
        
        if(s->out->color_spec == DXT1) {
                s->encoder = dxt_encoder_create(DXT_TYPE_DXT1, s->out->tiles[0].width, s->out->tiles[0].height, format);
                s->out->aux |= AUX_RGB;
                s->out->tiles[0].data_len = s->out->tiles[0].width * s->out->tiles[0].height / 2;
        } else if(s->out->color_spec == DXT5){
                s->encoder = dxt_encoder_create(DXT_TYPE_DXT5_YCOCG, s->out->tiles[0].width, s->out->tiles[0].height, format);
                s->out->aux |= AUX_YUV; /* YCoCg */
                s->out->tiles[0].data_len = s->out->tiles[0].width * s->out->tiles[0].height;
        }
        
        for (x = 0; x < frame->grid_width; ++x) {
                for (y = 0; y < frame->grid_height; ++y) {
                        tile_get(s->out, x, y)->linesize = s->out->tiles[0].width;
                        switch(format) { 
                                case DXT_FORMAT_RGBA:
                                        tile_get(s->out, x, y)->linesize *= 4;
                                        break;
                                case DXT_FORMAT_RGB:
                                        tile_get(s->out, x, y)->linesize *= 3;
                                        break;
                                case DXT_FORMAT_YUV422:
                                        tile_get(s->out, x, y)->linesize *= 2;
                                        break;
                        }
                        tile_get(s->out, x, y)->data_len = s->out->tiles[0].data_len;
                        tile_get(s->out, x, y)->data = (char *) malloc(s->out->tiles[0].data_len);
                }
        }
        
        if(!s->encoder) {
                fprintf(stderr, "[DXT GLSL] Failed to create encoder.\n");
                exit(128);
        }
        
        s->decoded = malloc(4 * s->out->tiles[0].width * s->out->tiles[0].height);
        
        s->configured = TRUE;
}

void * dxt_glsl_compress_init(char * opts)
{
        struct video_compress *s;
        
        s = (struct video_compress *) malloc(sizeof(struct video_compress));
        s->out = NULL;
        s->decoded = NULL;
        
        x11_enter_thread();
        
        if(opts && strcmp(opts, "help") == 0) {
                printf("DXT GLSL comperssion usage:\n");
                printf("\t-cg:DXT1\n");
                printf("\t\tcompress with DXT1\n");
                printf("\t-cg:DXT5\n");
                printf("\t\tcompress with DXT5 YCoCg\n");
                return NULL;
        }
        
        if(opts) {
                if(strcasecmp(opts, "DXT5") == 0) {
                        s->color_spec = DXT5;
                } else if(strcasecmp(opts, "DXT1") == 0) {
                        s->color_spec = DXT1;
                } else {
                        fprintf(stderr, "Unknown compression : %s\n", opts);
                        return NULL;
                }
        } else {
                s->color_spec = DXT1;
        }
                
        s->configured = FALSE;

        return s;
}

struct video_frame * dxt_glsl_compress(void *arg, struct video_frame * tx)
{
        struct video_compress *s = (struct video_compress *) arg;
        int i;
        unsigned char *line1, *line2;
        
        int x, y;
        
        if(!s->configured)
                configure_with(s, tx);

        for (x = 0; x < tx->grid_width;  ++x) {
                for (y = 0; y < tx->grid_height;  ++y) {
                        struct tile *in_tile = tile_get(tx, x, y);
                        struct tile *out_tile = tile_get(s->out, x, y);
                        
                        line1 = (unsigned char *) in_tile->data;
                        line2 = (unsigned char *) s->decoded;
                        
                        for (i = 0; i < (int) in_tile->height; ++i) {
                                s->decoder(line2, line1, out_tile->linesize,
                                                0, 8, 16);
                                line1 += vc_get_linesize(in_tile->width, tx->color_spec);
                                line2 += out_tile->linesize;
                        }
                        
                        if(s->interlaced_input)
                                vc_deinterlace((unsigned char *) s->decoded, out_tile->linesize,
                                                out_tile->height);
                        
                        dxt_encoder_compress(s->encoder,
                                        (unsigned char *) s->decoded,
                                        (unsigned char *) out_tile->data);
                }
        }
        
        return s->out;
}

void dxt_glsl_compress_done(void *arg)
{
        struct video_compress *s = (struct video_compress *) arg;
        
        free(s->out->tiles[0].data);
        vf_free(s->out);
        free(s);
}