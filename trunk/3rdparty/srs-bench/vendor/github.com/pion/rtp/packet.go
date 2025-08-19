// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

package rtp

import (
	"encoding/binary"
	"fmt"
	"io"
)

// Extension RTP Header extension.
type Extension struct {
	id      uint8
	payload []byte
}

// Header represents an RTP packet header.
type Header struct {
	Version          uint8
	Padding          bool
	Extension        bool
	Marker           bool
	PayloadType      uint8
	SequenceNumber   uint16
	Timestamp        uint32
	SSRC             uint32
	CSRC             []uint32
	ExtensionProfile uint16
	Extensions       []Extension

	// PaddingLength is the length of the padding in bytes. It is not part of the RTP header
	// (it is sent in the last byte of RTP packet padding), but logically it belongs here.
	PaddingSize byte

	// Deprecated: will be removed in a future version.
	PayloadOffset int
}

// Packet represents an RTP Packet.
type Packet struct {
	Header
	Payload []byte

	PaddingSize byte // Deprecated: will be removed in a future version. Use Header.PaddingSize instead.

	// Deprecated: will be removed in a future version.
	Raw []byte

	// Please do not add any new field directly to Packet struct unless you know that it is safe.
	// pion internally passes Header and Payload separately, what causes bugs like
	// https://github.com/pion/webrtc/issues/2403 .
}

const (
	// ExtensionProfileOneByte is the RTP One Byte Header Extension Profile, defined in RFC 8285.
	ExtensionProfileOneByte = 0xBEDE
	// ExtensionProfileTwoByte is the RTP Two Byte Header Extension Profile, defined in RFC 8285.
	ExtensionProfileTwoByte = 0x1000
	// CryptexProfileOneByte is the Cryptex One Byte Header Extension Profile, defined in RFC 9335.
	CryptexProfileOneByte = 0xC0DE
	// CryptexProfileTwoByte is the Cryptex Two Byte Header Extension Profile, defined in RFC 9335.
	CryptexProfileTwoByte = 0xC2DE
)

const (
	headerLength        = 4
	versionShift        = 6
	versionMask         = 0x3
	paddingShift        = 5
	paddingMask         = 0x1
	extensionShift      = 4
	extensionMask       = 0x1
	extensionIDReserved = 0xF
	ccMask              = 0xF
	markerShift         = 7
	markerMask          = 0x1
	ptMask              = 0x7F
	seqNumOffset        = 2
	seqNumLength        = 2
	timestampOffset     = 4
	timestampLength     = 4
	ssrcOffset          = 8
	ssrcLength          = 4
	csrcOffset          = 12
	csrcLength          = 4
)

// String helps with debugging by printing packet information in a readable way.
func (p Packet) String() string {
	out := "RTP PACKET:\n"

	out += fmt.Sprintf("\tVersion: %v\n", p.Version)
	out += fmt.Sprintf("\tMarker: %v\n", p.Marker)
	out += fmt.Sprintf("\tPayload Type: %d\n", p.PayloadType)
	out += fmt.Sprintf("\tSequence Number: %d\n", p.SequenceNumber)
	out += fmt.Sprintf("\tTimestamp: %d\n", p.Timestamp)
	out += fmt.Sprintf("\tSSRC: %d (%x)\n", p.SSRC, p.SSRC)
	out += fmt.Sprintf("\tPayload Length: %d\n", len(p.Payload))

	return out
}

