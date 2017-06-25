#ifndef TS_MUXER_H
#define TS_MUXER_H

#include "muxer.h"

int Mp4RemuxToFragmented(
    char const* filePath,
    void* wopaque, BufferCallback);

#endif
