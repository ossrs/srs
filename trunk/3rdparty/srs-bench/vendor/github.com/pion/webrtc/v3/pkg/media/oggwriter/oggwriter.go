// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

// Package oggwriter implements OGG media container writer
package oggwriter

import (
	"encoding/binary"
	"errors"
	"io"
	"os"

	"github.com/pion/randutil"
	"github.com/pion/rtp"
	"github.com/pion/rtp/codecs"
)

const (
	pageHeaderTypeContinuationOfStream = 0x00
	pageHeaderTypeBeginningOfStream    = 0x02
	pageHeaderTypeEndOfStream          = 0x04
	defaultPreSkip                     = 3840 // 3840 recommended in the RFC
	idPageSignature                    = "OpusHead"
	commentPageSignature               = "OpusTags"
	pageHeaderSignature                = "OggS"
)

var (
	errFileNotOpened    = errors.New("file not opened")
	errInvalidNilPacket = errors.New("invalid nil packet")
)

// OggWriter is used to take RTP packets and write them to an OGG on disk
type OggWriter struct {
	stream                  io.Writer
	fd                      *os.File
	sampleRate              uint32
	channelCount            uint16
	serial                  uint32
	pageIndex               uint32
	checksumTable           *[256]uint32
	previousGranulePosition uint64
	previousTimestamp       uint32
	lastPayloadSize         int
}

// New builds a new OGG Opus writer
func New(fileName string, sampleRate uint32, channelCount uint16) (*OggWriter, error) {
	f, err := os.Create(fileName) //nolint:gosec
	if err != nil {
		return nil, err
	}
	writer, err := NewWith(f, sampleRate, channelCount)
	if err != nil {
		return nil, f.Close()
	}
	writer.fd = f
	return writer, nil
}

// NewWith initialize a new OGG Opus writer with an io.Writer output
func NewWith(out io.Writer, sampleRate uint32, channelCount uint16) (*OggWriter, error) {
	if out == nil {
		return nil, errFileNotOpened
	}

	writer := &OggWriter{
		stream:        out,
		sampleRate:    sampleRate,
		channelCount:  channelCount,
		serial:        randutil.NewMathRandomGenerator().Uint32(),
		checksumTable: generateChecksumTable(),

		// Timestamp and Granule MUST start from 1
		// Only headers can have 0 values
		previousTimestamp:       1,
		previousGranulePosition: 1,
	}
	if err := writer.writeHeaders(); err != nil {
		return nil, err
	}

	return writer, nil
}

/*
    ref: https://tools.ietf.org/html/rfc7845.html
    https://git.xiph.org/?p=opus-tools.git;a=blob;f=src/opus_header.c#l219

       Page 0         Pages 1 ... n        Pages (n+1) ...
    +------------+ +---+ +---+ ... +---+ +-----------+ +---------+ +--
    |            | |   | |   |     |   | |           | |         | |
    |+----------+| |+-----------------+| |+-------------------+ +-----
    |||ID Header|| ||  Comment Header || ||Audio Data Packet 1| | ...
    |+----------+| |+-----------------+| |+-------------------+ +-----
    |            | |   | |   |     |   | |           | |         | |
    +------------+ +---+ +---+ ... +---+ +-----------+ +---------+ +--
    ^      ^                           ^
    |      |                           |
    |      |                           Mandatory Page Break
    |      |
    |      ID header is contained on a single page
    |
    'Beginning Of Stream'

   Figure 1: Example Packet Organization for a Logical Ogg Opus Stream
*/

func (i *OggWriter) writeHeaders() error {
	// ID Header
	oggIDHeader := make([]byte, 19)

	copy(oggIDHeader[0:], idPageSignature)                          // Magic Signature 'OpusHead'
	oggIDHeader[8] = 1                                              // Version
	oggIDHeader[9] = uint8(i.channelCount)                          // Channel count
	binary.LittleEndian.PutUint16(oggIDHeader[10:], defaultPreSkip) // pre-skip
	binary.LittleEndian.PutUint32(oggIDHeader[12:], i.sampleRate)   // original sample rate, any valid sample e.g 48000
	binary.LittleEndian.PutUint16(oggIDHeader[16:], 0)              // output gain
	oggIDHeader[18] = 0                                             // channel map 0 = one stream: mono or stereo

	// Reference: https://tools.ietf.org/html/rfc7845.html#page-6
	// RFC specifies that the ID Header page should have a granule position of 0 and a Header Type set to 2 (StartOfStream)
	data := i.createPage(oggIDHeader, pageHeaderTypeBeginningOfStream, 0, i.pageIndex)
	if err := i.writeToStream(data); err != nil {
		return err
	}
	i.pageIndex++

	// Comment Header
	oggCommentHeader := make([]byte, 21)
	copy(oggCommentHeader[0:], commentPageSignature)        // Magic Signature 'OpusTags'
	binary.LittleEndian.PutUint32(oggCommentHeader[8:], 5)  // Vendor Length
	copy(oggCommentHeader[12:], "pion")                     // Vendor name 'pion'
	binary.LittleEndian.PutUint32(oggCommentHeader[17:], 0) // User Comment List Length

	// RFC specifies that the page where the CommentHeader completes should have a granule position of 0
	data = i.createPage(oggCommentHeader, pageHeaderTypeContinuationOfStream, 0, i.pageIndex)
	if err := i.writeToStream(data); err != nil {
		return err
	}
	i.pageIndex++

	return nil
}

