package rtcp

// Author: adwpc

import (
	"encoding/binary"
	"errors"
	"fmt"
	"math"
)

// https://tools.ietf.org/html/draft-holmer-rmcat-transport-wide-cc-extensions-01#page-5
// 0                   1                   2                   3
// 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// |V=2|P|  FMT=15 |    PT=205     |           length              |
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// |                     SSRC of packet sender                     |
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// |                      SSRC of media source                     |
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// |      base sequence number     |      packet status count      |
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// |                 reference time                | fb pkt. count |
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// |          packet chunk         |         packet chunk          |
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// .                                                               .
// .                                                               .
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// |         packet chunk          |  recv delta   |  recv delta   |
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// .                                                               .
// .                                                               .
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// |           recv delta          |  recv delta   | zero padding  |
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+

// for packet status chunk
const (
	// type of packet status chunk
	TypeTCCRunLengthChunk    = 0
	TypeTCCStatusVectorChunk = 1

	// len of packet status chunk
	packetStatusChunkLength = 2
)

// type of packet status symbol and recv delta
const (
	// https://tools.ietf.org/html/draft-holmer-rmcat-transport-wide-cc-extensions-01#section-3.1.1
	TypeTCCPacketNotReceived = uint16(iota)
	TypeTCCPacketReceivedSmallDelta
	TypeTCCPacketReceivedLargeDelta
	// https://tools.ietf.org/html/draft-holmer-rmcat-transport-wide-cc-extensions-01#page-7
	// see Example 2: "packet received, w/o recv delta"
	TypeTCCPacketReceivedWithoutDelta
)

// for status vector chunk
const (
	// https://tools.ietf.org/html/draft-holmer-rmcat-transport-wide-cc-extensions-01#section-3.1.4
	TypeTCCSymbolSizeOneBit = 0
	TypeTCCSymbolSizeTwoBit = 1

	// Notice: RFC is wrong: "packet received" (0) and "packet not received" (1)
	// if S == TypeTCCSymbolSizeOneBit, symbol list will be: TypeTCCPacketNotReceived TypeTCCPacketReceivedSmallDelta
	// if S == TypeTCCSymbolSizeTwoBit, symbol list will be same as above:
)

func numOfBitsOfSymbolSize() map[uint16]uint16 {
	return map[uint16]uint16{
		TypeTCCSymbolSizeOneBit: 1,
		TypeTCCSymbolSizeTwoBit: 2,
	}
}

var (
	errPacketStatusChunkLength = errors.New("packet status chunk must be 2 bytes")
	errDeltaExceedLimit        = errors.New("delta exceed limit")
)

// PacketStatusChunk has two kinds:
// RunLengthChunk and StatusVectorChunk
type PacketStatusChunk interface {
	Marshal() ([]byte, error)
	Unmarshal(rawPacket []byte) error
}

// RunLengthChunk T=TypeTCCRunLengthChunk
// 0                   1
// 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// |T| S |       Run Length        |
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
type RunLengthChunk struct {
	PacketStatusChunk

	// T = TypeTCCRunLengthChunk
	Type uint16

	// S: type of packet status
	// kind: TypeTCCPacketNotReceived or...
	PacketStatusSymbol uint16

	// RunLength: count of S
	RunLength uint16
}

// Marshal ..
func (r RunLengthChunk) Marshal() ([]byte, error) {
	chunk := make([]byte, 2)

	// append 1 bit '0'
	dst, err := setNBitsOfUint16(0, 1, 0, 0)
	if err != nil {
		return nil, err
	}

	// append 2 bit PacketStatusSymbol
	dst, err = setNBitsOfUint16(dst, 2, 1, r.PacketStatusSymbol)
	if err != nil {
		return nil, err
	}

	// append 13 bit RunLength
	dst, err = setNBitsOfUint16(dst, 13, 3, r.RunLength)
	if err != nil {
		return nil, err
	}

	binary.BigEndian.PutUint16(chunk, dst)
	return chunk, nil
}

