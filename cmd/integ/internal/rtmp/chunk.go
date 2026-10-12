// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT
package rtmp

import (
	"bufio"
	"bytes"
	"encoding/binary"
	"fmt"
	"io"
)

// RTMP message types.
const (
	MsgSetChunkSize  = 1
	MsgAbort         = 2
	MsgAck           = 3
	MsgUserControl   = 4
	MsgWindowAckSize = 5
	MsgSetPeerBW     = 6
	MsgAudio         = 8
	MsgVideo         = 9
	MsgDataAMF3      = 15
	MsgCommandAMF3   = 17
	MsgDataAMF0      = 18
	MsgCommandAMF0   = 20
	MsgAggregate     = 22
)

// A Message is one RTMP message.
type Message struct {
	Type      uint8
	Timestamp uint32
	StreamID  uint32
	Payload   []byte
}

// extendedTimestamp is the 3-byte timestamp that says a 4-byte one follows.
const extendedTimestamp = 0xffffff

// chunkStream is what the reader remembers of a chunk stream: the header of
// its last message and the message it is reading.
type chunkStream struct {
	started   bool
	timestamp uint32
	delta     uint32
	length    uint32
	typ       uint8
	streamID  uint32
	extended  bool
	payload   []byte
}

// A chunkReader reads messages from RTMP chunks.
type chunkReader struct {
	r       *countingReader
	size    int
	streams map[uint32]*chunkStream
}

type countingReader struct {
	r *bufio.Reader
	n uint64
}

func (c *countingReader) Read(b []byte) (int, error) {
	n, err := c.r.Read(b)
	c.n += uint64(n)
	return n, err
}

func newChunkReader(r io.Reader) *chunkReader {
	return &chunkReader{r: &countingReader{r: bufio.NewReaderSize(r, 64*1024)}, size: 128, streams: map[uint32]*chunkStream{}}
}

// SetChunkSize sets the size of the chunks the peer sends.
func (r *chunkReader) SetChunkSize(n int) { r.size = n }

// BytesRead is how many bytes the reader read from the connection.
func (r *chunkReader) BytesRead() uint64 { return r.r.n }

// ReadMessage reads chunks until a message is complete. It returns io.EOF
// only when the connection ends between messages.
func (r *chunkReader) ReadMessage() (*Message, error) {
	first := true
	for {
		m, err := r.readChunk()
		if err == io.EOF && !first {
			err = io.ErrUnexpectedEOF
		}
		if err != nil || m != nil {
			return m, err
		}
		first = false
	}
}

func (r *chunkReader) read(b []byte) error {
	_, err := io.ReadFull(r.r, b)
	return err
}

