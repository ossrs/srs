package main

import (
	"encoding/binary"
	"errors"
	"io"
	"sync"
)

type rtmpMessage struct {
	timestamp uint32
	typeID    byte
	streamID  uint32
	payload   []byte
}

func put24(b []byte, v uint32) { b[0], b[1], b[2] = byte(v>>16), byte(v>>8), byte(v) }
func get24(b []byte) uint32    { return uint32(b[0])<<16 | uint32(b[1])<<8 | uint32(b[2]) }

type rtmpWriter struct {
	w         io.Writer
	chunkSize int
	mu        sync.Mutex
}

func newRTMPWriter(w io.Writer) *rtmpWriter { return &rtmpWriter{w: w, chunkSize: 128} }

func (w *rtmpWriter) write(csid int, m rtmpMessage) error {
	if csid < 2 || csid > 63 || w.chunkSize <= 0 || len(m.payload) > 0xffffff {
		return errors.New("invalid RTMP chunk")
	}
	w.mu.Lock()
	defer w.mu.Unlock()
	var h [16]byte
	h[0] = byte(csid)
	extended := m.timestamp >= 0xffffff
	if extended {
		put24(h[1:4], 0xffffff)
	} else {
		put24(h[1:4], m.timestamp)
	}
	put24(h[4:7], uint32(len(m.payload)))
	h[7] = m.typeID
	binary.LittleEndian.PutUint32(h[8:12], m.streamID)
	n := 12
	if extended {
		binary.BigEndian.PutUint32(h[12:16], m.timestamp)
		n = 16
	}
	if _, err := w.w.Write(h[:n]); err != nil {
		return err
	}
	for pos := 0; pos < len(m.payload); {
		end := pos + w.chunkSize
		if end > len(m.payload) {
			end = len(m.payload)
		}
		if _, err := w.w.Write(m.payload[pos:end]); err != nil {
			return err
		}
		pos = end
		if pos < len(m.payload) {
			if _, err := w.w.Write([]byte{byte(0xc0 | csid)}); err != nil {
				return err
			}
			if extended {
				if _, err := w.w.Write(h[12:16]); err != nil {
					return err
				}
			}
		}
	}
	return nil
}

type chunkState struct {
	message  rtmpMessage
	length   int
	delta    uint32
	extended bool
	read     int
}

type rtmpReader struct {
	r         io.Reader
	chunkSize int
	streams   map[int]*chunkState
}

func newRTMPReader(r io.Reader) *rtmpReader {
	return &rtmpReader{r: r, chunkSize: 128, streams: make(map[int]*chunkState)}
}

