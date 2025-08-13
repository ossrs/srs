package format

import (
	"fmt"
	"strconv"
	"strings"

	"github.com/pion/rtp"
)

func findClockRate(payloadType uint8, rtpMap string, isApplication bool) (int, error) {
	// get clock rate from payload type
	// https://en.wikipedia.org/wiki/RTP_payload_formats
	switch payloadType {
	case 0, 1, 2, 3, 4, 5, 7, 8, 9, 12, 13, 15, 18:
		return 8000, nil

	case 6:
		return 16000, nil

	case 10, 11:
		return 44100, nil

	case 14, 25, 26, 28, 31, 32, 33, 34:
		return 90000, nil

	case 16:
		return 11025, nil

	case 17:
		return 22050, nil
	}

	// get clock rate from rtpmap
	// https://tools.ietf.org/html/rfc4566
	// a=rtpmap:<payload type> <encoding name>/<clock rate> [/<encoding parameters>]
	if rtpMap != "" {
		if tmp := strings.Split(rtpMap, "/"); len(tmp) >= 2 {
			v, err := strconv.ParseUint(tmp[1], 10, 31)
			if err != nil {
				return 0, err
			}
			return int(v), nil
		}
	}

	// application format without clock rate.
	// do not throw an error, but return zero, that disables RTCP sender and receiver reports.
	if isApplication || rtpMap != "" {
		return 0, nil
	}

	return 0, fmt.Errorf("clock rate not found")
}

// Generic is a generic RTP format.
type Generic struct {
	PayloadTyp uint8
	RTPMa      string
	FMT        map[string]string

	// clock rate of the format. Filled when calling Init().
	ClockRat int
}

// Init computes the clock rate of the format. It is mandatory to call it.
func (f *Generic) Init() error {
	var err error
	f.ClockRat, err = findClockRate(f.PayloadTyp, f.RTPMa, true)
	return err
}

func (f *Generic) unmarshal(ctx *unmarshalContext) error {
	f.PayloadTyp = ctx.payloadType
	f.RTPMa = ctx.rtpMap
	f.FMT = ctx.fmtp

	var err error
	f.ClockRat, err = findClockRate(f.PayloadTyp, f.RTPMa, ctx.mediaType == "application")
	return err
}

// Codec implements Format.
func (f *Generic) Codec() string {
	return "Generic"
}

// ClockRate implements Format.
func (f *Generic) ClockRate() int {
	return f.ClockRat
}

// PayloadType implements Format.
func (f *Generic) PayloadType() uint8 {
	return f.PayloadTyp
}

// RTPMap implements Format.
func (f *Generic) RTPMap() string {
	return f.RTPMa
}

// FMTP implements Format.
func (f *Generic) FMTP() map[string]string {
	return f.FMT
}

// PTSEqualsDTS implements Format.
func (f *Generic) PTSEqualsDTS(*rtp.Packet) bool {
	return true
}
