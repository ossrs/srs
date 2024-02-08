package rtcp

import (
	"encoding/binary"
	"errors"
	"fmt"
)

// https://www.rfc-editor.org/rfc/rfc8888.html#name-rtcp-congestion-control-fee
//  0                   1                   2                   3
//  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// |V=2|P| FMT=11  |   PT = 205    |          length               |
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// |                 SSRC of RTCP packet sender                    |
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// |                   SSRC of 1st RTP Stream                      |
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// |          begin_seq            |          num_reports          |
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// |R|ECN|  Arrival time offset    | ...                           .
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// .                                                               .
// .                                                               .
// .                                                               .
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// |                   SSRC of nth RTP Stream                      |
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// |          begin_seq            |          num_reports          |
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// |R|ECN|  Arrival time offset    | ...                           |
// .                                                               .
// .                                                               .
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// |                 Report Timestamp (32 bits)                    |
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+

var (
	errReportBlockLength   = errors.New("feedback report blocks must be at least 8 bytes")
	errIncorrectNumReports = errors.New("feedback report block contains less reports than num_reports")
	errMetricBlockLength   = errors.New("feedback report metric blocks must be exactly 2 bytes")
)

// ECN represents the two ECN bits
type ECN uint8

const (
	//nolint:misspell
	// ECNNonECT signals Non ECN-Capable Transport, Non-ECT
	ECNNonECT ECN = iota // 00

	//nolint:misspell
	// ECNECT1 signals ECN Capable Transport, ECT(0)
	ECNECT1 // 01

	//nolint:misspell
	// ECNECT0 signals ECN Capable Transport, ECT(1)
	ECNECT0 // 10

	// ECNCE signals ECN Congestion Encountered, CE
	ECNCE // 11
)

const (
	reportTimestampLength = 4
	reportBlockOffset     = 8
)

// CCFeedbackReport is a Congestion Control Feedback Report as defined in
// https://www.rfc-editor.org/rfc/rfc8888.html#name-rtcp-congestion-control-fee
type CCFeedbackReport struct {
	// SSRC of sender
	SenderSSRC uint32

	// Report Blocks
	ReportBlocks []CCFeedbackReportBlock

	// Basetime
	ReportTimestamp uint32
}

// DestinationSSRC returns an array of SSRC values that this packet refers to.
func (b CCFeedbackReport) DestinationSSRC() []uint32 {
	ssrcs := make([]uint32, len(b.ReportBlocks))
	for i, block := range b.ReportBlocks {
		ssrcs[i] = block.MediaSSRC
	}
	return ssrcs
}

// Len returns the length of the report in bytes
func (b *CCFeedbackReport) Len() uint16 {
	n := uint16(0)
	for _, block := range b.ReportBlocks {
		n += block.len()
	}
	return reportBlockOffset + n + reportTimestampLength
}

// Header returns the Header associated with this packet.
func (b *CCFeedbackReport) Header() Header {
	return Header{
		Padding: false,
		Count:   FormatCCFB,
		Type:    TypeTransportSpecificFeedback,
		Length:  b.Len()/4 - 1,
	}
}

// Marshal encodes the Congestion Control Feedback Report in binary
func (b CCFeedbackReport) Marshal() ([]byte, error) {
	header := b.Header()
	headerBuf, err := header.Marshal()
	if err != nil {
		return nil, err
	}
	length := 4 * (header.Length + 1)
	buf := make([]byte, length)
	copy(buf[:headerLength], headerBuf)
	binary.BigEndian.PutUint32(buf[headerLength:], b.SenderSSRC)
	offset := uint16(reportBlockOffset)
	for _, block := range b.ReportBlocks {
		b, err := block.marshal()
		if err != nil {
			return nil, err
		}
		copy(buf[offset:], b)
		offset += block.len()
	}

	binary.BigEndian.PutUint32(buf[offset:], b.ReportTimestamp)
	return buf, nil
}

func (b CCFeedbackReport) String() string {
	out := fmt.Sprintf("CCFB:\n\tHeader %v\n", b.Header())
	out += fmt.Sprintf("CCFB:\n\tSender SSRC %d\n", b.SenderSSRC)
	out += fmt.Sprintf("\tReport Timestamp %d\n", b.ReportTimestamp)
	out += "\tFeedback Reports \n"
	for _, report := range b.ReportBlocks {
		out += fmt.Sprintf("%v ", report)
	}
	out += "\n"
	return out
}

// Unmarshal decodes the Congestion Control Feedback Report from binary
func (b *CCFeedbackReport) Unmarshal(rawPacket []byte) error {
	if len(rawPacket) < headerLength+ssrcLength+reportTimestampLength {
		return errPacketTooShort
	}

	var h Header
	if err := h.Unmarshal(rawPacket); err != nil {
		return err
	}
	if h.Type != TypeTransportSpecificFeedback {
		return errWrongType
	}

	b.SenderSSRC = binary.BigEndian.Uint32(rawPacket[headerLength:])

	reportTimestampOffset := uint16(len(rawPacket) - reportTimestampLength)
	b.ReportTimestamp = binary.BigEndian.Uint32(rawPacket[reportTimestampOffset:])

	offset := uint16(reportBlockOffset)
	b.ReportBlocks = []CCFeedbackReportBlock{}
	for offset < reportTimestampOffset {
		var block CCFeedbackReportBlock
		if err := block.unmarshal(rawPacket[offset:]); err != nil {
			return err
		}
		b.ReportBlocks = append(b.ReportBlocks, block)
		offset += block.len()
	}

	return nil
}

