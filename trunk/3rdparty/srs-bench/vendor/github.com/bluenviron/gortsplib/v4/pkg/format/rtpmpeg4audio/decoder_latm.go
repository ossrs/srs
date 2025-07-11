package rtpmpeg4audio

import (
	"fmt"

	"github.com/bluenviron/mediacommon/v2/pkg/codecs/mpeg4audio"
	"github.com/pion/rtp"
)

func (d *Decoder) decodeLATM(pkt *rtp.Packet) ([][]byte, error) {
	var au []byte
	buf := pkt.Payload

	if d.fragmentsSize == 0 {
		pl, n, err := payloadLengthInfoDecode(buf)
		if err != nil {
			return nil, err
		}

		buf = buf[n:]
		bl := len(buf)

		if pl <= bl {
			au = buf[:pl]
			// there could be other data, due to otherDataPresent. Ignore it.
		} else {
			if pl > mpeg4audio.MaxAccessUnitSize {
				errSize := pl
				d.resetFragments()
				return nil, fmt.Errorf("access unit size (%d) is too big, maximum is %d",
					errSize, mpeg4audio.MaxAccessUnitSize)
			}

			d.fragments = append(d.fragments, buf)
			d.fragmentsSize = pl
			d.fragmentsExpected = pl - bl
			d.fragmentNextSeqNum = pkt.SequenceNumber + 1
			return nil, ErrMorePacketsNeeded
		}
	} else {
		if pkt.SequenceNumber != d.fragmentNextSeqNum {
			d.resetFragments()
			return nil, fmt.Errorf("discarding frame since a RTP packet is missing")
		}

		bl := len(buf)

		if d.fragmentsExpected > bl {
			d.fragments = append(d.fragments, buf)
			d.fragmentsExpected -= bl
			d.fragmentNextSeqNum++
			return nil, ErrMorePacketsNeeded
		}

		d.fragments = append(d.fragments, buf[:d.fragmentsExpected])
		// there could be other data, due to otherDataPresent. Ignore it.

		au = joinFragments(d.fragments, d.fragmentsSize)
		d.resetFragments()
	}

	return [][]byte{au}, nil
}