// Unmarshal parses the passed byte slice and stores the result in the Header.
// It returns the number of bytes read n and any error.
func (h *Header) Unmarshal(buf []byte) (n int, err error) { //nolint:gocognit,cyclop
	if len(buf) < headerLength {
		return 0, fmt.Errorf("%w: %d < %d", errHeaderSizeInsufficient, len(buf), headerLength)
	}

	/*
	 *  0                   1                   2                   3
	 *  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
	 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
	 * |V=2|P|X|  CC   |M|     PT      |       sequence number         |
	 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
	 * |                           timestamp                           |
	 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
	 * |           synchronization source (SSRC) identifier            |
	 * +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
	 * |            contributing source (CSRC) identifiers             |
	 * |                             ....                              |
	 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
	 */

	h.Version = buf[0] >> versionShift & versionMask
	h.Padding = (buf[0] >> paddingShift & paddingMask) > 0
	h.Extension = (buf[0] >> extensionShift & extensionMask) > 0
	nCSRC := int(buf[0] & ccMask)
	if cap(h.CSRC) < nCSRC || h.CSRC == nil {
		h.CSRC = make([]uint32, nCSRC)
	} else {
		h.CSRC = h.CSRC[:nCSRC]
	}

	n = csrcOffset + (nCSRC * csrcLength)
	if len(buf) < n {
		return n, fmt.Errorf("size %d < %d: %w", len(buf), n,
			errHeaderSizeInsufficient)
	}

	h.Marker = (buf[1] >> markerShift & markerMask) > 0
	h.PayloadType = buf[1] & ptMask

	h.SequenceNumber = binary.BigEndian.Uint16(buf[seqNumOffset : seqNumOffset+seqNumLength])
	h.Timestamp = binary.BigEndian.Uint32(buf[timestampOffset : timestampOffset+timestampLength])
	h.SSRC = binary.BigEndian.Uint32(buf[ssrcOffset : ssrcOffset+ssrcLength])

	for i := range h.CSRC {
		offset := csrcOffset + (i * csrcLength)
		h.CSRC[i] = binary.BigEndian.Uint32(buf[offset:])
	}

	if h.Extensions != nil {
		h.Extensions = h.Extensions[:0]
	}

	if h.Extension { // nolint: nestif
		if expected := n + 4; len(buf) < expected {
			return n, fmt.Errorf("size %d < %d: %w",
				len(buf), expected,
				errHeaderSizeInsufficientForExtension,
			)
		}

		h.ExtensionProfile = binary.BigEndian.Uint16(buf[n:])
		n += 2
		extensionLength := int(binary.BigEndian.Uint16(buf[n:])) * 4
		n += 2
		extensionEnd := n + extensionLength

		if len(buf) < extensionEnd {
			return n, fmt.Errorf("size %d < %d: %w", len(buf), extensionEnd, errHeaderSizeInsufficientForExtension)
		}

		if h.ExtensionProfile == ExtensionProfileOneByte || h.ExtensionProfile == ExtensionProfileTwoByte {
			var (
				extid      uint8
				payloadLen int
			)

			for n < extensionEnd {
				if buf[n] == 0x00 { // padding
					n++

					continue
				}

				if h.ExtensionProfile == ExtensionProfileOneByte {
					extid = buf[n] >> 4
					payloadLen = int(buf[n]&^0xF0 + 1)
					n++

					if extid == extensionIDReserved {
						break
					}
				} else {
					extid = buf[n]
					n++

					if len(buf) <= n {
						return n, fmt.Errorf("size %d < %d: %w", len(buf), n, errHeaderSizeInsufficientForExtension)
					}

					payloadLen = int(buf[n])
					n++
				}

				if extensionPayloadEnd := n + payloadLen; len(buf) <= extensionPayloadEnd {
					return n, fmt.Errorf("size %d < %d: %w", len(buf), extensionPayloadEnd, errHeaderSizeInsufficientForExtension)
				}

				extension := Extension{id: extid, payload: buf[n : n+payloadLen]}
				h.Extensions = append(h.Extensions, extension)
				n += payloadLen
			}
		} else {
			// RFC3550 Extension
			extension := Extension{id: 0, payload: buf[n:extensionEnd]}
			h.Extensions = append(h.Extensions, extension)
			n += len(h.Extensions[0].payload)
		}
	}

	return n, nil
}

