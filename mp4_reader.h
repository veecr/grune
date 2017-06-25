#ifndef MP4_READER
#define MP4_READER

#include "muxer.h"

typedef struct _FrameReader FrameReader;

typedef void(*WriteFrameCallback)(void* opaque, uint8_t* buf, int size, int64_t pts, int64_t dts, int duration);

FrameReader* NewMp4FrameReader(void* ropaque, BufferCallback readFunction);
enum AVMediaType Mp4FrameReaderGetMediaType(FrameReader* fr);
void Mp4FrameReaderGetAsc(FrameReader* fr, uint8_t const** ascBuf, int* ascSize);
void Mp4FrameReaderGetSpsAndPps(FrameReader* fr, uint8_t const** spsBuf, int* spsSize, uint8_t const** ppsBuf, int* ppsSize);
void Mp4FrameReaderSeekToFrame(FrameReader* fr, int64_t frameIndex);
int Mp4FrameReaderReadFrame(FrameReader* fr, void* opaque, WriteFrameCallback writeFunction);
void* Mp4FrameReaderGetOpaquePointer(FrameReader* fr);
int64_t Mp4FrameReaderGetDuration(FrameReader* fr);
int64_t Mp4FrameReaderGetNumFrames(FrameReader* fr);
void FreeMp4FrameReader(FrameReader* fr);

#endif
