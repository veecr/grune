#ifndef TS_MUXER_H
#define TS_MUXER_H

#include "muxer.h"

int remuxToTs(
    void* aropaque, BufferCallback,
    void* vropaque, BufferCallback,
    void* wopaque, BufferCallback);

#endif