// Unmarshal parses the passed byte slice and stores the result in the Packet.
func (p *Packet) Unmarshal(buf []byte) error {
	n, err := p.Header.Unmarshal(buf)
	if err != nil {
		return err
	}

	end := len(buf)
	if p.Header.Padding {
		if end <= n {
			return errTooSmall
		}
		p.Header.PaddingSize = buf[end-1]
		end -= int(p.Header.PaddingSize)
	} else {
		p.Header.PaddingSize = 0
	}
	p.PaddingSize = p.Header.PaddingSize
	if end < n {
		return errTooSmall
	}

	p.Payload = buf[n:end]

	return nil
}

// Marshal serializes the header into bytes.
func (h Header) Marshal() (buf []byte, err error) {
	buf = make([]byte, h.MarshalSize())

	n, err := h.MarshalTo(buf)
	if err != nil {
		return nil, err
	}

	return buf[:n], nil
}

// MarshalTo serializes the header and writes to the buffer.
func (h Header) MarshalTo(buf []byte) (n int, err error) { //nolint:cyclop
	/*
	 *  0                   1                   2                   3
	 *  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
	 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
	 * |V=2|P|X|  CC   |M|     PT      |       sequence number         |
	 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
	 * |                           timestamp                           |
	 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
	 * |           synchronization source (SSRC) identifier            |
	 * +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
	 * |            contributing source (CSRC) identifiers             |
	 * |                             ....                              |
	 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
	 */

	size := h.MarshalSize()
	if size > len(buf) {
		return 0, io.ErrShortBuffer
	}

	// The first byte contains the version, padding bit, extension bit,
	// and csrc size.
	buf[0] = (h.Version << versionShift) | uint8(len(h.CSRC)) // nolint: gosec // G115
	if h.Padding {
		buf[0] |= 1 << paddingShift
	}

	if h.Extension {
		buf[0] |= 1 << extensionShift
	}

	// The second byte contains the marker bit and payload type.
	buf[1] = h.PayloadType
	if h.Marker {
		buf[1] |= 1 << markerShift
	}

	binary.BigEndian.PutUint16(buf[2:4], h.SequenceNumber)
	binary.BigEndian.PutUint32(buf[4:8], h.Timestamp)
	binary.BigEndian.PutUint32(buf[8:12], h.SSRC)

	n = 12
	for _, csrc := range h.CSRC {
		binary.BigEndian.PutUint32(buf[n:n+4], csrc)
		n += 4
	}

	if h.Extension {
		extHeaderPos := n
		binary.BigEndian.PutUint16(buf[n+0:n+2], h.ExtensionProfile)
		n += 4
		startExtensionsPos := n

		switch h.ExtensionProfile {
		// RFC 8285 RTP One Byte Header Extension
		case ExtensionProfileOneByte:
			for _, extension := range h.Extensions {
				buf[n] = extension.id<<4 | (uint8(len(extension.payload)) - 1) // nolint: gosec // G115
				n++
				n += copy(buf[n:], extension.payload)
			}
		// RFC 8285 RTP Two Byte Header Extension
		case ExtensionProfileTwoByte:
			for _, extension := range h.Extensions {
				buf[n] = extension.id
				n++
				buf[n] = uint8(len(extension.payload)) // nolint: gosec // G115
				n++
				n += copy(buf[n:], extension.payload)
			}
		default: // RFC3550 Extension
			extlen := len(h.Extensions[0].payload)
			if extlen%4 != 0 {
				// the payload must be in 32-bit words.
				return 0, io.ErrShortBuffer
			}
			n += copy(buf[n:], h.Extensions[0].payload)
		}

		// calculate extensions size and round to 4 bytes boundaries
		extSize := n - startExtensionsPos
		roundedExtSize := ((extSize + 3) / 4) * 4

		// nolint: gosec // G115 false positive
		binary.BigEndian.PutUint16(buf[extHeaderPos+2:extHeaderPos+4], uint16(roundedExtSize/4))

		// add padding to reach 4 bytes boundaries
		for i := 0; i < roundedExtSize-extSize; i++ {
			buf[n] = 0
			n++
		}
	}

	return n, nil
}

