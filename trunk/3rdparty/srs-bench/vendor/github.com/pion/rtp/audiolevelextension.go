package rtp

import (
	"errors"
)

const (
	// audioLevelExtensionSize One byte header size
	audioLevelExtensionSize = 1
)

var errAudioLevelOverflow = errors.New("audio level overflow")

// AudioLevelExtension is a extension payload format described in
// https://tools.ietf.org/html/rfc6464
//
// Implementation based on:
// https://chromium.googlesource.com/external/webrtc/+/e2a017725570ead5946a4ca8235af27470ca0df9/webrtc/modules/rtp_rtcp/source/rtp_header_extensions.cc#49
//
// One byte format:
// 0                   1
// 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// |  ID   | len=0 |V| level       |
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
//
// Two byte format:
// 0                   1                   2                   3
// 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// |      ID       |     len=1     |V|    level    |    0 (pad)    |
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
type AudioLevelExtension struct {
	Level uint8
	Voice bool
}

// Marshal serializes the members to buffer
func (a *AudioLevelExtension) Marshal() ([]byte, error) {
	if a.Level > 127 {
		return nil, errAudioLevelOverflow
	}
	voice := uint8(0x00)
	if a.Voice {
		voice = 0x80
	}
	buf := make([]byte, audioLevelExtensionSize)
	buf[0] = voice | a.Level
	return buf, nil
}

// Unmarshal parses the passed byte slice and stores the result in the members
func (a *AudioLevelExtension) Unmarshal(rawData []byte) error {
	if len(rawData) < audioLevelExtensionSize {
		return errTooSmall
	}
	a.Level = rawData[0] & 0x7F
	a.Voice = rawData[0]&0x80 != 0
	return nil
}
