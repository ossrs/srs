// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

package rtcp

import (
	"bytes"
	"encoding/binary"
	"fmt"
	"math"
)

// ReceiverEstimatedMaximumBitrate contains the receiver's estimated maximum bitrate.
// see: https://tools.ietf.org/html/draft-alvestrand-rmcat-remb-03
type ReceiverEstimatedMaximumBitrate struct {
	// SSRC of sender
	SenderSSRC uint32

	// Estimated maximum bitrate
	Bitrate float32

	// SSRC entries which this packet applies to
	SSRCs []uint32
}

// Marshal serializes the packet and returns a byte slice.
func (p ReceiverEstimatedMaximumBitrate) Marshal() (buf []byte, err error) {
	// Allocate a buffer of the exact output size.
	buf = make([]byte, p.MarshalSize())

	// Write to our buffer.
	n, err := p.MarshalTo(buf)
	if err != nil {
		return nil, err
	}

	// This will always be true but just to be safe.
	if n != len(buf) {
		return nil, errWrongMarshalSize
	}

	return buf, nil
}

// MarshalSize returns the size of the packet once marshaled
func (p ReceiverEstimatedMaximumBitrate) MarshalSize() int {
	return 20 + 4*len(p.SSRCs)
}

// MarshalTo serializes the packet to the given byte slice.
func (p ReceiverEstimatedMaximumBitrate) MarshalTo(buf []byte) (n int, err error) {
	const bitratemax = 0x3FFFFp+63
	/*
	    0                   1                   2                   3
	    0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
	   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
	   |V=2|P| FMT=15  |   PT=206      |             length            |
	   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
	   |                  SSRC of packet sender                        |
	   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
	   |                  SSRC of media source                         |
	   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
	   |  Unique identifier 'R' 'E' 'M' 'B'                            |
	   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
	   |  Num SSRC     | BR Exp    |  BR Mantissa                      |
	   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
	   |   SSRC feedback                                               |
	   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
	   |  ...                                                          |
	*/

	size := p.MarshalSize()
	if len(buf) < size {
		return 0, errPacketTooShort
	}

	buf[0] = 143 // v=2, p=0, fmt=15
	buf[1] = 206

	// Length of this packet in 32-bit words minus one.
	length := uint16((p.MarshalSize() / 4) - 1)
	binary.BigEndian.PutUint16(buf[2:4], length)

	binary.BigEndian.PutUint32(buf[4:8], p.SenderSSRC)
	binary.BigEndian.PutUint32(buf[8:12], 0) // always zero

	// ALL HAIL REMB
	buf[12] = 'R'
	buf[13] = 'E'
	buf[14] = 'M'
	buf[15] = 'B'

	// Write the length of the ssrcs to follow at the end
	buf[16] = byte(len(p.SSRCs))

	exp := 0
	bitrate := p.Bitrate

	if bitrate >= bitratemax {
		bitrate = bitratemax
	}

	if bitrate < 0 {
		return 0, errInvalidBitrate
	}

	for bitrate >= (1 << 18) {
		bitrate /= 2.0
		exp++
	}

	if exp >= (1 << 6) {
		return 0, errInvalidBitrate
	}

	mantissa := uint(math.Floor(float64(bitrate)))

	// We can't quite use the binary package because
	// a) it's a uint24 and b) the exponent is only 6-bits
	// Just trust me; this is big-endian encoding.
	buf[17] = byte(exp<<2) | byte(mantissa>>16)
	buf[18] = byte(mantissa >> 8)
	buf[19] = byte(mantissa)

	// Write the SSRCs at the very end.
	n = 20
	for _, ssrc := range p.SSRCs {
		binary.BigEndian.PutUint32(buf[n:n+4], ssrc)
		n += 4
	}

	return n, nil
}

