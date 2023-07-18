package rtp

import (
	"encoding/binary"
)

const (
	// transport-wide sequence
	transportCCExtensionSize = 2
)

// TransportCCExtension is a extension payload format in
// https://tools.ietf.org/html/draft-holmer-rmcat-transport-wide-cc-extensions-01
// 0                   1                   2                   3
// 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// |       0xBE    |    0xDE       |           length=1            |
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// |  ID   | L=1   |transport-wide sequence number | zero padding  |
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
type TransportCCExtension struct {
	TransportSequence uint16
}

// Marshal serializes the members to buffer
func (t TransportCCExtension) Marshal() ([]byte, error) {
	buf := make([]byte, transportCCExtensionSize)
	binary.BigEndian.PutUint16(buf[0:2], t.TransportSequence)
	return buf, nil
}

// Unmarshal parses the passed byte slice and stores the result in the members
func (t *TransportCCExtension) Unmarshal(rawData []byte) error {
	if len(rawData) < transportCCExtensionSize {
		return errTooSmall
	}
	t.TransportSequence = binary.BigEndian.Uint16(rawData[0:2])
	return nil
}
