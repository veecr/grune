#ifndef MUXER_H
#define MUXER_H

#include <stdint.h>

typedef int(*BufferCallback)(void *opaque, uint8_t *buf, int buf_size);
typedef int64_t(*SeekCallback)(void *opaque, int64_t to, int whence);

#endif
