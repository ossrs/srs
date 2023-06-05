package rtcp

import (
	"fmt"
	"strings"
)

// A CompoundPacket is a collection of RTCP packets transmitted as a single packet with
// the underlying protocol (for example UDP).
//
// To maximize the resolution of receiption statistics, the first Packet in a CompoundPacket
// must always be either a SenderReport or a ReceiverReport.  This is true even if no data
// has been sent or received, in which case an empty ReceiverReport must be sent, and even
// if the only other RTCP packet in the compound packet is a Goodbye.
//
// Next, a SourceDescription containing a CNAME item must be included in each CompoundPacket
// to identify the source and to begin associating media for purposes such as lip-sync.
//
// Other RTCP packet types may follow in any order. Packet types may appear more than once.
type CompoundPacket []Packet

// Validate returns an error if this is not an RFC-compliant CompoundPacket.
func (c CompoundPacket) Validate() error {
	if len(c) == 0 {
		return errEmptyCompound
	}

	// SenderReport and ReceiverReport are the only types that
	// are allowed to be the first packet in a compound datagram
	switch c[0].(type) {
	case *SenderReport, *ReceiverReport:
		// ok
	default:
		return errBadFirstPacket
	}

	for _, pkt := range c[1:] {
		switch p := pkt.(type) {
		// If the number of RecetpionReports exceeds 31 additional ReceiverReports
		// can be included here.
		case *ReceiverReport:
			continue

		// A SourceDescription containing a CNAME must be included in every
		// CompoundPacket.
		case *SourceDescription:
			var hasCNAME bool
			for _, c := range p.Chunks {
				for _, it := range c.Items {
					if it.Type == SDESCNAME {
						hasCNAME = true
					}
				}
			}

			if !hasCNAME {
				return errMissingCNAME
			}

			return nil

		// Other packets are not permitted before the CNAME
		default:
			return errPacketBeforeCNAME
		}
	}

	// CNAME never reached
	return errMissingCNAME
}

// CNAME returns the CNAME that *must* be present in every CompoundPacket
func (c CompoundPacket) CNAME() (string, error) {
	var err error

	if len(c) < 1 {
		return "", errEmptyCompound
	}

	for _, pkt := range c[1:] {
		sdes, ok := pkt.(*SourceDescription)
		if ok {
			for _, c := range sdes.Chunks {
				for _, it := range c.Items {
					if it.Type == SDESCNAME {
						return it.Text, err
					}
				}
			}
		} else {
			_, ok := pkt.(*ReceiverReport)
			if !ok {
				err = errPacketBeforeCNAME
			}
		}
	}
	return "", errMissingCNAME
}

// Marshal encodes the CompoundPacket as binary.
func (c CompoundPacket) Marshal() ([]byte, error) {
	if err := c.Validate(); err != nil {
		return nil, err
	}

	p := []Packet(c)
	return Marshal(p)
}

// Unmarshal decodes a CompoundPacket from binary.
func (c *CompoundPacket) Unmarshal(rawData []byte) error {
	out := make(CompoundPacket, 0)
	for len(rawData) != 0 {
		p, processed, err := unmarshal(rawData)
		if err != nil {
			return err
		}

		out = append(out, p)
		rawData = rawData[processed:]
	}
	*c = out

	if err := c.Validate(); err != nil {
		return err
	}

	return nil
}

// DestinationSSRC returns the synchronization sources associated with this
// CompoundPacket's reception report.
func (c CompoundPacket) DestinationSSRC() []uint32 {
	if len(c) == 0 {
		return nil
	}

	return c[0].DestinationSSRC()
}

func (c CompoundPacket) String() string {
	out := "CompoundPacket\n"
	for _, p := range c {
		stringer, canString := p.(fmt.Stringer)
		if canString {
			out += stringer.String()
		} else {
			out += stringify(p)
		}
	}
	out = strings.TrimSuffix(strings.ReplaceAll(out, "\n", "\n\t"), "\t")
	return out
}