// Unmarshal ..
func (r *RunLengthChunk) Unmarshal(rawPacket []byte) error {
	if len(rawPacket) != packetStatusChunkLength {
		return errPacketStatusChunkLength
	}

	// record type
	r.Type = TypeTCCRunLengthChunk

	// get PacketStatusSymbol
	// r.PacketStatusSymbol = uint16(rawPacket[0] >> 5 & 0x03)
	r.PacketStatusSymbol = getNBitsFromByte(rawPacket[0], 1, 2)

	// get RunLength
	// r.RunLength = uint16(rawPacket[0]&0x1F)*256 + uint16(rawPacket[1])
	r.RunLength = getNBitsFromByte(rawPacket[0], 3, 5)<<8 + uint16(rawPacket[1])
	return nil
}

// StatusVectorChunk T=typeStatusVecotrChunk
// 0                   1
// 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// |T|S|       symbol list         |
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
type StatusVectorChunk struct {
	PacketStatusChunk
	// T = TypeTCCRunLengthChunk
	Type uint16

	// TypeTCCSymbolSizeOneBit or TypeTCCSymbolSizeTwoBit
	SymbolSize uint16

	// when SymbolSize = TypeTCCSymbolSizeOneBit, SymbolList is 14*1bit:
	// TypeTCCSymbolListPacketReceived or TypeTCCSymbolListPacketNotReceived
	// when SymbolSize = TypeTCCSymbolSizeTwoBit, SymbolList is 7*2bit:
	// TypeTCCPacketNotReceived TypeTCCPacketReceivedSmallDelta TypeTCCPacketReceivedLargeDelta or typePacketReserved
	SymbolList []uint16
}

// Marshal ..
func (r StatusVectorChunk) Marshal() ([]byte, error) {
	chunk := make([]byte, 2)

	// set first bit '1'
	dst, err := setNBitsOfUint16(0, 1, 0, 1)
	if err != nil {
		return nil, err
	}

	// set second bit SymbolSize
	dst, err = setNBitsOfUint16(dst, 1, 1, r.SymbolSize)
	if err != nil {
		return nil, err
	}

	numOfBits := numOfBitsOfSymbolSize()[r.SymbolSize]
	// append 14 bit SymbolList
	for i, s := range r.SymbolList {
		index := numOfBits*uint16(i) + 2
		dst, err = setNBitsOfUint16(dst, numOfBits, index, s)
		if err != nil {
			return nil, err
		}
	}

	binary.BigEndian.PutUint16(chunk, dst)
	// set SymbolList(bit8-15)
	// chunk[1] = uint8(r.SymbolList) & 0x0f
	return chunk, nil
}

// Unmarshal ..
func (r *StatusVectorChunk) Unmarshal(rawPacket []byte) error {
	if len(rawPacket) != packetStatusChunkLength {
		return errPacketStatusChunkLength
	}

	r.Type = TypeTCCStatusVectorChunk
	r.SymbolSize = getNBitsFromByte(rawPacket[0], 1, 1)

	if r.SymbolSize == TypeTCCSymbolSizeOneBit {
		for i := uint16(0); i < 6; i++ {
			r.SymbolList = append(r.SymbolList, getNBitsFromByte(rawPacket[0], 2+i, 1))
		}
		for i := uint16(0); i < 8; i++ {
			r.SymbolList = append(r.SymbolList, getNBitsFromByte(rawPacket[1], i, 1))
		}
		return nil
	}
	if r.SymbolSize == TypeTCCSymbolSizeTwoBit {
		for i := uint16(0); i < 3; i++ {
			r.SymbolList = append(r.SymbolList, getNBitsFromByte(rawPacket[0], 2+i*2, 2))
		}
		for i := uint16(0); i < 4; i++ {
			r.SymbolList = append(r.SymbolList, getNBitsFromByte(rawPacket[1], i*2, 2))
		}
		return nil
	}

	r.SymbolSize = getNBitsFromByte(rawPacket[0], 2, 6)<<8 + uint16(rawPacket[1])
	return nil
}