// Unmarshal reads a REMB packet from the given byte slice.
func (p *ReceiverEstimatedMaximumBitrate) Unmarshal(buf []byte) (err error) {
	const mantissamax = 0x7FFFFF
	/*
	    0                   1                   2                   3
	    0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
	   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
	   |V=2|P| FMT=15  |   PT=206      |             length            |
	   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
	   |                  SSRC of packet sender                        |
	   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
	   |                  SSRC of media source                         |
	   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
	   |  Unique identifier 'R' 'E' 'M' 'B'                            |
	   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
	   |  Num SSRC     | BR Exp    |  BR Mantissa                      |
	   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
	   |   SSRC feedback                                               |
	   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
	   |  ...                                                          |
	*/

	// 20 bytes is the size of the packet with no SSRCs
	if len(buf) < 20 {
		return errPacketTooShort
	}

	// version  must be 2
	version := buf[0] >> 6
	if version != 2 {
		return fmt.Errorf("%w expected(2) actual(%d)", errBadVersion, version)
	}

	// padding must be unset
	padding := (buf[0] >> 5) & 1
	if padding != 0 {
		return fmt.Errorf("%w expected(0) actual(%d)", errWrongPadding, padding)
	}

	// fmt must be 15
	fmtVal := buf[0] & 31
	if fmtVal != 15 {
		return fmt.Errorf("%w expected(15) actual(%d)", errWrongFeedbackType, fmtVal)
	}

	// Must be payload specific feedback
	if buf[1] != 206 {
		return fmt.Errorf("%w expected(206) actual(%d)", errWrongPayloadType, buf[1])
	}

	// length is the number of 32-bit words, minus 1
	length := binary.BigEndian.Uint16(buf[2:4])
	size := int((length + 1) * 4)

	// There's not way this could be legit
	if size < 20 {
		return errHeaderTooSmall
	}

	// Make sure the buffer is large enough.
	if len(buf) < size {
		return errPacketTooShort
	}

	// The sender SSRC is 32-bits
	p.SenderSSRC = binary.BigEndian.Uint32(buf[4:8])

	// The destination SSRC must be 0
	media := binary.BigEndian.Uint32(buf[8:12])
	if media != 0 {
		return errSSRCMustBeZero
	}

	// REMB rules all around me
	if !bytes.Equal(buf[12:16], []byte{'R', 'E', 'M', 'B'}) {
		return errMissingREMBidentifier
	}

	// The next byte is the number of SSRC entries at the end.
	num := int(buf[16])

	// Now we know the expected size, make sure they match.
	if size != 20+4*num {
		return errSSRCNumAndLengthMismatch
	}

	// Get the 6-bit exponent value.
	exp := buf[17] >> 2
	exp += 127 // bias for IEEE754
	exp += 23  // IEEE754 biases the decimal to the left, abs-send-time biases it to the right

	// The remaining 2-bits plus the next 16-bits are the mantissa.
	mantissa := uint32(buf[17]&3)<<16 | uint32(buf[18])<<8 | uint32(buf[19])

	if mantissa != 0 {
		// ieee754 requires an implicit leading bit
		for (mantissa & (mantissamax + 1)) == 0 {
			exp--
			mantissa *= 2
		}
	}

	// bitrate = mantissa * 2^exp
	p.Bitrate = math.Float32frombits((uint32(exp) << 23) | (mantissa & mantissamax))

	// Clear any existing SSRCs
	p.SSRCs = nil

	// Loop over and parse the SSRC entires at the end.
	// We already verified that size == num * 4
	for n := 20; n < size; n += 4 {
		ssrc := binary.BigEndian.Uint32(buf[n : n+4])
		p.SSRCs = append(p.SSRCs, ssrc)
	}

	return nil
}

// Header returns the Header associated with this packet.
func (p *ReceiverEstimatedMaximumBitrate) Header() Header {
	return Header{
		Count:  FormatREMB,
		Type:   TypePayloadSpecificFeedback,
		Length: uint16((p.MarshalSize() / 4) - 1),
	}
}

// String prints the REMB packet in a human-readable format.
func (p *ReceiverEstimatedMaximumBitrate) String() string {
	// Keep a table of powers to units for fast conversion.
	bitUnits := []string{"b", "Kb", "Mb", "Gb", "Tb", "Pb", "Eb"}

	// Do some unit conversions because b/s is far too difficult to read.
	bitrate := p.Bitrate
	powers := 0

	// Keep dividing the bitrate until it's under 1000
	for bitrate >= 1000.0 && powers < len(bitUnits) {
		bitrate /= 1000.0
		powers++
	}

	unit := bitUnits[powers]

	return fmt.Sprintf("ReceiverEstimatedMaximumBitrate %x %.2f %s/s", p.SenderSSRC, bitrate, unit)
}

// DestinationSSRC returns an array of SSRC values that this packet refers to.
func (p *ReceiverEstimatedMaximumBitrate) DestinationSSRC() []uint32 {
	return p.SSRCs
}
