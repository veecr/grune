/*
 * Copyright (c) 2013 Stefano Sabatini
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

/**
 * @file
 * libavformat/libavcodec demuxing and muxing API example.
 *
 * Remux streams from one container format to another.
 * @example remuxing.c
 */

#include <libavutil/timestamp.h>
#include <libavutil/opt.h>
#include <libavformat/avformat.h>

//int64_t seekFunction(void *opaque, int64_t offset, int whence);

static void log_packet(const AVFormatContext *fmt_ctx, const AVPacket *pkt, const char *tag)
{
    AVRational *time_base = &fmt_ctx->streams[pkt->stream_index]->time_base;

    printf("%s: pts:%s pts_time:%s dts:%s dts_time:%s duration:%s duration_time:%s stream_index:%d tnum:%d tden:%d\n",
           tag,
           av_ts2str(pkt->pts), av_ts2timestr(pkt->pts, time_base),
           av_ts2str(pkt->dts), av_ts2timestr(pkt->dts, time_base),
           av_ts2str(pkt->duration), av_ts2timestr(pkt->duration, time_base),
           pkt->stream_index,
           time_base->num,
           time_base->den);
}

typedef struct _StreamTimings {
    int ptsOffset;
    int dtsOffset;
} StreamTimings;

typedef struct _ConcatContext {
    AVFormatContext *ofmt_ctx;
    StreamTimings lastTimings[2];
} ConcatContext;

static void copy_input_to_output(ConcatContext *conCtx, AVFormatContext *ifmt_ctx, AVFormatContext *ofmt_ctx)
{
    StreamTimings streamTimings[2];

    while (1) {
        AVStream *in_stream, *out_stream;
        AVPacket pkt;
        StreamTimings lastTimings;
        int ret, si;

        ret = av_read_frame(ifmt_ctx, &pkt);
        if (ret < 0)
            break;

        si = pkt.stream_index;
        in_stream  = ifmt_ctx->streams[si];
        out_stream = ofmt_ctx->streams[si];
        lastTimings = conCtx->lastTimings[si];

        //log_packet(ifmt_ctx, &pkt, "in");

        pkt.pts = lastTimings.ptsOffset + av_rescale_q_rnd(pkt.pts, in_stream->time_base, out_stream->time_base, AV_ROUND_NEAR_INF|AV_ROUND_PASS_MINMAX);
        pkt.dts = lastTimings.dtsOffset + av_rescale_q_rnd(pkt.dts, in_stream->time_base, out_stream->time_base, AV_ROUND_NEAR_INF|AV_ROUND_PASS_MINMAX);
        pkt.duration = av_rescale_q(pkt.duration, in_stream->time_base, out_stream->time_base);
        pkt.pos = -1;

        //log_packet(ofmt_ctx, &pkt, "out");

        streamTimings[si].ptsOffset = pkt.pts + pkt.duration;
        streamTimings[si].dtsOffset = pkt.dts + pkt.duration;

        ret = av_interleaved_write_frame(ofmt_ctx, &pkt);
        if (ret < 0) {
            fprintf(stderr, "Error muxing packet\n");
            break;
        }

        av_free_packet(&pkt);
    }

    conCtx->lastTimings[0] = streamTimings[0];
    conCtx->lastTimings[1] = streamTimings[1];
}