const (
	ssrcOffset          = 0
	beginSequenceOffset = 4
	numReportsOffset    = 6
	reportsOffset       = 8

	maxMetricBlocks = 16384
)

// CCFeedbackReportBlock is a Feedback Report Block
type CCFeedbackReportBlock struct {
	// SSRC of the RTP stream on which this block is reporting
	MediaSSRC     uint32
	BeginSequence uint16
	MetricBlocks  []CCFeedbackMetricBlock
}

// len returns the length of the report block in bytes
func (b *CCFeedbackReportBlock) len() uint16 {
	n := len(b.MetricBlocks)
	if n%2 != 0 {
		n++
	}
	return reportsOffset + 2*uint16(n)
}

func (b CCFeedbackReportBlock) String() string {
	out := fmt.Sprintf("\tReport Block Media SSRC %d\n", b.MediaSSRC)
	out += fmt.Sprintf("\tReport Begin Sequence Nr %d\n", b.BeginSequence)
	out += fmt.Sprintf("\tReport length %d\n\t", len(b.MetricBlocks))
	for i, block := range b.MetricBlocks {
		out += fmt.Sprintf("{nr: %d, rx: %v, ts: %v} ", b.BeginSequence+uint16(i), block.Received, block.ArrivalTimeOffset)
	}
	out += "\n"
	return out
}

// marshal encodes the Congestion Control Feedback Report Block in binary
func (b CCFeedbackReportBlock) marshal() ([]byte, error) {
	if len(b.MetricBlocks) > maxMetricBlocks {
		return nil, errTooManyReports
	}

	buf := make([]byte, b.len())
	binary.BigEndian.PutUint32(buf[ssrcOffset:], b.MediaSSRC)
	binary.BigEndian.PutUint16(buf[beginSequenceOffset:], b.BeginSequence)

	length := uint16(len(b.MetricBlocks))
	if length > 0 {
		length--
	}

	binary.BigEndian.PutUint16(buf[numReportsOffset:], length)

	for i, block := range b.MetricBlocks {
		b, err := block.marshal()
		if err != nil {
			return nil, err
		}
		copy(buf[reportsOffset+i*2:], b)
	}

	return buf, nil
}

// Unmarshal decodes the Congestion Control Feedback Report Block from binary
func (b *CCFeedbackReportBlock) unmarshal(rawPacket []byte) error {
	if len(rawPacket) < reportsOffset {
		return errReportBlockLength
	}
	b.MediaSSRC = binary.BigEndian.Uint32(rawPacket[:beginSequenceOffset])
	b.BeginSequence = binary.BigEndian.Uint16(rawPacket[beginSequenceOffset:numReportsOffset])
	numReportsField := binary.BigEndian.Uint16(rawPacket[numReportsOffset:])
	if numReportsField == 0 {
		return nil
	}
	endSequence := b.BeginSequence + numReportsField
	numReports := endSequence - b.BeginSequence + 1

	if len(rawPacket) < int(reportsOffset+numReports*2) {
		return errIncorrectNumReports
	}
	b.MetricBlocks = make([]CCFeedbackMetricBlock, numReports)
	for i := uint16(0); i < numReports; i++ {
		var mb CCFeedbackMetricBlock
		offset := reportsOffset + 2*i
		if err := mb.unmarshal(rawPacket[offset : offset+2]); err != nil {
			return err
		}
		b.MetricBlocks[i] = mb
	}
	return nil
}

const (
	metricBlockLength = 2
)

// CCFeedbackMetricBlock is a Feedback Metric Block
type CCFeedbackMetricBlock struct {
	Received bool
	ECN      ECN

	// Offset in 1/1024 seconds before Report Timestamp
	ArrivalTimeOffset uint16
}

// Marshal encodes the Congestion Control Feedback Metric Block in binary
func (b CCFeedbackMetricBlock) marshal() ([]byte, error) {
	buf := make([]byte, 2)
	r := uint16(0)
	if b.Received {
		r = 1
	}
	dst, err := setNBitsOfUint16(0, 1, 0, r)
	if err != nil {
		return nil, err
	}
	dst, err = setNBitsOfUint16(dst, 2, 1, uint16(b.ECN))
	if err != nil {
		return nil, err
	}
	dst, err = setNBitsOfUint16(dst, 13, 3, b.ArrivalTimeOffset)
	if err != nil {
		return nil, err
	}

	binary.BigEndian.PutUint16(buf, dst)
	return buf, nil
}

// Unmarshal decodes the Congestion Control Feedback Metric Block from binary
func (b *CCFeedbackMetricBlock) unmarshal(rawPacket []byte) error {
	if len(rawPacket) != metricBlockLength {
		return errMetricBlockLength
	}
	b.Received = rawPacket[0]&0x80 != 0
	if !b.Received {
		b.ECN = ECNNonECT
		b.ArrivalTimeOffset = 0
		return nil
	}
	b.ECN = ECN(rawPacket[0] >> 5 & 0x03)
	b.ArrivalTimeOffset = binary.BigEndian.Uint16(rawPacket) & 0x1FFF
	return nil
}
