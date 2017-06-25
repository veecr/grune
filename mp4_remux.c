/*
 * Copyright (c) 2014 veecr.
 */

#include "muxer.h"
#include <libavformat/avformat.h>
#include <libavutil/timestamp.h>
#include <libavutil/opt.h>

typedef struct {
    AVStream *st;
    AVFormatContext *ofmt_ctx;
    int64_t next_pts;
} OutputStream;

/*
static void log_packet(AVStream *stream, const AVPacket *pkt, const char *tag)
{
    AVRational *time_base = &stream->time_base;

    printf("%s: pts:%s pts_time:%s dts:%s dts_time:%s duration:%s duration_time:%s stream_index:%d tnum:%d tden:%d\n",
           tag,
           av_ts2str(pkt->pts), av_ts2timestr(pkt->pts, time_base),
           av_ts2str(pkt->dts), av_ts2timestr(pkt->dts, time_base),
           av_ts2str(pkt->duration), av_ts2timestr(pkt->duration, time_base),
           pkt->stream_index,
           time_base->num,
           time_base->den);
}
*/

static int write_packet(AVFormatContext *ifmt_ctx, OutputStream *out_stream) {
    AVStream *in_stream;
    AVPacket pkt, ipkt;
    int ret;

    ret = av_read_frame(ifmt_ctx, &ipkt);
    if (ret < 0) {
        return 0;
    }

    pkt = ipkt;

    in_stream  = ifmt_ctx->streams[pkt.stream_index];

    //log_packet(in_stream, &pkt, "in");

    pkt.pts = av_rescale_q_rnd(pkt.pts, in_stream->time_base, out_stream->st->time_base, AV_ROUND_NEAR_INF|AV_ROUND_PASS_MINMAX);
    pkt.dts = av_rescale_q_rnd(pkt.dts, in_stream->time_base, out_stream->st->time_base, AV_ROUND_NEAR_INF|AV_ROUND_PASS_MINMAX);
    pkt.duration = (int)av_rescale_q(pkt.duration, in_stream->time_base, out_stream->st->time_base);
    pkt.pos = -1;
    pkt.stream_index = out_stream->st->index;

    //log_packet(out_stream->st, &pkt, "out");

    out_stream->next_pts = pkt.pts + pkt.duration;

    ret = av_interleaved_write_frame(out_stream->ofmt_ctx, &pkt);
    if (ret < 0) {
        fprintf(stderr, "Error muxing packet\n");
        return ret;
    }

    av_free_packet(&pkt);

    return 1;
}

static int addStreams(OutputStream* os, AVFormatContext* ofmt_ctx, AVFormatContext* ifmt_ctx) {
    int ret;
    AVStream *in_stream = ifmt_ctx->streams[0];

    os->st = avformat_new_stream(ofmt_ctx, in_stream->codec->codec);
    if (!os->st) {
        fprintf(stderr, "Failed allocating output stream\n");
        return AVERROR_UNKNOWN;
    }

    os->ofmt_ctx = ofmt_ctx;
    
    // Hack: Forcing frame_size to stop warning.
    //if (in_stream->codec->frame_size == 0) {
    //    in_stream->codec->frame_size = 1024;
    //}

    ret = avcodec_copy_context(os->st->codec, in_stream->codec);
    if (ret < 0) {
        fprintf(stderr, "Failed to copy context from input to output stream codec context\n");
        return ret;
    }
    os->st->codec->codec_tag = 0;
    if (ofmt_ctx->oformat->flags & AVFMT_GLOBALHEADER)
        os->st->codec->flags |= CODEC_FLAG_GLOBAL_HEADER;

    os->st->time_base = in_stream->codec->time_base;
    
    return 0;
}

int Mp4RemuxToFragmented(
        char const* filePath,
        void* wopaque, BufferCallback writeFunction,
        SeekCallback seekFunction)
{
    int ret = 0;
    AVFormatContext *ifmt_ctx = 0, *ofmt_ctx = 0;
    unsigned char *ibuf, *obuf;
    OutputStream stream = { 0 };
    int more = 1;
    
    // Open output.
    obuf = av_malloc(8192);
    if (obuf == NULL) {
        goto end;
    }

    avformat_alloc_output_context2(&ofmt_ctx, NULL, "mp4", 0);
    if (!ofmt_ctx) {
        fprintf(stderr, "Could not create output context\n");
        ret = AVERROR_UNKNOWN;
        goto end;
    }
    
    ret = av_opt_set(ofmt_ctx, "movflags", "frag_keyframe+empty_moov+omit_tfhd_offset", AV_OPT_SEARCH_CHILDREN);
    if (ret < 0) {
        fprintf(stderr, "Failed to set fragmentation and empty moov option.\n");
        goto end;
    }

    ofmt_ctx->pb = avio_alloc_context(obuf, 8192, 1, wopaque, 0, writeFunction, 0);
    if (ofmt_ctx->pb == NULL) {
        fprintf(stderr, "Could not create output buffer.");
        goto end;
    }

    // Open input.
    ibuf = av_malloc(8192);
    if (ibuf == NULL) {
        goto end;
    }

    if ((ret = avformat_open_input(&ifmt_ctx, filePath, 0, 0)) < 0) {
        fprintf(stderr, "Could not open input.");
        goto end;
    }

    if ((ret = avformat_find_stream_info(ifmt_ctx, 0)) < 0) {
        fprintf(stderr, "Failed to retrieve input stream information");
        goto end;
    }

    ret = addStreams(&stream, ofmt_ctx, ifmt_ctx);
    if (ret < 0) {
        fprintf(stderr, "Error occurred when adding video streams.\n");
        goto end;
    }

    ret = avformat_write_header(ofmt_ctx, NULL);
    if (ret < 0) {
        fprintf(stderr, "Error occurred when opening output file\n");
        goto end;
    }

    // Dump formats.
    //av_dump_format(vifmt_ctx, 0, 0, 0);
    //av_dump_format(aifmt_ctx, 0, 0, 0);
    //av_dump_format(ofmt_ctx, 0, NULL, 1);

    while (more) {
        more = write_packet(ifmt_ctx, &stream);
    }

    av_write_trailer(ofmt_ctx);

end:
    avformat_free_context(ifmt_ctx);
    avformat_free_context(ofmt_ctx);

    if (ret < 0 && ret != AVERROR_EOF) {
        fprintf(stderr, "Error occurred: %s\n", av_err2str(ret));
        return 1;
    }

    return 0;
}