func (r *rtmpReader) read() (rtmpMessage, error) {
	for {
		var b [1]byte
		if _, err := io.ReadFull(r.r, b[:]); err != nil {
			return rtmpMessage{}, err
		}
		format, csid := int(b[0]>>6), int(b[0]&0x3f)
		if csid < 2 {
			var extra [2]byte
			n := 1
			if csid == 1 {
				n = 2
			}
			if _, err := io.ReadFull(r.r, extra[:n]); err != nil {
				return rtmpMessage{}, err
			}
			csid = 64 + int(extra[0])
			if n == 2 {
				csid += int(extra[1]) * 256
			}
		}
		state := r.streams[csid]
		if state == nil {
			if format != 0 {
				return rtmpMessage{}, errors.New("RTMP chunk without initial header")
			}
			state = &chunkState{}
			r.streams[csid] = state
		}
		if format != 3 {
			var h [11]byte
			size := []int{11, 7, 3}[format]
			if _, err := io.ReadFull(r.r, h[:size]); err != nil {
				return rtmpMessage{}, err
			}
			ts := get24(h[:3])
			state.extended = ts == 0xffffff
			if state.extended {
				var ext [4]byte
				if _, err := io.ReadFull(r.r, ext[:]); err != nil {
					return rtmpMessage{}, err
				}
				ts = binary.BigEndian.Uint32(ext[:])
			}
			if format == 0 {
				state.message.timestamp = ts
				state.message.streamID = binary.LittleEndian.Uint32(h[7:11])
			} else {
				state.message.timestamp += ts
				state.delta = ts
			}
			if format < 2 {
				state.length = int(get24(h[3:6]))
				state.message.typeID = h[6]
			}
			if state.length > 16<<20 {
				return rtmpMessage{}, errors.New("RTMP message too large")
			}
			state.read = 0
			state.message.payload = make([]byte, state.length)
		} else {
			if state.extended {
				var ext [4]byte
				if _, err := io.ReadFull(r.r, ext[:]); err != nil {
					return rtmpMessage{}, err
				}
			}
			if state.read == state.length {
				state.message.timestamp += state.delta
				state.read = 0
				state.message.payload = make([]byte, state.length)
			}
		}
		if r.chunkSize <= 0 {
			return rtmpMessage{}, errors.New("invalid RTMP chunk size")
		}
		count := state.length - state.read
		if count > r.chunkSize {
			count = r.chunkSize
		}
		if _, err := io.ReadFull(r.r, state.message.payload[state.read:state.read+count]); err != nil {
			return rtmpMessage{}, err
		}
		state.read += count
		if state.read == state.length {
			m := state.message
			if m.typeID == 1 && len(m.payload) == 4 {
				size := binary.BigEndian.Uint32(m.payload)
				if size == 0 || size > 1<<20 {
					return rtmpMessage{}, errors.New("invalid RTMP peer chunk size")
				}
				r.chunkSize = int(size)
			}
			return m, nil
		}
	}
}

// readFLVHeader reads the FLV header and the first previous tag size.
func readFLVHeader(reader io.Reader) error {
	var header [9]byte
	if _, err := io.ReadFull(reader, header[:]); err != nil {
		return err
	}
	if string(header[:3]) != "FLV" || binary.BigEndian.Uint32(header[5:9]) < 9 {
		return errors.New("invalid FLV header")
	}
	_, err := io.CopyN(io.Discard, reader, int64(binary.BigEndian.Uint32(header[5:9])-9)+4)
	return err
}

// readFLVTag reads one tag of any type with its previous tag size; io.EOF
// only when the stream ends between two tags.
func readFLVTag(reader io.Reader) (rtmpMessage, error) {
	var h [11]byte
	if _, err := io.ReadFull(reader, h[:]); err != nil {
		return rtmpMessage{}, err
	}
	size := get24(h[1:4])
	if size > 16<<20 {
		return rtmpMessage{}, errors.New("FLV tag too large")
	}
	payload := make([]byte, size)
	if _, err := io.ReadFull(reader, payload); err != nil {
		return rtmpMessage{}, noEOF(err)
	}
	var previous [4]byte
	if _, err := io.ReadFull(reader, previous[:]); err != nil {
		return rtmpMessage{}, noEOF(err)
	}
	if binary.BigEndian.Uint32(previous[:]) != size+11 {
		return rtmpMessage{}, errors.New("invalid FLV previous tag size")
	}
	return rtmpMessage{timestamp: get24(h[4:7]) | uint32(h[7])<<24, typeID: h[0], payload: payload}, nil
}

// noEOF turns an end inside a tag into io.ErrUnexpectedEOF.
func noEOF(err error) error {
	if err == io.EOF {
		return io.ErrUnexpectedEOF
	}
	return err
}

func readFLVTags(reader io.Reader) ([]rtmpMessage, error) {
	if err := readFLVHeader(reader); err != nil {
		return nil, err
	}
	var tags []rtmpMessage
	for {
		tag, err := readFLVTag(reader)
		if err == io.EOF {
			break
		}
		if err != nil {
			return nil, err
		}
		if tag.typeID == 8 || tag.typeID == 9 {
			tags = append(tags, tag)
		}
	}
	if len(tags) == 0 {
		return nil, errors.New("FLV has no media tags")
	}
	return tags, nil
}
