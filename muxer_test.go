package muxer

import (
	"bytes"
	"fmt"
	"testing"
)

func TestMuxer(t *testing.T) {
	out := bytes.Buffer{}
	filenames := []string{"1.mp4", "2.mp4", "3.mp4"}
	StreamVideo(&out, filenames)
	fmt.Printf("Wrote %d bytes to buffer.\n", out.Len())
}
