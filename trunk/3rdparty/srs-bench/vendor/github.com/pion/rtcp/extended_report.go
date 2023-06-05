package rtcp

import (
	"fmt"
)

// The ExtendedReport packet is an Implementation of RTCP Extended
// Reports defined in RFC 3611. It is used to convey detailed
// information about an RTP stream. Each packet contains one or
// more report blocks, each of which conveys a different kind of
// information.
//
//  0                   1                   2                   3
//  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// |V=2|P|reserved |   PT=XR=207   |             length            |
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// |                              SSRC                             |
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// :                         report blocks                         :
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
type ExtendedReport struct {
	SenderSSRC uint32 `fmt:"0x%X"`
	Reports    []ReportBlock
}

// ReportBlock represents a single report within an ExtendedReport
// packet
type ReportBlock interface {
	DestinationSSRC() []uint32
	setupBlockHeader()
	unpackBlockHeader()
}

// TypeSpecificField as described in RFC 3611 section 4.5. In typical
// cases, users of ExtendedReports shouldn't need to access this,
// and should instead use the corresponding fields in the actual
// report blocks themselves.
type TypeSpecificField uint8

// XRHeader defines the common fields that must appear at the start
// of each report block. In typical cases, users of ExtendedReports
// shouldn't need to access this. For locally-constructed report
// blocks, these values will not be accurate until the corresponding
// packet is marshaled.
type XRHeader struct {
	BlockType    BlockTypeType
	TypeSpecific TypeSpecificField `fmt:"0x%X"`
	BlockLength  uint16
}

// BlockTypeType specifies the type of report in a report block
type BlockTypeType uint8

// Extended Report block types from RFC 3611.
const (
	LossRLEReportBlockType               = 1 // RFC 3611, section 4.1
	DuplicateRLEReportBlockType          = 2 // RFC 3611, section 4.2
	PacketReceiptTimesReportBlockType    = 3 // RFC 3611, section 4.3
	ReceiverReferenceTimeReportBlockType = 4 // RFC 3611, section 4.4
	DLRRReportBlockType                  = 5 // RFC 3611, section 4.5
	StatisticsSummaryReportBlockType     = 6 // RFC 3611, section 4.6
	VoIPMetricsReportBlockType           = 7 // RFC 3611, section 4.7
)

// String converts the Extended report block types into readable strings
func (t BlockTypeType) String() string {
	switch t {
	case LossRLEReportBlockType:
		return "LossRLEReportBlockType"
	case DuplicateRLEReportBlockType:
		return "DuplicateRLEReportBlockType"
	case PacketReceiptTimesReportBlockType:
		return "PacketReceiptTimesReportBlockType"
	case ReceiverReferenceTimeReportBlockType:
		return "ReceiverReferenceTimeReportBlockType"
	case DLRRReportBlockType:
		return "DLRRReportBlockType"
	case StatisticsSummaryReportBlockType:
		return "StatisticsSummaryReportBlockType"
	case VoIPMetricsReportBlockType:
		return "VoIPMetricsReportBlockType"
	}
	return fmt.Sprintf("invalid value %d", t)
}

// rleReportBlock defines the common structure used by both
// Loss RLE report blocks (RFC 3611 §4.1) and Duplicate RLE
// report blocks (RFC 3611 §4.2).
//
//  0                   1                   2                   3
//  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// |  BT = 1 or 2  | rsvd. |   T   |         block length          |
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// |                        SSRC of source                         |
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// |          begin_seq            |             end_seq           |
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// |          chunk 1              |             chunk 2           |
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// :                              ...                              :
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// |          chunk n-1            |             chunk n           |
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
type rleReportBlock struct {
	XRHeader
	T        uint8  `encoding:"omit"`
	SSRC     uint32 `fmt:"0x%X"`
	BeginSeq uint16
	EndSeq   uint16
	Chunks   []Chunk
}

// Chunk as defined in RFC 3611, section 4.1. These represent information
// about packet losses and packet duplication. They have three representations:
//
// Run Length Chunk:
//
//   0                   1
//   0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5
//  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
//  |C|R|        run length         |
//  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
//
// Bit Vector Chunk:
//
//   0                   1
//   0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5
//  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
//  |C|        bit vector           |
//  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
//
// Terminating Null Chunk:
//
//   0                   1
//   0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5
//  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
//  |0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0|
//  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
type Chunk uint16

