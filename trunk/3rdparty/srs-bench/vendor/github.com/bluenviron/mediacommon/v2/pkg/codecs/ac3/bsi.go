package ac3

import (
	"fmt"

	"github.com/bluenviron/mediacommon/v2/pkg/bits"
)

// BSI is a Bit Stream Information.
// Specification: ATSC, AC-3, Table 5.2
type BSI struct {
	Bsid  uint8
	Bsmod uint8
	Acmod uint8
	LfeOn bool
}

// Unmarshal decodes a BSI.
func (b *BSI) Unmarshal(buf []byte) error {
	if len(buf) < 2 {
		return fmt.Errorf("not enough bits")
	}

	b.Bsid = buf[0] >> 3
	if b.Bsid != 0x08 {
		return fmt.Errorf("invalid bsid")
	}

	b.Bsmod = buf[0] & 0b111

	buf = buf[1:]
	pos := 0

	tmp := bits.ReadBitsUnsafe(buf, &pos, 3)
	b.Acmod = uint8(tmp)

	if ((b.Acmod & 0x1) != 0) && (b.Acmod != 0x1) {
		bits.ReadBitsUnsafe(buf, &pos, 2) // cmixlev
	}

	if (b.Acmod & 0x4) != 0 {
		bits.ReadBitsUnsafe(buf, &pos, 2) // surmixlev
	}

	if b.Acmod == 0x2 {
		bits.ReadBitsUnsafe(buf, &pos, 2) // dsurmod
	}

	b.LfeOn = bits.ReadFlagUnsafe(buf, &pos)

	return nil
}

// ChannelCount returns the channel count.
func (b BSI) ChannelCount() int {
	var n int
	switch b.Acmod {
	case 0b001:
		n = 1
	case 0b010, 0b000:
		n = 2
	case 0b011, 0b100:
		n = 3
	case 0b101, 0b110:
		n = 4
	default:
		n = 5
	}

	if b.LfeOn {
		return n + 1
	}
	return n
}
