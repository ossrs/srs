package format

import (
	"bytes"
	"encoding/base64"
	"fmt"
	"strconv"
	"sync"

	"github.com/bluenviron/mediacommon/v2/pkg/codecs/h265"
	"github.com/pion/rtp"

	"github.com/bluenviron/gortsplib/v4/pkg/format/rtph265"
)

// H265 is the RTP format for the H265 codec.
// Specification: https://datatracker.ietf.org/doc/html/rfc7798
type H265 struct {
	PayloadTyp uint8
	VPS        []byte
	SPS        []byte
	PPS        []byte
	MaxDONDiff int

	mutex sync.RWMutex
}

func (f *H265) unmarshal(ctx *unmarshalContext) error {
	f.PayloadTyp = ctx.payloadType

	for key, val := range ctx.fmtp {
		switch key {
		case "sprop-vps":
			var err error
			f.VPS, err = base64.StdEncoding.DecodeString(val)
			if err != nil {
				return fmt.Errorf("invalid sprop-vps (%v)", ctx.fmtp)
			}

			// some cameras ship parameters with Annex-B prefix
			f.VPS = bytes.TrimPrefix(f.VPS, []byte{0, 0, 0, 1})

		case "sprop-sps":
			var err error
			f.SPS, err = base64.StdEncoding.DecodeString(val)
			if err != nil {
				return fmt.Errorf("invalid sprop-sps (%v)", ctx.fmtp)
			}

			// some cameras ship parameters with Annex-B prefix
			f.SPS = bytes.TrimPrefix(f.SPS, []byte{0, 0, 0, 1})

			var spsp h265.SPS
			err = spsp.Unmarshal(f.SPS)
			if err != nil {
				return fmt.Errorf("invalid SPS: %w", err)
			}

		case "sprop-pps":
			var err error
			f.PPS, err = base64.StdEncoding.DecodeString(val)
			if err != nil {
				return fmt.Errorf("invalid sprop-pps (%v)", ctx.fmtp)
			}

			// some cameras ship parameters with Annex-B prefix
			f.PPS = bytes.TrimPrefix(f.PPS, []byte{0, 0, 0, 1})

			var ppsp h265.PPS
			err = ppsp.Unmarshal(f.PPS)
			if err != nil {
				return fmt.Errorf("invalid PPS: %w", err)
			}

		case "sprop-max-don-diff":
			tmp, err := strconv.ParseUint(val, 10, 31)
			if err != nil {
				return fmt.Errorf("invalid sprop-max-don-diff (%v)", ctx.fmtp)
			}
			f.MaxDONDiff = int(tmp)
		}
	}

	return nil
}

// Codec implements Format.
func (f *H265) Codec() string {
	return "H265"
}

// ClockRate implements Format.
func (f *H265) ClockRate() int {
	return 90000
}

// PayloadType implements Format.
func (f *H265) PayloadType() uint8 {
	return f.PayloadTyp
}

// RTPMap implements Format.
func (f *H265) RTPMap() string {
	return "H265/90000"
}

// FMTP implements Format.
func (f *H265) FMTP() map[string]string {
	f.mutex.RLock()
	defer f.mutex.RUnlock()

	fmtp := make(map[string]string)
	if f.VPS != nil {
		fmtp["sprop-vps"] = base64.StdEncoding.EncodeToString(f.VPS)
	}
	if f.SPS != nil {
		fmtp["sprop-sps"] = base64.StdEncoding.EncodeToString(f.SPS)
	}
	if f.PPS != nil {
		fmtp["sprop-pps"] = base64.StdEncoding.EncodeToString(f.PPS)
	}
	if f.MaxDONDiff != 0 {
		fmtp["sprop-max-don-diff"] = strconv.FormatInt(int64(f.MaxDONDiff), 10)
	}

	return fmtp
}

// PTSEqualsDTS implements Format.
func (f *H265) PTSEqualsDTS(pkt *rtp.Packet) bool {
	if len(pkt.Payload) == 0 {
		return false
	}

	typ := h265.NALUType((pkt.Payload[0] >> 1) & 0b111111)

	switch typ {
	case h265.NALUType_IDR_W_RADL, h265.NALUType_IDR_N_LP, h265.NALUType_CRA_NUT,
		h265.NALUType_VPS_NUT, h265.NALUType_SPS_NUT, h265.NALUType_PPS_NUT:
		return true

	case h265.NALUType_AggregationUnit:
		if len(pkt.Payload) < 4 {
			return false
		}

		payload := pkt.Payload[2:]

		for {
			size := uint16(payload[0])<<8 | uint16(payload[1])
			payload = payload[2:]

			if size == 0 || int(size) > len(payload) {
				return false
			}

			var nalu []byte
			nalu, payload = payload[:size], payload[size:]

			typ = h265.NALUType((nalu[0] >> 1) & 0b111111)
			switch typ {
			case h265.NALUType_IDR_W_RADL, h265.NALUType_IDR_N_LP, h265.NALUType_CRA_NUT,
				h265.NALUType_VPS_NUT, h265.NALUType_SPS_NUT, h265.NALUType_PPS_NUT:
				return true
			}

			if len(payload) == 0 {
				break
			}

			if len(payload) < 2 {
				return false
			}
		}

	case h265.NALUType_FragmentationUnit:
		if len(pkt.Payload) < 3 {
			return false
		}

		start := pkt.Payload[2] >> 7
		if start != 1 {
			return false
		}

		typ := h265.NALUType(pkt.Payload[2] & 0b111111)
		switch typ {
		case h265.NALUType_IDR_W_RADL, h265.NALUType_IDR_N_LP, h265.NALUType_CRA_NUT,
			h265.NALUType_VPS_NUT, h265.NALUType_SPS_NUT, h265.NALUType_PPS_NUT:
			return true
		}
	}

	return false
}

// CreateDecoder creates a decoder able to decode the content of the format.
func (f *H265) CreateDecoder() (*rtph265.Decoder, error) {
	d := &rtph265.Decoder{
		MaxDONDiff: f.MaxDONDiff,
	}

	err := d.Init()
	if err != nil {
		return nil, err
	}

	return d, nil
}

// CreateEncoder creates an encoder able to encode the content of the format.
func (f *H265) CreateEncoder() (*rtph265.Encoder, error) {
	e := &rtph265.Encoder{
		PayloadType: f.PayloadTyp,
		MaxDONDiff:  f.MaxDONDiff,
	}

	err := e.Init()
	if err != nil {
		return nil, err
	}

	return e, nil
}

// SafeSetParams sets the codec parameters.
func (f *H265) SafeSetParams(vps []byte, sps []byte, pps []byte) {
	f.mutex.Lock()
	defer f.mutex.Unlock()
	f.VPS = vps
	f.SPS = sps
	f.PPS = pps
}

// SafeParams returns the codec parameters.
func (f *H265) SafeParams() ([]byte, []byte, []byte) {
	f.mutex.RLock()
	defer f.mutex.RUnlock()
	return f.VPS, f.SPS, f.PPS
}