// LossRLEReportBlock is used to report information about packet
// losses, as described in RFC 3611, section 4.1
type LossRLEReportBlock rleReportBlock

// DestinationSSRC returns an array of SSRC values that this report block refers to.
func (b *LossRLEReportBlock) DestinationSSRC() []uint32 {
	return []uint32{b.SSRC}
}

func (b *LossRLEReportBlock) setupBlockHeader() {
	b.XRHeader.BlockType = LossRLEReportBlockType
	b.XRHeader.TypeSpecific = TypeSpecificField(b.T & 0x0F)
	b.XRHeader.BlockLength = uint16(wireSize(b)/4 - 1)
}

func (b *LossRLEReportBlock) unpackBlockHeader() {
	b.T = uint8(b.XRHeader.TypeSpecific) & 0x0F
}

// DuplicateRLEReportBlock is used to report information about packet
// duplication, as described in RFC 3611, section 4.1
type DuplicateRLEReportBlock rleReportBlock

// DestinationSSRC returns an array of SSRC values that this report block refers to.
func (b *DuplicateRLEReportBlock) DestinationSSRC() []uint32 {
	return []uint32{b.SSRC}
}

func (b *DuplicateRLEReportBlock) setupBlockHeader() {
	b.XRHeader.BlockType = DuplicateRLEReportBlockType
	b.XRHeader.TypeSpecific = TypeSpecificField(b.T & 0x0F)
	b.XRHeader.BlockLength = uint16(wireSize(b)/4 - 1)
}

func (b *DuplicateRLEReportBlock) unpackBlockHeader() {
	b.T = uint8(b.XRHeader.TypeSpecific) & 0x0F
}

// ChunkType enumerates the three kinds of chunks described in RFC 3611 section 4.1.
type ChunkType uint8

// These are the valid values that ChunkType can assume
const (
	RunLengthChunkType       = 0
	BitVectorChunkType       = 1
	TerminatingNullChunkType = 2
)

func (c Chunk) String() string {
	switch c.Type() {
	case RunLengthChunkType:
		runType, _ := c.RunType()
		return fmt.Sprintf("[RunLength type=%d, length=%d]", runType, c.Value())
	case BitVectorChunkType:
		return fmt.Sprintf("[BitVector 0b%015b]", c.Value())
	case TerminatingNullChunkType:
		return "[TerminatingNull]"
	}
	return fmt.Sprintf("[0x%X]", uint16(c))
}

// Type returns the ChunkType that this Chunk represents
func (c Chunk) Type() ChunkType {
	if c == 0 {
		return TerminatingNullChunkType
	}
	return ChunkType(c >> 15)
}

// RunType returns the RunType that this Chunk represents. It is
// only valid if ChunkType is RunLengthChunkType.
func (c Chunk) RunType() (uint, error) {
	if c.Type() != RunLengthChunkType {
		return 0, errWrongChunkType
	}
	return uint((c >> 14) & 0x01), nil
}

// Value returns the value represented in this Chunk
func (c Chunk) Value() uint {
	switch c.Type() {
	case RunLengthChunkType:
		return uint(c & 0x3FFF)
	case BitVectorChunkType:
		return uint(c & 0x7FFF)
	case TerminatingNullChunkType:
		return 0
	}
	return uint(c)
}

// PacketReceiptTimesReportBlock represents a Packet Receipt Times
// report block, as described in RFC 3611 section 4.3.
//
//  0                   1                   2                   3
//  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// |     BT=3      | rsvd. |   T   |         block length          |
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// |                        SSRC of source                         |
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// |          begin_seq            |             end_seq           |
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// |       Receipt time of packet begin_seq                        |
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// |       Receipt time of packet (begin_seq + 1) mod 65536        |
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// :                              ...                              :
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// |       Receipt time of packet (end_seq - 1) mod 65536          |
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
type PacketReceiptTimesReportBlock struct {
	XRHeader
	T           uint8  `encoding:"omit"`
	SSRC        uint32 `fmt:"0x%X"`
	BeginSeq    uint16
	EndSeq      uint16
	ReceiptTime []uint32
}

