package dtls

import "encoding/binary"

type recordLayerHeader struct {
	contentType     contentType
	contentLen      uint16
	protocolVersion protocolVersion
	epoch           uint16
	sequenceNumber  uint64 // uint48 in spec
}

const (
	recordLayerHeaderSize = 13
	maxSequenceNumber     = 0x0000FFFFFFFFFFFF

	dtls1_2Major = 0xfe
	dtls1_2Minor = 0xfd

	dtls1_0Major = 0xfe
	dtls1_0Minor = 0xff

	// VersionDTLS12 is the DTLS version in the same style as
	// VersionTLSXX from crypto/tls
	VersionDTLS12 = 0xfefd
)

var (
	protocolVersion1_0 = protocolVersion{dtls1_0Major, dtls1_0Minor} //nolint:gochecknoglobals
	protocolVersion1_2 = protocolVersion{dtls1_2Major, dtls1_2Minor} //nolint:gochecknoglobals
)

// https://tools.ietf.org/html/rfc4346#section-6.2.1
type protocolVersion struct {
	major, minor uint8
}

func (v protocolVersion) Equal(x protocolVersion) bool {
	return v.major == x.major && v.minor == x.minor
}

func (r *recordLayerHeader) Marshal() ([]byte, error) {
	if r.sequenceNumber > maxSequenceNumber {
		return nil, errSequenceNumberOverflow
	}

	out := make([]byte, recordLayerHeaderSize)
	out[0] = byte(r.contentType)
	out[1] = r.protocolVersion.major
	out[2] = r.protocolVersion.minor
	binary.BigEndian.PutUint16(out[3:], r.epoch)
	putBigEndianUint48(out[5:], r.sequenceNumber)
	binary.BigEndian.PutUint16(out[recordLayerHeaderSize-2:], r.contentLen)
	return out, nil
}

func (r *recordLayerHeader) Unmarshal(data []byte) error {
	if len(data) < recordLayerHeaderSize {
		return errBufferTooSmall
	}
	r.contentType = contentType(data[0])
	r.protocolVersion.major = data[1]
	r.protocolVersion.minor = data[2]
	r.epoch = binary.BigEndian.Uint16(data[3:])

	// SequenceNumber is stored as uint48, make into uint64
	seqCopy := make([]byte, 8)
	copy(seqCopy[2:], data[5:11])
	r.sequenceNumber = binary.BigEndian.Uint64(seqCopy)

	if !r.protocolVersion.Equal(protocolVersion1_0) && !r.protocolVersion.Equal(protocolVersion1_2) {
		return errUnsupportedProtocolVersion
	}

	return nil
}
