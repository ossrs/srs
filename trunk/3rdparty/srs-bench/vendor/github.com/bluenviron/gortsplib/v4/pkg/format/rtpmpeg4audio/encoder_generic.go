package rtpmpeg4audio

import (
	"github.com/pion/rtp"

	"github.com/bluenviron/mediacommon/v2/pkg/bits"
	"github.com/bluenviron/mediacommon/v2/pkg/codecs/mpeg4audio"
)

func packetCountGeneric(avail, le int) int {
	n := le / avail
	if (le % avail) != 0 {
		n++
	}
	return n
}

func (e *Encoder) encodeGeneric(aus [][]byte) ([]*rtp.Packet, error) {
	var rets []*rtp.Packet
	var batch [][]byte
	timestamp := uint32(0)

	// split AUs into batches
	for _, au := range aus {
		if e.lenGenericAggregated(batch, au) <= e.PayloadMaxSize {
			// add to existing batch
			batch = append(batch, au)
		} else {
			// write current batch
			if batch != nil {
				pkts, err := e.writeGenericBatch(batch, timestamp)
				if err != nil {
					return nil, err
				}
				rets = append(rets, pkts...)
				timestamp += uint32(len(batch)) * mpeg4audio.SamplesPerAccessUnit
			}

			// initialize new batch
			batch = [][]byte{au}
		}
	}

	// write last batch
	pkts, err := e.writeGenericBatch(batch, timestamp)
	if err != nil {
		return nil, err
	}
	rets = append(rets, pkts...)

	return rets, nil
}

func (e *Encoder) writeGenericBatch(aus [][]byte, timestamp uint32) ([]*rtp.Packet, error) {
	if len(aus) != 1 || e.lenGenericAggregated(aus, nil) < e.PayloadMaxSize {
		return e.writeGenericAggregated(aus, timestamp)
	}

	return e.writeGenericFragmented(aus[0], timestamp)
}

func (e *Encoder) writeGenericFragmented(au []byte, timestamp uint32) ([]*rtp.Packet, error) {
	auHeadersLen := e.SizeLength + e.IndexLength
	auHeadersLenBytes := auHeadersLen / 8
	if (auHeadersLen % 8) != 0 {
		auHeadersLenBytes++
	}

	avail := e.PayloadMaxSize - 2 - auHeadersLenBytes
	le := len(au)
	packetCount := packetCountGeneric(avail, le)

	ret := make([]*rtp.Packet, packetCount)
	le = avail

	for i := range ret {
		if i == (packetCount - 1) {
			le = len(au)
		}

		payload := make([]byte, 2+auHeadersLenBytes+le)

		// AU-headers-length
		payload[0] = byte(auHeadersLen >> 8)
		payload[1] = byte(auHeadersLen)

		// AU-headers
		pos := 0
		bits.WriteBitsUnsafe(payload[2:], &pos, uint64(le), e.SizeLength)
		bits.WriteBitsUnsafe(payload[2:], &pos, 0, e.IndexLength)

		// AU
		copy(payload[2+auHeadersLenBytes:], au)
		au = au[le:]

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

func (e *Encoder) lenGenericAggregated(aus [][]byte, addAU []byte) int {
	n := 2 // AU-headers-length

	// AU-headers
	auHeadersLen := 0
	i := 0
	for range aus {
		if i == 0 {
			auHeadersLen += e.SizeLength + e.IndexLength
		} else {
			auHeadersLen += e.SizeLength + e.IndexDeltaLength
		}
		i++
	}
	if addAU != nil {
		if i == 0 {
			auHeadersLen += e.SizeLength + e.IndexLength
		} else {
			auHeadersLen += e.SizeLength + e.IndexDeltaLength
		}
	}
	n += auHeadersLen / 8
	if (auHeadersLen % 8) != 0 {
		n++
	}

	// AU
	for _, au := range aus {
		n += len(au)
	}
	n += len(addAU)

	return n
}

func (e *Encoder) writeGenericAggregated(aus [][]byte, timestamp uint32) ([]*rtp.Packet, error) {
	payload := make([]byte, e.lenGenericAggregated(aus, nil))

	// AU-headers
	written := 0
	pos := 0
	for i, au := range aus {
		bits.WriteBitsUnsafe(payload[2:], &pos, uint64(len(au)), e.SizeLength)
		written += e.SizeLength
		if i == 0 {
			bits.WriteBitsUnsafe(payload[2:], &pos, 0, e.IndexLength)
			written += e.IndexLength
		} else {
			bits.WriteBitsUnsafe(payload[2:], &pos, 0, e.IndexDeltaLength)
			written += e.IndexDeltaLength
		}
	}
	pos = 2 + (written / 8)
	if (written % 8) != 0 {
		pos++
	}

	// AU-headers-length
	payload[0] = byte(written >> 8)
	payload[1] = byte(written)

	// AUs
	for _, au := range aus {
		auLen := copy(payload[pos:], au)
		pos += auLen
	}

	pkt := &rtp.Packet{
		Header: rtp.Header{
			Version:        rtpVersion,
			PayloadType:    e.PayloadType,
			SequenceNumber: e.sequenceNumber,
			Timestamp:      timestamp,
			SSRC:           *e.SSRC,
			Marker:         true,
		},
		Payload: payload,
	}

	e.sequenceNumber++

	return []*rtp.Packet{pkt}, nil
}