// DestinationSSRC returns an array of SSRC values that this report block refers to.
func (b *PacketReceiptTimesReportBlock) DestinationSSRC() []uint32 {
	return []uint32{b.SSRC}
}

func (b *PacketReceiptTimesReportBlock) setupBlockHeader() {
	b.XRHeader.BlockType = PacketReceiptTimesReportBlockType
	b.XRHeader.TypeSpecific = TypeSpecificField(b.T & 0x0F)
	b.XRHeader.BlockLength = uint16(wireSize(b)/4 - 1)
}

func (b *PacketReceiptTimesReportBlock) unpackBlockHeader() {
	b.T = uint8(b.XRHeader.TypeSpecific) & 0x0F
}

// ReceiverReferenceTimeReportBlock encodes a Receiver Reference Time
// report block as described in RFC 3611 section 4.4.
//
//  0                   1                   2                   3
//  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// |     BT=4      |   reserved    |       block length = 2        |
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// |              NTP timestamp, most significant word             |
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// |             NTP timestamp, least significant word             |
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
type ReceiverReferenceTimeReportBlock struct {
	XRHeader
	NTPTimestamp uint64
}

// DestinationSSRC returns an array of SSRC values that this report block refers to.
func (b *ReceiverReferenceTimeReportBlock) DestinationSSRC() []uint32 {
	return []uint32{}
}

func (b *ReceiverReferenceTimeReportBlock) setupBlockHeader() {
	b.XRHeader.BlockType = ReceiverReferenceTimeReportBlockType
	b.XRHeader.TypeSpecific = 0
	b.XRHeader.BlockLength = uint16(wireSize(b)/4 - 1)
}

func (b *ReceiverReferenceTimeReportBlock) unpackBlockHeader() {
}

// DLRRReportBlock encodes a DLRR Report Block as described in
// RFC 3611 section 4.5.
//
//  0                   1                   2                   3
//  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// |     BT=5      |   reserved    |         block length          |
// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
// |                 SSRC_1 (SSRC of first receiver)               | sub-
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+ block
// |                         last RR (LRR)                         |   1
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// |                   delay since last RR (DLRR)                  |
// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
// |                 SSRC_2 (SSRC of second receiver)              | sub-
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+ block
// :                               ...                             :   2
// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
type DLRRReportBlock struct {
	XRHeader
	Reports []DLRRReport
}

// DLRRReport encodes a single report inside a DLRRReportBlock.
type DLRRReport struct {
	SSRC   uint32 `fmt:"0x%X"`
	LastRR uint32
	DLRR   uint32
}

// DestinationSSRC returns an array of SSRC values that this report block refers to.
func (b *DLRRReportBlock) DestinationSSRC() []uint32 {
	ssrc := make([]uint32, len(b.Reports))
	for i, r := range b.Reports {
		ssrc[i] = r.SSRC
	}
	return ssrc
}

func (b *DLRRReportBlock) setupBlockHeader() {
	b.XRHeader.BlockType = DLRRReportBlockType
	b.XRHeader.TypeSpecific = 0
	b.XRHeader.BlockLength = uint16(wireSize(b)/4 - 1)
}

func (b *DLRRReportBlock) unpackBlockHeader() {
}

