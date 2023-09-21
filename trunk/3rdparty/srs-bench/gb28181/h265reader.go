// Package h265reader implements a H265 Annex-B Reader
package gb28181

import (
	"bytes"
	"errors"
	"io"
)

type NalUnitType uint8

// Enums for NalUnitTypes
const (
	NaluTypeSliceTrailN NalUnitType = 0 // 0x0
	NaluTypeSliceTrailR NalUnitType = 1 // 0x01
	NaluTypeSliceTsaN   NalUnitType = 2 // 0x02
	NaluTypeSliceTsaR   NalUnitType = 3 // 0x03
	NaluTypeSliceStsaN  NalUnitType = 4 // 0x04
	NaluTypeSliceStsaR  NalUnitType = 5 // 0x05
	NaluTypeSliceRadlN  NalUnitType = 6 // 0x06
	NaluTypeSliceRadlR  NalUnitType = 7 // 0x07
	NaluTypeSliceRaslN  NalUnitType = 8 // 0x06
	NaluTypeSliceRaslR  NalUnitType = 9 // 0x09

	NaluTypeSliceBlaWlp       NalUnitType = 16 // 0x10
	NaluTypeSliceBlaWradl     NalUnitType = 17 // 0x11
	NaluTypeSliceBlaNlp       NalUnitType = 18 // 0x12
	NaluTypeSliceIdr          NalUnitType = 19 // 0x13
	NaluTypeSliceIdrNlp       NalUnitType = 20 // 0x14
	NaluTypeSliceCranut       NalUnitType = 21 // 0x15
	NaluTypeSliceRsvIrapVcl22 NalUnitType = 22 // 0x16
	NaluTypeSliceRsvIrapVcl23 NalUnitType = 23 // 0x17

	NaluTypeVps       NalUnitType = 32 // 0x20
	NaluTypeSps       NalUnitType = 33 // 0x21
	NaluTypePps       NalUnitType = 34 // 0x22
	NaluTypeAud       NalUnitType = 35 // 0x23
	NaluTypeSei       NalUnitType = 39 // 0x27
	NaluTypeSeiSuffix NalUnitType = 40 // 0x28

	NaluTypeUnspecified NalUnitType = 48 // 0x30
)

// H265Reader reads data from stream and constructs h265 nal units
type H265Reader struct {
	stream                      io.Reader
	nalBuffer                   []byte
	countOfConsecutiveZeroBytes int
	nalPrefixParsed             bool
	readBuffer                  []byte
}

var (
	errNilReader           = errors.New("stream is nil")
	errDataIsNotH265Stream = errors.New("data is not a H265 bitstream")
)

// NewReader creates new H265Reader
func NewReader(in io.Reader) (*H265Reader, error) {
	if in == nil {
		return nil, errNilReader
	}

	reader := &H265Reader{
		stream:          in,
		nalBuffer:       make([]byte, 0),
		nalPrefixParsed: false,
		readBuffer:      make([]byte, 0),
	}

	return reader, nil
}

// NAL H.265 Network Abstraction Layer
type NAL struct {
	PictureOrderCount uint32

	// NAL header
	ForbiddenZeroBit   bool
	UnitType           NalUnitType
	NuhLayerId         uint8
	NuhTemporalIdPlus1 uint8

	Data []byte // header byte + rbsp
}

func (reader *H265Reader) read(numToRead int) (data []byte) {
	for len(reader.readBuffer) < numToRead {
		buf := make([]byte, 4096)
		n, err := reader.stream.Read(buf)
		if n == 0 || err != nil {
			break
		}
		buf = buf[0:n]
		reader.readBuffer = append(reader.readBuffer, buf...)
	}
	var numShouldRead int
	if numToRead <= len(reader.readBuffer) {
		numShouldRead = numToRead
	} else {
		numShouldRead = len(reader.readBuffer)
	}
	data = reader.readBuffer[0:numShouldRead]
	reader.readBuffer = reader.readBuffer[numShouldRead:]
	return data
}

func (reader *H265Reader) bitStreamStartsWithH265Prefix() (prefixLength int, e error) {
	nalPrefix3Bytes := []byte{0, 0, 1}
	nalPrefix4Bytes := []byte{0, 0, 0, 1}

	prefixBuffer := reader.read(4)

	n := len(prefixBuffer)

	if n == 0 {
		return 0, io.EOF
	}

	if n < 3 {
		return 0, errDataIsNotH265Stream
	}

	nalPrefix3BytesFound := bytes.Equal(nalPrefix3Bytes, prefixBuffer[:3])
	if n == 3 {
		if nalPrefix3BytesFound {
			return 0, io.EOF
		}
		return 0, errDataIsNotH265Stream
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
	return 0, errDataIsNotH265Stream
}

// NextNAL reads from stream and returns then next NAL,
// and an error if there is incomplete frame data.
// Returns all nil values when no more NALs are available.
func (reader *H265Reader) NextNAL() (*NAL, error) {
	if !reader.nalPrefixParsed {
		_, err := reader.bitStreamStartsWithH265Prefix()
		if err != nil {
			return nil, err
		}

		reader.nalPrefixParsed = true
	}

	for {
		buffer := reader.read(1)
		n := len(buffer)

		if n != 1 {
			break
		}
		readByte := buffer[0]
		nalFound := reader.processByte(readByte)
		if nalFound {
			nal := newNal(reader.nalBuffer)
			nal.parseHeader()
			if nal.UnitType == NaluTypeSeiSuffix || nal.UnitType == NaluTypeSei {
				reader.nalBuffer = nil
				continue
			} else {
				break
			}
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

func (reader *H265Reader) processByte(readByte byte) (nalFound bool) {
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
			nalUnitLength := len(reader.nalBuffer) - countOfConsecutiveZeroBytesInPrefix
			reader.nalBuffer = reader.nalBuffer[0:nalUnitLength]
			reader.countOfConsecutiveZeroBytes = 0
			nalFound = true
		} else {
			reader.countOfConsecutiveZeroBytes = 0
		}
	default:
		reader.countOfConsecutiveZeroBytes = 0
	}

	return nalFound
}

func newNal(data []byte) *NAL {
	return &NAL{PictureOrderCount: 0, ForbiddenZeroBit: false, UnitType: NaluTypeUnspecified, Data: data}
}

func (h *NAL) parseHeader() {
	firstByte := h.Data[0]
	h.ForbiddenZeroBit = (((firstByte & 0x80) >> 7) == 1) // 0x80 = 0b10000000
	h.UnitType = NalUnitType((firstByte & 0x7E) >> 1)     // 0x1F = 0b01111110
}