const (
	// TypeTCCDeltaScaleFactor https://tools.ietf.org/html/draft-holmer-rmcat-transport-wide-cc-extensions-01#section-3.1.5
	TypeTCCDeltaScaleFactor = 250
)

// RecvDelta are represented as multiples of 250us
// small delta is 1 byte: [0，63.75]ms = [0, 63750]us = [0, 255]*250us
// big delta is 2 bytes: [-8192.0, 8191.75]ms = [-8192000, 8191750]us = [-32768, 32767]*250us
// https://tools.ietf.org/html/draft-holmer-rmcat-transport-wide-cc-extensions-01#section-3.1.5
type RecvDelta struct {
	Type uint16
	// us
	Delta int64
}

// Marshal ..
func (r RecvDelta) Marshal() ([]byte, error) {
	delta := r.Delta / TypeTCCDeltaScaleFactor

	// small delta
	if r.Type == TypeTCCPacketReceivedSmallDelta && delta >= 0 && delta <= math.MaxUint8 {
		deltaChunk := make([]byte, 1)
		deltaChunk[0] = byte(delta)
		return deltaChunk, nil
	}

	// big delta
	if r.Type == TypeTCCPacketReceivedLargeDelta && delta >= math.MinInt16 && delta <= math.MaxInt16 {
		deltaChunk := make([]byte, 2)
		binary.BigEndian.PutUint16(deltaChunk, uint16(delta))
		return deltaChunk, nil
	}

	// overflow
	return nil, errDeltaExceedLimit
}

// Unmarshal ..
func (r *RecvDelta) Unmarshal(rawPacket []byte) error {
	chunkLen := len(rawPacket)

	// must be 1 or 2 bytes
	if chunkLen != 1 && chunkLen != 2 {
		return errDeltaExceedLimit
	}

	if chunkLen == 1 {
		r.Type = TypeTCCPacketReceivedSmallDelta
		r.Delta = TypeTCCDeltaScaleFactor * int64(rawPacket[0])
		return nil
	}

	r.Type = TypeTCCPacketReceivedLargeDelta
	r.Delta = TypeTCCDeltaScaleFactor * int64(int16(binary.BigEndian.Uint16(rawPacket)))
	return nil
}

const (
	// the offset after header
	baseSequenceNumberOffset = 8
	packetStatusCountOffset  = 10
	referenceTimeOffset      = 12
	fbPktCountOffset         = 15
	packetChunkOffset        = 16
)

// TransportLayerCC for sender-BWE
// https://tools.ietf.org/html/draft-holmer-rmcat-transport-wide-cc-extensions-01#page-5
type TransportLayerCC struct {
	// header
	Header Header

	// SSRC of sender
	SenderSSRC uint32

	// SSRC of the media source
	MediaSSRC uint32

	// Transport wide sequence of rtp extension
	BaseSequenceNumber uint16

	// PacketStatusCount
	PacketStatusCount uint16

	// ReferenceTime
	ReferenceTime uint32

	// FbPktCount
	FbPktCount uint8

	// PacketChunks
	PacketChunks []PacketStatusChunk

	// RecvDeltas
	RecvDeltas []*RecvDelta
}

// Header returns the Header associated with this packet.
// func (t *TransportLayerCC) Header() Header {
// return t.Header
// return Header{
// Padding: true,
// Count:   FormatTCC,
// Type:    TypeTCCTransportSpecificFeedback,
// // https://tools.ietf.org/html/rfc4585#page-33
// Length: uint16((t.len() / 4) - 1),
// }
// }

func (t *TransportLayerCC) packetLen() uint16 {
	n := uint16(headerLength + packetChunkOffset + len(t.PacketChunks)*2)
	for _, d := range t.RecvDeltas {
		if d.Type == TypeTCCPacketReceivedSmallDelta {
			n++
		} else {
			n += 2
		}
	}
	return n
}

// Len return total bytes with padding
func (t *TransportLayerCC) Len() uint16 {
	n := t.packetLen()
	// has padding
	if n%4 != 0 {
		n = (n/4 + 1) * 4
	}

	return n
}