// StatisticsSummaryReportBlock encodes a Statistics Summary Report
// Block as described in RFC 3611, section 4.6.
//
//  0                   1                   2                   3
//  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// |     BT=6      |L|D|J|ToH|rsvd.|       block length = 9        |
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// |                        SSRC of source                         |
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// |          begin_seq            |             end_seq           |
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// |                        lost_packets                           |
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// |                        dup_packets                            |
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// |                         min_jitter                            |
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// |                         max_jitter                            |
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// |                         mean_jitter                           |
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// |                         dev_jitter                            |
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// | min_ttl_or_hl | max_ttl_or_hl |mean_ttl_or_hl | dev_ttl_or_hl |
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
type StatisticsSummaryReportBlock struct {
	XRHeader
	LossReports      bool              `encoding:"omit"`
	DuplicateReports bool              `encoding:"omit"`
	JitterReports    bool              `encoding:"omit"`
	TTLorHopLimit    TTLorHopLimitType `encoding:"omit"`
	SSRC             uint32            `fmt:"0x%X"`
	BeginSeq         uint16
	EndSeq           uint16
	LostPackets      uint32
	DupPackets       uint32
	MinJitter        uint32
	MaxJitter        uint32
	MeanJitter       uint32
	DevJitter        uint32
	MinTTLOrHL       uint8
	MaxTTLOrHL       uint8
	MeanTTLOrHL      uint8
	DevTTLOrHL       uint8
}

// TTLorHopLimitType encodes values for the ToH field in
// a StatisticsSummaryReportBlock
type TTLorHopLimitType uint8

// Values for TTLorHopLimitType
const (
	ToHMissing = 0
	ToHIPv4    = 1
	ToHIPv6    = 2
)

func (t TTLorHopLimitType) String() string {
	switch t {
	case ToHMissing:
		return "[ToH Missing]"
	case ToHIPv4:
		return "[ToH = IPv4]"
	case ToHIPv6:
		return "[ToH = IPv6]"
	}
	return "[ToH Flag is Invalid]"
}

// DestinationSSRC returns an array of SSRC values that this report block refers to.
func (b *StatisticsSummaryReportBlock) DestinationSSRC() []uint32 {
	return []uint32{b.SSRC}
}

func (b *StatisticsSummaryReportBlock) setupBlockHeader() {
	b.XRHeader.BlockType = StatisticsSummaryReportBlockType
	b.XRHeader.TypeSpecific = 0x00
	if b.LossReports {
		b.XRHeader.TypeSpecific |= 0x80
	}
	if b.DuplicateReports {
		b.XRHeader.TypeSpecific |= 0x40
	}
	if b.JitterReports {
		b.XRHeader.TypeSpecific |= 0x20
	}
	b.XRHeader.TypeSpecific |= TypeSpecificField((b.TTLorHopLimit & 0x03) << 3)
	b.XRHeader.BlockLength = uint16(wireSize(b)/4 - 1)
}

func (b *StatisticsSummaryReportBlock) unpackBlockHeader() {
	b.LossReports = b.XRHeader.TypeSpecific&0x80 != 0
	b.DuplicateReports = b.XRHeader.TypeSpecific&0x40 != 0
	b.JitterReports = b.XRHeader.TypeSpecific&0x20 != 0
	b.TTLorHopLimit = TTLorHopLimitType((b.XRHeader.TypeSpecific & 0x18) >> 3)
}

// VoIPMetricsReportBlock encodes a VoIP Metrics Report Block as described
// in RFC 3611, section 4.7.
//
//  0                   1                   2                   3
//  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// |     BT=7      |   reserved    |       block length = 8        |
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// |                        SSRC of source                         |
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// |   loss rate   | discard rate  | burst density |  gap density  |
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// |       burst duration          |         gap duration          |
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// |     round trip delay          |       end system delay        |
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// | signal level  |  noise level  |     RERL      |     Gmin      |
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// |   R factor    | ext. R factor |    MOS-LQ     |    MOS-CQ     |
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// |   RX config   |   reserved    |          JB nominal           |
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// |          JB maximum           |          JB abs max           |
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
type VoIPMetricsReportBlock struct {
	XRHeader
	SSRC           uint32 `fmt:"0x%X"`
	LossRate       uint8
	DiscardRate    uint8
	BurstDensity   uint8
	GapDensity     uint8
	BurstDuration  uint16
	GapDuration    uint16
	RoundTripDelay uint16
	EndSystemDelay uint16
	SignalLevel    uint8
	NoiseLevel     uint8
	RERL           uint8
	Gmin           uint8
	RFactor        uint8
	ExtRFactor     uint8
	MOSLQ          uint8
	MOSCQ          uint8
	RXConfig       uint8
	_              uint8
	JBNominal      uint16
	JBMaximum      uint16
	JBAbsMax       uint16
}

