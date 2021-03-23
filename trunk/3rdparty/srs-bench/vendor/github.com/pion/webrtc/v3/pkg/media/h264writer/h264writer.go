// Package h264writer implements H264 media container writer
package h264writer

import (
	"bytes"
	"encoding/binary"
	"io"
	"os"

	"github.com/pion/rtp"
	"github.com/pion/rtp/codecs"
)

type (
	// H264Writer is used to take RTP packets, parse them and
	// write the data to an io.Writer.
	// Currently it only supports non-interleaved mode
	// Therefore, only 1-23, 24 (STAP-A), 28 (FU-A) NAL types are allowed.
	// https://tools.ietf.org/html/rfc6184#section-5.2
	H264Writer struct {
		writer      io.Writer
		hasKeyFrame bool
	}
)

// New builds a new H264 writer
func New(filename string) (*H264Writer, error) {
	f, err := os.Create(filename)
	if err != nil {
		return nil, err
	}

	return NewWith(f), nil
}

// NewWith initializes a new H264 writer with an io.Writer output
func NewWith(w io.Writer) *H264Writer {
	return &H264Writer{
		writer: w,
	}
}

// WriteRTP adds a new packet and writes the appropriate headers for it
func (h *H264Writer) WriteRTP(packet *rtp.Packet) error {
	if len(packet.Payload) == 0 {
		return nil
	}

	if !h.hasKeyFrame {
		if h.hasKeyFrame = isKeyFrame(packet.Payload); !h.hasKeyFrame {
			// key frame not defined yet. discarding packet
			return nil
		}
	}

	data, err := (&codecs.H264Packet{}).Unmarshal(packet.Payload)
	if err != nil {
		return err
	}

	_, err = h.writer.Write(data)

	return err
}

// Close closes the underlying writer
func (h *H264Writer) Close() error {
	if h.writer != nil {
		if closer, ok := h.writer.(io.Closer); ok {
			return closer.Close()
		}
	}

	return nil
}

func isKeyFrame(data []byte) bool {
	const typeSTAPA = 24

	var word uint32

	payload := bytes.NewReader(data)
	err := binary.Read(payload, binary.BigEndian, &word)

	if err != nil || (word&0x1F000000)>>24 != typeSTAPA {
		return false
	}

	return word&0x1F == 7
}