func (t TransportLayerCC) String() string {
	out := fmt.Sprintf("TransportLayerCC:\n\tHeader %v\n", t.Header)
	out += fmt.Sprintf("TransportLayerCC:\n\tSender Ssrc %d\n", t.SenderSSRC)
	out += fmt.Sprintf("\tMedia Ssrc %d\n", t.MediaSSRC)
	out += fmt.Sprintf("\tBase Sequence Number %d\n", t.BaseSequenceNumber)
	out += fmt.Sprintf("\tStatus Count %d\n", t.PacketStatusCount)
	out += fmt.Sprintf("\tReference Time %d\n", t.ReferenceTime)
	out += fmt.Sprintf("\tFeedback Packet Count %d\n", t.FbPktCount)
	out += "\tPacketChunks "
	for _, chunk := range t.PacketChunks {
		out += fmt.Sprintf("%+v ", chunk)
	}
	out += "\n\tRecvDeltas "
	for _, delta := range t.RecvDeltas {
		out += fmt.Sprintf("%+v ", delta)
	}
	out += "\n"
	return out
}

// Marshal encodes the TransportLayerCC in binary
func (t TransportLayerCC) Marshal() ([]byte, error) {
	header, err := t.Header.Marshal()
	if err != nil {
		return nil, err
	}

	payload := make([]byte, t.Len()-headerLength)
	binary.BigEndian.PutUint32(payload, t.SenderSSRC)
	binary.BigEndian.PutUint32(payload[4:], t.MediaSSRC)
	binary.BigEndian.PutUint16(payload[baseSequenceNumberOffset:], t.BaseSequenceNumber)
	binary.BigEndian.PutUint16(payload[packetStatusCountOffset:], t.PacketStatusCount)
	ReferenceTimeAndFbPktCount := appendNBitsToUint32(0, 24, t.ReferenceTime)
	ReferenceTimeAndFbPktCount = appendNBitsToUint32(ReferenceTimeAndFbPktCount, 8, uint32(t.FbPktCount))
	binary.BigEndian.PutUint32(payload[referenceTimeOffset:], ReferenceTimeAndFbPktCount)

	for i, chunk := range t.PacketChunks {
		b, err := chunk.Marshal()
		if err != nil {
			return nil, err
		}
		copy(payload[packetChunkOffset+i*2:], b)
	}

	recvDeltaOffset := packetChunkOffset + len(t.PacketChunks)*2
	var i int
	for _, delta := range t.RecvDeltas {
		b, err := delta.Marshal()
		if err == nil {
			copy(payload[recvDeltaOffset+i:], b)
			i++
			if delta.Type == TypeTCCPacketReceivedLargeDelta {
				i++
			}
		}
	}

	if t.Header.Padding {
		payload[len(payload)-1] = uint8(t.Len() - t.packetLen())
	}

	return append(header, payload...), nil
}

