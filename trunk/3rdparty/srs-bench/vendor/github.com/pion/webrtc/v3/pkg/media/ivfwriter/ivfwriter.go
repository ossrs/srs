// Package ivfwriter implements IVF media container writer
package ivfwriter

import (
	"encoding/binary"
	"errors"
	"io"
	"os"

	"github.com/pion/rtp"
	"github.com/pion/rtp/codecs"
)

var (
	errFileNotOpened    = errors.New("file not opened")
	errInvalidNilPacket = errors.New("invalid nil packet")
)

// IVFWriter is used to take RTP packets and write them to an IVF on disk
type IVFWriter struct {
	ioWriter     io.Writer
	count        uint64
	seenKeyFrame bool
	currentFrame []byte
}

// New builds a new IVF writer
func New(fileName string) (*IVFWriter, error) {
	f, err := os.Create(fileName)
	if err != nil {
		return nil, err
	}
	writer, err := NewWith(f)
	if err != nil {
		return nil, err
	}
	writer.ioWriter = f
	return writer, nil
}

// NewWith initialize a new IVF writer with an io.Writer output
func NewWith(out io.Writer) (*IVFWriter, error) {
	if out == nil {
		return nil, errFileNotOpened
	}

	writer := &IVFWriter{
		ioWriter:     out,
		seenKeyFrame: false,
	}
	if err := writer.writeHeader(); err != nil {
		return nil, err
	}
	return writer, nil
}

func (i *IVFWriter) writeHeader() error {
	header := make([]byte, 32)
	copy(header[0:], "DKIF")                        // DKIF
	binary.LittleEndian.PutUint16(header[4:], 0)    // Version
	binary.LittleEndian.PutUint16(header[6:], 32)   // Header size
	copy(header[8:], "VP80")                        // FOURCC
	binary.LittleEndian.PutUint16(header[12:], 640) // Width in pixels
	binary.LittleEndian.PutUint16(header[14:], 480) // Height in pixels
	binary.LittleEndian.PutUint32(header[16:], 30)  // Framerate denominator
	binary.LittleEndian.PutUint32(header[20:], 1)   // Framerate numerator
	binary.LittleEndian.PutUint32(header[24:], 900) // Frame count, will be updated on first Close() call
	binary.LittleEndian.PutUint32(header[28:], 0)   // Unused

	_, err := i.ioWriter.Write(header)
	return err
}

// WriteRTP adds a new packet and writes the appropriate headers for it
func (i *IVFWriter) WriteRTP(packet *rtp.Packet) error {
	if i.ioWriter == nil {
		return errFileNotOpened
	}

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

	frameHeader := make([]byte, 12)
	binary.LittleEndian.PutUint32(frameHeader[0:], uint32(len(i.currentFrame))) // Frame length
	binary.LittleEndian.PutUint64(frameHeader[4:], i.count)                     // PTS

	i.count++

	if _, err := i.ioWriter.Write(frameHeader); err != nil {
		return err
	} else if _, err := i.ioWriter.Write(i.currentFrame); err != nil {
		return err
	}

	i.currentFrame = nil
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
