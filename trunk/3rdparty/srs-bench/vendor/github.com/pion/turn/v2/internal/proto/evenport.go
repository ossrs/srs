package proto

import "github.com/pion/stun"

// EvenPort represents EVEN-PORT attribute.
//
// This attribute allows the client to request that the port in the
// relayed transport address be even, and (optionally) that the server
// reserve the next-higher port number.
//
// RFC 5766 Section 14.6
type EvenPort struct {
	// ReservePort means that the server is requested to reserve
	// the next-higher port number (on the same IP address)
	// for a subsequent allocation.
	ReservePort bool
}

func (p EvenPort) String() string {
	if p.ReservePort {
		return "reserve: true"
	}
	return "reserve: false"
}

const (
	evenPortSize = 1
	firstBitSet  = (1 << 8) - 1 // 0b100000000
)

// AddTo adds EVEN-PORT to message.
func (p EvenPort) AddTo(m *stun.Message) error {
	v := make([]byte, evenPortSize)
	if p.ReservePort {
		// Set first bit to 1.
		v[0] = firstBitSet
	}
	m.Add(stun.AttrEvenPort, v)
	return nil
}

// GetFrom decodes EVEN-PORT from message.
func (p *EvenPort) GetFrom(m *stun.Message) error {
	v, err := m.Get(stun.AttrEvenPort)
	if err != nil {
		return err
	}
	if err = stun.CheckSize(stun.AttrEvenPort, len(v), evenPortSize); err != nil {
		return err
	}
	if v[0]&firstBitSet > 0 {
		p.ReservePort = true
	}
	return nil
}
