// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT
package rtmp

import (
	"bytes"
	"io"
	"reflect"
	"testing"
)

func payload(n int) []byte {
	b := make([]byte, n)
	for i := range b {
		b[i] = byte(i)
	}
	return b
}

func TestChunkRoundTrip(t *testing.T) {
	cases := []struct {
		name      string
		chunkSize int
		csid      uint32
		msgs      []*Message
	}{
		{"OneChunk", 128, 3, []*Message{{Type: MsgCommandAMF0, Timestamp: 10, StreamID: 1, Payload: payload(100)}}},
		{"ManyChunks", 128, 6, []*Message{{Type: MsgVideo, Timestamp: 40, StreamID: 1, Payload: payload(1000)}}},
		{"ExactChunk", 128, 6, []*Message{{Type: MsgVideo, Timestamp: 40, StreamID: 1, Payload: payload(256)}}},
		{"Empty", 128, 6, []*Message{{Type: MsgAudio, Timestamp: 0, StreamID: 1, Payload: []byte{}}}},
		{"ChunkSize4096", 4096, 6, []*Message{{Type: MsgVideo, Timestamp: 1, StreamID: 1, Payload: payload(10000)}}},
		{"ChunkSize60000", 60000, 6, []*Message{{Type: MsgVideo, Timestamp: 1, StreamID: 1, Payload: payload(130000)}}},
		{"ExtendedTimestamp", 128, 7, []*Message{
			{Type: MsgAudio, Timestamp: 0xffffff, StreamID: 1, Payload: payload(300)},
			{Type: MsgAudio, Timestamp: 0x12345678, StreamID: 1, Payload: payload(300)},
		}},
		{"TwoByteCSID", 128, 100, []*Message{{Type: MsgVideo, Timestamp: 1, StreamID: 1, Payload: payload(200)}}},
		{"ThreeByteCSID", 128, 1000, []*Message{{Type: MsgVideo, Timestamp: 1, StreamID: 1, Payload: payload(200)}}},
	}
	for _, tc := range cases {
		t.Run(tc.name, func(t *testing.T) {
			var buf bytes.Buffer
			w := newChunkWriter(&buf)
			w.SetChunkSize(tc.chunkSize)
			for _, m := range tc.msgs {
				if err := w.WriteMessage(tc.csid, m); err != nil {
					t.Fatalf("write: %v", err)
				}
			}
			r := newChunkReader(&buf)
			r.SetChunkSize(tc.chunkSize)
			for i, want := range tc.msgs {
				got, err := r.ReadMessage()
				if err != nil {
					t.Fatalf("read %v: %v", i, err)
				}
				if !reflect.DeepEqual(got, want) {
					t.Errorf("message %v got %+v, want %+v", i, got, want)
				}
			}
			if _, err := r.ReadMessage(); err != io.EOF {
				t.Errorf("after the last message got %v, want io.EOF", err)
			}
		})
	}
}

// TestChunkReaderCompressedHeaders reads messages whose headers use the
// formats 1, 2 and 3, like a server that compresses them.
func TestChunkReaderCompressedHeaders(t *testing.T) {
	b := []byte{
		// fmt 0, csid 4: ts 100, length 2, type 8, stream 1.
		0x04, 0, 0, 100, 0, 0, 2, 8, 1, 0, 0, 0, 0xa1, 0xa2,
		// fmt 1, csid 4: delta 20, length 3, type 9.
		0x44, 0, 0, 20, 0, 0, 3, 9, 0xb1, 0xb2, 0xb3,
		// fmt 2, csid 4: delta 30.
		0x84, 0, 0, 30, 0xc1, 0xc2, 0xc3,
		// fmt 3, csid 4: a new message with the same delta.
		0xc4, 0xd1, 0xd2, 0xd3,
	}
	r := newChunkReader(bytes.NewReader(b))
	want := []*Message{
		{Type: 8, Timestamp: 100, StreamID: 1, Payload: []byte{0xa1, 0xa2}},
		{Type: 9, Timestamp: 120, StreamID: 1, Payload: []byte{0xb1, 0xb2, 0xb3}},
		{Type: 9, Timestamp: 150, StreamID: 1, Payload: []byte{0xc1, 0xc2, 0xc3}},
		{Type: 9, Timestamp: 180, StreamID: 1, Payload: []byte{0xd1, 0xd2, 0xd3}},
	}
	for i, w := range want {
		got, err := r.ReadMessage()
		if err != nil {
			t.Fatalf("read %v: %v", i, err)
		}
		if !reflect.DeepEqual(got, w) {
			t.Errorf("message %v got %+v, want %+v", i, got, w)
		}
	}
}

