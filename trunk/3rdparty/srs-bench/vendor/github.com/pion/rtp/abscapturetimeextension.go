// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

package rtp

import (
	"encoding/binary"
	"time"
)

const (
	absCaptureTimeExtensionSize         = 8
	absCaptureTimeExtendedExtensionSize = 16
)

// AbsCaptureTimeExtension is a extension payload format in.
// http://www.webrtc.org/experiments/rtp-hdrext/abs-capture-time
// 0                   1                   2                   3
// 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// |  ID   | len=7 |     absolute capture timestamp (bit 0-23)     |
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// |             absolute capture timestamp (bit 24-55)            |
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// |  ... (56-63)  |
// +-+-+-+-+-+-+-+-+
// .
type AbsCaptureTimeExtension struct {
	Timestamp                   uint64
	EstimatedCaptureClockOffset *int64
}

// Marshal serializes the members to buffer.
func (t AbsCaptureTimeExtension) Marshal() ([]byte, error) {
	if t.EstimatedCaptureClockOffset != nil {
		buf := make([]byte, 16)
		binary.BigEndian.PutUint64(buf[0:8], t.Timestamp)
		binary.BigEndian.PutUint64(buf[8:16], uint64(*t.EstimatedCaptureClockOffset)) // nolint: gosec // G115

		return buf, nil
	}
	buf := make([]byte, 8)
	binary.BigEndian.PutUint64(buf[0:8], t.Timestamp)

	return buf, nil
}

// Unmarshal parses the passed byte slice and stores the result in the members.
func (t *AbsCaptureTimeExtension) Unmarshal(rawData []byte) error {
	if len(rawData) < absCaptureTimeExtensionSize {
		return errTooSmall
	}
	t.Timestamp = binary.BigEndian.Uint64(rawData[0:8])
	if len(rawData) >= absCaptureTimeExtendedExtensionSize {
		offset := int64(binary.BigEndian.Uint64(rawData[8:16])) // nolint: gosec // G115 false positive
		t.EstimatedCaptureClockOffset = &offset
	}

	return nil
}

// CaptureTime produces the estimated time.Time represented by this extension.
func (t AbsCaptureTimeExtension) CaptureTime() time.Time {
	return toTime(t.Timestamp)
}

// EstimatedCaptureClockOffsetDuration produces the estimated time.Duration represented by this extension.
func (t AbsCaptureTimeExtension) EstimatedCaptureClockOffsetDuration() *time.Duration {
	if t.EstimatedCaptureClockOffset == nil {
		return nil
	}
	offset := *t.EstimatedCaptureClockOffset
	negative := false
	if offset < 0 {
		offset = -offset
		negative = true
	}
	duration := time.Duration(offset/(1<<32))*time.Second + time.Duration((offset&0xFFFFFFFF)*1e9/(1<<32))*time.Nanosecond
	if negative {
		duration = -duration
	}

	return &duration
}

// NewAbsCaptureTimeExtension makes new AbsCaptureTimeExtension from time.Time.
func NewAbsCaptureTimeExtension(captureTime time.Time) *AbsCaptureTimeExtension {
	return &AbsCaptureTimeExtension{
		Timestamp: toNtpTime(captureTime),
	}
}

// NewAbsCaptureTimeExtensionWithCaptureClockOffset makes new AbsCaptureTimeExtension from time.Time and a clock offset.
func NewAbsCaptureTimeExtensionWithCaptureClockOffset(
	captureTime time.Time,
	captureClockOffset time.Duration,
) *AbsCaptureTimeExtension {
	ns := captureClockOffset.Nanoseconds()
	negative := false
	if ns < 0 {
		ns = -ns
		negative = true
	}
	lsb := (ns / 1e9) & 0xFFFFFFFF
	msb := (((ns % 1e9) * (1 << 32)) / 1e9) & 0xFFFFFFFF
	offset := (lsb << 32) | msb
	if negative {
		offset = -offset
	}

	return &AbsCaptureTimeExtension{
		Timestamp:                   toNtpTime(captureTime),
		EstimatedCaptureClockOffset: &offset,
	}
}
