/*
 * Copyright (c) 2014 veecr.
 */

#include "mp4_frame_writer.h"
#include <libavutil/timestamp.h>
#include <libavutil/opt.h>
#include <libavformat/avformat.h>

typedef struct {
    AVStream *st;
} OutputStream;

struct _FrameWriter {
    AVFormatContext *ofmt_ctx;
    OutputStream video_st;
    OutputStream audio_st;
};

FrameWriter* NewMp4FrameWriter(void* wopaque, BufferCallback writeFunction)
{
    int ret;
    unsigned char *obuf;
    FrameWriter* fw;

    fw = malloc(sizeof(FrameWriter));
    
    // Open output.
    obuf = av_malloc(8192);
    if (obuf == NULL) {
        goto end;
    }

    avformat_alloc_output_context2(&fw->ofmt_ctx, NULL, "mp4", NULL);
    if (!fw->ofmt_ctx) {
        fprintf(stderr, "Could not create output context\n");
        ret = AVERROR_UNKNOWN;
        goto end;
    }
    
    ret = av_opt_set(fw->ofmt_ctx, "movflags", "frag_keyframe+empty_moov+omit_tfhd_offset", AV_OPT_SEARCH_CHILDREN);
    if (ret < 0) {
        fprintf(stderr, "Failed to set fragmentation and empty moov option.\n");
        goto end;
    }

    fw->ofmt_ctx->pb = avio_alloc_context(obuf, 8192, 1, wopaque, 0, writeFunction, 0);
    if (fw->ofmt_ctx->pb == NULL) {
        fprintf(stderr, "Could not create output buffer.");
        goto end;
    }

    return fw;

end:
    FreeMp4FrameWriter(fw);
    return 0;
}

void Mp4FrameWriterAddAudioStream(FrameWriter* fw, const int samplerate, const int bitrate) {
    AVCodecContext *c;
    OutputStream *ost = &fw->audio_st;
    AVFormatContext *oc = fw->ofmt_ctx;

    ost->st = avformat_new_stream(oc, 0);
    if (!ost->st) {
        fprintf(stderr, "Could not allocate stream\n");
        return;
    }

    ost->st->id = oc->nb_streams-1;
    c = ost->st->codec;
    c->codec_id = AV_CODEC_ID_AAC;

    c->codec_type = AVMEDIA_TYPE_AUDIO;
    c->time_base = (AVRational){ 1, 44100 };
    c->sample_rate = samplerate;
    c->channels = 1;
    c->bit_rate = bitrate;
    c->time_base = ost->st->time_base;
    ost->st->time_base = (AVRational){ 1, 44100 };

    /* Some formats want stream headers to be separate. */
    if (oc->oformat->flags & AVFMT_GLOBALHEADER)
        ost->st->codec->flags |= CODEC_FLAG_GLOBAL_HEADER;
}

void Mp4FrameWriterAddVideoStream(FrameWriter* fw, const int width, const int height, const int bitrate) {
    AVCodecContext *c;
    OutputStream *ost = &fw->video_st;
    AVFormatContext *oc = fw->ofmt_ctx;

    ost->st = avformat_new_stream(oc, 0);
    if (!ost->st) {
        fprintf(stderr, "Could not allocate stream\n");
        return;
    }

    ost->st->id = oc->nb_streams-1;
    c = ost->st->codec;
    c->codec_id = AV_CODEC_ID_H264;

    c->codec_type = AVMEDIA_TYPE_VIDEO;
    c->profile = FF_PROFILE_H264_HIGH;
    c->pix_fmt = AV_PIX_FMT_YUV420P;
    //c->framerate = (AVRational){ 1, 30 };
    c->bit_rate = bitrate;
    c->width = width;
    c->height = height;
    c->time_base = (AVRational){ 1, 60 };
    ost->st->time_base = (AVRational){ 1, 19200 };

    /* Some formats want stream headers to be separate. */
    if (oc->oformat->flags & AVFMT_GLOBALHEADER)
        ost->st->codec->flags |= CODEC_FLAG_GLOBAL_HEADER;
}