// TestChunkReaderInterleaved reads two messages whose chunks interleave.
func TestChunkReaderInterleaved(t *testing.T) {
	var a, v bytes.Buffer
	w := newChunkWriter(&a)
	w.WriteMessage(7, &Message{Type: MsgAudio, Timestamp: 1, StreamID: 1, Payload: payload(200)})
	w = newChunkWriter(&v)
	w.WriteMessage(6, &Message{Type: MsgVideo, Timestamp: 2, StreamID: 1, Payload: payload(200)})

	// Each is a 12-byte header and 128 bytes, then a 1-byte header and 72.
	ab, vb := a.Bytes(), v.Bytes()
	if len(ab) != 213 || len(vb) != 213 {
		t.Fatalf("wrote %v and %v bytes, want 213 each", len(ab), len(vb))
	}
	var b []byte
	b = append(b, ab[:140]...)
	b = append(b, vb[:140]...)
	b = append(b, ab[140:]...)
	b = append(b, vb[140:]...)

	r := newChunkReader(bytes.NewReader(b))
	for _, typ := range []uint8{MsgAudio, MsgVideo} {
		m, err := r.ReadMessage()
		if err != nil {
			t.Fatalf("read: %v", err)
		}
		if m.Type != typ || !bytes.Equal(m.Payload, payload(200)) {
			t.Errorf("got type %v with %v bytes, want type %v with the 200-byte payload", m.Type, len(m.Payload), typ)
		}
	}
}

func TestChunkReaderErrors(t *testing.T) {
	cases := map[string][]byte{
		"FirstChunkNotFmt0": {0x44, 0, 0, 20, 0, 0, 3, 9, 1, 2, 3},
		"ShortHeader":       {0x04, 0, 0},
		"ShortPayload":      {0x04, 0, 0, 1, 0, 0, 10, 8, 1, 0, 0, 0, 1},
	}
	for name, b := range cases {
		t.Run(name, func(t *testing.T) {
			r := newChunkReader(bytes.NewReader(b))
			if m, err := r.ReadMessage(); err == nil || err == io.EOF {
				t.Errorf("got %+v, %v; want an error other than io.EOF", m, err)
			}
		})
	}
}

func TestChunkReaderCountsBytes(t *testing.T) {
	var buf bytes.Buffer
	w := newChunkWriter(&buf)
	w.WriteMessage(6, &Message{Type: MsgVideo, Timestamp: 1, StreamID: 1, Payload: payload(300)})
	n := buf.Len()
	r := newChunkReader(&buf)
	if _, err := r.ReadMessage(); err != nil {
		t.Fatalf("read: %v", err)
	}
	if r.BytesRead() != uint64(n) {
		t.Errorf("bytes read %v, want %v", r.BytesRead(), n)
	}
}

// TestEncodeChunks splits a message like the writer: format 0, then a format
// 3 header before each chunk after the first.
func TestEncodeChunks(t *testing.T) {
	m := &Message{Type: MsgVideo, Timestamp: 40, StreamID: 1, Payload: payload(300)}
	b := EncodeChunks(6, m, 128)
	if want := 12 + 300 + 2; len(b) != want {
		t.Fatalf("%v bytes, want %v", len(b), want)
	}
	if b[0] != 0x06 || b[12+128] != 0xc6 || b[12+128+1+128] != 0xc6 {
		t.Errorf("basic headers %#x %#x %#x, want 0x06 0xc6 0xc6", b[0], b[12+128], b[12+128+1+128])
	}
	r := newChunkReader(bytes.NewReader(b))
	got, err := r.ReadMessage()
	if err != nil {
		t.Fatalf("read: %v", err)
	}
	if !reflect.DeepEqual(got, m) {
		t.Errorf("got %+v, want %+v", got, m)
	}
}