// MarshalSize returns the size of the header once marshaled.
func (h Header) MarshalSize() int {
	// NOTE: Be careful to match the MarshalTo() method.
	size := 12 + (len(h.CSRC) * csrcLength)

	if h.Extension {
		extSize := 4

		switch h.ExtensionProfile {
		// RFC 8285 RTP One Byte Header Extension
		case ExtensionProfileOneByte:
			for _, extension := range h.Extensions {
				extSize += 1 + len(extension.payload)
			}
		// RFC 8285 RTP Two Byte Header Extension
		case ExtensionProfileTwoByte:
			for _, extension := range h.Extensions {
				extSize += 2 + len(extension.payload)
			}
		default:
			extSize += len(h.Extensions[0].payload)
		}

		// extensions size must have 4 bytes boundaries
		size += ((extSize + 3) / 4) * 4
	}

	return size
}

// SetExtension sets an RTP header extension.
func (h *Header) SetExtension(id uint8, payload []byte) error { //nolint:gocognit, cyclop
	if h.Extension { // nolint: nestif
		switch h.ExtensionProfile {
		// RFC 8285 RTP One Byte Header Extension
		case ExtensionProfileOneByte:
			if id < 1 || id > 14 {
				return fmt.Errorf("%w actual(%d)", errRFC8285OneByteHeaderIDRange, id)
			}
			if len(payload) > 16 {
				return fmt.Errorf("%w actual(%d)", errRFC8285OneByteHeaderSize, len(payload))
			}
		// RFC 8285 RTP Two Byte Header Extension
		case ExtensionProfileTwoByte:
			if id < 1 {
				return fmt.Errorf("%w actual(%d)", errRFC8285TwoByteHeaderIDRange, id)
			}
			if len(payload) > 255 {
				return fmt.Errorf("%w actual(%d)", errRFC8285TwoByteHeaderSize, len(payload))
			}
		default: // RFC3550 Extension
			if id != 0 {
				return fmt.Errorf("%w actual(%d)", errRFC3550HeaderIDRange, id)
			}
		}

		// Update existing if it exists else add new extension
		for i, extension := range h.Extensions {
			if extension.id == id {
				h.Extensions[i].payload = payload

				return nil
			}
		}

		h.Extensions = append(h.Extensions, Extension{id: id, payload: payload})

		return nil
	}

	// No existing header extensions
	h.Extension = true

	switch payloadLen := len(payload); {
	case payloadLen <= 16:
		h.ExtensionProfile = ExtensionProfileOneByte
	case payloadLen > 16 && payloadLen < 256:
		h.ExtensionProfile = ExtensionProfileTwoByte
	}

	h.Extensions = append(h.Extensions, Extension{id: id, payload: payload})

	return nil
}

// GetExtensionIDs returns an extension id array.
func (h *Header) GetExtensionIDs() []uint8 {
	if !h.Extension {
		return nil
	}

	if len(h.Extensions) == 0 {
		return nil
	}

	ids := make([]uint8, 0, len(h.Extensions))
	for _, extension := range h.Extensions {
		ids = append(ids, extension.id)
	}

	return ids
}

// GetExtension returns an RTP header extension.
func (h *Header) GetExtension(id uint8) []byte {
	if !h.Extension {
		return nil
	}
	for _, extension := range h.Extensions {
		if extension.id == id {
			return extension.payload
		}
	}

	return nil
}

// DelExtension Removes an RTP Header extension.
func (h *Header) DelExtension(id uint8) error {
	if !h.Extension {
		return errHeaderExtensionsNotEnabled
	}
	for i, extension := range h.Extensions {
		if extension.id == id {
			h.Extensions = append(h.Extensions[:i], h.Extensions[i+1:]...)

			return nil
		}
	}

	return errHeaderExtensionNotFound
}

