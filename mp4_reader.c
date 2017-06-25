/*
 * Copyright (c) 2014 veecr.
 */

#include "mp4_reader.h"
#include <libavformat/avformat.h>
#ifdef __linux__
#include <netinet/in.h>
#endif

struct _FrameReader {
    AVFormatContext *ifmt_ctx;
    void* ropaque;
};

FrameReader* NewMp4FrameReader(void* ropaque, BufferCallback readFunction) {
    int ret;
    unsigned char *vibuf;
    
    FrameReader* fr = malloc(sizeof(FrameReader));
    fr->ropaque = ropaque;
    
    vibuf = av_malloc(8192);
    if (vibuf == NULL) {
        goto end;
    }

    fr->ifmt_ctx = avformat_alloc_context();
    fr->ifmt_ctx->pb = avio_alloc_context(vibuf, 8192, 0, ropaque, readFunction, 0, 0);
    if (fr->ifmt_ctx->pb == NULL) {
        fprintf(stderr, "Could not create input buffer.");
        goto end;
    }

    if ((ret = avformat_open_input(&fr->ifmt_ctx, "v.mp4", 0, 0)) < 0) {
        fprintf(stderr, "Could not open input.");
        goto end;
    }
/*
    if ((ret = avformat_find_stream_info(fr->ifmt_ctx, 0)) < 0) {
        fprintf(stderr, "Failed to retrieve input stream information");
        goto end;
    }
*/
    return fr;

end:
    FreeMp4FrameReader(fr);
    return 0;
}

enum AVMediaType Mp4FrameReaderGetMediaType(FrameReader* fr) {
    return fr->ifmt_ctx->streams[0]->codec->codec_type;
}

void Mp4FrameReaderGetSpsAndPps(FrameReader* fr, uint8_t const** spsBuf, int* spsSize, uint8_t const** ppsBuf, int* ppsSize) {
    uint8_t* ed = fr->ifmt_ctx->streams[0]->codec->extradata;
    uint16_t spsSizeBigEnd = *(uint16_t*)(ed+6);
    *spsSize = ntohs(spsSizeBigEnd);
    *spsBuf = ed + 8;
    
    uint16_t ppsSizeBigEnd = *(uint16_t*)(ed+*spsSize+9);
    *ppsSize = ntohs(ppsSizeBigEnd);
    *ppsBuf = ed + *spsSize + 11;
}

void Mp4FrameReaderGetAsc(FrameReader* fr, uint8_t const** ascBuf, int* ascSize) {
    *ascBuf = fr->ifmt_ctx->streams[0]->codec->extradata;
    *ascSize = fr->ifmt_ctx->streams[0]->codec->extradata_size;
}

int Mp4FrameReaderReadFrame(FrameReader* fr, void* opaque, WriteFrameCallback cb) {
    int ret;
    AVPacket pkt;

    ret = av_read_frame(fr->ifmt_ctx, &pkt);
    if (ret < 0) {
        return 0;
    }

    cb(opaque, pkt.data, pkt.size, pkt.pts, pkt.dts, pkt.duration);

    av_free_packet(&pkt);

    return 1;
}

void* Mp4FrameReaderGetOpaquePointer(FrameReader* fr) {
    return fr->ropaque;
}

int64_t Mp4FrameReaderGetDuration(FrameReader* fr) {
    return ((fr->ifmt_ctx->streams[0]->duration - fr->ifmt_ctx->streams[0]->start_time) * 1000) / fr->ifmt_ctx->streams[0]->time_base.den;
}

void Mp4FrameReaderSeekToFrame(FrameReader* fr, int64_t frameIndex) {
    int64_t seekTarget = frameIndex * 19200;
    av_seek_frame(fr->ifmt_ctx, -1, seekTarget, AVSEEK_FLAG_ANY);
}

int64_t Mp4FrameReaderGetNumFrames(FrameReader* fr) {
    return fr ? fr->ifmt_ctx->streams[0]->nb_index_entries : 0;
}

void FreeMp4FrameReader(FrameReader* fr) {
    avformat_free_context(fr->ifmt_ctx);
    free(fr);
}
