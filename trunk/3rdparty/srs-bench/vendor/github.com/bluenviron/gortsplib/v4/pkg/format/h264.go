package format

import (
	"bytes"
	"encoding/base64"
	"encoding/hex"
	"fmt"
	"strconv"
	"strings"
	"sync"

	"github.com/pion/rtp"

	"github.com/bluenviron/gortsplib/v4/pkg/format/rtph264"
	"github.com/bluenviron/mediacommon/v2/pkg/codecs/h264"
)

// H264 is the RTP format for the H264 codec.
// Specification: https://datatracker.ietf.org/doc/html/rfc6184
type H264 struct {
	PayloadTyp        uint8
	SPS               []byte
	PPS               []byte
	PacketizationMode int

	mutex sync.RWMutex
}

func (f *H264) unmarshal(ctx *unmarshalContext) error {
	f.PayloadTyp = ctx.payloadType

	for key, val := range ctx.fmtp {
		switch key {
		case "sprop-parameter-sets":
			tmp := strings.Split(val, ",")
			if len(tmp) >= 2 {
				sps, err := base64.StdEncoding.DecodeString(tmp[0])
				if err != nil {
					return fmt.Errorf("invalid sprop-parameter-sets (%v)", val)
				}

				// some cameras ship parameters with Annex-B prefix
				sps = bytes.TrimPrefix(sps, []byte{0, 0, 0, 1})

				pps, err := base64.StdEncoding.DecodeString(tmp[1])
				if err != nil {
					return fmt.Errorf("invalid sprop-parameter-sets (%v)", val)
				}

				// some cameras ship parameters with Annex-B prefix
				pps = bytes.TrimPrefix(pps, []byte{0, 0, 0, 1})

				var spsp h264.SPS
				err = spsp.Unmarshal(sps)
				if err != nil {
					continue
				}

				f.SPS = sps
				f.PPS = pps
			}

		case "packetization-mode":
			tmp, err := strconv.ParseUint(val, 10, 31)
			if err != nil {
				return fmt.Errorf("invalid packetization-mode (%v)", val)
			}

			f.PacketizationMode = int(tmp)
		}
	}

	return nil
}

// Codec implements Format.
func (f *H264) Codec() string {
	return "H264"
}

// ClockRate implements Format.
func (f *H264) ClockRate() int {
	return 90000
}

// PayloadType implements Format.
func (f *H264) PayloadType() uint8 {
	return f.PayloadTyp
}

// RTPMap implements Format.
func (f *H264) RTPMap() string {
	return "H264/90000"
}

// FMTP implements Format.
func (f *H264) FMTP() map[string]string {
	f.mutex.RLock()
	defer f.mutex.RUnlock()

	fmtp := make(map[string]string)

	if f.PacketizationMode != 0 {
		fmtp["packetization-mode"] = strconv.FormatInt(int64(f.PacketizationMode), 10)
	}

	var tmp []string
	if f.SPS != nil {
		tmp = append(tmp, base64.StdEncoding.EncodeToString(f.SPS))
	}
	if f.PPS != nil {
		tmp = append(tmp, base64.StdEncoding.EncodeToString(f.PPS))
	}
	if tmp != nil {
		fmtp["sprop-parameter-sets"] = strings.Join(tmp, ",")
	}
	if len(f.SPS) >= 4 {
		fmtp["profile-level-id"] = strings.ToUpper(hex.EncodeToString(f.SPS[1:4]))
	}

	return fmtp
}

// PTSEqualsDTS implements Format.
func (f *H264) PTSEqualsDTS(pkt *rtp.Packet) bool {
	if len(pkt.Payload) == 0 {
		return false
	}

	typ := h264.NALUType(pkt.Payload[0] & 0x1F)

	switch typ {
	case h264.NALUTypeIDR, h264.NALUTypeSPS, h264.NALUTypePPS:
		return true

	case 24: // STAP-A
		payload := pkt.Payload[1:]

		for {
			if len(payload) < 2 {
				return false
			}

			size := uint16(payload[0])<<8 | uint16(payload[1])
			payload = payload[2:]

			if size == 0 || int(size) > len(payload) {
				return false
			}

			var nalu []byte
			nalu, payload = payload[:size], payload[size:]

			typ = h264.NALUType(nalu[0] & 0x1F)
			switch typ {
			case h264.NALUTypeIDR, h264.NALUTypeSPS, h264.NALUTypePPS:
				return true
			}

			if len(payload) == 0 {
				break
			}
		}

	case 28: // FU-A
		if len(pkt.Payload) < 2 {
			return false
		}

		start := pkt.Payload[1] >> 7
		if start != 1 {
			return false
		}

		typ := h264.NALUType(pkt.Payload[1] & 0x1F)
		switch typ {
		case h264.NALUTypeIDR, h264.NALUTypeSPS, h264.NALUTypePPS:
			return true
		}
	}

	return false
}

// CreateDecoder creates a decoder able to decode the content of the format.
func (f *H264) CreateDecoder() (*rtph264.Decoder, error) {
	d := &rtph264.Decoder{
		PacketizationMode: f.PacketizationMode,
	}

	err := d.Init()
	if err != nil {
		return nil, err
	}

	return d, nil
}

// CreateEncoder creates an encoder able to encode the content of the format.
func (f *H264) CreateEncoder() (*rtph264.Encoder, error) {
	e := &rtph264.Encoder{
		PayloadType:       f.PayloadTyp,
		PacketizationMode: f.PacketizationMode,
	}

	err := e.Init()
	if err != nil {
		return nil, err
	}

	return e, nil
}

// SafeSetParams sets the codec parameters.
func (f *H264) SafeSetParams(sps []byte, pps []byte) {
	f.mutex.Lock()
	defer f.mutex.Unlock()
	f.SPS = sps
	f.PPS = pps
}

// SafeParams returns the codec parameters.
func (f *H264) SafeParams() ([]byte, []byte) {
	f.mutex.RLock()
	defer f.mutex.RUnlock()
	return f.SPS, f.PPS
}
