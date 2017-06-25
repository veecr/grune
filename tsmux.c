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

static int write_packet(AVBitStreamFilterContext *bsfc, AVFormatContext *ifmt_ctx, OutputStream *out_stream) {
    AVStream *in_stream;
    AVPacket pkt, ipkt;
    int ret, bsfr;

    ret = av_read_frame(ifmt_ctx, &ipkt);
    if (ret < 0) {
        return 0;
    }

    pkt = ipkt;

    if (bsfc != 0) {
        bsfr = av_bitstream_filter_filter(
                bsfc,
                ifmt_ctx->streams[pkt.stream_index]->codec,
                NULL,
                &pkt.data,
                &pkt.size,
                ipkt.data,
                ipkt.size,
                ipkt.flags & AV_PKT_FLAG_KEY);
        if (bsfr < 0) {
            fprintf(stderr, "Error in bitstream filter\n");
            return ret;
        }
    }

    in_stream  = ifmt_ctx->streams[pkt.stream_index];

    //log_packet(in_stream, &pkt, "in");

    pkt.pts = av_rescale_q_rnd(pkt.pts, in_stream->time_base, out_stream->st->time_base, AV_ROUND_NEAR_INF|AV_ROUND_PASS_MINMAX);
    pkt.dts = av_rescale_q_rnd(pkt.dts, in_stream->time_base, out_stream->st->time_base, AV_ROUND_NEAR_INF|AV_ROUND_PASS_MINMAX);
    pkt.duration = av_rescale_q(pkt.duration, in_stream->time_base, out_stream->st->time_base);
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

    ret = avcodec_copy_context(os->st->codec, in_stream->codec);
    if (ret < 0) {
        fprintf(stderr, "Failed to copy context from input to output stream codec context\n");
        return ret;
    }
    os->st->codec->codec_tag = 0;
    if (ofmt_ctx->oformat->flags & AVFMT_GLOBALHEADER)
        os->st->codec->flags |= CODEC_FLAG_GLOBAL_HEADER;

    os->st->time_base = in_stream->codec->time_base;

    // Ripped off from ffmpeg.c
    if (in_stream->codec->time_base.den &&
        av_q2d(in_stream->codec->time_base)*in_stream->codec->ticks_per_frame > av_q2d(in_stream->time_base) &&
        av_q2d(in_stream->time_base) < 1.0/500)
    {
        os->st->time_base = in_stream->codec->time_base;
        os->st->time_base.num *= in_stream->codec->ticks_per_frame;
    }

    os->st->time_base = av_add_q(os->st->time_base, (AVRational){0, 1});

    return 0;
}

int remuxToTs(
        void* aropaque, BufferCallback audioReadFunction,
        void* vropaque, BufferCallback videoReadFunction,
        void* wopaque, BufferCallback writeFunction)
{
    int ret;
    AVFormatContext *aifmt_ctx = 0, *vifmt_ctx = 0, *ofmt_ctx = 0;
    unsigned char *aibuf, *vibuf, *obuf;
    AVBitStreamFilterContext* bsfc = 0;
    OutputStream video_st = { 0 }, audio_st = { 0 };
    int more_audio = 0, more_video = 0;
    
    // Open output.
    obuf = av_malloc(8192);
    if (obuf == NULL) {
        goto end;
    }

    avformat_alloc_output_context2(&ofmt_ctx, NULL, "mpegts", NULL);
    if (!ofmt_ctx) {
        fprintf(stderr, "Could not create output context\n");
        ret = AVERROR_UNKNOWN;
        goto end;
    }
    
    ofmt_ctx->pb = avio_alloc_context(obuf, 8192, 1, wopaque, 0, writeFunction, 0);
    if (ofmt_ctx->pb == NULL) {
        fprintf(stderr, "Could not create output buffer.");
        goto end;
    }

    // Open video input.
    if (vropaque != 0) {
        vibuf = av_malloc(8192);
        if (vibuf == NULL) {
            goto end;
        }

        vifmt_ctx = avformat_alloc_context();
        vifmt_ctx->pb = avio_alloc_context(vibuf, 8192, 0, vropaque, videoReadFunction, 0, 0);
        if (vifmt_ctx->pb == NULL) {
            fprintf(stderr, "Could not create input buffer.");
            goto end;
        }

        if ((ret = avformat_open_input(&vifmt_ctx, "v.mp4", 0, 0)) < 0) {
            fprintf(stderr, "Could not open input.");
            goto end;
        }

        if ((ret = avformat_find_stream_info(vifmt_ctx, 0)) < 0) {
            fprintf(stderr, "Failed to retrieve input stream information");
            goto end;
        }

        ret = addStreams(&video_st, ofmt_ctx, vifmt_ctx);
        if (ret < 0) {
            fprintf(stderr, "Error occurred when adding video streams.\n");
            goto end;
        }

        bsfc = av_bitstream_filter_init("h264_mp4toannexb");
        if (!bsfc) {
            fprintf(stderr, "Error occurred when creating bitstream filter\n");
            goto end;
        }

        more_video = 1;
    }

    // Open audio input.
    if (aropaque != 0) {
        aibuf = av_malloc(8192);
        if (aibuf == NULL) {
            goto end;
        }

        aifmt_ctx = avformat_alloc_context();
        aifmt_ctx->pb = avio_alloc_context(aibuf, 8192, 0, aropaque, audioReadFunction, 0, 0);
        if (aifmt_ctx->pb == NULL) {
            fprintf(stderr, "Could not create input buffer.");
            goto end;
        }

        if ((ret = avformat_open_input(&aifmt_ctx, "a.mp4", 0, 0)) < 0) {
            fprintf(stderr, "Could not open input.");
            goto end;
        }

        if ((ret = avformat_find_stream_info(aifmt_ctx, 0)) < 0) {
            fprintf(stderr, "Failed to retrieve input stream information");
            goto end;
        }

        ret = addStreams(&audio_st, ofmt_ctx, aifmt_ctx);
        if (ret < 0) {
            fprintf(stderr, "Error occurred when adding audio streams.\n");
            goto end;
        }

        more_audio = 1;
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

    while (more_audio || more_video) {
        if (more_video && 
            (!more_audio || av_compare_ts(video_st.next_pts,
                                          video_st.st->time_base,
                                          audio_st.next_pts,
                                          audio_st.st->time_base) <= 0))
        {
            more_video = write_packet(bsfc, vifmt_ctx, &video_st);
        } else {
            more_audio = write_packet(0, aifmt_ctx, &audio_st);
        }
    }

    av_write_trailer(ofmt_ctx);

end:
    av_bitstream_filter_close(bsfc);

    avformat_free_context(aifmt_ctx);
    avformat_free_context(vifmt_ctx);
    avformat_free_context(ofmt_ctx);

    if (ret < 0 && ret != AVERROR_EOF) {
        fprintf(stderr, "Error occurred: %s\n", av_err2str(ret));
        return 1;
    }

    return 0;
}