// Marshal serializes the packet into bytes.
func (p Packet) Marshal() (buf []byte, err error) {
	buf = make([]byte, p.MarshalSize())

	n, err := p.MarshalTo(buf)
	if err != nil {
		return nil, err
	}

	return buf[:n], nil
}

// MarshalTo serializes the packet and writes to the buffer.
func (p *Packet) MarshalTo(buf []byte) (n int, err error) {
	if p.Header.Padding && p.paddingSize() == 0 {
		return 0, errInvalidRTPPadding
	}

	n, err = p.Header.MarshalTo(buf)
	if err != nil {
		return 0, err
	}

	return marshalPayloadAndPaddingTo(buf, n, &p.Header, p.Payload, p.paddingSize())
}

func marshalPayloadAndPaddingTo(buf []byte, offset int, header *Header, payload []byte, paddingSize byte,
) (n int, err error) {
	// Make sure the buffer is large enough to hold the packet.
	if offset+len(payload)+int(paddingSize) > len(buf) {
		return 0, io.ErrShortBuffer
	}

	m := copy(buf[offset:], payload)

	if header.Padding {
		buf[offset+m+int(paddingSize-1)] = paddingSize
	}

	return offset + m + int(paddingSize), nil
}

// MarshalSize returns the size of the packet once marshaled.
func (p Packet) MarshalSize() int {
	return p.Header.MarshalSize() + len(p.Payload) + int(p.paddingSize())
}

// Clone returns a deep copy of p.
func (p Packet) Clone() *Packet {
	clone := &Packet{}
	clone.Header = p.Header.Clone()
	if p.Payload != nil {
		clone.Payload = make([]byte, len(p.Payload))
		copy(clone.Payload, p.Payload)
	}
	clone.PaddingSize = p.PaddingSize

	return clone
}

// Clone returns a deep copy h.
func (h Header) Clone() Header {
	clone := h
	if h.CSRC != nil {
		clone.CSRC = make([]uint32, len(h.CSRC))
		copy(clone.CSRC, h.CSRC)
	}
	if h.Extensions != nil {
		ext := make([]Extension, len(h.Extensions))
		for i, e := range h.Extensions {
			ext[i] = e
			if e.payload != nil {
				ext[i].payload = make([]byte, len(e.payload))
				copy(ext[i].payload, e.payload)
			}
		}
		clone.Extensions = ext
	}

	return clone
}

func (p *Packet) paddingSize() byte {
	if p.Header.PaddingSize > 0 {
		return p.Header.PaddingSize
	}

	return p.PaddingSize
}

// MarshalPacketTo serializes the header and payload into bytes.
// Parts of pion code passes RTP header and payload separately, so this function
// is provided to help with that.
//
// Deprecated: this function is a temporary workaround and will be removed in pion/webrtc v5.
func MarshalPacketTo(buf []byte, header *Header, payload []byte) (int, error) {
	n, err := header.MarshalTo(buf)
	if err != nil {
		return 0, err
	}

	return marshalPayloadAndPaddingTo(buf, n, header, payload, header.PaddingSize)
}

// PacketMarshalSize returns the size of the header and payload once marshaled.
// Parts of pion code passes RTP header and payload separately, so this function
// is provided to help with that.
//
// Deprecated: this function is a temporary workaround and will be removed in pion/webrtc v5.
func PacketMarshalSize(header *Header, payload []byte) int {
	return header.MarshalSize() + len(payload) + int(header.PaddingSize)
}

// HeaderAndPacketMarshalSize returns the size of the header and full packet once marshaled.
// Parts of pion code passes RTP header and payload separately, so this function
// is provided to help with that.
//
// Deprecated: this function is a temporary workaround and will be removed in pion/webrtc v5.
func HeaderAndPacketMarshalSize(header *Header, payload []byte) (headerSize int, packetSize int) {
	headerSize = header.MarshalSize()

	return headerSize, headerSize + len(payload) + int(header.PaddingSize)
}
