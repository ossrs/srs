package rtcp

import (
	"encoding/binary"
	"fmt"
)

// A ReceiverReport (RR) packet provides reception quality feedback for an RTP stream
type ReceiverReport struct {
	// The synchronization source identifier for the originator of this RR packet.
	SSRC uint32
	// Zero or more reception report blocks depending on the number of other
	// sources heard by this sender since the last report. Each reception report
	// block conveys statistics on the reception of RTP packets from a
	// single synchronization source.
	Reports []ReceptionReport
	// Extension contains additional, payload-specific information that needs to
	// be reported regularly about the receiver.
	ProfileExtensions []byte
}

const (
	ssrcLength     = 4
	rrSSRCOffset   = headerLength
	rrReportOffset = rrSSRCOffset + ssrcLength
)

// Marshal encodes the ReceiverReport in binary
func (r ReceiverReport) Marshal() ([]byte, error) {
	/*
	 *         0                   1                   2                   3
	 *         0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
	 *        +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
	 * header |V=2|P|    RC   |   PT=RR=201   |             length            |
	 *        +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
	 *        |                     SSRC of packet sender                     |
	 *        +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
	 * report |                 SSRC_1 (SSRC of first source)                 |
	 * block  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
	 *   1    | fraction lost |       cumulative number of packets lost       |
	 *        +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
	 *        |           extended highest sequence number received           |
	 *        +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
	 *        |                      interarrival jitter                      |
	 *        +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
	 *        |                         last SR (LSR)                         |
	 *        +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
	 *        |                   delay since last SR (DLSR)                  |
	 *        +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
	 * report |                 SSRC_2 (SSRC of second source)                |
	 * block  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
	 *   2    :                               ...                             :
	 *        +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
	 *        |                  profile-specific extensions                  |
	 *        +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
	 */

	rawPacket := make([]byte, r.len())
	packetBody := rawPacket[headerLength:]

	binary.BigEndian.PutUint32(packetBody, r.SSRC)

	for i, rp := range r.Reports {
		data, err := rp.Marshal()
		if err != nil {
			return nil, err
		}
		offset := ssrcLength + receptionReportLength*i
		copy(packetBody[offset:], data)
	}

	if len(r.Reports) > countMax {
		return nil, errTooManyReports
	}

	pe := make([]byte, len(r.ProfileExtensions))
	copy(pe, r.ProfileExtensions)

	// if the length of the profile extensions isn't devisible
	// by 4, we need to pad the end.
	for (len(pe) & 0x3) != 0 {
		pe = append(pe, 0)
	}

	rawPacket = append(rawPacket, pe...)

	hData, err := r.Header().Marshal()
	if err != nil {
		return nil, err
	}
	copy(rawPacket, hData)

	return rawPacket, nil
}

// Unmarshal decodes the ReceiverReport from binary
func (r *ReceiverReport) Unmarshal(rawPacket []byte) error {
	/*
	 *         0                   1                   2                   3
	 *         0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
	 *        +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
	 * header |V=2|P|    RC   |   PT=RR=201   |             length            |
	 *        +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
	 *        |                     SSRC of packet sender                     |
	 *        +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
	 * report |                 SSRC_1 (SSRC of first source)                 |
	 * block  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
	 *   1    | fraction lost |       cumulative number of packets lost       |
	 *        +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
	 *        |           extended highest sequence number received           |
	 *        +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
	 *        |                      interarrival jitter                      |
	 *        +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
	 *        |                         last SR (LSR)                         |
	 *        +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
	 *        |                   delay since last SR (DLSR)                  |
	 *        +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
	 * report |                 SSRC_2 (SSRC of second source)                |
	 * block  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
	 *   2    :                               ...                             :
	 *        +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
	 *        |                  profile-specific extensions                  |
	 *        +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
	 */

	if len(rawPacket) < (headerLength + ssrcLength) {
		return errPacketTooShort
	}

	var h Header
	if err := h.Unmarshal(rawPacket); err != nil {
		return err
	}

	if h.Type != TypeReceiverReport {
		return errWrongType
	}

	r.SSRC = binary.BigEndian.Uint32(rawPacket[rrSSRCOffset:])

	for i := rrReportOffset; i < len(rawPacket) && len(r.Reports) < int(h.Count); i += receptionReportLength {
		var rr ReceptionReport
		if err := rr.Unmarshal(rawPacket[i:]); err != nil {
			return err
		}
		r.Reports = append(r.Reports, rr)
	}
	r.ProfileExtensions = rawPacket[rrReportOffset+(len(r.Reports)*receptionReportLength):]

	if uint8(len(r.Reports)) != h.Count {
		return errInvalidHeader
	}

	return nil
}

func (r *ReceiverReport) len() int {
	repsLength := 0
	for _, rep := range r.Reports {
		repsLength += rep.len()
	}
	return headerLength + ssrcLength + repsLength
}

// Header returns the Header associated with this packet.
func (r *ReceiverReport) Header() Header {
	return Header{
		Count:  uint8(len(r.Reports)),
		Type:   TypeReceiverReport,
		Length: uint16((r.len()/4)-1) + uint16(getPadding(len(r.ProfileExtensions))),
	}
}

// DestinationSSRC returns an array of SSRC values that this packet refers to.
func (r *ReceiverReport) DestinationSSRC() []uint32 {
	out := make([]uint32, len(r.Reports))
	for i, v := range r.Reports {
		out[i] = v.SSRC
	}
	return out
}

func (r ReceiverReport) String() string {
	out := fmt.Sprintf("ReceiverReport from %x\n", r.SSRC)
	out += "\tSSRC    \tLost\tLastSequence\n"
	for _, i := range r.Reports {
		out += fmt.Sprintf("\t%x\t%d/%d\t%d\n", i.SSRC, i.FractionLost, i.TotalLost, i.LastSequenceNumber)
	}
	out += fmt.Sprintf("\tProfile Extension Data: %v\n", r.ProfileExtensions)
	return out
}
