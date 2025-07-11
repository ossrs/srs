package rtpmjpeg

import (
	"crypto/rand"
	"fmt"
	"sort"

	"github.com/pion/rtp"

	"github.com/bluenviron/mediacommon/v2/pkg/codecs/jpeg"
)

const (
	rtpVersion            = 2
	defaultPayloadMaxSize = 1460 // 1500 (UDP MTU) - 20 (IP header) - 8 (UDP header) - 12 (RTP header)
)

func randUint32() (uint32, error) {
	var b [4]byte
	_, err := rand.Read(b[:])
	if err != nil {
		return 0, err
	}
	return uint32(b[0])<<24 | uint32(b[1])<<16 | uint32(b[2])<<8 | uint32(b[3]), nil
}

// Encoder is a RTP/M-JPEG encoder.
// Specification: https://datatracker.ietf.org/doc/html/rfc2435
type Encoder struct {
	// SSRC of packets (optional).
	// It defaults to a random value.
	SSRC *uint32

	// initial sequence number of packets (optional).
	// It defaults to a random value.
	InitialSequenceNumber *uint16

	// maximum size of packet payloads (optional).
	// It defaults to 1460.
	PayloadMaxSize int

	sequenceNumber uint16
}

// Init initializes the encoder.
func (e *Encoder) Init() error {
	if e.SSRC == nil {
		v, err := randUint32()
		if err != nil {
			return err
		}
		e.SSRC = &v
	}
	if e.InitialSequenceNumber == nil {
		v, err := randUint32()
		if err != nil {
			return err
		}
		v2 := uint16(v)
		e.InitialSequenceNumber = &v2
	}
	if e.PayloadMaxSize == 0 {
		e.PayloadMaxSize = defaultPayloadMaxSize
	}

	e.sequenceNumber = *e.InitialSequenceNumber
	return nil
}

// Encode encodes an image into RTP/M-JPEG packets.
func (e *Encoder) Encode(image []byte) ([]*rtp.Packet, error) {
	l := len(image)
	if l < 2 || image[0] != 0xFF || image[1] != jpeg.MarkerStartOfImage {
		return nil, fmt.Errorf("SOI not found")
	}

	image = image[2:]
	var sof *jpeg.StartOfFrame1
	var dri *jpeg.DefineRestartInterval
	quantizationTables := make(map[uint8][]byte)
	var data []byte

outer:
	for {
		if len(image) < 2 {
			break
		}

		h0, h1 := image[0], image[1]
		image = image[2:]

		if h0 != 0xFF {
			return nil, fmt.Errorf("invalid image")
		}

		switch h1 {
		case 0xE0, 0xE1, 0xE2, // JFIF
			jpeg.MarkerDefineHuffmanTable,
			jpeg.MarkerComment:
			mlen := int(image[0])<<8 | int(image[1])
			if len(image) < mlen {
				return nil, fmt.Errorf("image is too short")
			}
			image = image[mlen:]

		case jpeg.MarkerDefineQuantizationTable:
			mlen := int(image[0])<<8 | int(image[1])
			if len(image) < mlen {
				return nil, fmt.Errorf("image is too short")
			}

			var dqt jpeg.DefineQuantizationTable
			err := dqt.Unmarshal(image[2:mlen])
			if err != nil {
				return nil, err
			}
			image = image[mlen:]

			for _, t := range dqt.Tables {
				quantizationTables[t.ID] = t.Data
			}

		case jpeg.MarkerDefineRestartInterval:
			mlen := int(image[0])<<8 | int(image[1])
			if len(image) < mlen {
				return nil, fmt.Errorf("image is too short")
			}

			dri = &jpeg.DefineRestartInterval{}
			err := dri.Unmarshal(image[2:mlen])
			if err != nil {
				return nil, err
			}
			image = image[mlen:]

		case jpeg.MarkerStartOfFrame1:
			mlen := int(image[0])<<8 | int(image[1])
			if len(image) < mlen {
				return nil, fmt.Errorf("image is too short")
			}

			sof = &jpeg.StartOfFrame1{}
			err := sof.Unmarshal(image[2:mlen])
			if err != nil {
				return nil, err
			}
			image = image[mlen:]

			if sof.Width > maxDimension {
				return nil, fmt.Errorf("an image with width of %d can't be sent with RTSP", sof.Width)
			}

			if sof.Height > maxDimension {
				return nil, fmt.Errorf("an image with height of %d can't be sent with RTSP", sof.Height)
			}

			if (sof.Width % 8) != 0 {
				return nil, fmt.Errorf("width must be multiple of 8")
			}

			if (sof.Height % 8) != 0 {
				return nil, fmt.Errorf("height must be multiple of 8")
			}

		case jpeg.MarkerStartOfScan:
			mlen := int(image[0])<<8 | int(image[1])
			if len(image) < mlen {
				return nil, fmt.Errorf("image is too short")
			}

			var sos jpeg.StartOfScan
			err := sos.Unmarshal(image[2:mlen])
			if err != nil {
				return nil, err
			}
			image = image[mlen:]

			data = image
			break outer

		default:
			return nil, fmt.Errorf("unknown marker: 0x%.2x", h1)
		}
	}

	if sof == nil {
		return nil, fmt.Errorf("SOF not found")
	}

	if sof.Type > 63 {
		return nil, fmt.Errorf("JPEG type %d is not supported", sof.Type)
	}

	if len(data) == 0 {
		return nil, fmt.Errorf("image data not found")
	}

	jh := headerJPEG{
		TypeSpecific: 0,
		Type:         sof.Type,
		Quantization: 255,
		Width:        sof.Width,
		Height:       sof.Height,
	}

	if dri != nil {
		jh.Type += 64
	}

	first := true
	offset := 0
	var ret []*rtp.Packet

	for {
		var buf []byte

		jh.FragmentOffset = uint32(offset)
		buf = jh.marshal(buf)

		if dri != nil {
			buf = headerRestartMarker{
				Interval: dri.Interval,
				Count:    0xFFFF,
			}.marshal(buf)
		}

		if first {
			first = false

			qth := headerQuantizationTable{}

			// gather and sort tables IDs
			ids := make([]uint8, len(quantizationTables))
			i := 0
			for id := range quantizationTables {
				ids[i] = id
				i++
			}
			sort.Slice(ids, func(i, j int) bool {
				return ids[i] < ids[j]
			})

			// add tables sorted by ID
			for _, id := range ids {
				qth.Tables = append(qth.Tables, quantizationTables[id])
			}

			buf = qth.marshal(buf)
		}

		remaining := e.PayloadMaxSize - len(buf)
		ldata := len(data)
		if remaining > ldata {
			remaining = ldata
		}

		buf = append(buf, data[:remaining]...)
		data = data[remaining:]
		offset += remaining

		ret = append(ret, &rtp.Packet{
			Header: rtp.Header{
				Version:        rtpVersion,
				PayloadType:    26,
				SequenceNumber: e.sequenceNumber,
				SSRC:           *e.SSRC,
				Marker:         len(data) == 0,
			},
			Payload: buf,
		})
		e.sequenceNumber++

		if len(data) == 0 {
			break
		}
	}

	return ret, nil
}