// DestinationSSRC returns an array of SSRC values that this report block refers to.
func (b *VoIPMetricsReportBlock) DestinationSSRC() []uint32 {
	return []uint32{b.SSRC}
}

func (b *VoIPMetricsReportBlock) setupBlockHeader() {
	b.XRHeader.BlockType = VoIPMetricsReportBlockType
	b.XRHeader.TypeSpecific = 0
	b.XRHeader.BlockLength = uint16(wireSize(b)/4 - 1)
}

func (b *VoIPMetricsReportBlock) unpackBlockHeader() {
}

// UnknownReportBlock is used to store bytes for any report block
// that has an unknown Report Block Type.
type UnknownReportBlock struct {
	XRHeader
	Bytes []byte
}

// DestinationSSRC returns an array of SSRC values that this report block refers to.
func (b *UnknownReportBlock) DestinationSSRC() []uint32 {
	return []uint32{}
}

func (b *UnknownReportBlock) setupBlockHeader() {
	b.XRHeader.BlockLength = uint16(wireSize(b)/4 - 1)
}

func (b *UnknownReportBlock) unpackBlockHeader() {
}

// Marshal encodes the ExtendedReport in binary
func (x ExtendedReport) Marshal() ([]byte, error) {
	for _, p := range x.Reports {
		p.setupBlockHeader()
	}

	length := wireSize(x)

	// RTCP Header
	header := Header{
		Type:   TypeExtendedReport,
		Length: uint16(length / 4),
	}
	headerBuffer, err := header.Marshal()
	if err != nil {
		return []byte{}, err
	}
	length += len(headerBuffer)

	rawPacket := make([]byte, length)
	buffer := packetBuffer{bytes: rawPacket}

	err = buffer.write(headerBuffer)
	if err != nil {
		return []byte{}, err
	}
	err = buffer.write(x)
	if err != nil {
		return []byte{}, err
	}

	return rawPacket, nil
}

// Unmarshal decodes the ExtendedReport from binary
func (x *ExtendedReport) Unmarshal(b []byte) error {
	var header Header
	if err := header.Unmarshal(b); err != nil {
		return err
	}
	if header.Type != TypeExtendedReport {
		return errWrongType
	}

	buffer := packetBuffer{bytes: b[headerLength:]}
	err := buffer.read(&x.SenderSSRC)
	if err != nil {
		return err
	}

	for len(buffer.bytes) > 0 {
		var block ReportBlock

		headerBuffer := buffer
		xrHeader := XRHeader{}
		err = headerBuffer.read(&xrHeader)
		if err != nil {
			return err
		}

		switch xrHeader.BlockType {
		case LossRLEReportBlockType:
			block = new(LossRLEReportBlock)
		case DuplicateRLEReportBlockType:
			block = new(DuplicateRLEReportBlock)
		case PacketReceiptTimesReportBlockType:
			block = new(PacketReceiptTimesReportBlock)
		case ReceiverReferenceTimeReportBlockType:
			block = new(ReceiverReferenceTimeReportBlock)
		case DLRRReportBlockType:
			block = new(DLRRReportBlock)
		case StatisticsSummaryReportBlockType:
			block = new(StatisticsSummaryReportBlock)
		case VoIPMetricsReportBlockType:
			block = new(VoIPMetricsReportBlock)
		default:
			block = new(UnknownReportBlock)
		}

		// We need to limit the amount of data available to
		// this block to the actual length of the block
		blockLength := (int(xrHeader.BlockLength) + 1) * 4
		blockBuffer := buffer.split(blockLength)
		err = blockBuffer.read(block)
		if err != nil {
			return err
		}
		block.unpackBlockHeader()
		x.Reports = append(x.Reports, block)
	}

	return nil
}

// DestinationSSRC returns an array of SSRC values that this packet refers to.
func (x *ExtendedReport) DestinationSSRC() []uint32 {
	ssrc := make([]uint32, 0)
	for _, p := range x.Reports {
		ssrc = append(ssrc, p.DestinationSSRC()...)
	}
	return ssrc
}

func (x *ExtendedReport) String() string {
	return stringify(x)
}
