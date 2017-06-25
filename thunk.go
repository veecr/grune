package grune

import "C"
import "unsafe"

func ByteSliceToCArray(byteSlice []byte, array unsafe.Pointer, size int) {
	var arrayptr = uintptr(array)

	for i := 0; i < len(byteSlice); i++ {
		*(*C.uchar)(unsafe.Pointer(arrayptr)) = C.uchar(byteSlice[i])
		arrayptr++
	}
}

//export WriteFunction
func WriteFunction(opaque unsafe.Pointer, buf unsafe.Pointer, num int) {
	ctx := (*WriterContext)(opaque)
	bytes := C.GoBytes(buf, C.int(num))
	ctx.w.Write(bytes)
}

//export ReadFunction
func ReadFunction(opaque unsafe.Pointer, buf unsafe.Pointer, size int) int {
	ctx := (*ReaderContext)(opaque)
	b := make([]byte, size)
	n, err := ctx.r.Read(b)
	if err != nil {
		return 0
	}
	ByteSliceToCArray(b, buf, n)
	return n
}
