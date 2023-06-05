package rtp

import (
	"time"
)

const (
	absSendTimeExtensionSize = 3
)

// AbsSendTimeExtension is a extension payload format in
// http://www.webrtc.org/experiments/rtp-hdrext/abs-send-time
type AbsSendTimeExtension struct {
	Timestamp uint64
}

// Marshal serializes the members to buffer.
func (t AbsSendTimeExtension) Marshal() ([]byte, error) {
	return []byte{
		byte(t.Timestamp & 0xFF0000 >> 16),
		byte(t.Timestamp & 0xFF00 >> 8),
		byte(t.Timestamp & 0xFF),
	}, nil
}

// Unmarshal parses the passed byte slice and stores the result in the members.
func (t *AbsSendTimeExtension) Unmarshal(rawData []byte) error {
	if len(rawData) < absSendTimeExtensionSize {
		return errTooSmall
	}
	t.Timestamp = uint64(rawData[0])<<16 | uint64(rawData[1])<<8 | uint64(rawData[2])
	return nil
}

// Estimate absolute send time according to the receive time.
// Note that if the transmission delay is larger than 64 seconds, estimated time will be wrong.
func (t *AbsSendTimeExtension) Estimate(receive time.Time) time.Time {
	receiveNTP := toNtpTime(receive)
	ntp := receiveNTP&0xFFFFFFC000000000 | (t.Timestamp&0xFFFFFF)<<14
	if receiveNTP < ntp {
		// Receive time must be always later than send time
		ntp -= 0x1000000 << 14
	}

	return toTime(ntp)
}

// NewAbsSendTimeExtension makes new AbsSendTimeExtension from time.Time.
func NewAbsSendTimeExtension(sendTime time.Time) *AbsSendTimeExtension {
	return &AbsSendTimeExtension{
		Timestamp: toNtpTime(sendTime) >> 14,
	}
}

func toNtpTime(t time.Time) uint64 {
	var s uint64
	var f uint64
	u := uint64(t.UnixNano())
	s = u / 1e9
	s += 0x83AA7E80 // offset in seconds between unix epoch and ntp epoch
	f = u % 1e9
	f <<= 32
	f /= 1e9
	s <<= 32

	return s | f
}

func toTime(t uint64) time.Time {
	s := t >> 32
	f := t & 0xFFFFFFFF
	f *= 1e9
	f >>= 32
	s -= 0x83AA7E80
	u := s*1e9 + f

	return time.Unix(0, int64(u))
}
