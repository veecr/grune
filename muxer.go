package muxer

// #cgo CFLAGS: -I/usr/local/include
// #cgo LDFLAGS: -L/usr/local/lib -lavformat -lavcodec -lavutil -lswscale -L. -lmuxer
// #include "tsmux.h"
// #include <stdio.h>
// #include <stdlib.h>
// #include <libavformat/avformat.h>
//
//static char** makeCharArray(int size) {
//    return calloc(sizeof(char*), size);
//}
//
//static void setArrayString(char **a, char *s, int n) {
//    a[n] = s;
//}
//
//static void freeCharArray(char **a, int size) {
//    int i;
//    for (i = 0; i < size; i++)
//        free(a[i]);
//    free(a);
//}
//
//typedef int(*callback_fcn)(void* opaque, uint8_t* buf, int buf_size);
//void WriteFunction(void*, void*, int);
//int ReadFunction(void*, void*, int);
//
//int writeFunction_cgo(void* opaque, uint8_t* buf, int buf_size) {
//    WriteFunction(opaque, buf, buf_size);
//    return 0;
//}
//
//int readFunction_cgo(void* opaque, uint8_t* buf, int buf_size) {
//    return ReadFunction(opaque, buf, buf_size);
//}
import "C"
import (
	"fmt"
	"io"
	"unsafe"
)

func init() {
	C.av_register_all()
	//C.av_log_set_level(C.AV_LOG_VERBOSE)
}

type WriterContext struct {
	w io.Writer
}

type ReaderContext struct {
	r io.Reader
}

/*
func StreamVideo(w io.Writer, filenames []string) error {
	cargs := C.makeCharArray(C.int(len(filenames)))
	defer C.freeCharArray(cargs, C.int(len(filenames)))
	for i, s := range filenames {
		C.setArrayString(cargs, C.CString(s), C.int(i))
	}
	ctx := WriterContext{w}
	ret := C.createStream(C.int(len(filenames)), cargs, unsafe.Pointer(&ctx), (C.callback_fcn)(unsafe.Pointer(C.writeFunction_cgo)))
	if ret != 0 {
		return fmt.Errorf("Error muxing.")
	}
	return nil
}
*/

func Mp4ToTs(ar io.Reader, vr io.Reader, w io.Writer) error {
	var (
		arctx *ReaderContext
		vrctx *ReaderContext
	)
	if ar != nil {
		arctx = &ReaderContext{ar}
	}
	if vr != nil {
		vrctx = &ReaderContext{vr}
	}
	wctx := &WriterContext{w}
	ret := C.remuxToTs(
		unsafe.Pointer(arctx), (C.callback_fcn)(unsafe.Pointer(C.readFunction_cgo)),
		unsafe.Pointer(vrctx), (C.callback_fcn)(unsafe.Pointer(C.readFunction_cgo)),
		unsafe.Pointer(wctx), (C.callback_fcn)(unsafe.Pointer(C.writeFunction_cgo)))
	if ret != 0 {
		return fmt.Errorf("Error muxing.")
	}
	return nil
}
