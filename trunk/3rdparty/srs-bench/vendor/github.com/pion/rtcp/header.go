package rtcp

import (
	"encoding/binary"
)

// PacketType specifies the type of an RTCP packet
type PacketType uint8

// RTCP packet types registered with IANA. See: https://www.iana.org/assignments/rtp-parameters/rtp-parameters.xhtml#rtp-parameters-4
const (
	TypeSenderReport              PacketType = 200 // RFC 3550, 6.4.1
	TypeReceiverReport            PacketType = 201 // RFC 3550, 6.4.2
	TypeSourceDescription         PacketType = 202 // RFC 3550, 6.5
	TypeGoodbye                   PacketType = 203 // RFC 3550, 6.6
	TypeApplicationDefined        PacketType = 204 // RFC 3550, 6.7 (unimplemented)
	TypeTransportSpecificFeedback PacketType = 205 // RFC 4585, 6051
	TypePayloadSpecificFeedback   PacketType = 206 // RFC 4585, 6.3

)

// Transport and Payload specific feedback messages overload the count field to act as a message type. those are listed here
const (
	FormatSLI  uint8 = 2
	FormatPLI  uint8 = 1
	FormatFIR  uint8 = 4
	FormatTLN  uint8 = 1
	FormatRRR  uint8 = 5
	FormatREMB uint8 = 15

	//https://tools.ietf.org/html/draft-holmer-rmcat-transport-wide-cc-extensions-01#page-5
	FormatTCC uint8 = 15
)

func (p PacketType) String() string {
	switch p {
	case TypeSenderReport:
		return "SR"
	case TypeReceiverReport:
		return "RR"
	case TypeSourceDescription:
		return "SDES"
	case TypeGoodbye:
		return "BYE"
	case TypeApplicationDefined:
		return "APP"
	case TypeTransportSpecificFeedback:
		return "TSFB"
	case TypePayloadSpecificFeedback:
		return "PSFB"
	default:
		return string(p)
	}
}

const rtpVersion = 2

// A Header is the common header shared by all RTCP packets
type Header struct {
	// If the padding bit is set, this individual RTCP packet contains
	// some additional padding octets at the end which are not part of
	// the control information but are included in the length field.
	Padding bool
	// The number of reception reports, sources contained or FMT in this packet (depending on the Type)
	Count uint8
	// The RTCP packet type for this packet
	Type PacketType
	// The length of this RTCP packet in 32-bit words minus one,
	// including the header and any padding.
	Length uint16
}

const (
	headerLength = 4
	versionShift = 6
	versionMask  = 0x3
	paddingShift = 5
	paddingMask  = 0x1
	countShift   = 0
	countMask    = 0x1f
	countMax     = (1 << 5) - 1
)

// Marshal encodes the Header in binary
func (h Header) Marshal() ([]byte, error) {
	/*
	 *  0                   1                   2                   3
	 *  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
	 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
	 * |V=2|P|    RC   |   PT=SR=200   |             length            |
	 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
	 */
	rawPacket := make([]byte, headerLength)

	rawPacket[0] |= rtpVersion << versionShift

	if h.Padding {
		rawPacket[0] |= 1 << paddingShift
	}

	if h.Count > 31 {
		return nil, errInvalidHeader
	}
	rawPacket[0] |= h.Count << countShift

	rawPacket[1] = uint8(h.Type)

	binary.BigEndian.PutUint16(rawPacket[2:], h.Length)

	return rawPacket, nil
}

// Unmarshal decodes the Header from binary
func (h *Header) Unmarshal(rawPacket []byte) error {
	if len(rawPacket) < headerLength {
		return errPacketTooShort
	}

	/*
	 *  0                   1                   2                   3
	 *  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
	 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
	 * |V=2|P|    RC   |      PT       |             length            |
	 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
	 */

	version := rawPacket[0] >> versionShift & versionMask
	if version != rtpVersion {
		return errBadVersion
	}

	h.Padding = (rawPacket[0] >> paddingShift & paddingMask) > 0
	h.Count = rawPacket[0] >> countShift & countMask

	h.Type = PacketType(rawPacket[1])

	h.Length = binary.BigEndian.Uint16(rawPacket[2:])

	return nil
}
