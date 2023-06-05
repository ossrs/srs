// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

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
		writer       io.Writer
		hasKeyFrame  bool
		cachedPacket *codecs.H264Packet
	}
)

// New builds a new H264 writer
func New(filename string) (*H264Writer, error) {
	f, err := os.Create(filename) //nolint:gosec
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

	if h.cachedPacket == nil {
		h.cachedPacket = &codecs.H264Packet{}
	}

	data, err := h.cachedPacket.Unmarshal(packet.Payload)
	if err != nil {
		return err
	}

	_, err = h.writer.Write(data)

	return err
}

// Close closes the underlying writer
func (h *H264Writer) Close() error {
	h.cachedPacket = nil
	if h.writer != nil {
		if closer, ok := h.writer.(io.Closer); ok {
			return closer.Close()
		}
	}

	return nil
}

func isKeyFrame(data []byte) bool {
	const (
		typeSTAPA       = 24
		typeSPS         = 7
		naluTypeBitmask = 0x1F
	)

	var word uint32

	payload := bytes.NewReader(data)
	if err := binary.Read(payload, binary.BigEndian, &word); err != nil {
		return false
	}

	naluType := (word >> 24) & naluTypeBitmask
	if naluType == typeSTAPA && word&naluTypeBitmask == typeSPS {
		return true
	} else if naluType == typeSPS {
		return true
	}

	return false
}
