package rtpmpeg4audio

import (
	"github.com/pion/rtp"

	"github.com/bluenviron/mediacommon/v2/pkg/codecs/mpeg4audio"
)

func (e *Encoder) packetCountLATM(auLen int, plil int) int {
	totalLen := plil + auLen
	n := totalLen / e.PayloadMaxSize
	if (totalLen % e.PayloadMaxSize) != 0 {
		n++
	}
	return n
}

func (e *Encoder) encodeLATM(aus [][]byte) ([]*rtp.Packet, error) {
	var rets []*rtp.Packet

	for i, au := range aus {
		timestamp := uint32(i) * mpeg4audio.SamplesPerAccessUnit

		add, err := e.encodeLATMSingle(au, timestamp)
		if err != nil {
			return nil, err
		}
		rets = append(rets, add...)
	}

	return rets, nil
}

func (e *Encoder) encodeLATMSingle(au []byte, timestamp uint32) ([]*rtp.Packet, error) {
	auLen := len(au)
	plil := payloadLengthInfoEncodeSize(auLen)
	packetCount := e.packetCountLATM(auLen, plil)

	ret := make([]*rtp.Packet, packetCount)
	le := e.PayloadMaxSize - plil

	for i := range ret {
		if i == (packetCount - 1) {
			le = len(au)
		}

		var payload []byte

		if i == 0 {
			payload = make([]byte, plil+le)
			payloadLengthInfoEncode(plil, auLen, payload)
			copy(payload[plil:], au[:le])
			au = au[le:]
			le = e.PayloadMaxSize
		} else {
			payload = au[:le]
			au = au[le:]
		}

		ret[i] = &rtp.Packet{
			Header: rtp.Header{
				Version:        rtpVersion,
				PayloadType:    e.PayloadType,
				SequenceNumber: e.sequenceNumber,
				Timestamp:      timestamp,
				SSRC:           *e.SSRC,
				Marker:         (i == packetCount-1),
			},
			Payload: payload,
		}

		e.sequenceNumber++
	}

	return ret, nil
}
