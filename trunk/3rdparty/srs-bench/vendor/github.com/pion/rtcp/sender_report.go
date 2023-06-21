package rtcp

import (
	"encoding/binary"
	"fmt"
)

// A SenderReport (SR) packet provides reception quality feedback for an RTP stream
type SenderReport struct {
	// The synchronization source identifier for the originator of this SR packet.
	SSRC uint32
	// The wallclock time when this report was sent so that it may be used in
	// combination with timestamps returned in reception reports from other
	// receivers to measure round-trip propagation to those receivers.
	NTPTime uint64
	// Corresponds to the same time as the NTP timestamp (above), but in
	// the same units and with the same random offset as the RTP
	// timestamps in data packets. This correspondence may be used for
	// intra- and inter-media synchronization for sources whose NTP
	// timestamps are synchronized, and may be used by media-independent
	// receivers to estimate the nominal RTP clock frequency.
	RTPTime uint32
	// The total number of RTP data packets transmitted by the sender
	// since starting transmission up until the time this SR packet was
	// generated.
	PacketCount uint32
	// The total number of payload octets (i.e., not including header or
	// padding) transmitted in RTP data packets by the sender since
	// starting transmission up until the time this SR packet was
	// generated.
	OctetCount uint32
	// Zero or more reception report blocks depending on the number of other
	// sources heard by this sender since the last report. Each reception report
	// block conveys statistics on the reception of RTP packets from a
	// single synchronization source.
	Reports []ReceptionReport
	// ProfileExtensions contains additional, payload-specific information that needs to
	// be reported regularly about the sender.
	ProfileExtensions []byte
}

const (
	srHeaderLength      = 24
	srSSRCOffset        = 0
	srNTPOffset         = srSSRCOffset + ssrcLength
	ntpTimeLength       = 8
	srRTPOffset         = srNTPOffset + ntpTimeLength
	rtpTimeLength       = 4
	srPacketCountOffset = srRTPOffset + rtpTimeLength
	srPacketCountLength = 4
	srOctetCountOffset  = srPacketCountOffset + srPacketCountLength
	srOctetCountLength  = 4
	srReportOffset      = srOctetCountOffset + srOctetCountLength
)

// Marshal encodes the SenderReport in binary
func (r SenderReport) Marshal() ([]byte, error) {
	/*
	 *         0                   1                   2                   3
	 *         0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
	 *        +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
	 * header |V=2|P|    RC   |   PT=SR=200   |             length            |
	 *        +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
	 *        |                         SSRC of sender                        |
	 *        +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
	 * sender |              NTP timestamp, most significant word             |
	 * info   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
	 *        |             NTP timestamp, least significant word             |
	 *        +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
	 *        |                         RTP timestamp                         |
	 *        +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
	 *        |                     sender's packet count                     |
	 *        +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
	 *        |                      sender's octet count                     |
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

	binary.BigEndian.PutUint32(packetBody[srSSRCOffset:], r.SSRC)
	binary.BigEndian.PutUint64(packetBody[srNTPOffset:], r.NTPTime)
	binary.BigEndian.PutUint32(packetBody[srRTPOffset:], r.RTPTime)
	binary.BigEndian.PutUint32(packetBody[srPacketCountOffset:], r.PacketCount)
	binary.BigEndian.PutUint32(packetBody[srOctetCountOffset:], r.OctetCount)

	offset := srHeaderLength
	for _, rp := range r.Reports {
		data, err := rp.Marshal()
		if err != nil {
			return nil, err
		}
		copy(packetBody[offset:], data)
		offset += receptionReportLength
	}

	if len(r.Reports) > countMax {
		return nil, errTooManyReports
	}

	copy(packetBody[offset:], r.ProfileExtensions)

	hData, err := r.Header().Marshal()
	if err != nil {
		return nil, err
	}
	copy(rawPacket, hData)

	return rawPacket, nil
}

// Unmarshal decodes the SenderReport from binary
func (r *SenderReport) Unmarshal(rawPacket []byte) error {
	/*
	 *         0                   1                   2                   3
	 *         0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
	 *        +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
	 * header |V=2|P|    RC   |   PT=SR=200   |             length            |
	 *        +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
	 *        |                         SSRC of sender                        |
	 *        +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
	 * sender |              NTP timestamp, most significant word             |
	 * info   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
	 *        |             NTP timestamp, least significant word             |
	 *        +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
	 *        |                         RTP timestamp                         |
	 *        +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
	 *        |                     sender's packet count                     |
	 *        +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
	 *        |                      sender's octet count                     |
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

	if len(rawPacket) < (headerLength + srHeaderLength) {
		return errPacketTooShort
	}

	var h Header
	if err := h.Unmarshal(rawPacket); err != nil {
		return err
	}

	if h.Type != TypeSenderReport {
		return errWrongType
	}

	packetBody := rawPacket[headerLength:]

	r.SSRC = binary.BigEndian.Uint32(packetBody[srSSRCOffset:])
	r.NTPTime = binary.BigEndian.Uint64(packetBody[srNTPOffset:])
	r.RTPTime = binary.BigEndian.Uint32(packetBody[srRTPOffset:])
	r.PacketCount = binary.BigEndian.Uint32(packetBody[srPacketCountOffset:])
	r.OctetCount = binary.BigEndian.Uint32(packetBody[srOctetCountOffset:])

	offset := srReportOffset
	for i := 0; i < int(h.Count); i++ {
		rrEnd := offset + receptionReportLength
		if rrEnd > len(packetBody) {
			return errPacketTooShort
		}
		rrBody := packetBody[offset : offset+receptionReportLength]
		offset = rrEnd

		var rr ReceptionReport
		if err := rr.Unmarshal(rrBody); err != nil {
			return err
		}
		r.Reports = append(r.Reports, rr)
	}

	if offset < len(packetBody) {
		r.ProfileExtensions = packetBody[offset:]
	}

	if uint8(len(r.Reports)) != h.Count {
		return errInvalidHeader
	}

	return nil
}

// DestinationSSRC returns an array of SSRC values that this packet refers to.
func (r *SenderReport) DestinationSSRC() []uint32 {
	out := make([]uint32, len(r.Reports)+1)
	for i, v := range r.Reports {
		out[i] = v.SSRC
	}
	out[len(r.Reports)] = r.SSRC
	return out
}

func (r *SenderReport) len() int {
	repsLength := 0
	for _, rep := range r.Reports {
		repsLength += rep.len()
	}
	return headerLength + srHeaderLength + repsLength + len(r.ProfileExtensions)
}

// Header returns the Header associated with this packet.
func (r *SenderReport) Header() Header {
	return Header{
		Count:  uint8(len(r.Reports)),
		Type:   TypeSenderReport,
		Length: uint16((r.len() / 4) - 1),
	}
}

func (r SenderReport) String() string {
	out := fmt.Sprintf("SenderReport from %x\n", r.SSRC)
	out += fmt.Sprintf("\tNTPTime:\t%d\n", r.NTPTime)
	out += fmt.Sprintf("\tRTPTIme:\t%d\n", r.RTPTime)
	out += fmt.Sprintf("\tPacketCount:\t%d\n", r.PacketCount)
	out += fmt.Sprintf("\tOctetCount:\t%d\n", r.OctetCount)

	out += "\tSSRC    \tLost\tLastSequence\n"
	for _, i := range r.Reports {
		out += fmt.Sprintf("\t%x\t%d/%d\t%d\n", i.SSRC, i.FractionLost, i.TotalLost, i.LastSequenceNumber)
	}
	out += fmt.Sprintf("\tProfile Extension Data: %v\n", r.ProfileExtensions)
	return out
}