// readChunk reads one chunk, and returns the message it completes, if any.
func (r *chunkReader) readChunk() (*Message, error) {
	var b [11]byte
	if err := r.read(b[:1]); err != nil {
		return nil, err
	}
	fmtType := b[0] >> 6
	csid := uint32(b[0] & 0x3f)
	switch csid {
	case 0:
		if err := r.read(b[:1]); err != nil {
			return nil, unexpected(err)
		}
		csid = 64 + uint32(b[0])
	case 1:
		if err := r.read(b[:2]); err != nil {
			return nil, unexpected(err)
		}
		csid = 64 + uint32(b[0]) + uint32(b[1])*256
	}

	cs := r.streams[csid]
	if cs == nil {
		cs = &chunkStream{}
		r.streams[csid] = cs
	}
	if !cs.started && fmtType != 0 {
		return nil, fmt.Errorf("chunk stream %v starts with format %v, want 0", csid, fmtType)
	}
	newMessage := cs.payload == nil

	headerSize := [4]int{11, 7, 3, 0}[fmtType]
	if err := r.read(b[:headerSize]); err != nil {
		return nil, unexpected(err)
	}
	var ts uint32
	if fmtType < 3 {
		ts = uint32(b[0])<<16 | uint32(b[1])<<8 | uint32(b[2])
		cs.extended = ts == extendedTimestamp
	}
	if fmtType < 2 {
		cs.length = uint32(b[3])<<16 | uint32(b[4])<<8 | uint32(b[5])
		cs.typ = b[6]
	}
	if fmtType == 0 {
		cs.streamID = binary.LittleEndian.Uint32(b[7:11])
	}
	if cs.extended {
		if err := r.read(b[:4]); err != nil {
			return nil, unexpected(err)
		}
		if fmtType < 3 {
			ts = binary.BigEndian.Uint32(b[:4])
		}
	}

	// Format 0 has an absolute timestamp, formats 1 and 2 a delta, and format
	// 3 at the start of a message repeats the last delta.
	if newMessage {
		switch fmtType {
		case 0:
			cs.timestamp, cs.delta = ts, 0
		case 1, 2:
			cs.delta = ts
			cs.timestamp += ts
		case 3:
			cs.timestamp += cs.delta
		}
		cs.payload = make([]byte, 0, cs.length)
	}
	cs.started = true

	n := int(cs.length) - len(cs.payload)
	if n > r.size {
		n = r.size
	}
	start := len(cs.payload)
	cs.payload = cs.payload[:start+n]
	if err := r.read(cs.payload[start:]); err != nil {
		return nil, unexpected(err)
	}
	if len(cs.payload) < int(cs.length) {
		return nil, nil
	}

	m := &Message{Type: cs.typ, Timestamp: cs.timestamp, StreamID: cs.streamID, Payload: cs.payload}
	cs.payload = nil
	return m, nil
}

func unexpected(err error) error {
	if err == io.EOF {
		return io.ErrUnexpectedEOF
	}
	return err
}

// A chunkWriter writes messages as RTMP chunks: format 0 for the first chunk
// of each message, format 3 for the rest.
type chunkWriter struct {
	w    *bufio.Writer
	size int
}

func newChunkWriter(w io.Writer) *chunkWriter {
	return &chunkWriter{w: bufio.NewWriterSize(w, 64*1024), size: 128}
}

// SetChunkSize sets the size of the chunks it writes; the peer must be told
// first.
func (w *chunkWriter) SetChunkSize(n int) { w.size = n }

// WriteMessage writes a message on a chunk stream, and flushes it.
func (w *chunkWriter) WriteMessage(csid uint32, m *Message) error {
	ts := m.Timestamp
	extended := ts >= extendedTimestamp
	if extended {
		ts = extendedTimestamp
	}

	n := len(m.Payload)
	h := appendBasicHeader(make([]byte, 0, 18), 0, csid)
	h = append(h, byte(ts>>16), byte(ts>>8), byte(ts), byte(n>>16), byte(n>>8), byte(n), m.Type)
	h = binary.LittleEndian.AppendUint32(h, m.StreamID)
	if extended {
		h = binary.BigEndian.AppendUint32(h, m.Timestamp)
	}
	w.w.Write(h)

	cont := appendBasicHeader(make([]byte, 0, 7), 3, csid)
	if extended {
		cont = binary.BigEndian.AppendUint32(cont, m.Timestamp)
	}
	for p := m.Payload; ; {
		k := min(len(p), w.size)
		w.w.Write(p[:k])
		p = p[k:]
		if len(p) == 0 {
			break
		}
		w.w.Write(cont)
	}
	return w.w.Flush()
}

func appendBasicHeader(b []byte, fmtType uint8, csid uint32) []byte {
	switch {
	case csid < 64:
		return append(b, fmtType<<6|byte(csid))
	case csid < 320:
		return append(b, fmtType<<6, byte(csid-64))
	default:
		return append(b, fmtType<<6|1, byte((csid-64)&0xff), byte((csid-64)>>8))
	}
}

// EncodeChunks is a message as the chunks a client writes at a chunk size:
// format 0 for the first chunk, format 3 for the rest. Cases cut it short to
// send part of a message.
func EncodeChunks(csid uint32, m *Message, chunkSize int) []byte {
	var b bytes.Buffer
	w := newChunkWriter(&b)
	w.SetChunkSize(chunkSize)
	w.WriteMessage(csid, m)
	return b.Bytes()
}
