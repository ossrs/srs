// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

// Package ivfwriter implements IVF media container writer
package ivfwriter

import (
	"encoding/binary"
	"errors"
	"io"
	"os"

	"github.com/pion/rtp"
	"github.com/pion/rtp/codecs"
	"github.com/pion/rtp/pkg/frame"
)

var (
	errFileNotOpened    = errors.New("file not opened")
	errInvalidNilPacket = errors.New("invalid nil packet")
	errCodecAlreadySet  = errors.New("codec is already set")
	errNoSuchCodec      = errors.New("no codec for this MimeType")
)

const (
	mimeTypeVP8 = "video/VP8"
	mimeTypeAV1 = "video/AV1"

	ivfFileHeaderSignature = "DKIF"
)

// IVFWriter is used to take RTP packets and write them to an IVF on disk
type IVFWriter struct {
	ioWriter     io.Writer
	count        uint64
	seenKeyFrame bool

	isVP8, isAV1 bool

	// VP8
	currentFrame []byte

	// AV1
	av1Frame frame.AV1
}

// New builds a new IVF writer
func New(fileName string, opts ...Option) (*IVFWriter, error) {
	f, err := os.Create(fileName) //nolint:gosec
	if err != nil {
		return nil, err
	}
	writer, err := NewWith(f, opts...)
	if err != nil {
		return nil, err
	}
	writer.ioWriter = f
	return writer, nil
}

// NewWith initialize a new IVF writer with an io.Writer output
func NewWith(out io.Writer, opts ...Option) (*IVFWriter, error) {
	if out == nil {
		return nil, errFileNotOpened
	}

	writer := &IVFWriter{
		ioWriter:     out,
		seenKeyFrame: false,
	}

	for _, o := range opts {
		if err := o(writer); err != nil {
			return nil, err
		}
	}

	if !writer.isAV1 && !writer.isVP8 {
		writer.isVP8 = true
	}

	if err := writer.writeHeader(); err != nil {
		return nil, err
	}
	return writer, nil
}

func (i *IVFWriter) writeHeader() error {
	header := make([]byte, 32)
	copy(header[0:], ivfFileHeaderSignature)      // DKIF
	binary.LittleEndian.PutUint16(header[4:], 0)  // Version
	binary.LittleEndian.PutUint16(header[6:], 32) // Header size

	// FOURCC
	if i.isVP8 {
		copy(header[8:], "VP80")
	} else if i.isAV1 {
		copy(header[8:], "AV01")
	}

	binary.LittleEndian.PutUint16(header[12:], 640) // Width in pixels
	binary.LittleEndian.PutUint16(header[14:], 480) // Height in pixels
	binary.LittleEndian.PutUint32(header[16:], 30)  // Framerate denominator
	binary.LittleEndian.PutUint32(header[20:], 1)   // Framerate numerator
	binary.LittleEndian.PutUint32(header[24:], 900) // Frame count, will be updated on first Close() call
	binary.LittleEndian.PutUint32(header[28:], 0)   // Unused

	_, err := i.ioWriter.Write(header)
	return err
}

func (i *IVFWriter) writeFrame(frame []byte) error {
	frameHeader := make([]byte, 12)
	binary.LittleEndian.PutUint32(frameHeader[0:], uint32(len(frame))) // Frame length
	binary.LittleEndian.PutUint64(frameHeader[4:], i.count)            // PTS
	i.count++

	if _, err := i.ioWriter.Write(frameHeader); err != nil {
		return err
	}
	_, err := i.ioWriter.Write(frame)
	return err
}

// WriteRTP adds a new packet and writes the appropriate headers for it
func (i *IVFWriter) WriteRTP(packet *rtp.Packet) error {
	if i.ioWriter == nil {
		return errFileNotOpened
	} else if len(packet.Payload) == 0 {
		return nil
	}

	if i.isVP8 {
		vp8Packet := codecs.VP8Packet{}
		if _, err := vp8Packet.Unmarshal(packet.Payload); err != nil {
			return err
		}

		isKeyFrame := vp8Packet.Payload[0] & 0x01
		switch {
		case !i.seenKeyFrame && isKeyFrame == 1:
			return nil
		case i.currentFrame == nil && vp8Packet.S != 1:
			return nil
		}

		i.seenKeyFrame = true
		i.currentFrame = append(i.currentFrame, vp8Packet.Payload[0:]...)

		if !packet.Marker {
			return nil
		} else if len(i.currentFrame) == 0 {
			return nil
		}

		if err := i.writeFrame(i.currentFrame); err != nil {
			return err
		}
		i.currentFrame = nil
	} else if i.isAV1 {
		av1Packet := &codecs.AV1Packet{}
		if _, err := av1Packet.Unmarshal(packet.Payload); err != nil {
			return err
		}

		obus, err := i.av1Frame.ReadFrames(av1Packet)
		if err != nil {
			return err
		}

		for j := range obus {
			if err := i.writeFrame(obus[j]); err != nil {
				return err
			}
		}
	}

	return nil
}

// Close stops the recording
func (i *IVFWriter) Close() error {
	if i.ioWriter == nil {
		// Returns no error as it may be convenient to call
		// Close() multiple times
		return nil
	}

	defer func() {
		i.ioWriter = nil
	}()

	if ws, ok := i.ioWriter.(io.WriteSeeker); ok {
		// Update the framecount
		if _, err := ws.Seek(24, 0); err != nil {
			return err
		}
		buff := make([]byte, 4)
		binary.LittleEndian.PutUint32(buff, uint32(i.count))
		if _, err := ws.Write(buff); err != nil {
			return err
		}
	}

	if closer, ok := i.ioWriter.(io.Closer); ok {
		return closer.Close()
	}

	return nil
}

// An Option configures a SampleBuilder.
type Option func(i *IVFWriter) error

// WithCodec configures if IVFWriter is writing AV1 or VP8 packets to disk
func WithCodec(mimeType string) Option {
	return func(i *IVFWriter) error {
		if i.isVP8 || i.isAV1 {
			return errCodecAlreadySet
		}

		switch mimeType {
		case mimeTypeVP8:
			i.isVP8 = true
		case mimeTypeAV1:
			i.isAV1 = true
		default:
			return errNoSuchCodec
		}

		return nil
	}
}