// Unmarshal ..
func (t *TransportLayerCC) Unmarshal(rawPacket []byte) error { //nolint:gocognit
	if len(rawPacket) < (headerLength + ssrcLength) {
		return errPacketTooShort
	}

	if err := t.Header.Unmarshal(rawPacket); err != nil {
		return err
	}

	// https://tools.ietf.org/html/rfc4585#page-33
	// header's length + payload's length
	totalLength := 4 * (t.Header.Length + 1)

	if totalLength < headerLength+packetChunkOffset {
		return errPacketTooShort
	}

	if len(rawPacket) < int(totalLength) {
		return errPacketTooShort
	}

	if t.Header.Type != TypeTransportSpecificFeedback || t.Header.Count != FormatTCC {
		return errWrongType
	}

	t.SenderSSRC = binary.BigEndian.Uint32(rawPacket[headerLength:])
	t.MediaSSRC = binary.BigEndian.Uint32(rawPacket[headerLength+ssrcLength:])
	t.BaseSequenceNumber = binary.BigEndian.Uint16(rawPacket[headerLength+baseSequenceNumberOffset:])
	t.PacketStatusCount = binary.BigEndian.Uint16(rawPacket[headerLength+packetStatusCountOffset:])
	t.ReferenceTime = get24BitsFromBytes(rawPacket[headerLength+referenceTimeOffset : headerLength+referenceTimeOffset+3])
	t.FbPktCount = rawPacket[headerLength+fbPktCountOffset]

	packetStatusPos := uint16(headerLength + packetChunkOffset)
	var processedPacketNum uint16
	for processedPacketNum < t.PacketStatusCount {
		if packetStatusPos+packetStatusChunkLength >= totalLength {
			return errPacketTooShort
		}
		typ := getNBitsFromByte(rawPacket[packetStatusPos : packetStatusPos+1][0], 0, 1)
		var iPacketStatus PacketStatusChunk
		switch typ {
		case TypeTCCRunLengthChunk:
			packetStatus := &RunLengthChunk{Type: typ}
			iPacketStatus = packetStatus
			err := packetStatus.Unmarshal(rawPacket[packetStatusPos : packetStatusPos+2])
			if err != nil {
				return err
			}

			packetNumberToProcess := min(t.PacketStatusCount-processedPacketNum, packetStatus.RunLength)
			if packetStatus.PacketStatusSymbol == TypeTCCPacketReceivedSmallDelta ||
				packetStatus.PacketStatusSymbol == TypeTCCPacketReceivedLargeDelta {
				for j := uint16(0); j < packetNumberToProcess; j++ {
					t.RecvDeltas = append(t.RecvDeltas, &RecvDelta{Type: packetStatus.PacketStatusSymbol})
				}
			}
			processedPacketNum += packetNumberToProcess
		case TypeTCCStatusVectorChunk:
			packetStatus := &StatusVectorChunk{Type: typ}
			iPacketStatus = packetStatus
			err := packetStatus.Unmarshal(rawPacket[packetStatusPos : packetStatusPos+2])
			if err != nil {
				return err
			}
			if packetStatus.SymbolSize == TypeTCCSymbolSizeOneBit {
				for j := 0; j < len(packetStatus.SymbolList); j++ {
					if packetStatus.SymbolList[j] == TypeTCCPacketReceivedSmallDelta {
						t.RecvDeltas = append(t.RecvDeltas, &RecvDelta{Type: TypeTCCPacketReceivedSmallDelta})
					}
				}
			}
			if packetStatus.SymbolSize == TypeTCCSymbolSizeTwoBit {
				for j := 0; j < len(packetStatus.SymbolList); j++ {
					if packetStatus.SymbolList[j] == TypeTCCPacketReceivedSmallDelta || packetStatus.SymbolList[j] == TypeTCCPacketReceivedLargeDelta {
						t.RecvDeltas = append(t.RecvDeltas, &RecvDelta{Type: packetStatus.SymbolList[j]})
					}
				}
			}
			processedPacketNum += uint16(len(packetStatus.SymbolList))
		}
		packetStatusPos += packetStatusChunkLength
		t.PacketChunks = append(t.PacketChunks, iPacketStatus)
	}

	recvDeltasPos := packetStatusPos
	for _, delta := range t.RecvDeltas {
		if recvDeltasPos >= totalLength {
			return errPacketTooShort
		}
		if delta.Type == TypeTCCPacketReceivedSmallDelta {
			err := delta.Unmarshal(rawPacket[recvDeltasPos : recvDeltasPos+1])
			if err != nil {
				return err
			}
			recvDeltasPos++
		}
		if delta.Type == TypeTCCPacketReceivedLargeDelta {
			err := delta.Unmarshal(rawPacket[recvDeltasPos : recvDeltasPos+2])
			if err != nil {
				return err
			}
			recvDeltasPos += 2
		}
	}

	return nil
}

// DestinationSSRC returns an array of SSRC values that this packet refers to.
func (t TransportLayerCC) DestinationSSRC() []uint32 {
	return []uint32{t.MediaSSRC}
}

func min(x, y uint16) uint16 {
	if x < y {
		return x
	}
	return y
}