const (
	pageHeaderSize = 27
)

func (i *OggWriter) createPage(payload []uint8, headerType uint8, granulePos uint64, pageIndex uint32) []byte {
	i.lastPayloadSize = len(payload)
	page := make([]byte, pageHeaderSize+1+i.lastPayloadSize)

	copy(page[0:], pageHeaderSignature)                 // page headers starts with 'OggS'
	page[4] = 0                                         // Version
	page[5] = headerType                                // 1 = continuation, 2 = beginning of stream, 4 = end of stream
	binary.LittleEndian.PutUint64(page[6:], granulePos) // granule position
	binary.LittleEndian.PutUint32(page[14:], i.serial)  // Bitstream serial number
	binary.LittleEndian.PutUint32(page[18:], pageIndex) // Page sequence number
	page[26] = 1                                        // Number of segments in page, giving always 1 segment
	page[27] = uint8(i.lastPayloadSize)                 // Segment Table inserting at 27th position since page header length is 27
	copy(page[28:], payload)                            // inserting at 28th since Segment Table(1) + header length(27)

	var checksum uint32
	for index := range page {
		checksum = (checksum << 8) ^ i.checksumTable[byte(checksum>>24)^page[index]]
	}
	binary.LittleEndian.PutUint32(page[22:], checksum) // Checksum - generating for page data and inserting at 22th position into 32 bits

	return page
}

// WriteRTP adds a new packet and writes the appropriate headers for it
func (i *OggWriter) WriteRTP(packet *rtp.Packet) error {
	if packet == nil {
		return errInvalidNilPacket
	}
	if len(packet.Payload) == 0 {
		return nil
	}

	opusPacket := codecs.OpusPacket{}
	if _, err := opusPacket.Unmarshal(packet.Payload); err != nil {
		// Only handle Opus packets
		return err
	}

	payload := opusPacket.Payload[0:]

	// Should be equivalent to sampleRate * duration
	if i.previousTimestamp != 1 {
		increment := packet.Timestamp - i.previousTimestamp
		i.previousGranulePosition += uint64(increment)
	}
	i.previousTimestamp = packet.Timestamp

	data := i.createPage(payload, pageHeaderTypeContinuationOfStream, i.previousGranulePosition, i.pageIndex)
	i.pageIndex++
	return i.writeToStream(data)
}

// Close stops the recording
func (i *OggWriter) Close() error {
	defer func() {
		i.fd = nil
		i.stream = nil
	}()

	// Returns no error has it may be convenient to call
	// Close() multiple times
	if i.fd == nil {
		// Close stream if we are operating on a stream
		if closer, ok := i.stream.(io.Closer); ok {
			return closer.Close()
		}
		return nil
	}

	// Seek back one page, we need to update the header and generate new CRC
	pageOffset, err := i.fd.Seek(-1*int64(i.lastPayloadSize+pageHeaderSize+1), 2)
	if err != nil {
		return err
	}

	payload := make([]byte, i.lastPayloadSize)
	if _, err := i.fd.ReadAt(payload, pageOffset+pageHeaderSize+1); err != nil {
		return err
	}

	data := i.createPage(payload, pageHeaderTypeEndOfStream, i.previousGranulePosition, i.pageIndex-1)
	if err := i.writeToStream(data); err != nil {
		return err
	}

	// Update the last page if we are operating on files
	// to mark it as the EOS
	return i.fd.Close()
}

// Wraps writing to the stream and maintains state
// so we can set values for EOS
func (i *OggWriter) writeToStream(p []byte) error {
	if i.stream == nil {
		return errFileNotOpened
	}

	_, err := i.stream.Write(p)
	return err
}

func generateChecksumTable() *[256]uint32 {
	var table [256]uint32
	const poly = 0x04c11db7

	for i := range table {
		r := uint32(i) << 24
		for j := 0; j < 8; j++ {
			if (r & 0x80000000) != 0 {
				r = (r << 1) ^ poly
			} else {
				r <<= 1
			}
			table[i] = (r & 0xffffffff)
		}
	}
	return &table
}
