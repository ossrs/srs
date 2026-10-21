package main

import (
	"bytes"
	"testing"
)

func TestRTMPChunkRoundTrip(t *testing.T) {
	var wire bytes.Buffer
	w := newRTMPWriter(&wire)
	w.chunkSize = 7
	want := rtmpMessage{timestamp: 0x1000002, typeID: 9, streamID: 1, payload: bytes.Repeat([]byte{0xab}, 31)}
	if err := w.write(6, want); err != nil {
		t.Fatal(err)
	}
	r := newRTMPReader(&wire)
	r.chunkSize = 7
	got, err := r.read()
	if err != nil {
		t.Fatal(err)
	}
	if got.timestamp != want.timestamp || got.typeID != want.typeID || got.streamID != want.streamID || !bytes.Equal(got.payload, want.payload) {
		t.Fatalf("message = %+v, want %+v", got, want)
	}
}

func TestRTMPInterleavedChunks(t *testing.T) {
	var wire bytes.Buffer
	w := newRTMPWriter(&wire)
	w.chunkSize = 3
	if err := w.write(5, rtmpMessage{typeID: 8, streamID: 1, payload: []byte("abcdefg")}); err != nil {
		t.Fatal(err)
	}
	if err := w.write(6, rtmpMessage{timestamp: 40, typeID: 9, streamID: 1, payload: []byte("hijkl")}); err != nil {
		t.Fatal(err)
	}
	r := newRTMPReader(&wire)
	r.chunkSize = 3
	for _, want := range []string{"abcdefg", "hijkl"} {
		got, err := r.read()
		if err != nil {
			t.Fatal(err)
		}
		if string(got.payload) != want {
			t.Fatalf("payload = %q, want %q", got.payload, want)
		}
	}
}

func TestFLVTagLoop(t *testing.T) {
	var wire bytes.Buffer
	wire.Write([]byte{'F', 'L', 'V', 1, 5, 0, 0, 0, 9, 0, 0, 0, 0})
	wire.Write([]byte{9, 0, 0, 3, 0, 0, 40, 0, 0, 0, 0, 1, 2, 3, 0, 0, 0, 14})
	tags, err := readFLVTags(bytes.NewReader(wire.Bytes()))
	if err != nil {
		t.Fatal(err)
	}
	if len(tags) != 1 || tags[0].timestamp != 40 || tags[0].typeID != 9 || !bytes.Equal(tags[0].payload, []byte{1, 2, 3}) {
		t.Fatalf("tags = %+v", tags)
	}
}
