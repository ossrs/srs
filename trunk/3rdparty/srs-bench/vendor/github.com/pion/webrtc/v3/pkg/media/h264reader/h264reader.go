// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

// Package h264reader implements a H264 Annex-B Reader
package h264reader

import (
	"bytes"
	"errors"
	"io"
)

// H264Reader reads data from stream and constructs h264 nal units
type H264Reader struct {
	stream                      io.Reader
	nalBuffer                   []byte
	countOfConsecutiveZeroBytes int
	nalPrefixParsed             bool
	readBuffer                  []byte
	tmpReadBuf                  []byte
}

var (
	errNilReader           = errors.New("stream is nil")
	errDataIsNotH264Stream = errors.New("data is not a H264 bitstream")
)

// NewReader creates new H264Reader
func NewReader(in io.Reader) (*H264Reader, error) {
	if in == nil {
		return nil, errNilReader
	}

	reader := &H264Reader{
		stream:          in,
		nalBuffer:       make([]byte, 0),
		nalPrefixParsed: false,
		readBuffer:      make([]byte, 0),
		tmpReadBuf:      make([]byte, 4096),
	}

	return reader, nil
}

// NAL H.264 Network Abstraction Layer
type NAL struct {
	PictureOrderCount uint32

	// NAL header
	ForbiddenZeroBit bool
	RefIdc           uint8
	UnitType         NalUnitType

	Data []byte // header byte + rbsp
}

func (reader *H264Reader) read(numToRead int) (data []byte, e error) {
	for len(reader.readBuffer) < numToRead {
		n, err := reader.stream.Read(reader.tmpReadBuf)
		if err != nil {
			return nil, err
		}
		if n == 0 {
			break
		}
		reader.readBuffer = append(reader.readBuffer, reader.tmpReadBuf[0:n]...)
	}
	var numShouldRead int
	if numToRead <= len(reader.readBuffer) {
		numShouldRead = numToRead
	} else {
		numShouldRead = len(reader.readBuffer)
	}
	data = reader.readBuffer[0:numShouldRead]
	reader.readBuffer = reader.readBuffer[numShouldRead:]
	return data, nil
}

func (reader *H264Reader) bitStreamStartsWithH264Prefix() (prefixLength int, e error) {
	nalPrefix3Bytes := []byte{0, 0, 1}
	nalPrefix4Bytes := []byte{0, 0, 0, 1}

	prefixBuffer, e := reader.read(4)
	if e != nil {
		return
	}

	n := len(prefixBuffer)

	if n == 0 {
		return 0, io.EOF
	}

	if n < 3 {
		return 0, errDataIsNotH264Stream
	}

	nalPrefix3BytesFound := bytes.Equal(nalPrefix3Bytes, prefixBuffer[:3])
	if n == 3 {
		if nalPrefix3BytesFound {
			return 0, io.EOF
		}
		return 0, errDataIsNotH264Stream
	}

	// n == 4
	if nalPrefix3BytesFound {
		reader.nalBuffer = append(reader.nalBuffer, prefixBuffer[3])
		return 3, nil
	}

	nalPrefix4BytesFound := bytes.Equal(nalPrefix4Bytes, prefixBuffer)
	if nalPrefix4BytesFound {
		return 4, nil
	}
	return 0, errDataIsNotH264Stream
}

// NextNAL reads from stream and returns then next NAL,
// and an error if there is incomplete frame data.
// Returns all nil values when no more NALs are available.
func (reader *H264Reader) NextNAL() (*NAL, error) {
	if !reader.nalPrefixParsed {
		_, err := reader.bitStreamStartsWithH264Prefix()
		if err != nil {
			return nil, err
		}

		reader.nalPrefixParsed = true
	}

	for {
		buffer, err := reader.read(1)
		if err != nil {
			break
		}

		n := len(buffer)

		if n != 1 {
			break
		}
		readByte := buffer[0]
		nalFound := reader.processByte(readByte)
		if nalFound {
			nal := newNal(reader.nalBuffer)
			nal.parseHeader()
			if nal.UnitType == NalUnitTypeSEI {
				reader.nalBuffer = nil
				continue
			}
			break
		}

		reader.nalBuffer = append(reader.nalBuffer, readByte)
	}

	if len(reader.nalBuffer) == 0 {
		return nil, io.EOF
	}

	nal := newNal(reader.nalBuffer)
	reader.nalBuffer = nil
	nal.parseHeader()

	return nal, nil
}

func (reader *H264Reader) processByte(readByte byte) (nalFound bool) {
	nalFound = false

	switch readByte {
	case 0:
		reader.countOfConsecutiveZeroBytes++
	case 1:
		if reader.countOfConsecutiveZeroBytes >= 2 {
			countOfConsecutiveZeroBytesInPrefix := 2
			if reader.countOfConsecutiveZeroBytes > 2 {
				countOfConsecutiveZeroBytesInPrefix = 3
			}

			if nalUnitLength := len(reader.nalBuffer) - countOfConsecutiveZeroBytesInPrefix; nalUnitLength > 0 {
				reader.nalBuffer = reader.nalBuffer[0:nalUnitLength]
				nalFound = true
			}
		}

		reader.countOfConsecutiveZeroBytes = 0
	default:
		reader.countOfConsecutiveZeroBytes = 0
	}

	return nalFound
}

func newNal(data []byte) *NAL {
	return &NAL{PictureOrderCount: 0, ForbiddenZeroBit: false, RefIdc: 0, UnitType: NalUnitTypeUnspecified, Data: data}
}

func (h *NAL) parseHeader() {
	firstByte := h.Data[0]
	h.ForbiddenZeroBit = (((firstByte & 0x80) >> 7) == 1) // 0x80 = 0b10000000
	h.RefIdc = (firstByte & 0x60) >> 5                    // 0x60 = 0b01100000
	h.UnitType = NalUnitType((firstByte & 0x1F) >> 0)     // 0x1F = 0b00011111
}
