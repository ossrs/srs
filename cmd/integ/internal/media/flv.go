// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT
package media

import (
	"bufio"
	"errors"
	"fmt"
	"io"
)

// FLV tag types.
const (
	TagAudio  = 8
	TagVideo  = 9
	TagScript = 18
)

// An FLVTag is one tag of an FLV file: its type, its timestamp in
// milliseconds, and its body.
type FLVTag struct {
	Type      uint8
	Timestamp uint32
	Data      []byte
}

// An FLVReader reads the tags of an FLV file.
type FLVReader struct {
	r *bufio.Reader
	// HasAudio and HasVideo are the flags of the FLV header.
	HasAudio, HasVideo bool
}

// NewFLVReader reads the FLV header and the first previous tag size.
func NewFLVReader(r io.Reader) (*FLVReader, error) {
	br := bufio.NewReader(r)
	var h [9]byte
	if _, err := io.ReadFull(br, h[:]); err != nil {
		return nil, fmt.Errorf("read FLV header: %w", err)
	}
	if string(h[:3]) != "FLV" {
		return nil, fmt.Errorf("not an FLV file: %q", h[:3])
	}
	offset := int(h[5])<<24 | int(h[6])<<16 | int(h[7])<<8 | int(h[8])
	if offset < 9 {
		return nil, fmt.Errorf("bad FLV header size %v", offset)
	}
	// Skip the rest of the header and the first previous tag size.
	if _, err := br.Discard(offset - 9 + 4); err != nil {
		return nil, fmt.Errorf("read FLV header: %w", unexpected(err))
	}
	return &FLVReader{r: br, HasAudio: h[4]&0x04 != 0, HasVideo: h[4]&0x01 != 0}, nil
}

// ReadTag returns the next tag, or io.EOF after the last one.
func (r *FLVReader) ReadTag() (*FLVTag, error) {
	var h [11]byte
	if _, err := io.ReadFull(r.r, h[:]); err != nil {
		if err == io.EOF {
			return nil, io.EOF
		}
		return nil, fmt.Errorf("read FLV tag header: %w", unexpected(err))
	}
	size := int(h[1])<<16 | int(h[2])<<8 | int(h[3])
	tag := &FLVTag{
		Type:      h[0] & 0x1f,
		Timestamp: uint32(h[7])<<24 | uint32(h[4])<<16 | uint32(h[5])<<8 | uint32(h[6]),
		Data:      make([]byte, size),
	}
	if _, err := io.ReadFull(r.r, tag.Data); err != nil {
		return nil, fmt.Errorf("read FLV tag body: %w", unexpected(err))
	}
	if _, err := r.r.Discard(4); err != nil {
		return nil, fmt.Errorf("read FLV previous tag size: %w", unexpected(err))
	}
	return tag, nil
}

// unexpected turns an EOF inside a structure into io.ErrUnexpectedEOF.
func unexpected(err error) error {
	if err == io.EOF {
		return io.ErrUnexpectedEOF
	}
	return err
}

// A VideoTag is the body of an FLV video tag, legacy or enhanced RTMP.
type VideoTag struct {
	// Codec is "h264", "hevc", "av1" or "vp9".
	Codec string
	Key   bool
	// SequenceHeader is set for the decoder configuration record.
	SequenceHeader bool
	// CTS is the composition time offset in milliseconds.
	CTS int32
	// Payload is the configuration record, or the frame.
	Payload []byte
	// EndOfSequence is set for the tag that ends the stream; it is no frame.
	EndOfSequence bool
	// Metadata is set for an enhanced RTMP metadata packet, such as the
	// colour info; it is no frame.
	Metadata bool
}

// The enhanced RTMP video packet types.
const (
	exSequenceStart  = 0
	exCodedFrames    = 1
	exSequenceEnd    = 2
	exCodedFramesX   = 3
	exMetadata       = 4
	exMPEG2TSSeqHead = 5
)

var fourCCs = map[string]string{"avc1": "h264", "hvc1": "hevc", "av01": "av1", "vp09": "vp9"}

// ParseVideo parses the body of a video tag.
func ParseVideo(data []byte) (VideoTag, error) {
	if len(data) < 1 {
		return VideoTag{}, errors.New("empty video tag")
	}
	var v VideoTag
	if data[0]&0x80 == 0 {
		// Legacy: frame type, codec id, then the AVC packet type and CTS.
		v.Key = data[0]>>4 == 1
		if codec := data[0] & 0x0f; codec != 7 {
			return v, fmt.Errorf("unsupported video codec id %v", codec)
		}
		if len(data) < 5 {
			return v, fmt.Errorf("short AVC video tag of %v bytes", len(data))
		}
		v.Codec = "h264"
		v.SequenceHeader = data[1] == 0
		v.EndOfSequence = data[1] == 2
		v.CTS = si24(data[2:5])
		v.Payload = data[5:]
		return v, nil
	}

	// Enhanced RTMP: frame type, packet type, then the FourCC.
	v.Key = (data[0]>>4)&0x07 == 1
	packet := data[0] & 0x0f
	if len(data) < 5 {
		return v, fmt.Errorf("short enhanced video tag of %v bytes", len(data))
	}
	codec, ok := fourCCs[string(data[1:5])]
	if !ok {
		return v, fmt.Errorf("unsupported video FourCC %q", data[1:5])
	}
	v.Codec = codec
	body := data[5:]
	switch packet {
	case exSequenceStart, exMPEG2TSSeqHead:
		v.SequenceHeader = true
	case exSequenceEnd:
		v.EndOfSequence = true
	case exMetadata:
		v.Metadata = true
	case exCodedFrames:
		// Only AVC and HEVC carry a composition time here.
		if codec == "h264" || codec == "hevc" {
			if len(body) < 3 {
				return v, errors.New("short enhanced video tag, no CTS")
			}
			v.CTS = si24(body[:3])
			body = body[3:]
		}
	}
	v.Payload = body
	return v, nil
}

// si24 reads a signed 24-bit big-endian integer.
func si24(b []byte) int32 {
	v := int32(b[0])<<16 | int32(b[1])<<8 | int32(b[2])
	if v&0x800000 != 0 {
		v -= 1 << 24
	}
	return v
}

// An AudioTag is the body of an FLV audio tag.
type AudioTag struct {
	// Codec is "aac" or "mp3".
	Codec string
	// SequenceHeader is set for the AAC AudioSpecificConfig.
	SequenceHeader bool
	Payload        []byte
}

// ParseAudio parses the body of an audio tag.
func ParseAudio(data []byte) (AudioTag, error) {
	if len(data) < 1 {
		return AudioTag{}, errors.New("empty audio tag")
	}
	switch format := data[0] >> 4; format {
	case 10:
		if len(data) < 2 {
			return AudioTag{}, errors.New("short AAC audio tag")
		}
		return AudioTag{Codec: "aac", SequenceHeader: data[1] == 0, Payload: data[2:]}, nil
	case 2:
		return AudioTag{Codec: "mp3", Payload: data[1:]}, nil
	default:
		return AudioTag{}, fmt.Errorf("unsupported audio format %v", format)
	}
}