void Mp4FrameWriterSetSpsPps(FrameWriter* fw, const uint8_t* spsBuf, int spsSize, const uint8_t* ppsBuf, int ppsSize) {
    AVCodecContext *c = fw->video_st.st->codec;
    int extradata_len = 8 + spsSize + 3 + ppsSize;
    int offset = 0;

    c->extradata = (uint8_t*)av_mallocz(extradata_len);

    c->extradata_size = extradata_len;
    c->extradata[0] = 0x01;
    c->extradata[1] = spsBuf[1];
    c->extradata[2] = spsBuf[2];
    c->extradata[3] = spsBuf[3];
    c->extradata[4] = 0xFC | 3;
    c->extradata[5] = 0xE0 | 1;
    c->extradata[6] = (spsSize >> 8) & 0x00ff;
    c->extradata[7] = spsSize & 0x00ff;
    offset += 8;

    memcpy(c->extradata+offset, spsBuf, spsSize);
    offset += spsSize;

    c->extradata[offset++] = 0x01;
    c->extradata[offset++] = (ppsSize >> 8) & 0x00ff;
    c->extradata[offset++] = ppsSize & 0x00ff;

    memcpy(c->extradata+offset, ppsBuf, ppsSize);
}

int Mp4FrameWriterWriteHeader(FrameWriter* fw) {
    int ret = avformat_write_header(fw->ofmt_ctx, NULL);
    if (ret < 0) {
        fprintf(stderr, "Error occurred when writing header.\n");
        return ret;
    }

    // Dump format.
    av_dump_format(fw->ofmt_ctx, 0, NULL, 1);

    return ret;
}

int Mp4FrameWriterWriteVclFrame(FrameWriter* fw, const uint8_t* buf, int size, int64_t pts, int64_t dts, int duration, int isKeyFrame) {
    AVPacket pkt = { 0 };
    av_init_packet(&pkt);
    
    pkt.stream_index = fw->video_st.st->index;
    pkt.pts = pts;
    pkt.dts = dts;
    pkt.duration = duration;
    pkt.buf = av_buffer_alloc(size);
    memcpy(pkt.buf->data, buf, size);
    pkt.data = pkt.buf->data;
    pkt.size = pkt.buf->size;
    pkt.flags = isKeyFrame ? AV_PKT_FLAG_KEY : 0;
    
    //fw->next_dts += 640;
    
    int ret = av_write_frame(fw->ofmt_ctx, &pkt);
    
    av_buffer_unref(&pkt.buf);
    
    return ret;
}

int Mp4FrameWriterWriteAudioPacket(FrameWriter* fw, const uint8_t* buf, int size, int64_t pts) {
    AVPacket pkt = { 0 };
    av_init_packet(&pkt);
    
    pkt.stream_index = fw->audio_st.st->index;
    pkt.pts = pts;
    pkt.dts = pts;
    pkt.duration = 1024;
    pkt.buf = av_buffer_alloc(size);
    memcpy(pkt.buf->data, buf, size);
    pkt.data = pkt.buf->data;
    pkt.size = pkt.buf->size;
    pkt.flags = AV_PKT_FLAG_KEY;
    
    int ret = av_write_frame(fw->ofmt_ctx, &pkt);
    
    av_buffer_unref(&pkt.buf);
    
    return ret;
}

void Mp4FrameWriterFlushFragment(FrameWriter* fw) {
    av_write_frame(fw->ofmt_ctx, 0);
}

void Mp4FrameWriterComplete(FrameWriter* fw) {
    av_write_trailer(fw->ofmt_ctx);
}

void FreeMp4FrameWriter(FrameWriter* fw) {
    avformat_free_context(fw->ofmt_ctx);
    free(fw);
}
