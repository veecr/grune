#ifndef MP4_MUXER_H
#define MP4_MUXER_H

#include "muxer.h"

typedef struct _FrameWriter FrameWriter;

FrameWriter* NewMp4FrameWriter(void* wopaque, BufferCallback writeFunction, char audio, char video);
void Mp4FrameWriterSetSpsPps(FrameWriter* fw, const uint8_t* spsBuf, int spsSize, const uint8_t* ppsBuf, int ppsSize);
int Mp4FrameWriterWriteHeader(FrameWriter* fw);
int Mp4FrameWriterWriteVclFrame(FrameWriter* fw, const uint8_t* buf, int size, int64_t pts, int64_t dts, int duration, int isKeyFrame);
int Mp4FrameWriterWriteAudioPacket(FrameWriter* fw, const uint8_t* buf, int size, int64_t pts);
void Mp4FrameWriterFlushFragment(FrameWriter* fw);
void Mp4FrameWriterComplete(FrameWriter* fw);
void FreeMp4FrameWriter(FrameWriter* fw);

#endif
