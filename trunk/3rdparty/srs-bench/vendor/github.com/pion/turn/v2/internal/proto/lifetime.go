package proto

import (
	"encoding/binary"
	"time"

	"github.com/pion/stun"
)

// DefaultLifetime in RFC 5766 is 10 minutes.
//
// RFC 5766 Section 2.2
const DefaultLifetime = time.Minute * 10

// Lifetime represents LIFETIME attribute.
//
// The LIFETIME attribute represents the duration for which the server
// will maintain an allocation in the absence of a refresh. The value
// portion of this attribute is 4-bytes long and consists of a 32-bit
// unsigned integral value representing the number of seconds remaining
// until expiration.
//
// RFC 5766 Section 14.2
type Lifetime struct {
	time.Duration
}

// uint32 seconds
const lifetimeSize = 4 // 4 bytes, 32 bits

// AddTo adds LIFETIME to message.
func (l Lifetime) AddTo(m *stun.Message) error {
	v := make([]byte, lifetimeSize)
	binary.BigEndian.PutUint32(v, uint32(l.Seconds()))
	m.Add(stun.AttrLifetime, v)
	return nil
}

// GetFrom decodes LIFETIME from message.
func (l *Lifetime) GetFrom(m *stun.Message) error {
	v, err := m.Get(stun.AttrLifetime)
	if err != nil {
		return err
	}
	if err = stun.CheckSize(stun.AttrLifetime, len(v), lifetimeSize); err != nil {
		return err
	}
	_ = v[lifetimeSize-1] // asserting length
	seconds := binary.BigEndian.Uint32(v)
	l.Duration = time.Second * time.Duration(seconds)
	return nil
}