int createStream(int numFiles, char **filenames, void* opaque, int(*writeFunction)(void *opaque, uint8_t *buf, int buf_size))
{
    AVOutputFormat *ofmt = NULL;
    AVFormatContext *ifmt_ctx = NULL, *ofmt_ctx = NULL;
    const char *in_filename;
    int ret, i, filenameIndex = 0;
    int64_t durationOffset = 0;
    unsigned char* buf = 0;
    
    buf = av_malloc(8192);
    if (buf == NULL) {
        goto end;
    }

    in_filename  = filenames[filenameIndex];

    av_register_all();

    if ((ret = avformat_open_input(&ifmt_ctx, in_filename, 0, 0)) < 0) {
        fprintf(stderr, "Could not open input file '%s'", in_filename);
        goto end;
    }

    if ((ret = avformat_find_stream_info(ifmt_ctx, 0)) < 0) {
        fprintf(stderr, "Failed to retrieve input stream information");
        goto end;
    }

    av_dump_format(ifmt_ctx, 0, in_filename, 0);

    avformat_alloc_output_context2(&ofmt_ctx, NULL, "mp4", NULL);
    if (!ofmt_ctx) {
        fprintf(stderr, "Could not create output context\n");
        ret = AVERROR_UNKNOWN;
        goto end;
    }
    
    ofmt = ofmt_ctx->oformat;

    for (i = 0; i < ifmt_ctx->nb_streams; i++) {
        AVStream *in_stream = ifmt_ctx->streams[i];
        AVStream *out_stream = avformat_new_stream(ofmt_ctx, in_stream->codec->codec);
        if (!out_stream) {
            fprintf(stderr, "Failed allocating output stream\n");
            ret = AVERROR_UNKNOWN;
            goto end;
        }

        ret = avcodec_copy_context(out_stream->codec, in_stream->codec);
        if (ret < 0) {
            fprintf(stderr, "Failed to copy context from input to output stream codec context\n");
            goto end;
        }
        out_stream->codec->codec_tag = 0;
        if (ofmt_ctx->oformat->flags & AVFMT_GLOBALHEADER)
            out_stream->codec->flags |= CODEC_FLAG_GLOBAL_HEADER;

        out_stream->codec->time_base = in_stream->time_base;
    }

    ret = av_opt_set(ofmt_ctx, "movflags", "frag_keyframe", AV_OPT_SEARCH_CHILDREN);
    if (ret < 0) {
        fprintf(stderr, "Failed to set fragmentation option\n");
        goto end;
    }

    av_dump_format(ofmt_ctx, 0, NULL, 1);

    //if (!(ofmt->flags & AVFMT_NOFILE)) {
        ofmt_ctx->pb = avio_alloc_context(buf, 8192, 1, opaque, 0, writeFunction, 0);
        if (ofmt_ctx->pb == NULL) {
            fprintf(stderr, "Could not create output buffer.");
            goto end;
        }
    //}

    ret = avformat_write_header(ofmt_ctx, NULL);
    if (ret < 0) {
        fprintf(stderr, "Error occurred when opening output file\n");
        goto end;
    }

    ConcatContext conCtx;
    memset(&conCtx, 0, sizeof(ConcatContext));

    copy_input_to_output(&conCtx, ifmt_ctx, ofmt_ctx);

    while (++filenameIndex < numFiles) {
        avformat_close_input(&ifmt_ctx);

        in_filename = filenames[filenameIndex];
        if ((ret = avformat_open_input(&ifmt_ctx, in_filename, 0, 0)) < 0) {
            fprintf(stderr, "Could not open input file '%s'", in_filename);
            goto end;
        }

        if ((ret = avformat_find_stream_info(ifmt_ctx, 0)) < 0) {
            fprintf(stderr, "Failed to retrieve input stream information");
            goto end;
        }

        av_dump_format(ifmt_ctx, 0, in_filename, 0);

        copy_input_to_output(&conCtx, ifmt_ctx, ofmt_ctx);
    }

    av_write_trailer(ofmt_ctx);
end:

    avformat_close_input(&ifmt_ctx);

    /* close output */
    //if (ofmt_ctx && !(ofmt->flags & AVFMT_NOFILE))
    //    avio_close(ofmt_ctx->pb);
    avformat_free_context(ofmt_ctx);

    if (ret < 0 && ret != AVERROR_EOF) {
        fprintf(stderr, "Error occurred: %s\n", av_err2str(ret));
        return 1;
    }

    return 0;
}
